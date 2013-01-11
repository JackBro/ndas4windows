/*++

Copyright (C)2002-2004 XIMETA, Inc.
All rights reserved.

--*/
#include "stdafx.h"
#include "ndasdevreg.h"
#include <ndas/ndasmsg.h>

#include "ndascfg.h"

#include "ndaseventpub.h"
#include "ndasobjs.h"

#include <shlwapi.h>

#include "xdbgflags.h"
#define XDBG_MODULE_FLAG XDF_NDASDEVREG
#include "xdebug.h"

#include "traceflags.h"

const TCHAR CNdasDeviceRegistrar::CFG_CONTAINER[] = _T("Devices");

namespace
{
	BOOL pCharToHex(TCHAR c, LPDWORD lpValue);
	BOOL pConvertStringToDeviceId(LPCTSTR szAddressString, NDAS_DEVICE_ID* pDeviceId);
}

CNdasDeviceRegistrar::
CNdasDeviceRegistrar(
	CNdasService& service,
	DWORD MaxSlotNo) :
	m_service(service),
	m_fBootstrapping(FALSE),
	m_dwMaxSlotNo(MaxSlotNo),
	m_slotbit(MaxSlotNo + 1)
{
	XTLCALLTRACE2(TCService);

	// Slot 0 is always occupied and reserved!
	m_slotbit[0] = true;
}

CNdasDeviceRegistrar::~CNdasDeviceRegistrar()
{
	XTLCALLTRACE2(TCService);
}

CNdasDevicePtr
CNdasDeviceRegistrar::Register(
	DWORD SlotNo,
	const NDAS_DEVICE_ID& DeviceId,
	DWORD dwFlags)
{
	//
	// this will lock this class from here
	// and releases the lock when the function returns;
	//
	InstanceAutoLock autolock(this);
	
	DBGPRT_INFO(_FT("Registering device %s at slot %d\n"), 
		LPCTSTR(CNdasDeviceId(DeviceId)), SlotNo);

	// If SlotNo is zero, automatically assign it.
	// check slot number
	if (0 == SlotNo)
	{
		SlotNo = LookupEmptySlot();
		if (0 == SlotNo)
		{
			return CNdasDevicePtr();
		}
	}
	else if (SlotNo > m_dwMaxSlotNo)
	{
		::SetLastError(NDASSVC_ERROR_INVALID_SLOT_NUMBER);
		return CNdasDevicePtr();
	}

	// check and see if the slot is occupied
	if (m_slotbit[SlotNo])
	{
		::SetLastError(NDASSVC_ERROR_SLOT_ALREADY_OCCUPIED);
		return CNdasDevicePtr();
	}

	// find an duplicate address
	{
		CNdasDevicePtr pExistingDevice = Find(DeviceId);
		if (0 != pExistingDevice.get()) 
		{
			::SetLastError(NDASSVC_ERROR_DUPLICATE_DEVICE_ENTRY);
			return CNdasDevicePtr();
		}
	}

	// register
	CNdasDevicePtr pDevice(new CNdasDevice(SlotNo, DeviceId, dwFlags));
	if (0 == pDevice.get()) 
	{
		// memory allocation failed
		// No need to set error here!
		return CNdasDevicePtr();
	}

	BOOL fSuccess = pDevice->Initialize();
	if (!fSuccess) 
	{
		DBGPRT_ERR_EX(_FT("Device initialization failed: "));
		return CNdasDevicePtr();
	}

	m_slotbit[SlotNo] = true;

	bool insertResult;

	XTLVERIFY( m_deviceSlotMap.insert(std::make_pair(SlotNo, pDevice)).second );
	//DeviceSlotMap::value_type(SlotNo, pDevice)).second;

	XTLVERIFY( m_deviceIdMap.insert(std::make_pair(DeviceId, pDevice)).second );
	//DeviceIdMap::value_type(DeviceId, pDevice)).second;

	XTLASSERT(m_deviceSlotMap.size() == m_deviceIdMap.size());

	if (dwFlags & NDAS_DEVICE_REG_FLAG_VOLATILE)
	{
	}
	else
	{
		XTL::CStaticStringBuffer<30> containerName(_T("Devices\\%04d"), SlotNo);
		fSuccess = _NdasSystemCfg.SetSecureValueEx(
			containerName, 
			_T("DeviceID"), 
			&DeviceId, 
			sizeof(DeviceId));

		if (!fSuccess) 
		{
			DBGPRT_WARN_EX(
				_FT("Writing registration entry to the registry failed at %s.\n"), 
				containerName.ToString());
		}

		fSuccess = _NdasSystemCfg.SetSecureValueEx(
			containerName,
			_T("RegFlags"),
			&dwFlags,
			sizeof(dwFlags));

		if (!fSuccess) 
		{
			DBGPRT_WARN_EX(
				_FT("Writing registration entry to the registry failed at %s.\n"), 
				containerName.ToString());
		}
	}

	//
	// During bootstrapping, we do not publish this event
	// Bootstrap process will publish an event later
	//
	if (!m_fBootstrapping) 
	{
		(void) m_service.GetEventPublisher().DeviceEntryChanged();
	}

	return pDevice;
}

CNdasDevicePtr
CNdasDeviceRegistrar::Register(
	const NDAS_DEVICE_ID& DeviceId,
	DWORD dwFlags)
{
	return Register(0, DeviceId, dwFlags);
}

BOOL 
CNdasDeviceRegistrar::Unregister(const NDAS_DEVICE_ID& DeviceId)
{
	InstanceAutoLock autolock(this);

	CNdasDeviceId cdevid(DeviceId);
	DBGPRT_INFO(_FT("Unregister device %s\n"), cdevid.ToString());

	DeviceIdMap::iterator itrId = m_deviceIdMap.find(DeviceId);
	if (m_deviceIdMap.end() == itrId) 
	{
		::SetLastError(NDASSVC_ERROR_DEVICE_ENTRY_NOT_FOUND);
	}

	CNdasDevicePtr pDevice = itrId->second;
	
	if (pDevice->GetStatus() != NDAS_DEVICE_STATUS_DISABLED) 
	{
		::SetLastError(NDASSVC_ERROR_CANNOT_UNREGISTER_ENABLED_DEVICE);
		return FALSE;
	}

	DWORD SlotNo = pDevice->GetSlotNo();
	XTLASSERT(0 != SlotNo);

	DeviceSlotMap::iterator itrSlot = m_deviceSlotMap.find(SlotNo);

	m_deviceIdMap.erase(itrId);
	m_deviceSlotMap.erase(itrSlot);
	m_slotbit[SlotNo] = false;

	XTL::CStaticStringBuffer<30> containerName(_T("Devices\\%04d"), SlotNo);
	BOOL fSuccess = _NdasSystemCfg.DeleteContainer(containerName, TRUE);
	if (!fSuccess) 
	{
		DBGPRT_WARN_EX(
			_FT("Deleting registration entry from the registry failed at %s.\n"), 
			containerName);
	}

	(void) m_service.GetEventPublisher().DeviceEntryChanged();

	return TRUE;
}

BOOL
CNdasDeviceRegistrar::Unregister(DWORD SlotNo)
{
	InstanceAutoLock autolock(this);

	DeviceSlotMap::iterator slot_itr = m_deviceSlotMap.find(SlotNo);

	if (m_deviceSlotMap.end() == slot_itr) 
	{
		// TODO: Make more specific error code
		::SetLastError(NDASSVC_ERROR_DEVICE_ENTRY_NOT_FOUND);
		return FALSE;
	}

	CNdasDevicePtr pDevice = slot_itr->second;

	if (NDAS_DEVICE_STATUS_DISABLED != pDevice->GetStatus()) 
	{
		// TODO: ::SetLastError(NDAS_ERROR_CANNOT_UNREGISTER_ENABLED_DEVICE);
		// TODO: Make more specific error code
		::SetLastError(NDASSVC_ERROR_CANNOT_UNREGISTER_ENABLED_DEVICE);
		return FALSE;
	}

	DeviceIdMap::iterator id_itr = m_deviceIdMap.find(pDevice->GetDeviceId());

	XTLASSERT(m_deviceIdMap.end() != id_itr);

	m_deviceIdMap.erase(id_itr);
	m_deviceSlotMap.erase(slot_itr);
	m_slotbit[SlotNo] = FALSE;

	XTL::CStaticStringBuffer<30> containerName(_T("Devices\\%04d"), SlotNo);
	BOOL fSuccess = _NdasSystemCfg.DeleteContainer(containerName, FALSE);
	
	if (!fSuccess) 
	{
		DBGPRT_WARN_EX(
			_FT("Deleting registration entry from the registry failed at %s.\n"), 
			containerName);
	}

	(void) m_service.GetEventPublisher().DeviceEntryChanged();

	return TRUE;

}


CNdasDevicePtr
CNdasDeviceRegistrar::Find(DWORD SlotNo)
{
	InstanceAutoLock autolock(this);
	DeviceSlotMap::const_iterator itr = m_deviceSlotMap.find(SlotNo);
	CNdasDevicePtr pDevice = (m_deviceSlotMap.end() == itr) ? CNdasDevicePtr() : itr->second;
	return pDevice;
}

CNdasDevicePtr
CNdasDeviceRegistrar::Find(const NDAS_DEVICE_ID& DeviceId)
{
	InstanceAutoLock autolock(this);
	DeviceIdMap::const_iterator itr = m_deviceIdMap.find(DeviceId);
	CNdasDevicePtr pDevice = (m_deviceIdMap.end() == itr) ? CNdasDevicePtr() : itr->second;
	return pDevice;
}

CNdasDevicePtr
CNdasDeviceRegistrar::Find(const NDAS_DEVICE_ID_EX& Device)
{
	return Device.UseSlotNo ? Find(Device.SlotNo) : Find(Device.DeviceId);
}

BOOL
CNdasDeviceRegistrar::ImportLegacyEntry(DWORD SlotNo, HKEY hEntryKey)
{
	static CONST size_t CB_ADDR = sizeof(TCHAR) * 18;

	HRESULT hr = E_FAIL;
	TCHAR szAddrVal[CB_ADDR + 1];
	DWORD cbAddrVal = sizeof(szAddrVal);
	DWORD dwValueType;

	LONG lResult = ::RegQueryValueEx(
		hEntryKey, 
		_T("Address"), 
		0, 
		&dwValueType, 
		(LPBYTE)szAddrVal,
		&cbAddrVal);

	if (ERROR_SUCCESS != lResult) 
	{
		// Ignore invalid values
		return FALSE;
	}

	if (cbAddrVal != CB_ADDR) 
	{
		DBGPRT_ERR(_FT("Invalid Entry(A): %s, ignored\n"), szAddrVal);
		return FALSE;
	}

	//
	// 00:0B:D0:00:D4:2F to NDAS_DEVICE_ID
	//

	NDAS_DEVICE_ID deviceId = {0};
	BOOL fSuccess = pConvertStringToDeviceId(szAddrVal, &deviceId);
	if (!fSuccess) 
	{
		DBGPRT_ERR(_FT("Invalid Entry(D): %s, ignored\n"), szAddrVal);
		return FALSE;
	}

	DBGPRT_INFO(_FT("Importing an entry: %s\n"), CNdasDeviceId(deviceId).ToString());

	TCHAR szNameVal[MAX_NDAS_DEVICE_NAME_LEN + 1] = {0};
	DWORD cbNameVal = sizeof(szNameVal);
	lResult = ::RegQueryValueEx(
		hEntryKey,
		_T("Name"),
		0,
		&dwValueType,
		(LPBYTE)szNameVal,
		&cbNameVal);

	if (ERROR_SUCCESS != lResult || _T('\0') == szNameVal[0]) 
	{
		TCHAR szDefaultName[MAX_NDAS_DEVICE_NAME_LEN + 1] = {0};
		fSuccess = _NdasSystemCfg.GetValueEx(
			_T("Devices"), 
			_T("DefaultPrefix"),
			szDefaultName,
			sizeof(szDefaultName));

		if (!fSuccess) 
		{
			hr = ::StringCchCopy(
				szDefaultName, 
				MAX_NDAS_DEVICE_NAME_LEN + 1,
				_T("NDAS Device "));
			XTLASSERT(SUCCEEDED(hr));
		}

		hr = ::StringCchPrintf(
			szNameVal,
			MAX_NDAS_DEVICE_NAME_LEN,
			_T("%s %d"), 
			szDefaultName, 
			SlotNo);
	}


	BYTE pbSerialKeyVal[9];
	DWORD cbSerialKeyVal = sizeof(pbSerialKeyVal);
	lResult = ::RegQueryValueEx(
		hEntryKey,
		_T("SerialKey"),
		0,
		&dwValueType,
		(LPBYTE)pbSerialKeyVal,
		&cbSerialKeyVal);

	if (ERROR_SUCCESS != lResult) 
	{
		return FALSE;
	}

	if (cbSerialKeyVal != sizeof(pbSerialKeyVal)) 
	{
		return FALSE;
	}

	ACCESS_MASK fAccessMode = GENERIC_READ;
	if (0xFF == pbSerialKeyVal[8]) 
	{
		// Registered as RW
		fAccessMode |= GENERIC_WRITE;
	}
	else if (0x00 == pbSerialKeyVal[8]) 
	{
		// Registered as RO
	}
	else 
	{
		// Invalid value
		return FALSE;
	}

	CNdasDevicePtr pDevice = Register(SlotNo, deviceId, 0);
	if (0 == pDevice.get()) 
	{
		DBGPRT_ERR_EX(_FT("Failed to register %s at %d during import: "),
			CNdasDeviceId(deviceId).ToString(),
			SlotNo);
		return FALSE;
	}

	// Always enable this!
	XTLVERIFY( pDevice->Enable(TRUE) );
	pDevice->SetName(szNameVal);
	pDevice->SetGrantedAccess(fAccessMode);

	return TRUE;
}

BOOL
CNdasDeviceRegistrar::ImportLegacySettings()
{
	BOOL fSuccess = FALSE;

	DBGPRT_INFO(_FT("Importing a legacy settings.\n"));

	XTL::AutoKeyHandle hRootKey;
	LONG lResult = ::RegOpenKeyEx(
		HKEY_LOCAL_MACHINE,
		_T("SOFTWARE\\XIMETA\\NetDisks"),
		0,
		KEY_READ,
		&hRootKey);

	if (ERROR_SUCCESS != lResult) 
	{
		DBGPRT_ERR_EX(_FT("Opening a legacy configuration key has failed:"));
		return FALSE;
	}

	DWORD n = 0;
	
	while (TRUE) 
	{

		static CONST DWORD MAX_KEY_NAME = 200;
		FILETIME ftLastWritten;
		TCHAR szKeyName[MAX_KEY_NAME] = {0};
		DWORD cchKeyName = MAX_KEY_NAME;
		lResult = ::RegEnumKeyEx(
			hRootKey, 
			n, 
			szKeyName, 
			&cchKeyName,
			0,
			NULL,
			NULL,
			&ftLastWritten);

		if (ERROR_SUCCESS != lResult) 
		{
			break;
		}

		int iKeyName;
		fSuccess = ::StrToIntEx(szKeyName, STIF_DEFAULT, &iKeyName);
		if (!fSuccess || 0 == iKeyName) 
		{
			continue;
		}

		XTL::AutoKeyHandle hSubKey;
		lResult = ::RegOpenKeyEx(
			hRootKey,
			szKeyName,
			0,
			KEY_READ,
			&hSubKey);

		if (ERROR_SUCCESS != lResult) 
		{
			continue;
		}


		ImportLegacyEntry(iKeyName, hSubKey);

		++n;
	}

	return TRUE;
}

BOOL
CNdasDeviceRegistrar::Bootstrap()
{
	BOOL fSuccess = FALSE;
	BOOL fMigrated = FALSE;

	//
	// Set bootstrapping flag to prevent multiple events 
	// for DeviceSetChange Events
	//

	m_fBootstrapping = TRUE;

	TCHAR szSubcontainer[30] = {0};
	for (DWORD i = 0; i < MAX_SLOT_NUMBER; ++i) 
	{
		HRESULT hr = ::StringCchPrintf(szSubcontainer, 30, TEXT("%s\\%04d"), CFG_CONTAINER, i);
		XTLASSERT(SUCCEEDED(hr) && "CFG_CONTAINER is too large???");

		BOOL fAutoRegistered = FALSE;
		fSuccess = _NdasSystemCfg.GetValueEx(
			szSubcontainer,
			_T("AutoRegistered"),
			&fAutoRegistered);

		if (fSuccess && fAutoRegistered)
		{
			DBGPRT_WARN_EX(_FT("Deleting %s: "), szSubcontainer);
			// Auto registered devices are not persistent
			// it is an error to show up here.
			// We just ignore those entries
			fSuccess = _NdasSystemCfg.DeleteContainer(szSubcontainer, TRUE);
			if (!fSuccess)
			{
				DBGPRT_WARN_EX(_FT("Deleting %s failed: "), szSubcontainer);
			}
			continue;
		}

		NDAS_DEVICE_ID deviceId;

		fSuccess = _NdasSystemCfg.GetSecureValueEx(
			szSubcontainer, 
			_T("DeviceID"),
			&deviceId,
			sizeof(deviceId));

		// ignore read fault - tampered or not exists
		if (!fSuccess) 
		{
			continue;
		}

		DWORD dwRegFlags;
		fSuccess = _NdasSystemCfg.GetSecureValueEx(
			szSubcontainer,
			_T("RegFlags"),
			&dwRegFlags,
			sizeof(dwRegFlags));

		if (!fSuccess)
		{
			dwRegFlags = NDAS_DEVICE_REG_FLAG_NONE;
		}

		CNdasDevicePtr pDevice = Register(i, deviceId, dwRegFlags);
		XTLASSERT(CNdasDeviceNullPtr != pDevice && "Failure of registration should not happed during bootstrap!");
		// This may happen due to auto-register feature!
		if (CNdasDeviceNullPtr == pDevice) 
		{
			continue;
		}

		DWORD cbUsed;
		NDAS_OEM_CODE oemCode;
		fSuccess = _NdasSystemCfg.GetSecureValueEx(
			szSubcontainer,
			_T("OEMCode"),
			&oemCode,
			sizeof(NDAS_OEM_CODE),
			&cbUsed);

		if (fSuccess && cbUsed == sizeof(NDAS_OEM_CODE))
		{
			pDevice->SetOemCode(oemCode);
		}

		ACCESS_MASK grantedAccess = GENERIC_READ;
		const DWORD cbBuffer = sizeof(ACCESS_MASK) + sizeof(NDAS_DEVICE_ID);
		BYTE pbBuffer[cbBuffer];
		
		fSuccess = _NdasSystemCfg.GetSecureValueEx(
			szSubcontainer,
			_T("GrantedAccess"),
			pbBuffer,
			cbBuffer);

		if (fSuccess) 
		{
			grantedAccess = *((ACCESS_MASK*)(pbBuffer));
		}
		grantedAccess |= GENERIC_READ; // to prevent invalid access mask configuration
		// XTLASSERT(grantedAccess & GENERIC_READ); // invalid configuration?
		pDevice->SetGrantedAccess(grantedAccess);

		TCHAR szDeviceName[MAX_NDAS_DEVICE_NAME_LEN + 1];
		fSuccess = _NdasSystemCfg.GetValueEx(
			szSubcontainer, 
			_T("DeviceName"),
			szDeviceName,
			sizeof(TCHAR)*(MAX_NDAS_DEVICE_NAME_LEN + 1));

		if (fSuccess) 
		{
			pDevice->SetName(szDeviceName);
		}

		BOOL fEnabled = FALSE;
		fSuccess = _NdasSystemCfg.GetValueEx(
			szSubcontainer,
			_T("Enabled"),
			&fEnabled);

		if (fSuccess && fEnabled) 
		{
			pDevice->Enable(fEnabled);
		}
	}

	//
	// Migration will be done only once 
	// if there is no registered devices in the current configurations
	// and if the migration flag (Install\Migrate = 1) is set
	//
	if (m_deviceSlotMap.size() == 0) 
	{
		fSuccess = _NdasSystemCfg.GetValueEx(_T("Install"), _T("Migrated"), &fMigrated);
		if (!fSuccess || !fMigrated) 
		{
			fMigrated = TRUE;
			ImportLegacySettings();
			_NdasSystemCfg.SetValueEx(_T("Install"), _T("Migrated"), fMigrated);
		}
	}

	//
	// Clear bootstrapping state
	//
	m_fBootstrapping = FALSE;

	return TRUE;
}

namespace
{
	// not used at this time (reserved for the future use)
	void 
	pDeleteAutoRegistered(
		const DeviceSlotMap::value_type& entry)
	{
		DWORD SlotNo = entry.first;
		CNdasDevicePtr pDevice = entry.second;
		if (pDevice->IsAutoRegistered())
		{
			TCHAR szContainer[30];
			HRESULT hr = ::StringCchPrintf(
				szContainer, 
				30, 
				_T("Devices\\%04d"), 
				SlotNo);
			_ASSERT(SUCCEEDED(hr));

			BOOL fSuccess = _NdasSystemCfg.DeleteContainer(szContainer, TRUE);
		}
	}
}

// not used at this time (reserved for the future use)
void
CNdasDeviceRegistrar::OnServiceShutdown()
{
	InstanceAutoLock autolock(this);
	std::for_each(
		m_deviceSlotMap.begin(), 
		m_deviceSlotMap.end(), 
		pDeleteAutoRegistered);
}

// not used at this time (reserved for the future use)
void
CNdasDeviceRegistrar::OnServiceStop()
{
	InstanceAutoLock autolock(this);

	std::for_each(
		m_deviceSlotMap.begin(), 
		m_deviceSlotMap.end(), 
		pDeleteAutoRegistered);
}

DWORD
CNdasDeviceRegistrar::LookupEmptySlot()
{
	InstanceAutoLock autolock(this);

	std::vector<bool>::const_iterator empty_begin = std::find(
		m_slotbit.begin(), m_slotbit.end(), 
		false);

	if (empty_begin == m_slotbit.end()) 
	{
		::SetLastError(NDASSVC_ERROR_DEVICE_ENTRY_SLOT_FULL);
		return 0;
	}

	DWORD emptySlotNo = static_cast<DWORD>(
		std::distance<std::vector<bool>::const_iterator>(
			m_slotbit.begin(), empty_begin));

	return emptySlotNo ;
}

DWORD
CNdasDeviceRegistrar::Size()
{
	InstanceAutoLock autolock(this);
	return m_deviceSlotMap.size();
}

DWORD
CNdasDeviceRegistrar::MaxSlotNo()
{
	return m_dwMaxSlotNo;
}

void 
CNdasDeviceRegistrar::GetItems(CNdasDeviceVector& dest)
{
	dest.clear();
	dest.reserve(m_deviceSlotMap.size());
	std::transform(
		m_deviceSlotMap.begin(), m_deviceSlotMap.end(),
		std::back_inserter(dest),
		select2nd<DeviceSlotMap::value_type>());
}

void 
CNdasDeviceRegistrar::Cleanup()
{
	m_deviceSlotMap.clear();
	m_deviceIdMap.clear();
}

namespace
{

BOOL
pCharToHex(TCHAR c, LPDWORD lpValue)
{
	XTLASSERT(!IsBadWritePtr(lpValue, sizeof(DWORD)));
	if (c >= _T('0') && c <= _T('9')) {
		*lpValue = c - _T('0') + 0x0;
		return TRUE;
	} else if (c >= _T('A') && c <= _T('F')) {
		*lpValue = c - _T('A') + 0xA;
		return TRUE;
	} else if (c >= _T('a') && c <= _T('f')) {
		*lpValue = c - _T('a') + 0xA;
		return TRUE;
	}
	return FALSE;
}

BOOL
pConvertStringToDeviceId(
	LPCTSTR szAddressString, 
	NDAS_DEVICE_ID* pDeviceId)
{
	XTLASSERT(!IsBadStringPtr(szAddressString,-1));
	XTLASSERT(!IsBadWritePtr(pDeviceId, sizeof(NDAS_DEVICE_ID)));

	static CONST size_t CCH_ADDR = 17;
	size_t cch = 0;
	HRESULT hr = StringCchLength(szAddressString, STRSAFE_MAX_CCH, &cch);
	if (FAILED(hr) || cch != CCH_ADDR) {
		return FALSE;
	}

	for (DWORD i = 0; i < 6; ++i) {
		CONST TCHAR* psz = szAddressString + i * 3;
		DWORD v1 = 0, v2 = 0;
		BOOL fSuccess = 
			pCharToHex(psz[0], &v1) &&
			pCharToHex(psz[1], &v2);
		if (!fSuccess) {
			return FALSE;
		}
		pDeviceId->Node[i] = static_cast<BYTE>(v1) * 0x10 + static_cast<BYTE>(v2);
	}
	return TRUE;
}

} // namespace


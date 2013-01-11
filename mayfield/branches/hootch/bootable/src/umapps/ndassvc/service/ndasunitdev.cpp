#include "stdafx.h"
#include <lfsfiltctl.h>
#include <ndasbusioctl.h>
#include <ndas/ndasmsg.h>
#include <ndas/ndasdib.h>

#include "ndasdev.h"
#include "ndashixcli.h"
#include "ndasobjs.h"
#include "ndascfg.h"
#include "sysutil.h"
// #include "scrc32.h"
#include "ndasdevcomm.h"
#include "ndasunitdevfactory.h"
#include "traceflags.h"

//////////////////////////////////////////////////////////////////////////
//
// CNdasUnitDevice class implementation
//
//////////////////////////////////////////////////////////////////////////

namespace
{

NDAS_LOGICALDEVICE_TYPE
pUnitDeviceLogicalDeviceType(
	NDAS_UNITDEVICE_TYPE udType,
	NDAS_UNITDEVICE_SUBTYPE udSubtype);

NDAS_UNITDEVICE_ID 
pCreateUnitDeviceId(
	const CNdasDevicePtr& pDevice,
	DWORD unitNo);

}

//
// Constructor
//

CNdasUnitDevice::CNdasUnitDevice(
	CNdasDevicePtr pParentDevice, 
	DWORD dwUnitNo, 
	NDAS_UNITDEVICE_TYPE type,
	NDAS_UNITDEVICE_SUBTYPE subType,
	const NDAS_UNITDEVICE_HARDWARE_INFO& unitDeviceInfo,
	const NDAS_LOGICALDEVICE_GROUP& ldGroup,
	DWORD ldSequence) :
	CStringizer(_T("%s.%02d"), pParentDevice->ToString(), dwUnitNo),
	m_pParentDevice(pParentDevice),
	m_unitDeviceId(pCreateUnitDeviceId(pParentDevice,dwUnitNo)),
	m_type(type),
	m_subType(subType),
	m_status(NDAS_UNITDEVICE_STATUS_NOT_MOUNTED),
	m_lastError(NDAS_UNITDEVICE_ERROR_NONE),
	m_udinfo(unitDeviceInfo),
	m_ldGroup(ldGroup),
	m_ldSequence(ldSequence),
	m_bSupposeFault(FALSE)
{
	XTLCALLTRACE2(TCUnitDevice);

	::ZeroMemory(
		&m_PrimaryHostInfo, 
		sizeof(NDAS_UNITDEVICE_PRIMARY_HOST_INFO));

	HRESULT hr = ::StringCchPrintf(
		m_szRegContainer,
		30,
		_T("Devices\\%04d\\%04d"),
		pParentDevice->GetSlotNo(),
		m_unitDeviceId.UnitNo);

	XTLASSERT(SUCCEEDED(hr));
}


//
// Destructor
//
CNdasUnitDevice::~CNdasUnitDevice()
{
	XTLTRACE2(TCUnitDevice,TLTrace,__FUNCTION__ "%ws\n", ToString());
}

const NDAS_UNITDEVICE_ID&
CNdasUnitDevice::GetUnitDeviceId() const
{
	return m_unitDeviceId;
}

void 
CNdasUnitDevice::GetUnitDeviceId(NDAS_UNITDEVICE_ID* pUnitDeviceId) const
{
	*pUnitDeviceId = m_unitDeviceId;
}


const NDAS_UNITDEVICE_HARDWARE_INFO& 
CNdasUnitDevice::GetHardwareInfo()
{
	return m_udinfo;
}

void 
CNdasUnitDevice::GetHardwareInfo(PNDAS_UNITDEVICE_HARDWARE_INFO pudinfo)
{
	XTLASSERT(!IsBadWritePtr(pudinfo, sizeof(NDAS_UNITDEVICE_HARDWARE_INFO)));
	{
		InstanceAutoLock autolock(this);
		::CopyMemory(pudinfo, &m_udinfo, sizeof(NDAS_UNITDEVICE_HARDWARE_INFO));
	}
}

const NDAS_UNITDEVICE_STAT& 
CNdasUnitDevice::GetStats()
{
	CNdasDevicePtr pParentDevice(m_pParentDevice);
	return pParentDevice->GetStats().UnitDevices[m_unitDeviceId.UnitNo];
}

void 
CNdasUnitDevice::GetStats(NDAS_UNITDEVICE_STAT& stat)
{
	InstanceAutoLock autolock(this);
	stat = GetStats();
}

ACCESS_MASK
CNdasUnitDevice::GetGrantedAccess()
{
	InstanceAutoLock autolock(this);
	CNdasDevicePtr pParentDevice(m_pParentDevice);
	return pParentDevice->GetGrantedAccess();
}

ACCESS_MASK
CNdasUnitDevice::GetAllowingAccess()
{
	InstanceAutoLock autolock(this);
	ACCESS_MASK granted = GetGrantedAccess();
	//
	// depends on the implementation of the driver
	// whether allows multiple write access or not
	//
	// TODO: Refine this!
	//
	return granted;
}

UINT64
CNdasUnitDevice::GetUserBlockCount()
{
	InstanceAutoLock autolock(this);
	return m_udinfo.SectorCount.QuadPart;
}

UINT64
CNdasUnitDevice::GetPhysicalBlockCount()
{
	InstanceAutoLock autolock(this);
	return m_udinfo.SectorCount.QuadPart;
}

BOOL
CNdasUnitDevice::UpdateStats()
{
	CNdasDevicePtr pParentDevice(m_pParentDevice);
	return pParentDevice->UpdateStats();
}

BOOL
CNdasUnitDevice::GetActualHostUsageCount(
	LPDWORD lpnROHosts, 
	LPDWORD lpnRWHosts, 
	BOOL bUpdate)
{
	InstanceAutoLock autolock(this);

	DWORD nROHosts = 0;
	DWORD nRWHosts = 0;

	if (!GetHostUsageCount(&nROHosts, &nRWHosts, bUpdate))
	{
		return FALSE;
	}

	//
	// Following cases does not need further consideration
	// - RW = 0, RO any
	// - RW = 1, RO = 0
	// Neither RO or RW is not NDAS_HOST_COUNT_UNKNOWN
	// Otherwise, we need HIX to discover the actual count
	//
	if ((nRWHosts == 0 || nROHosts == 0) &&
		(NDAS_HOST_COUNT_UNKNOWN != nRWHosts) &&
		(NDAS_HOST_COUNT_UNKNOWN != nROHosts))
	{
		*lpnROHosts = nROHosts;
		*lpnRWHosts = nRWHosts;
		return TRUE;
	}

	DWORD nHosts = 
		(NDAS_HOST_COUNT_UNKNOWN == nRWHosts || NDAS_HOST_COUNT_UNKNOWN == nROHosts) ? 
		NDAS_MAX_CONNECTION_V11 : nROHosts + nRWHosts;

	NDAS_UNITDEVICE_ID unitDeviceId = GetUnitDeviceId();

	//
	// BUG: Workaround for weird lock
	// We should unlock the global lock (yes, it's global lock at the moment)
	// for local HIX server to work!
	//

	autolock.Release();

	CNdasHIXDiscover hixdisc(pGetNdasHostGuid());
	BOOL fSuccess = hixdisc.Initialize();
	if (!fSuccess) 
	{
		*lpnROHosts = nROHosts;
		*lpnRWHosts = nRWHosts;
		return TRUE;
	}

	fSuccess = hixdisc.Discover(
		unitDeviceId,
		NHIX_UDA_READ_ACCESS, // read bit is set - all hosts
		nHosts,
		2000);

	if (!fSuccess) 
	{
		*lpnROHosts = nROHosts;
		*lpnRWHosts = nRWHosts;
		return TRUE;
	}

	if (NDAS_HOST_COUNT_UNKNOWN == nRWHosts || NDAS_HOST_COUNT_UNKNOWN == nROHosts)
	{
		// If any host count is unknown, use HIX counter only
		nROHosts = 0;
		nRWHosts = 0;
		DWORD nRepliedHosts = hixdisc.GetHostCount(unitDeviceId);
		for (DWORD i = 0; i < nRepliedHosts; ++i) 
		{
			NHIX_UDA uda = 0;
			fSuccess = hixdisc.GetHostData(unitDeviceId,i,&uda);
			XTLASSERT(fSuccess); // index must be valid!
			if (NHIX_UDA_SHARED_READ_WRITE_SECONDARY_ACCESS == uda ||
				NHIX_UDA_SHARED_READ_WRITE_PRIMARY_ACCESS == uda ||
				NHIX_UDA_READ_WRITE_ACCESS == uda)
			{
				++nRWHosts;
			}
			else if (uda == NHIX_UDA_READ_ACCESS)
			{
				++nROHosts;
			}
			else
			{
				XTLTRACE2(TCUnitDevice, TLWarning, "Invalid uda %08X\n", uda);
			}
		}
	}
	else
	{
		// Otherwise, use SharedRW counter
		DWORD nRepliedHosts = hixdisc.GetHostCount(unitDeviceId);
		for (DWORD i = 0; i < nRepliedHosts; ++i) 
		{
			NHIX_UDA uda = 0;
			fSuccess = hixdisc.GetHostData(unitDeviceId,i,&uda);
			XTLASSERT(fSuccess); // index must be valid!
			if (uda == NHIX_UDA_SHARED_READ_WRITE_SECONDARY_ACCESS)
			{
				--nROHosts;
				++nRWHosts;
			}
		}
	}

	*lpnROHosts = nROHosts;
	*lpnRWHosts = nRWHosts;

	XTLTRACE2(TCUnitDevice, TLInfo,
		"Actual Host Usage Count: RO %d, RW %d.\n",
		*lpnROHosts, 
		*lpnRWHosts);

	return TRUE;
}

BOOL
CNdasUnitDevice::GetHostUsageCount(
	LPDWORD lpnROHosts, 
	LPDWORD lpnRWHosts, 
	BOOL fUpdate)
{
	InstanceAutoLock autolock(this);

	XTLASSERT(!IsBadWritePtr(lpnROHosts, sizeof(DWORD)));
	XTLASSERT(!IsBadWritePtr(lpnRWHosts, sizeof(DWORD)));

	CNdasDevicePtr pParentDevice(m_pParentDevice);

	if (fUpdate) 
	{
		BOOL fSuccess = pParentDevice->UpdateStats();
		if (!fSuccess) 
		{
			XTLTRACE2_ERR(TCUnitDevice, TLError, "Update device status failed.\n");
			return FALSE;
		}
	}

	const NDAS_DEVICE_STAT& dstat = pParentDevice->GetStats();
	*lpnROHosts = dstat.UnitDevices[m_unitDeviceId.UnitNo].ROHostCount;
	*lpnRWHosts = dstat.UnitDevices[m_unitDeviceId.UnitNo].RWHostCount;

	XTLTRACE2(TCUnitDevice, TLInfo,
		"Host Usage Count: RO %d, RW %d.\n", 
		*lpnROHosts, 
		*lpnRWHosts);

	return TRUE;
}

void
CNdasUnitDevice::UpdatePrimaryHostInfo(
	const NDAS_UNITDEVICE_PRIMARY_HOST_INFO& info)
{
	InstanceAutoLock autolock(this);

	::CopyMemory(
		&m_PrimaryHostInfo,
		&info,
		sizeof(NDAS_UNITDEVICE_PRIMARY_HOST_INFO));

	m_PrimaryHostInfo.LastUpdate = ::GetTickCount();

	XTLTRACE2(TCUnitDevice, TLNoise,
		"Primary Host Usage Updated: %ws, Timestamp %d\n", 
		this->ToString(),
		m_PrimaryHostInfo.LastUpdate);
}

void 
CNdasUnitDevice::_SetStatus(NDAS_UNITDEVICE_STATUS newStatus)
{ 
	InstanceAutoLock autolock(this);

	m_status = newStatus; 
}

bool
CNdasUnitDevice::RegisterToLogicalDeviceManager()
{
	InstanceAutoLock autolock(this);

	CNdasLogicalDeviceManager& manager = pGetNdasLogicalDeviceManager();
	m_pLogicalDevice = manager.Register(shared_from_this());
	if (CNdasLogicalDeviceNullPtr == m_pLogicalDevice) 
	{
		XTLTRACE2_ERR(TCUnitDevice, TLError, 
			"Failed to register a unit device to the LDM.\n");
		return false;
	}

	return true;
}

bool 
CNdasUnitDevice::UnregisterFromLogicalDeviceManager()
{
	InstanceAutoLock autolock(this);
	CNdasLogicalDeviceManager& manager = pGetNdasLogicalDeviceManager();
	if (manager.Unregister(shared_from_this()))
	{
		m_pLogicalDevice.reset();
		return true;
	}
	return false;
}


void
CNdasUnitDevice::SetFault(bool bFault)
{
	m_bSupposeFault = bFault;
}

bool
CNdasUnitDevice::IsFault()
{
	return m_bSupposeFault;
}

UINT64 
CNdasUnitDevice::GetDevicePassword()
{
	CNdasDevicePtr pParentDevice(m_pParentDevice);
	return pParentDevice->GetHardwarePassword();
}

DWORD 
CNdasUnitDevice::GetDeviceUserID(ACCESS_MASK access)
{
	const DWORD RO_SET[] = { 0x00000001, 0x00000002 };
	const DWORD RW_SET[] = { 0x00010001, 0x00020002 };
	
	const DWORD* userIdSet = (GENERIC_WRITE & access) ? RW_SET : RO_SET;

	XTLASSERT(m_unitDeviceId.UnitNo < RTL_NUMBER_OF(RO_SET));

	DWORD index = min(m_unitDeviceId.UnitNo, RTL_NUMBER_OF(RO_SET) - 1);

	return userIdSet[index];
}

BOOL
CNdasUnitDevice::CheckNDFSCompatibility()
{
	InstanceAutoLock autolock(this);
	//
	// Unit devices other than disks are not allowed for NDFS
	//
	if (m_type != NDAS_UNITDEVICE_TYPE_DISK) 
	{
		return FALSE;
	}

	//
	// LfsFilter compatibility check.
	// NDFS Major version should be same
	// 
	WORD wHostNDFSVerMajor;
	BOOL fSuccess = ::LfsFiltCtlGetVersion(
		NULL, NULL, NULL, NULL,
		&wHostNDFSVerMajor, NULL);

	if (!fSuccess) 
	{
		XTLTRACE2_ERR(TCUnitDevice, TLError, "Getting LFS Filter Version failed.\n");
		return FALSE;
	}

	//
	// Primary Host Info is valid for 30 seconds
	//
#if 0
	DWORD dwMaxTickAllowance = 30 * 1000;
	if (0 != m_PrimaryHostInfo.NDFSCompatVersion &&
		::GetTickCount() < m_PrimaryHostInfo.LastUpdate + dwMaxTickAllowance) 
	{
		// primary host info is valid
		return (wHostNDFSVerMajor == m_PrimaryHostInfo.NDFSCompatVersion);
	}
#endif

	//
	// No Primary Host Info is available (IX)
	// Use HIX to discover
	//
	CNdasHIXDiscover hixdisc(pGetNdasHostGuid());
	fSuccess = hixdisc.Initialize();
	if (!fSuccess) 
	{
		XTLTRACE2_ERR(TCUnitDevice, TLError, "HIXDiscover init failed.\n");
		return FALSE;
	}

	NDAS_UNITDEVICE_ID udid = GetUnitDeviceId();

	DWORD timeout = NdasServiceConfig::Get(nscWriteShareCheckTimeout);

	fSuccess = hixdisc.Discover(udid,NHIX_UDA_SHRW_PRIM,1,timeout);
	if (!fSuccess)
	{
		XTLTRACE2_ERR(TCUnitDevice, TLError, "hixdisc.Discover failed.\n");
		return FALSE;
	}

	DWORD nHosts = hixdisc.GetHostCount(udid);
	if (0 == nHosts) 
	{
		XTLTRACE2_ERR(TCUnitDevice, TLError, "GetHostCount failed.\n");
		return FALSE;
	}

	GUID hostGuid;
	fSuccess = hixdisc.GetHostData(udid,0,NULL,&hostGuid,NULL,NULL);
	if (!fSuccess) 
	{
		XTLTRACE2_ERR(TCUnitDevice, TLError, "GetHostData failed.\n");
		return FALSE;
	}

	CNdasHostInfoCache* phic = pGetNdasHostInfoCache();
	XTLASSERT(phic); // phic is not null (by pGetNdasHostInfoCache)
	const NDAS_HOST_INFO* pHostInfo = phic->GetHostInfo(&hostGuid);
	if (NULL == pHostInfo) 
	{
		XTLTRACE2_ERR(TCUnitDevice, TLError, "GetHostInfo failed.\n");
		return FALSE;
	}

	//
	// ReservedVerInfo contains NDFS Version Information
	//
	if (pHostInfo->ReservedVerInfo.VersionMajor != wHostNDFSVerMajor) 
	{
		XTLTRACE2(TCUnitDevice, TLError,
			"Host NDFS %d, Primary NDFS %d failed.\n",
			wHostNDFSVerMajor, pHostInfo->ReservedVerInfo.VersionMajor);
		return FALSE;
	}

	//
	// Primary and this host's NDFS compatible version is same
	//
	return TRUE;
}

DWORD 
CNdasUnitDevice::GetOptimalMaxRequestBlock()
{
	InstanceAutoLock autolock(this);

	sysutil::NDIS_MEDIUM ndisMedium;
	sysutil::NDIS_PHYSICAL_MEDIUM ndisPhysicalMedium;
	LONGLONG llLinkSpeed;

	CNdasDevicePtr pParentDevice(m_pParentDevice);

	pParentDevice->Lock();

	DWORD dwDefaultMRB = pParentDevice->GetMaxTransferBlocks();
	DWORD dwOptimalMRB;
	const LPX_ADDRESS localAddr = pParentDevice->GetLocalLpxAddress();

	UCHAR hardwareVersion = pParentDevice->GetHardwareVersion();

	pParentDevice->Unlock();

	BOOL fSuccess = sysutil::GetNetConnCharacteristics(
		6,
		localAddr.Node,
		&llLinkSpeed,
		&ndisMedium,
		&ndisPhysicalMedium);

	if (!fSuccess) {
		//
		// TODO: Do we have to more defensive to lower to 32?
		//
		return dwDefaultMRB;
	}

	DWORD lsMbps = static_cast<DWORD>(llLinkSpeed / 10000);

	//
	// Criteria
	//
	// Actually NdisMedium cannot be used for detecting Wireless LAN
	// Wireless WAN is completely different to Wireless LAN
	// 
	// If the physical medium is WirelessLan
	// MRB is less than 64
	// Wireless link speed is quite variable.
	// 54Mbps, 48Mbps, 36MBps, 24Mbps,...
	// 11Mbps, 5.5MBps, 2MBps, 1MBps...
	// 
	// The criteria is at this time, if the link speed is more than
	// 11MBps. MRB will be 32, otherwise 16
	// 
	// Otherwise (NdisPhysicalMediumMax (INVALID_VALUE) and 802_3
	// can be regarded as Ethernet Medium
	//
	// 100Mbps or higher: 128 (or 104 in V2.0)
	//
	// 

	// Otherwise        : 64
	//

	// Wireless
	if (ndisMedium == sysutil::NdisMedium802_3 &&
		ndisPhysicalMedium == sysutil::NdisPhysicalMediumWirelessLan)
	{
		if (lsMbps >= 11) {
			dwOptimalMRB = 32;
		} else {
			dwOptimalMRB = 16;
		}
	}
	// Otherwise
	else
	{
		if (lsMbps >= 100) {
			dwOptimalMRB = 128; // Limit is set below.
		} else {
			dwOptimalMRB = 32;
		}
	}

	//
	// There is a known bug in a giga chipset:
	// In a certain situation that DMA request for IDE device is overflowed,
	// it is suspected that FIFO queue data in NDAS chip is overwritten 
	// by the subsequent data. 
	// Limiting Max Request Block to 104 will alleviate or prevent
	// such a situation.
	//
	const LPCTSTR MAX_REQUEST_BLOCK_LIMIT_KEY = _T("MaxRequestBlockLimit");
	const DWORD MAX_REQUEST_BLOCK_LIMIT_DEFAULT = 
		(2 == hardwareVersion) ? 104 : 128;
	
	DWORD dwMaxRequestBlockLimit;
	TCHAR szMaxRequestBlockLimitKey[64];
	HRESULT hr = ::StringCchPrintf(
		szMaxRequestBlockLimitKey, 
		RTL_NUMBER_OF(szMaxRequestBlockLimitKey),
		_T("%s.%d"),
		MAX_REQUEST_BLOCK_LIMIT_KEY,
		hardwareVersion);
	XTLASSERT(SUCCEEDED(hr));

	fSuccess = _NdasSystemCfg.GetValueEx(
		_T("ndassvc"),
		szMaxRequestBlockLimitKey,
		&dwMaxRequestBlockLimit);
	if (!fSuccess || 0 == dwMaxRequestBlockLimit)
	{
		// MaxRequestBlockLimit.{Version} is not specified
		// Locate MaxRequestBlockLimit
		fSuccess = _NdasSystemCfg.GetValueEx(
			_T("ndassvc"),
			MAX_REQUEST_BLOCK_LIMIT_KEY,
			&dwMaxRequestBlockLimit);
		if (!fSuccess || 0 == dwMaxRequestBlockLimit)
		{
			dwMaxRequestBlockLimit = MAX_REQUEST_BLOCK_LIMIT_DEFAULT;
		}
	}

	if (dwOptimalMRB > dwMaxRequestBlockLimit)
	{
		dwOptimalMRB = dwMaxRequestBlockLimit;
	}

	if (dwOptimalMRB > dwDefaultMRB)
	{
		dwOptimalMRB = dwDefaultMRB;
	}

	return dwOptimalMRB;
}

const NDAS_LOGICALDEVICE_GROUP&
CNdasUnitDevice::GetLDGroup() const
{
	return m_ldGroup;
}

DWORD
CNdasUnitDevice::GetLDSequence() const
{
	return m_ldSequence;
}

CNdasDevicePtr
CNdasUnitDevice::GetParentDevice() const
{ 
	return CNdasDevicePtr(m_pParentDevice); 
}

DWORD 
CNdasUnitDevice::GetUnitNo() const
{ 
	return m_unitDeviceId.UnitNo; 
}

NDAS_UNITDEVICE_TYPE 
CNdasUnitDevice::GetType() const
{ 
	return m_type; 
}

NDAS_UNITDEVICE_SUBTYPE 
CNdasUnitDevice::GetSubType() const
{ 
	return m_subType; 
}

NDAS_UNITDEVICE_STATUS 
CNdasUnitDevice::GetStatus()
{ 
	InstanceAutoLock autolock(this);
	return m_status; 
}

NDAS_UNITDEVICE_ERROR 
CNdasUnitDevice::GetLastError()
{ 
	InstanceAutoLock autolock(this);
	return m_lastError; 
}

CNdasLogicalDevicePtr
CNdasUnitDevice::GetLogicalDevice()
{
	InstanceAutoLock autolock(this);
	return m_pLogicalDevice;
}

void
CNdasUnitDevice::OnMounted()
{
	_SetStatus(NDAS_UNITDEVICE_STATUS_MOUNTED);
}

void
CNdasUnitDevice::OnUnmounted()
{
	_SetStatus(NDAS_UNITDEVICE_STATUS_NOT_MOUNTED);
	CNdasDevicePtr pParentDevice(m_pParentDevice);
	pParentDevice->OnUnitDeviceUnmounted(shared_from_this());
}

bool
CNdasUnitDevice::IsVolatile() const
{
	const CNdasDevicePtr pParentDevice(m_pParentDevice);
	return pParentDevice->IsVolatile();
}

//////////////////////////////////////////////////////////////////////////
//
// CNdasUnitDiskDevice class implementation
//
//////////////////////////////////////////////////////////////////////////

//
// Constructor
//

CNdasUnitDiskDevice::CNdasUnitDiskDevice(
	CNdasDevicePtr pParentDevice, 
	DWORD dwUnitNo, 
	NDAS_UNITDEVICE_DISK_TYPE diskType,
	const NDAS_UNITDEVICE_HARDWARE_INFO& unitDevInfo,
	const NDAS_LOGICALDEVICE_GROUP& ldGroup,
	DWORD ldSequence,
	ULONG ulUserBlocks,
	PVOID pAddTargetInfo,
	const NDAS_CONTENT_ENCRYPT& contentEncrypt,
	NDAS_DIB_V2* pDIBv2) :
	m_ulUserBlocks(ulUserBlocks),
	m_pAddTargetInfo(pAddTargetInfo),
	m_contentEncrypt(contentEncrypt),
	m_pDIBv2(pDIBv2),
	m_diskType(diskType),
	CNdasUnitDevice(
		pParentDevice, 
		dwUnitNo, 
		NDAS_UNITDEVICE_TYPE_DISK,
		CreateSubType(diskType),
		unitDevInfo,
		ldGroup,
		ldSequence)
{
	//
	// m_pDIBv2 and m_pAddTargetInfo will be deleted 
	// by this class on destruction
	//
	XTLASSERT(
		(NDAS_CONTENT_ENCRYPT_METHOD_NONE == m_contentEncrypt.Method &&
		m_contentEncrypt.KeyLength == 0) ||
		(NDAS_CONTENT_ENCRYPT_METHOD_NONE != m_contentEncrypt.Method &&
		m_contentEncrypt.KeyLength > 0));

	XTLTRACE2(TCUnitDevice, TLTrace, "%ws\n", ToString());
}

//
// Destructor
//

CNdasUnitDiskDevice::~CNdasUnitDiskDevice()
{
	XTLTRACE2(TCUnitDevice, TLTrace, "%ws\n", ToString());

	if (NULL != m_pAddTargetInfo) 
	{
		(void) ::HeapFree(::GetProcessHeap(), 0, m_pAddTargetInfo);
		m_pAddTargetInfo = NULL;
	}
	if (NULL != m_pDIBv2) 
	{
		(void) ::HeapFree(::GetProcessHeap(), 0, m_pDIBv2);
		m_pDIBv2  = NULL;
	}
}

BOOL
CNdasUnitDiskDevice::HasSameDIBInfo()
{
	CNdasUnitDeviceCreator udCreator(GetParentDevice(), GetUnitNo());

	CNdasUnitDevicePtr pUnitDeviceNow( udCreator.CreateUnitDevice() );

	if (CNdasUnitDeviceNullPtr== pUnitDeviceNow) 
	{
		return FALSE;
	}

	if (GetType() != pUnitDeviceNow->GetType()) 
	{
		return FALSE;
	}

	CNdasUnitDiskDevice* pUnitDiskDeviceNow = 
		reinterpret_cast<CNdasUnitDiskDevice*>(pUnitDeviceNow.get());

	if (!HasSameDIBInfo(*pUnitDiskDeviceNow)) 
	{
		return FALSE;
	}

	return TRUE;
}

BOOL
CNdasUnitDiskDevice::HasSameDIBInfo(
	CNdasUnitDiskDevice &NdasUnitDiskDevice)
{
	InstanceAutoLock autolock(this);
	//
	// TODO: Changed to the actual size!!
	//
	return (0 == memcmp(m_pDIBv2, NdasUnitDiskDevice.m_pDIBv2,
		sizeof(NDAS_DIB_V2))) ? TRUE : FALSE;
}

UINT64
CNdasUnitDiskDevice::GetUserBlockCount()
{
	InstanceAutoLock autolock(this);
	// Reserve 2 MB for internal use (2 * 2 * 1024 blocks)
	// ULONG ulDiskBlocks = (ULONG) m_devInfo.SectorCount - (2 * 1024 * 2); 

	//	return ulDiskBlocks;
	return m_ulUserBlocks;
}

PVOID 
CNdasUnitDiskDevice::GetAddTargetInfo()
{
	return m_pAddTargetInfo;
}

const NDAS_CONTENT_ENCRYPT&
CNdasUnitDiskDevice::GetEncryption()
{
	return m_contentEncrypt;
}

BOOL
CNdasUnitDiskDevice::IsBitmapClean()
{
	InstanceAutoLock autolock(this);

	CNdasDevicePtr pParentDevice(m_pParentDevice);
	CNdasDeviceComm devComm(pParentDevice, m_unitDeviceId.UnitNo);

	BOOL fSuccess = devComm.Connect(FALSE);
	if(!fSuccess) 
	{
		return FALSE;
	}

	NDAS_UNITDEVICE_HARDWARE_INFO udinfo = {0};
	udinfo.Size = sizeof(NDAS_UNITDEVICE_HARDWARE_INFO);
	fSuccess = devComm.GetUnitDeviceInformation(&udinfo);
	if (!fSuccess) 
	{
		XTLTRACE2_ERR(TCUnitDevice, TLError,
			"GetUnitDeviceInformation of %ws failed\n", 
			this->ToString());
		return FALSE;
	}

	BYTE BitmapData[128 * 512] = {0};

	// 1MB from NDAS_BLOCK_LOCATION_BITMAP
	for(INT i = 0; i < 16; i++)  
	{
		fSuccess = devComm.ReadDiskBlock(BitmapData, 
			NDAS_BLOCK_LOCATION_BITMAP + (i * 128), 128);

		if(!fSuccess) 
		{
			return FALSE;
		}

		PULONG pBitmapData = (PULONG)BitmapData;
		for (INT j = 0; j < 128 * 512 / 4; ++j) 
		{
			if(*pBitmapData) 
			{
				return FALSE;
			}
			pBitmapData++;
		}
	}	

	return TRUE;
}

const NDAS_CONTENT_ENCRYPT NDAS_CONTENT_ENCRYPT_NONE = {
	NDAS_CONTENT_ENCRYPT_METHOD_NONE
};

//////////////////////////////////////////////////////////////////////////
//
// Null Unit Disk Device
//
//////////////////////////////////////////////////////////////////////////

CNdasNullUnitDiskDevice::
CNdasNullUnitDiskDevice(
	CNdasDevicePtr pParentDevice,
	DWORD UnitNo,
	const NDAS_UNITDEVICE_HARDWARE_INFO& UnitDevHardwareInfo,
	NDAS_UNITDEVICE_ERROR Error) :
	CNdasUnitDiskDevice(
		pParentDevice, 
		UnitNo, 
		NDAS_UNITDEVICE_DISK_TYPE_UNKNOWN,
		UnitDevHardwareInfo,
		NDAS_LOGICALDEVICE_GROUP_NONE,
		0,
		0,
		NULL,
		NDAS_CONTENT_ENCRYPT_NONE,
		NULL)
{
	XTLCALLTRACE();
	m_status = NDAS_UNITDEVICE_STATUS_UNKNOWN;
	m_lastError = Error;
}

CNdasNullUnitDiskDevice::
~CNdasNullUnitDiskDevice()
{
	XTLCALLTRACE();
}

DWORD 
CNdasNullUnitDiskDevice::
GetLDSequence() const 
{ 
	XTLASSERT(FALSE);
	return 0;
}

const NDAS_LOGICALDEVICE_GROUP& 
CNdasNullUnitDiskDevice::
GetLDGroup() const
{
	XTLASSERT(FALSE);
	return NDAS_LOGICALDEVICE_GROUP_NONE;

}

bool 
CNdasNullUnitDiskDevice::
RegisterToLogicalDeviceManager()
{
	XTLTRACE("NullRegister\n");
	return true;
}

bool 
CNdasNullUnitDiskDevice::
UnregisterFromLogicalDeviceManager()
{
	XTLTRACE("NullUnregister\n");
	return true;
}

ACCESS_MASK 
CNdasNullUnitDiskDevice::
GetAllowingAccess()
{
	XTLASSERT(FALSE && "Null unit device do not have this property");
	return 0;
}

UINT64 
CNdasNullUnitDiskDevice::
GetUserBlockCount()
{
	XTLASSERT(FALSE && "Null unit device do not have this property");
	return 0;
}

UINT64 
CNdasNullUnitDiskDevice::
GetPhysicalBlockCount()
{
	XTLASSERT(FALSE && "Null unit device do not have this property");
	return 0;
}

BOOL 
CNdasNullUnitDiskDevice::
CheckNDFSCompatibility()
{
	return FALSE;
}

CNdasLogicalDevicePtr 
CNdasNullUnitDiskDevice::
GetLogicalDevice()
{
	return CNdasLogicalDeviceNullPtr;
}

//////////////////////////////////////////////////////////////////////////
//
// Null Unit Device
//
//////////////////////////////////////////////////////////////////////////

CNdasNullUnitDevice::
CNdasNullUnitDevice(
	CNdasDevicePtr pParentDevice, 
	DWORD UnitNo) : 
	CNdasUnitDevice(
		pParentDevice, 
		UnitNo, 
		NDAS_UNITDEVICE_TYPE_UNKNOWN, 
		NDAS_UNITDEVICE_SUBTYPE_NONE, 
		NDAS_UNITDEVICE_HARDWARE_INFO_NONE,
		NDAS_LOGICALDEVICE_GROUP_NONE,
		0)
{
	XTLCALLTRACE();
	m_status = NDAS_UNITDEVICE_STATUS_UNKNOWN;
	m_lastError = NDAS_UNITDEVICE_ERROR_IDENTIFY_FAILURE;
}

//CNdasNullUnitDevice::
//CNdasNullUnitDevice(
//	CNdasDevicePtr pParentDevice,
//	DWORD UnitNo,
//	NDAS_UNITDEVICE_TYPE Type,
//	const NDAS_UNITDEVICE_HARDWARE_INFO& UnitDevHardwareInfo,
//	NDAS_UNITDEVICE_ERROR Error) :
//	CNdasUnitDevice(
//		pParentDevice, 
//		UnitNo, 
//		Type, 
//		CreateSubType(0), 
//		UnitDevHardwareInfo,
//		NDAS_LOGICALDEVICE_GROUP_NONE,
//		0)
//{
//	XTLCALLTRACE();
//	m_status = NDAS_UNITDEVICE_STATUS_UNKNOWN;
//	m_lastError = Error;
//}

CNdasNullUnitDevice::
~CNdasNullUnitDevice()
{
	XTLCALLTRACE();
}

DWORD 
CNdasNullUnitDevice::
GetLDSequence() const 
{ 
	XTLASSERT(FALSE);
	return 0;
}

const NDAS_LOGICALDEVICE_GROUP& 
CNdasNullUnitDevice::
GetLDGroup() const
{
	XTLASSERT(FALSE);
	return NDAS_LOGICALDEVICE_GROUP_NONE;
}

bool 
CNdasNullUnitDevice::
RegisterToLogicalDeviceManager()
{
	XTLTRACE("NullRegister\n");
	return true;
}

bool 
CNdasNullUnitDevice::
UnregisterFromLogicalDeviceManager()
{
	XTLTRACE("NullUnregister\n");
	return true;
}

ACCESS_MASK 
CNdasNullUnitDevice::
GetAllowingAccess()
{
	XTLASSERT(FALSE && "Null unit device do not have this property");
	return 0;
}

UINT64 
CNdasNullUnitDevice::
GetUserBlockCount()
{
	XTLASSERT(FALSE && "Null unit device do not have this property");
	return 0;
}

UINT64 
CNdasNullUnitDevice::
GetPhysicalBlockCount()
{
	XTLASSERT(FALSE && "Null unit device do not have this property");
	return 0;
}

BOOL 
CNdasNullUnitDevice::
CheckNDFSCompatibility()
{
	return FALSE;
}

CNdasLogicalDevicePtr 
CNdasNullUnitDevice::
GetLogicalDevice()
{
	return CNdasLogicalDeviceNullPtr;
}

//////////////////////////////////////////////////////////////////////////
//
// Utility Function Implementations
//
//////////////////////////////////////////////////////////////////////////

namespace
{

NDAS_UNITDEVICE_ID 
pCreateUnitDeviceId(
	const CNdasDevicePtr& pDevice,
	DWORD unitNo)
{
	NDAS_UNITDEVICE_ID deviceID = {pDevice->GetDeviceId(), unitNo};
	return deviceID;
}

NDAS_LOGICALDEVICE_TYPE
pUnitDeviceLogicalDeviceType(
	NDAS_UNITDEVICE_TYPE udType,
	NDAS_UNITDEVICE_SUBTYPE udSubtype)
{
	switch (udType) 
	{
	case NDAS_UNITDEVICE_TYPE_DISK:
		switch (udSubtype.DiskDeviceType) 
		{
		case NDAS_UNITDEVICE_DISK_TYPE_SINGLE:
			return NDAS_LOGICALDEVICE_TYPE_DISK_SINGLE;
		case NDAS_UNITDEVICE_DISK_TYPE_VIRTUAL_DVD:
			return NDAS_LOGICALDEVICE_TYPE_VIRTUAL_DVD;
		case NDAS_UNITDEVICE_DISK_TYPE_AGGREGATED:
			return NDAS_LOGICALDEVICE_TYPE_DISK_AGGREGATED;
		case NDAS_UNITDEVICE_DISK_TYPE_MIRROR_MASTER:
		case NDAS_UNITDEVICE_DISK_TYPE_MIRROR_SLAVE:
			return NDAS_LOGICALDEVICE_TYPE_DISK_MIRRORED;
		case NDAS_UNITDEVICE_DISK_TYPE_RAID0:
			return NDAS_LOGICALDEVICE_TYPE_DISK_RAID0;
		case NDAS_UNITDEVICE_DISK_TYPE_RAID1:
			return NDAS_LOGICALDEVICE_TYPE_DISK_RAID1;
		case NDAS_UNITDEVICE_DISK_TYPE_RAID1_R2:
			return NDAS_LOGICALDEVICE_TYPE_DISK_RAID1_R2;
		case NDAS_UNITDEVICE_DISK_TYPE_RAID4:
			return NDAS_LOGICALDEVICE_TYPE_DISK_RAID4;
		case NDAS_UNITDEVICE_DISK_TYPE_RAID4_R2:
			return NDAS_LOGICALDEVICE_TYPE_DISK_RAID4_R2;
		}
		break;
	case NDAS_UNITDEVICE_TYPE_CDROM:
		return NDAS_LOGICALDEVICE_TYPE_DVD;
	case NDAS_UNITDEVICE_TYPE_COMPACT_BLOCK:
		return NDAS_LOGICALDEVICE_TYPE_FLASHCARD;
	case NDAS_UNITDEVICE_TYPE_OPTICAL_MEMORY:
		return NDAS_LOGICALDEVICE_TYPE_MO;
	}
	XTLASSERT(FALSE);
	return NDAS_LOGICALDEVICE_TYPE_UNKNOWN;
}

}


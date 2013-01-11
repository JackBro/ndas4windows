#ifndef _NDAS_PNP_H_
#define _NDAS_PNP_H_
#pragma once
#include <xtl/xtlpnpevent.h>
#include <xtl/xtlautores.h>

class CNdasService;

typedef enum _DEVICE_HANDLE_TYPE {
	PnpLogicalUnitHandle, 
	PnpVolumeHandle,
	PnpStoragePortHandle,
} DEVICE_HANDLE_TYPE;

typedef struct _DEVICE_HANDLE_NOTIFY_DATA {
	DEVICE_HANDLE_TYPE HandleType;
	NDAS_LOCATION NdasLocation;
} DEVICE_HANDLE_NOTIFY_DATA, *PDEVICE_HANDLE_NOTIFY_DATA;

typedef struct _NDAS_LOCATION_DATA {
	NDAS_LOCATION NdasLocation;
	TCHAR DevicePath[MAX_PATH];
} NDAS_LOCATION_DATA;

class CNdasServiceDeviceEventHandler : 
	public XTL::CDeviceEventHandler<CNdasServiceDeviceEventHandler>
{
protected:

	BOOL m_bNoLfs;
	BOOL m_bInitialized;
	BOOL m_bROFilterFilteringStarted;
	XTL::AutoFileHandle m_hROFilter;

	HANDLE m_hRecipient;
	DWORD m_dwReceptionFlags;
	
	std::set<DWORD> m_FilteredDriveNumbers;

	CRITICAL_SECTION m_DevNotifyMapSection;
	std::map<HDEVNOTIFY,DEVICE_HANDLE_NOTIFY_DATA> m_DevNotifyMap;
	std::vector<NDAS_LOCATION_DATA> m_NdasLocationData;

	std::vector<HDEVNOTIFY> m_DeviceInterfaceNotifyHandles;

	typedef std::map<HDEVNOTIFY,DEVICE_HANDLE_NOTIFY_DATA> DevNotifyMap;

	std::vector<CComPtr<INdasLogicalUnit> > m_NdasLogicalUnitDrives;

public:

	CNdasServiceDeviceEventHandler();
	~CNdasServiceDeviceEventHandler();
	
	bool Initialize(HANDLE hRecipient, DWORD dwReceptionFlag); 
	void Uninitialize();

	void OnDeviceInterfaceArrival(PDEV_BROADCAST_DEVICEINTERFACE dbcc);

	void OnStoragePortDeviceInterfaceArrival(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
	void OnVolumeDeviceInterfaceArrival(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
	void OnLogicalUnitInterfaceArrival(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);

	LRESULT OnDeviceHandleQueryRemove(PDEV_BROADCAST_HANDLE pdbch);
	void OnDeviceHandleQueryRemoveFailed(PDEV_BROADCAST_HANDLE pdbch);
	void OnDeviceHandleRemovePending(PDEV_BROADCAST_HANDLE pdbch);
	void OnDeviceHandleRemoveComplete(PDEV_BROADCAST_HANDLE pdbch);
	void OnDeviceHandleCustomEvent(PDEV_BROADCAST_HANDLE pdbch);

	void OnVolumeArrival(PDEV_BROADCAST_VOLUME dbcv);
	void OnVolumeRemoveComplete(PDEV_BROADCAST_VOLUME dbcv);

	void OnShutdown();

	HRESULT GetLogicalUnitDevicePath(
		__in NDAS_LOCATION NdasLocation,
		__out BSTR* DevicePath);

	void RescanDriveLetters();

protected:

	HRESULT RegisterDeviceInterfaceNotification(
		__in LPCGUID InterfaceGuid,
		__in LPCSTR TypeName);

	HRESULT RegisterVolumeHandleNotification(
		__in HANDLE DeviceHandle, 
		__in LPCTSTR DevicePath);

	HRESULT RegisterStoragePortHandleNotification(
		__in HANDLE DeviceHandle,
		__in NDAS_LOCATION NdasLocation,
		__in LPCTSTR DevicePath);

	HRESULT RegisterLogicalUnitHandleNotification(
		__in HANDLE DeviceHandle, 
		__in NDAS_LOCATION NdasLocation,
		__in LPCTSTR DevicePath);

	HRESULT RegisterDeviceHandleNotification(
		__in HANDLE DeviceHandle, 
		__in DEVICE_HANDLE_TYPE HandleType,
		__in NDAS_LOCATION NdasLocation,
		__in LPCTSTR DevicePath);

	HRESULT UnregisterDeviceHandleNotification(
		__in HDEVNOTIFY DevNotifyHandle);

	const DEVICE_HANDLE_NOTIFY_DATA* GetDeviceHandleNotificationData(
		__in HDEVNOTIFY NotifyHandle);

	HRESULT pRegisterNdasScsiPorts();
	HRESULT pRegisterLogicalUnits();
	HRESULT pRegisterLogicalUnit(
		__in HDEVINFO DevInfoSet, 
		__in LPCGUID InterfaceGuid);

	void pOnVolumeArrivalOrRemoval(PDEV_BROADCAST_VOLUME dbcv, BOOL Removal);
	void pOnVolumeArrivalOrRemoval(DWORD UnitMask, BOOL Removal);
};

#endif /* _NDAS_PNP_H_ */

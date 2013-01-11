#ifndef _NDAS_PNP_H_
#define _NDAS_PNP_H_
#pragma once
#include "pnpevent.h"
#include "autores.h"
#include <map>
#include <set>

class CNdasServicePowerEventHandler :
	public ximeta::CPowerEventHandler
{
public:
	virtual LRESULT OnQuerySuspend(DWORD dwFlags);
	virtual VOID OnQuerySuspendFailed();
	virtual VOID OnSuspend();
	virtual VOID OnResumeAutomatic();
	virtual VOID OnResumeSuspend();
	virtual VOID OnResumeCritical();
};

class CNdasServiceDeviceEventHandler : 
	public ximeta::CDeviceEventHandler
{
	BOOL m_bNoLfs;
	BOOL m_bInitialized;
	AutoFileHandle m_hROFilter;
	BOOL m_bROFilterFilteringStarted;

	const HANDLE m_hRecipient;
	const DWORD m_dwReceptionFlags;

	HDEVNOTIFY m_hStoragePortNotify;
	HDEVNOTIFY m_hVolumeNotify;
	HDEVNOTIFY m_hDiskNotify;
	HDEVNOTIFY m_hCdRomClassNotify;

	typedef std::set<DWORD> FilterdDriveNumberSet;
	
	FilterdDriveNumberSet m_FilteredDriveNumbers;

	typedef enum _DevNotifyInfoType {
		DEVNOTIFYINFO_TYPE_STORAGEPORT,
		DEVNOTIFYINFO_TYPE_VOLUME,
		DEVNOTIFYINFO_TYPE_DISK,
		DEVNOTIFYINFO_TYPE_CDROM
	} DevNotifyInfoType;

	static LPCTSTR DevNotifyInfoTypeString(DevNotifyInfoType t)
	{
		switch (t) {
		case DEVNOTIFYINFO_TYPE_STORAGEPORT: return _T("STORAGEPORT");
		case DEVNOTIFYINFO_TYPE_VOLUME: return _T("VOLUME");
		case DEVNOTIFYINFO_TYPE_DISK: return _T("DISK");
		case DEVNOTIFYINFO_TYPE_CDROM: return _T("CDROM");
		default:
			return _T("DEVNOTIFYINFO_TYPE_???");
		}
	}

	typedef struct _DevNotifyInfo {
		DWORD LogDevSlotNo;
		DevNotifyInfoType Type;
	} DevNotifyInfo, *PDevNotifyInfo;

	typedef std::map<HDEVNOTIFY,DevNotifyInfo> DevNotifyMap;
	DevNotifyMap m_DevNotifyMap;

protected:

	HDEVNOTIFY RegisterDeviceInterfaceNotification(LPCGUID classGuid);
	HDEVNOTIFY RegisterDeviceHandleNotification(HANDLE hDeviceFile);
	
	LRESULT OnStoragePortDeviceInterfaceArrival(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
//	LRESULT OnStoragePortDeviceInterfaceQueryRemove(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
//	LRESULT OnStoragePortDeviceInterfaceQueryRemoveFailed(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
//	LRESULT OnStoragePortDeviceInterfaceRemovePending(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
	LRESULT OnStoragePortDeviceInterfaceRemoveComplete(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);

	LRESULT OnVolumeDeviceInterfaceArrival(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
//	LRESULT OnVolumeDeviceInterfaceQueryRemove(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
//	LRESULT OnVolumeDeviceInterfaceQueryRemoveFailed(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
//	LRESULT OnVolumeDeviceInterfaceRemovePending(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
	LRESULT OnVolumeDeviceInterfaceRemoveComplete(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);

	LRESULT OnDiskDeviceInterfaceArrival(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
//	LRESULT OnDiskDeviceInterfaceQueryRemove(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
//	LRESULT OnDiskDeviceInterfaceQueryRemoveFailed(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
//	LRESULT OnDiskDeviceInterfaceRemovePending(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
	LRESULT OnDiskDeviceInterfaceRemoveComplete(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);

	LRESULT OnCdRomDeviceInterfaceArrival(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
//	LRESULT OnCdRomDeviceInterfaceQueryRemove(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
//	LRESULT OnCdRomDeviceInterfaceQueryRemoveFailed(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
//	LRESULT OnCdRomDeviceInterfaceRemovePending(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);
	LRESULT OnCdRomDeviceInterfaceRemoveComplete(PDEV_BROADCAST_DEVICEINTERFACE pdbhdr);

	LRESULT OnDeviceHandleQueryRemove(PDEV_BROADCAST_HANDLE pdbch);
	LRESULT OnDeviceHandleQueryRemoveFailed(PDEV_BROADCAST_HANDLE pdbch);
	LRESULT OnDeviceHandleRemovePending(PDEV_BROADCAST_HANDLE pdbch);
	LRESULT OnDeviceHandleRemoveComplete(PDEV_BROADCAST_HANDLE pdbch);

public:

	CNdasServiceDeviceEventHandler(HANDLE hRecipient, DWORD dwReceptionFlag);
	~CNdasServiceDeviceEventHandler();
	BOOL Initialize(); 

	virtual LRESULT OnDeviceArrival(PDEV_BROADCAST_HDR pdbhdr);
	virtual LRESULT OnDeviceQueryRemove(PDEV_BROADCAST_HDR pdbhdr);
	virtual LRESULT OnDeviceQueryRemoveFailed(PDEV_BROADCAST_HDR pdbhdr);
	virtual LRESULT OnDeviceRemovePending(PDEV_BROADCAST_HDR pdbhdr);
	virtual LRESULT OnDeviceRemoveComplete(PDEV_BROADCAST_HDR pdbhdr);
};

#endif /* _NDAS_PNP_H_ */
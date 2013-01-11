#ifndef __NDASSCSI_PRIV_H__
#define __NDASSCSI_PRIV_H__

//
// Windows kernel library headers
//

#include <ntddk.h>
#include <scsi.h>
//#include "winscsiext.h"


//
//	NDAS common headers
//

#include "public\ndas\ndasiomsg.h"
#include "basetsdex.h"
#include "ndas/ndasdib.h"
#include "ndasscsiioctl.h"


//
//	kernel library headers
//

#include "lsklib.h"
#include "lslur.h"
#include "lsccb.h"
#include "kdebug.h"
#include "lsutils.h"
#include "draidexp.h"

//////////////////////////////////////////////////////////////////////////
//
//	Defines
//
//
//	Adapter information for HwInitialize()
//

#define NDSC_TIMER_VALUE				1000					// micro sec

//
// SCSI Adapter max numbers
//

#define	MAX_NR_LOGICAL_TARGETS	0x1
#define MAX_NR_LOGICAL_LU		0x1
#define INITIATOR_ID			(MAX_NR_LOGICAL_TARGETS+1)

//
//	define new SCSI operation for the completion notification
//
#define SCSIOP_COMPLETE		0xFE

//
//	Pool tags
//
#define	NDSC_PTAG_IOCTL			'oiSN'
#define NDSC_PTAG_WORKITEM		'cwSN'
#define NDSC_PTAG_SRB			'rsSN'
#define NDSC_PTAG_SRB_IOCTL		'isSN'
#define NDSC_PTAG_SRB_CMPDATA	'dsSN'
#define NDSC_PTAG_CMPDATA		'mcSN'
#define NDSC_PTAG_ENUMINFO		'neSN'

//////////////////////////////////////////////////////////////////////////
//
//	Event log modules
//	Event log unique IDs
//
#define	EVTLOG_MASK(LOGVALUE, MASK) ((LOGVALUE) & (MASK))
#define	EVTLOG_UNIQUEID(EVTMODULE, EVTID, USERDATA) ((EVTLOG_MASK(EVTMODULE, 0xff) << 24) |	\
													(EVTLOG_MASK(EVTID, 0xff)<<16) |		\
													EVTLOG_MASK(USERDATA, 0xffff))


//	FindEnumInfo module
#define EVTLOG_MODULE_FINDADAPTER			0x02
#define	EVTLOG_START_FIND					0x01
#define	EVTLOG_DETECT_LURDESC				0x02
#define	EVTLOG_DETECT_ADDTARGET				0x03
#define	EVTLOG_SUCCEED_FIND					0x04
#define	EVTLOG_FAIL_TO_GET_ENUM_INFO		0x10
#define	EVTLOG_FAIL_TO_ADD_LUR				0x11
#define	EVTLOG_INVALID_TARGETTYPE			0x12
#define	EVTLOG_FAIL_TO_ALLOC_LURDESC		0x13
#define	EVTLOG_FAIL_TO_TRANSLATE			0x14
#define	EVTLOG_FAIL_TO_CREATE_LUR			0x15
#define	EVTLOG_FIRST_TIME_INSTALL			0x16

// Adapter control
#define EVTLOG_MODULE_ADAPTERCONTROL		0x03
#define	EVTLOG_BUSRESET_OCCUR				0x01
#define	EVTLOG_ADAPTER_STOP					0x02
#define	EVTLOG_STOP_DURING_POWERSAVING		0x03
#define	EVTLOG_STOP_DURING_STOPPING			0x04
#define EVTLOG_INQUIRY_DURING_POWERSAVING	0x05
#define EVTLOG_INQUIRY_DURING_STOPPING		0x06
#define EVTLOG_INQUIRY_LUR_NOT_FOUND		0x07
#define EVTLOG_DOUBLE_POWERSAVING			0x08
#define EVTLOG_RESTART_NOT_INPOWERSAVING	0x09
#define EVTLOG_SCSIOP_INPOWERSAVING			0x0a
#define EVTLOG_DISCARD_SRB					0x0b
#define EVTLOG_STOP_WITH_INVALIDLUR			0x0c

// IRP-method completion
#define EVTLOG_MODULE_COMPIRP				0x04
#define EVTLOG_NO_SHIPPED_SRB				0x01
#define EVTLOG_ABORT_SRB_ENTERED			0x02
#define EVTLOG_CCB_ALLOCATION_FAIL			0x03

// Completion
#define EVTLOG_MODULE_COMPLETION			0x05
#define EVTLOG_START_RECONNECTION			0x01
#define EVTLOG_END_RECONNECTION				0x02
#define EVTLOG_SUCCEED_UPGRADE				0x03
#define EVTLOG_FAIL_UPGRADE					0x04
#define EVTLOG_MEMBER_IN_ERROR				0x05
#define EVTLOG_FAIL_COMPLIRP				0x06
#define EVTLOG_MEMBER_RECOVERED				0x07
#define EVTLOG_START_RECOVERING				0x08
#define EVTLOG_END_RECOVERING				0x09
#define EVTLOG_DISK_POWERRECYCLED			0x0a
#define EVTLOG_RAID_FAILURE					0x0b				

// Ioctl
#define EVTLOG_MODULE_IOCTL					0x06
#define EVTLOG_FAIL_UPGRADEIOCTL			0x01
#define EVTLOG_FAIL_WHILESTOPPING			0x02
#define EVTLOG_FAIL_LURNULL					0x03
#define EVTLOG_FAIL_SENDSTOPCCB				0x04
#define EVTLOG_FAIL_DELAYEDOP				0x05


//////////////////////////////////////////////////////////////////////////
//
//	Structures
//	Keep the same values as in ndasscsiioctl.h, ndastypes.h
//

#define NDASSCSI_ADAPTER_STATUS_MASK						0x000000ff
#define	NDASSCSI_ADAPTER_STATUS_INIT						0x00000000
#define	NDASSCSI_ADAPTER_STATUS_RUNNING					0x00000001
#define NDASSCSI_ADAPTER_STATUS_STOPPING					0x00000002
//#define NDASSCSI_ADAPTER_STATUS_IN_ERROR					0x00000003
#define NDASSCSI_ADAPTER_STATUS_STOPPED					0x00000004	// only used for NDASBUS notification

#define NDASSCSI_ADAPTER_STATUSFLAG_MASK					0xffffff00
#define NDASSCSI_ADAPTER_STATUSFLAG_RECONNECT_PENDING	0x00000100
#define NDASSCSI_ADAPTER_STATUSFLAG_RESTARTING			0x00000200
#define NDASSCSI_ADAPTER_STATUSFLAG_BUSRESET_PENDING		0x00000400
#define NDASSCSI_ADAPTER_STATUSFLAG_MEMBER_FAULT			0x00000800
#define NDASSCSI_ADAPTER_STATUSFLAG_RECOVERING			0x00001000
#define NDASSCSI_ADAPTER_STATUSFLAG_ABNORMAL_TERMINAT	0x00002000
//#define NDASSCSI_ADAPTER_STATUSFLAG_DEFECTIVE			0x00004000
#define NDASSCSI_ADAPTER_STATUSFLAG_POWERRECYCLED		0x00008000
#define NDASSCSI_ADAPTER_STATUSFLAG_RAID_FAILURE			0x00010000
#define NDASSCSI_ADAPTER_STATUSFLAG_RAID_NORMAL			0x00020000
#define NDASSCSI_ADAPTER_STATUSFLAG_RESETSTATUS			0x01000000	// only used for NDASBUS notification
#define NDASSCSI_ADAPTER_STATUSFLAG_NEXT_EVENT_EXIST		0x02000000	// only used for NDASSVC notification

#define ADAPTER_ISSTATUS(PHWDEV, STATUS)		\
		(((PHWDEV)->AdapterStatus & NDASSCSI_ADAPTER_STATUS_MASK) == (STATUS))
#define ADAPTER_SETSTATUS(PHWDEV, STATUS)		\
		( (PHWDEV)->AdapterStatus = ((PHWDEV)->AdapterStatus & (NDASSCSI_ADAPTER_STATUSFLAG_MASK)) | (STATUS))

#define ADAPTER_ISSTATUSFLAG(PHWDEV, STATUSFLAG)		\
		(((PHWDEV)->AdapterStatus & (STATUSFLAG)) != 0)
#define ADAPTER_SETSTATUSFLAG(PHWDEV, STATUSFLAG)		\
		( (PHWDEV)->AdapterStatus |= (STATUSFLAG))
#define ADAPTER_RESETSTATUSFLAG(PHWDEV, STATUSFLAG)		\
		( (PHWDEV)->AdapterStatus &= ~(STATUSFLAG))
#define NR_MAX_NDASSCSI_LURS				1


//////////////////////////////////////////////////////////////////////////
//
//	Structures
//

//
//	Windows Major and minor version numbers
//

#define NT_MAJOR_VERSION	5
#define W2K_MINOR_VERSION	0
//#define XP_MINOR_VERSION	1


//
//	NDASSCSI driver-wide variables
//

typedef struct _NDAS_MINI_GLOBALS {

	//
	// Current Windows versions
	//

	ULONG			MajorVersion;
	ULONG			MinorVersion;
	ULONG			BuildNumber;
	BOOLEAN			CheckedVersion;


	//
	// Driver object
	//

	PDRIVER_OBJECT	DriverObject;


	//
	// Save scsiport's unload routine
	//

	PDRIVER_UNLOAD	ScsiportUnload;



	//
	//	Handle for TDI PnP event
	//

	HANDLE			TdiPnP;


	//
	// Driver-dedicated thread that handles work items
	//

	HANDLE			DriverThreadHandle;
	PVOID			DriverThreadObject;

#if __NDAS_SCSI_OLD_VERSION__
	DRAID_GLOBALS	DraidGlobal;
#endif

	//
	//	Work item queue
	//

	KEVENT			WorkItemQueueEvent;
	KSPIN_LOCK		WorkItemQueueSpinlock;
	LIST_ENTRY		WorkItemQueue;
	LONG			WorkItemCnt;

} NDAS_MINI_GLOBALS, *PNDAS_MINI_GLOBALS;


//
//	Adapter device object's extension
//

typedef struct _MINIPORT_DEVICE_EXTENSION {
	PDEVICE_OBJECT	ScsiportFdoObject;

	//
	//	NDASSCSI adapter status
	//
	KSPIN_LOCK		LanscsiAdapterSpinLock;	// protect AdapterStatus, LastBusResetTime.
	ULONG			AdapterStatus;
	BOOLEAN			TimerOn;					// protected by Scsiport synch.

	LONG			RequestExecuting;			// protected by Scsiport synch.
	KSPIN_LOCK		CcbTimerCompletionListSpinLock;
	LIST_ENTRY		CcbTimerCompletionList;

	//
	//	NDASSCSI adapter spec information.
	//
	ULONG			SlotNumber;
	UCHAR			InitiatorId;
    UCHAR			NumberOfBuses;
    UCHAR			MaximumNumberOfTargets;
    UCHAR			MaximumNumberOfLogicalUnits;
	ULONG			AdapterMaxDataTransferLength;
	LARGE_INTEGER	EnabledTime;
	ULONG			EnumFlags;

	//
	//	LUR
	//
	LONG			LURCount;
	PLURELATION		LURs[NR_MAX_NDASSCSI_LURS];
	BOOLEAN			LURSavedSecondaryFeature[NR_MAX_NDASSCSI_LURS];

} MINIPORT_DEVICE_EXTENSION, *PMINIPORT_DEVICE_EXTENSION;


//
//	LU device object's extension
//

typedef struct _MINIPORT_LU_EXTENSION {

	PLURELATION		LUR;	// Obsolete

} MINIPORT_LU_EXTENSION, *PMINIPORT_LU_EXTENSION;


//
//	NDASSCSI's work item
//

typedef struct _NDSC_WORKITEM NDSC_WORKITEM, *PNDSC_WORKITEM;

typedef
VOID
(*PNDSC_WORKITEM_ROUTINE) (
    IN PDEVICE_OBJECT DeviceObject,
    IN PNDSC_WORKITEM NdscWorkItem
    );

typedef struct _NDSC_WORKITEM_INIT {

	PNDSC_WORKITEM_ROUTINE		WorkitemRoutine;
	PCCB						Ccb;
	PVOID						Arg1;
	PVOID						Arg2;
	PVOID						Arg3;

	//
	//	Driver use only
	//

	BOOLEAN						TerminateWorkerThread;

} NDSC_WORKITEM_INIT, *PNDSC_WORKITEM_INIT;


typedef struct _NDSC_WORKITEM {

	//
	//	User-suplied context
	//

	PNDSC_WORKITEM_ROUTINE		WorkitemRoutine;
	PCCB						Ccb;
	PVOID						Arg1;
	PVOID						Arg2;
	PVOID						Arg3;


	//
	//	Private field for a worker thread
	//

	LIST_ENTRY					WorkItemList;
	BOOLEAN						TerminateWorkerThread;
	PDEVICE_OBJECT				DeviceObject;
	PNDAS_MINI_GLOBALS				NdscGlobals;

} NDSC_WORKITEM, *PNDSC_WORKITEM;


#define NDSC_INIT_WORKITEM(WI_POINTER, WIROUTINE, CCB, ARG1, ARG2, ARG3) {	\
	(WI_POINTER)->WorkitemRoutine = (WIROUTINE);							\
	(WI_POINTER)->Ccb = (CCB);												\
	(WI_POINTER)->Arg1 = (ARG1);											\
	(WI_POINTER)->Arg2 = (ARG2);											\
	(WI_POINTER)->Arg3 = (ARG3);											\
	(WI_POINTER)->TerminateWorkerThread = FALSE;}


//////////////////////////////////////////////////////////////////////////
//
//	exported variables
//

extern NDAS_MINI_GLOBALS NdasMiniGlobalData;

//////////////////////////////////////////////////////////////////////////
//
//	exported functions to NDASSCSI
//

//
//	ndasscsi.c
//

NTSTATUS
MiniQueueWorkItem(
		IN PNDAS_MINI_GLOBALS		NdscGlobals,
		IN PDEVICE_OBJECT		DeviceObject,
		IN PNDSC_WORKITEM_INIT	NdscWorkItemInit
	);

NTSTATUS
SendCcbToLURSync(
		PMINIPORT_DEVICE_EXTENSION	HwDeviceExtension,
		PLURELATION		LUR,
		UINT32			CcbOpCode
	);


//
//
//	ndscioctl.c
//
NTSTATUS
MiniSrbControl(
	   IN PMINIPORT_DEVICE_EXTENSION	HwDeviceExtension,
	   IN PMINIPORT_LU_EXTENSION		LuExtension,
	   IN PSCSI_REQUEST_BLOCK			Srb
	);

NTSTATUS
MiniSendIoctlSrb(
		IN PDEVICE_OBJECT	DeviceObject,
		IN ULONG			IoctlCode,
		IN PVOID			InputBuffer,
		IN LONG				InputBufferLength,
		OUT PVOID			OutputBuffer,
		IN LONG				OutputBufferLength
	);

NTSTATUS
MiniSendIoctlSrbAsynch(
		IN PDEVICE_OBJECT	DeviceObject,
		IN ULONG			IoctlCode,
		IN PVOID			InputBuffer,
		IN LONG				InputBufferLength,
		OUT PVOID			OutputBuffer,
		IN LONG				OutputBufferLength
);


//
//	ndsccomp.c
//

PDEVICE_OBJECT
FindScsiportFdo(
		IN OUT	PMINIPORT_DEVICE_EXTENSION	HwDeviceExtension
	);

ULONG
GetScsiAdapterPdoEnumInfo(
	IN PMINIPORT_DEVICE_EXTENSION	HwDeviceExtension,
	IN ULONG						SystemIoBusNumber,
	OUT PLONG						AddDevInfoLength,
	OUT PVOID						*AddDevInfo,
	OUT	PULONG						Flags
);

NTSTATUS
IoctlToLanscsiBus(
	IN ULONG  IoControlCode,
    IN PVOID  InputBuffer  OPTIONAL,
    IN ULONG  InputBufferLength,
    OUT PVOID  OutputBuffer  OPTIONAL,
    IN ULONG  OutputBufferLength,
	OUT PULONG	BufferNeeded
);

NTSTATUS
UpdateStatusInLSBus(
		ULONG SlotNo,
		ULONG	AdapterStatus
);

VOID
UpdatePdoInfoInLSBus(
		PMINIPORT_DEVICE_EXTENSION	HwDeviceExtension,
		UINT32						AdapterStatus
	);



NTSTATUS
NdscAdapterCompletion(
		  IN PCCB							Ccb,
		  IN PMINIPORT_DEVICE_EXTENSION		HwDeviceExtension
	);

VOID
MiniLurnCallback(
	PLURELATION	Lur,
	PLURN_EVENT	LurnEvent
	);

VOID
NdasMiniLogError(
	IN PMINIPORT_DEVICE_EXTENSION HwDeviceExtension,
	IN PSCSI_REQUEST_BLOCK Srb OPTIONAL,
	IN UCHAR PathId,
	IN UCHAR TargetId,
	IN UCHAR Lun,
	IN ULONG ErrorCode,
	IN ULONG UniqueId
);


#endif // __NDASSCSI_PRIV_H__

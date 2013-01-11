#ifndef __NETDISK_MANAGER_H__
#define __NETDISK_MANAGER_H__


#define WAKEUP_VOLUME_FILE_NAME		L"\\HobinAndKyungtaeWakeupVolume"
#define TOUCH_VOLUME_FILE_NAME		L"\\HobinAndKyungtaeTouchVolume"
#define REMOUNT_VOLUME_FILE_NAME	L"\\HobinAndKyungtaeRemountVolume"


typedef struct _NETDISK_MANAGER
{
	KSPIN_LOCK			SpinLock;
    LONG				ReferenceCount;

	PLFS				Lfs;

	KSPIN_LOCK			EnabledNetdiskQSpinLock;		
	LIST_ENTRY			EnabledNetdiskQueue;			// Enabled Netdisk Queue

	FAST_MUTEX			NDManFastMutex;

	struct
	{
		HANDLE			ThreadHandle;
		PVOID			ThreadObject;

#define NETDISK_MANAGER_THREAD_INITIALIZING	0x00000001
#define NETDISK_MANAGER_THREAD_START		0x00000004
#define NETDISK_MANAGER_THREAD_ERROR		0x80000000
#define NETDISK_MANAGER_THREAD_TERMINATED	0x01000000
		
		ULONG			Flags;

		KEVENT			ReadyEvent;

		KSPIN_LOCK		RequestQSpinLock;		// protect RequestQueue
		LIST_ENTRY		RequestQueue;
		KEVENT			RequestEvent;
	
	} Thread;

} NETDISK_MANAGER, *PNETDISK_MANAGER;


typedef enum _NETDISK_VOLUME_STATE
{
	VolumeEnabled = 0,
	VolumeMounting,
	VolumeMounted,
	VolumeSurpriseRemoved,
	VolumeDismounted

} NETDISK_VOLUME_STATE, *PNETDISK_VOLUME_STATE;


typedef struct _NETDISK_VOLUME 
{
	NETDISK_VOLUME_STATE		VolumeState;
	PLFS_DEVICE_EXTENSION		LfsDeviceExt;	
	NTSTATUS					MountStatus;

} NETDISK_VOLUME, *PNETDISK_VOLUME;

#ifndef __NDFS__

typedef enum _NETDISK_ENABLE_MODE
{
	NETDISK_UNKNOWN_MODE = 0,
	NETDISK_READ_ONLY,
	NETDISK_SECONDARY,
	NETDISK_PRIMARY,		 
	NETDISK_SECONDARY2PRIMARY
	
} NETDISK_ENABLE_MODE, *PNETDISK_ENABLE_MODE;

#endif

typedef struct _NETDISK_PARTITION
{
	KSPIN_LOCK						SpinLock;
    LONG							ReferenceCount;

#define NETDISK_PARTITION_ENABLED			0x00000001
#define NETDISK_PARTITION_MOUNTING			0x00000002
#define NETDISK_PARTITION_MOUNTED			0x00000004
#define NETDISK_PARTITION_DISMOUNTED		0x00000008
#define NETDISK_PARTITION_DISABLED			0x00000010
#define NETDISK_PARTITION_SHUTDOWN			0x00000020
#define NETDISK_PARTITION_SURPRISE_REMOVAL	0x01000000
#define NETDISK_PARTITION_CORRUPTED			0x02000000
#define NETDISK_PARTITION_ERROR				0x80000000
	ULONG							Flags;

	LIST_ENTRY						ListEntry;
	struct _ENABLED_NETDISK			*EnabledNetdisk;
	
	NETDISK_PARTITION_INFORMATION	NetdiskPartitionInformation;
	NETDISK_VOLUME					NetdiskVolume[NETDISK_SECONDARY2PRIMARY+1][2];
	LFS_FILE_SYSTEM_TYPE			FileSystemType;

	ULONG							ExportCount;
	ULONG							LocalExportCount;

} NETDISK_PARTITION, *PNETDISK_PARTITION;


typedef struct _ENABLED_NETDISK
{
	KSPIN_LOCK					SpinLock;
    LONG						ReferenceCount;
	LIST_ENTRY					ListEntry;
	PNETDISK_MANAGER			NetdiskManager;

#define ENABLED_NETDISK_ENABLED				0x00000001
#define ENABLED_NETDISK_UNPLUGING			0x00000002
#define ENABLED_NETDISK_UNPLUGED			0x00000004
#define ENABLED_NETDISK_SURPRISE_REMOVAL	0x01000000
#define ENABLED_NETDISK_CORRUPTED			0x02000000
#define ENABLED_NETDISK_ERROR				0x80000000
	ULONG						Flags;

	NETDISK_INFORMATION			NetdiskInformation;
	NETDISK_ENABLE_MODE			NetdiskEnableMode;
	PDRIVE_LAYOUT_INFORMATION	DriveLayoutInformation;

	NETDISK_PARTITION			DummyNetdiskPartition;
	KSPIN_LOCK					NetdiskPartitionQSpinLock;
	LIST_ENTRY					NetdiskPartitionQueue;


	LONG						UnplugInProgressCount; // Number of partitions that are in unplugging state.
	LONG						DispatchInProgressCount; // Number of IO dispatching by DispatchWinXpRequest.

} ENABLED_NETDISK, *PENABLED_NETDISK;


typedef enum _NETDISK_MANAGER_REQUEST_TYPE
{
	NETDISK_MANAGER_REQUEST_DISCONNECT = 1,
	NETDISK_MANAGER_REQUEST_DOWN,
	NETDISK_MANAGER_REQUEST_TOUCH_VOLUME,

} NETDISK_MANAGER_REQUEST_TYPE, *PNETDISK_MANAGER_REQUEST_TYPE;


typedef struct _NETDISK_MANAGER_REQUEST
{
	NETDISK_MANAGER_REQUEST_TYPE	RequestType;
	LONG							ReferenceCount;

	LIST_ENTRY						ListEntry;

	BOOLEAN							Synchronous;
	KEVENT							CompleteEvent;
	BOOLEAN							Success;
    IO_STATUS_BLOCK					IoStatus;
	PNETDISK_PARTITION				NetdiskPartition;
	
} NETDISK_MANAGER_REQUEST, *PNETDISK_MANAGER_REQUEST;

PNETDISK_PARTITION
LookUpNetdiskPartition(
	IN PENABLED_NETDISK			EnabledNetdisk,
    IN PPARTITION_INFORMATION	PartitionInformation,
	IN PLARGE_INTEGER			StartingOffset,
	IN PLFS_DEVICE_EXTENSION	LfsDeviceExt,	
	OUT	PNETDISK_ENABLE_MODE	NetdiskEnableMode
	);

#endif
#ifndef __SECONDARY_H__
#define __SECONDARY_H__


typedef enum _SECONDARY_REQ_TYPE {

	SECONDARY_REQ_CONNECT = 1,
	SECONDARY_REQ_SEND_MESSAGE,
	SECONDARY_REQ_DISCONNECT,
	SECONDARY_REQ_DOWN

} SECONDARY_REQ_TYPE, *PSECONDARY_REQ_TYPE;


typedef struct _SECONDARY {

#if __NDAS_FS__
	KSEMAPHORE						Semaphore;
#endif

	FAST_MUTEX						FastMutex ;	//  make critical section.
	
    LONG							ReferenceCount;

#define SECONDARY_FLAG_INITIALIZING			0x00000001
#define SECONDARY_FLAG_START				0x00000002
#define SECONDARY_FLAG_CLOSED				0x00000004

#define SECONDARY_FLAG_RECONNECTING			0x00000010
#if !__NDAS_FS__
#define SECONDARY_FLAG_CLEANUP_VOLUME		0x00000020
#define SECONDARY_FLAG_REMOUNT_VOLUME		0x00000040
#else
#define SECONDARY_FLAG_ERROR				0x80000000
#endif

	ULONG							Flags;

#if __NDAS_FS__
	struct _LFS_DEVICE_EXTENSION	*LfsDeviceExt;
#else
	PVOLUME_DEVICE_OBJECT			VolDo;	
#endif

	LPX_ADDRESS						PrimaryAddressList[MAX_SOCKETLPX_INTERFACE];
	LONG							NumberOfPrimaryAddress;

	ULONG							SessionId;

	LIST_ENTRY						FcbQueue;
#if __NDAS_FS__
	KSPIN_LOCK						FcbQSpinLock;	// protects FcbQueue
#else
	FAST_MUTEX						FcbQMutex;
#endif

#if __NDAS_FS__

	LIST_ENTRY						CcbQueue;
	FAST_MUTEX						CcbQMutex;	// protects CcbQueue
	ULONG							CcbCount;

#else

	LIST_ENTRY						RecoveryCcbQueue;
	FAST_MUTEX						RecoveryCcbQMutex;	// protects CcbQueue
	LIST_ENTRY						DeletedFcbQueue;

#endif

	HANDLE							ThreadHandle;
	PVOID							ThreadObject;
	KEVENT							ReadyEvent;

	UINT8								Buffer[sizeof(NDFS_REQUEST_HEADER) + sizeof(NDFS_WINXP_REQUEST_HEADER) + DEFAULT_NDAS_MAX_DATA_SIZE];

	LARGE_INTEGER					TryCloseTime;
	PIO_WORKITEM					TryCloseWorkItem;
	BOOLEAN							TryCloseActive;

	LIST_ENTRY						RequestQueue;
	KSPIN_LOCK						RequestQSpinLock;	// protects RequestQueue
	KEVENT							RequestEvent;

#if __NDAS_FS__
    LIST_ENTRY						DirNotifyList;
    PNOTIFY_SYNC					NotifySync;
#endif

	TDI_CONNECTION_INFO				ConnectionInfo;

	HANDLE							RecoveryThreadHandle;
	KEVENT							RecoveryReadyEvent;

	struct {

#define SECONDARY_THREAD_FLAG_INITIALIZING			0x00000001
#define SECONDARY_THREAD_FLAG_START					0x00000002
#define SECONDARY_THREAD_FLAG_STOPED				0x00000004
#define SECONDARY_THREAD_FLAG_TERMINATED			0x00000008

#define SECONDARY_THREAD_FLAG_CONNECTED				0x00000010
#define SECONDARY_THREAD_FLAG_DISCONNECTED			0x00000020

#define SECONDARY_THREAD_FLAG_UNCONNECTED			0x10000000
#define SECONDARY_THREAD_FLAG_REMOTE_DISCONNECTED	0x20000000
#define SECONDARY_THREAD_FLAG_ERROR					0x80000000

		ULONG						Flags;

		NTSTATUS					SessionStatus;

		NDAS_FC_STATISTICS			SendNdasFcStatistics;
		NDAS_FC_STATISTICS			RecvNdasFcStatistics;

		HANDLE						AddressFileHandle;
		PFILE_OBJECT				AddressFileObject;

		HANDLE						ConnectionFileHandle;
		PFILE_OBJECT				ConnectionFileObject;

		struct _SECONDARY_REQUEST	*SessionSlot[MAX_SLOT_PER_SESSION];
		LONG						IdleSlotCount;

		LONG						ReceiveWaitingCount;
#if __NDAS_FS__
		LPXTDI_OVERLAPPED_CONTEXT	ReceiveOverlapped;
#else
		TDI_RECEIVE_CONTEXT			TdiReceiveContext;
#endif
		NDFS_REPLY_HEADER			NdfsReplyHeader;
		
		SESSION_CONTEXT				SessionContext;
#if __NDAS_FS__
		LFS_TRANS_CTX				TransportCtx;
#endif

		NDFS_REPLY_HEADER			SplitNdfsReplyHeader;
		
#if __NDAS_FS__
		BOOLEAN						DiskGroup;
		LONG						VolRefreshTick;
#endif

	} Thread;
	
} SECONDARY, *PSECONDARY;


typedef struct _SECONDARY_REQUEST {

	SECONDARY_REQ_TYPE	RequestType;
	LONG				ReferenceCount;			// reference count
	
	LIST_ENTRY			ListEntry;

	ULONG				SessionId;

	BOOLEAN				Synchronous;
	KEVENT				CompleteEvent;
	NTSTATUS			ExecuteStatus;

	PUCHAR				OutputBuffer;

	struct	_LFS_CCB	*Ccb;
	BOOLEAN				PotentialDeadVolLock;

	ULONG				NdfsMessageAllocationSize;

	PUCHAR				SendBuffer;
	ULONG				SendBufferLength;

	PMDL				SendMdl;

	PUCHAR				RecvBuffer;
	ULONG				RecvBufferLength;

	PMDL				RecvMdl;

#include <pshpack1.h>

	union {

		UINT8		NdfsMessage[1];

		UNALIGNED struct {

			NDFS_REQUEST_HEADER	NdfsRequestHeader;
			UINT8				NdfsRequestData[1];
		};

		UNALIGNED struct {

			NDFS_REPLY_HEADER	NdfsReplyHeader;
			UINT8				NdfsReplyData[1];
		};
	};

#include <poppack.h>
	
} SECONDARY_REQUEST, *PSECONDARY_REQUEST;


typedef struct _NON_PAGED_FCB {

	SECTION_OBJECT_POINTERS	SectionObjectPointers;
	FAST_MUTEX				AdvancedFcbHeaderMutex;

} NON_PAGED_FCB, *PNON_PAGED_FCB;


typedef struct _LFS_FCB {

    FSRTL_ADVANCED_FCB_HEADER	Header;
    PNON_PAGED_FCB				NonPaged;

    FILE_LOCK					FileLock;

	LONG						ReferenceCount;			// reference count
	LIST_ENTRY					ListEntry;

	UNICODE_STRING				FullFileName;
    WCHAR						FullFileNameBuffer[NDFS_MAX_PATH];	

	UNICODE_STRING				CaseInSensitiveFullFileName;
    WCHAR						CaseInSensitiveFullFileNameBuffer[NDFS_MAX_PATH];	

	LONG						OpenCount;
	LONG						UncleanCount;
	BOOLEAN						FileRecordSegmentHeaderAvail;
	BOOLEAN						DeletePending;
	LARGE_INTEGER				OpenTime;

} LFS_FCB, *PLFS_FCB;


typedef enum _TYPE_OF_OPEN {

    UnopenedFileObject = 1,
    UserFileOpen,
    UserDirectoryOpen,
    UserVolumeOpen,
    VirtualVolumeFile,
    DirectoryFile,
    EaFile

} TYPE_OF_OPEN;


typedef struct	_LFS_CCB {

	UINT32					LfsMark;
	PSECONDARY				Secondary;
	PFILE_OBJECT			FileObject;
	BOOLEAN					RelatedFileObjectClosed;

	PLFS_FCB				Fcb;
	
	LIST_ENTRY				ListEntry;

	BOOLEAN					Corrupted;
	ULONG					SessionId;
	UINT64					PrimaryFileHandle;

	TYPE_OF_OPEN			TypeOfOpen;
	
	UINT32					IrpFlags;
	UINT8						IrpSpFlags;
	WINXP_REQUEST_CREATE	CreateContext;

#if 0
	VCN						CachedVcn;
	PCHAR					Cache;
#endif

	//
	// Valid only in secondary mode(or handles opened in secondary mode) 
	//			  and after IRP_MJ_DIRECTORY_CONTROL/IRP_MN_QUERY_DIRECTORY is called with DirectoryFile.
	// Used to restore current file query index after reconnecting to another primary. 
	// Set to (ULONG)-1 for unused.
	//
	ULONG					LastQueryFileIndex;

	// 
	//	Set when LastQueryFileIndex is set with current session's ID
	//  Need to use LastQueryFileIndex value for next query if session ID is changed.
	//
	ULONG					LastDirectoryQuerySessionId;

		
#if __NDAS_FS_TEST_MODE__
	PVOID					OriginalFsContext;
	PVOID					OriginalFsContext2;
#endif

	ULONG					BufferLength;
	UINT8						Buffer[1];

} LFS_CCB, *PLFS_CCB;


#endif

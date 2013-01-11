#ifndef __NDAS_NTFS_H__
#define __NDAS_NTFS_H__


#define NDNTFS_BUG						0
#define NDNTFS_INSUFFICIENT_RESOURCES	0
#define NDNTFS_UNEXPECTED				0
#define NDNTFS_LPX_BUG					0
#define NDNTFS_REQUIRED					0
#define NDNTFS_IRP_NOT_IMPLEMENTED		0
#define NDNTFS_IRP_NOT_IMPLEMENTED		0
#define NDNTFS_IRP_NOT_IMPLEMENTED		0


#define NDNTFS_TIME_OUT							(3000*HZ)
#define NDNTFS_SECONDARY_THREAD_FLAG_TIME_OUT	(3*HZ)
#define NDNTFS_TRY_CLOSE_DURATION				NDNTFS_SECONDARY_THREAD_FLAG_TIME_OUT


#define THREAD_WAIT_OBJECTS 3           // Builtin usable wait blocks from <ntddk.h>
#define ADD_ALIGN8(Length) ((Length+7) >> 3 << 3)

#define OPERATION_NAME_BUFFER_SIZE 80

#define IS_SECONDARY_FILEOBJECT( FileObject ) (												\
																							\
	FileObject			   &&																\
	FileObject->FsContext  &&																\
	FileObject->FsContext2 &&																\
	FlagOn(((PSCB)(FileObject->FsContext))->Fcb->NdNtfsFlags, ND_NTFS_FCB_FLAG_SECONDARY)	\
)


#define SECONDARY_MESSAGE_TAG		'tmsN'
#define NDNTFS_ALLOC_TAG			'tafN'
#define PRIMARY_SESSION_BUFFERE_TAG	'tbsN'
#define PRIMARY_SESSION_MESSAGE_TAG 'mspN'
#define OPEN_FILE_TAG				'tofN'
#define FILE_NAME_TAG				'tnfN'
#define TAG_FCB						'tffN'
#define TAG_FILENAME_BUFFER			'fbfN'
#define TAG_CCB						'cbfN'
#define TAG_FCB_NONPAGED			'fnfN'
#define TAG_ERESOURCE				'erfN'
#define TAG_CCB						'cbfN'


#define	MEMORY_CHECK_SIZE		0

#define MAX_RECONNECTION_TRY	60

typedef struct _SESSION_CONTEXT {

	_U32	SessionKey;

	union {

		_U8		Flags;

		struct {

			_U8 MessageSecurity:1;
			_U8 RwDataSecurity:1;
			_U8 Reserved:6;
		};
	};

	_U16	ChallengeLength;
	_U8		ChallengeBuffer[MAX_CHALLENGE_LEN];
	_U32	PrimaryMaxDataSize;
	_U32	SecondaryMaxDataSize;
	_U16	Uid;
	_U16	Tid;
	_U16	NdfsMajorVersion;
	_U16	NdfsMinorVersion;

	_U8		SessionSlotCount;

	_U32	BytesPerFileRecordSegment;
	_U32	BytesPerSector;
	ULONG	SectorShift;
	ULONG	SectorMask;
	_U32	BytesPerCluster;
	ULONG	ClusterShift;
	ULONG	ClusterMask;

} SESSION_CONTEXT, *PSESSION_CONTEXT;


extern ULONG gOsMajorVersion;
extern ULONG gOsMinorVersion;


#define IS_WINDOWSXP_OR_LATER() \
    (((gOsMajorVersion == 5) && (gOsMinorVersion >= 1)) || \
     (gOsMajorVersion > 5))

#define IS_WINDOWS2K() \
    ((gOsMajorVersion == 5) && (gOsMinorVersion == 0))

#define IS_WINDOWSXP() \
    ((gOsMajorVersion == 5) && (gOsMinorVersion == 1))

#define IS_WINDOWSNET() \
    ((gOsMajorVersion == 5) && (gOsMinorVersion == 2))

typedef
VOID 
(*NDASNTFS_FSRTL_NOTIFY_FILTER_REPORT_CHANGE) ( 
	IN PNOTIFY_SYNC NotifySync,
    IN PLIST_ENTRY NotifyList,
    IN PSTRING FullTargetName,
    IN USHORT TargetNameOffset,
    IN PSTRING StreamName OPTIONAL,
    IN PSTRING NormalizedParentName OPTIONAL,
    IN ULONG FilterMatch,
    IN ULONG Action,
    IN PVOID TargetContext,
    IN PVOID FilterContext
    );

extern NDASNTFS_FSRTL_NOTIFY_FILTER_REPORT_CHANGE NdasNtfsFsRtlNotifyFilterReportChange;

typedef
VOID 
(*NDASNTFS_FSRTL_NOTIFY_FILTER_CHANGE_DIRECTORY) ( 
	IN PNOTIFY_SYNC NotifySync,
	IN PLIST_ENTRY NotifyList,
	IN PVOID FsContext,
	IN PSTRING FullDirectoryName,
	IN BOOLEAN WatchTree,
	IN BOOLEAN IgnoreBuffer,
	IN ULONG CompletionFilter,
	IN PIRP NotifyIrp,
	IN PCHECK_FOR_TRAVERSE_ACCESS TraverseCallback OPTIONAL,
	IN PSECURITY_SUBJECT_CONTEXT SubjectContext OPTIONAL,
	IN PFILTER_REPORT_CHANGE FilterCallback OPTIONAL
	);

extern NDASNTFS_FSRTL_NOTIFY_FILTER_CHANGE_DIRECTORY NdasNtfsFsRtlNotifyFilterChangeDirectory;

#endif

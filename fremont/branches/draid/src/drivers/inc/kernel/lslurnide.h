#ifndef LANSCSI_LURN_IDE_H
#define LANSCSI_LURN_IDE_H

extern LURN_INTERFACE LurnIdeDiskInterface;
extern LURN_INTERFACE LurnIdeCDDVDInterface;
extern LURN_INTERFACE LurnIdeVCDDVDInterface;


//////////////////////////////////////////////////////////////////////////
//
//	defines
//
#define PACKETCMD_BUFFER_POOL_TAG			'bpSL'

#define FREQ_PER_SEC						NANO100_PER_SEC

#define LURNIDE_GENERIC_TIMEOUT				(FREQ_PER_SEC*8)

#define LURNIDE_IDLE_TIMEOUT				(FREQ_PER_SEC*5)

#define LURNIDE_THREAD_START_TIME_OUT	(NANO100_PER_SEC * 10)

//////////////////////////////////////////////////////////////////////////
//	Lurn Ide Extension
//

#define LUDATA_FLAG_POWERMGMT_SUPPORT	0x00000001
#define LUDATA_FLAG_POWERMGMT_ENABLED	0x00000002
#define LUDATA_FLAG_WCACHE_SUPPORT		0x00000004
#define LUDATA_FLAG_WCACHE_ENABLED		0x00000008
#define LUDATA_FLAG_FUA_SUPPORT			0x00000010
#define LUDATA_FLAG_FUA_ENABLED			0x00000020
#define LUDATA_FLAG_SMART_SUPPORT		0x00000040
#define LUDATA_FLAG_SMART_ENABLED		0x00000080


typedef struct _NDAS_DEVLOCK_STATUS {
	UCHAR	Acquired:1;				// Lock acquired
	UCHAR	AddressRangeValid:1;	// Valid is IOs' address range that requires lock acquisition
	UCHAR	Lost:1;					// Lock lost during reconnection.
	UCHAR	Reserved1:5;
	UCHAR	Reserved2[3];
	UINT64	StartingAddress;		// IO address range
	UINT64	EndingAddress;			// IO address range
} NDAS_DEVLOCK_STATUS, *PNDAS_DEVLOCK_STATUS;

typedef	struct _LU_HWDATA {

	//
	// NDAS device info
	//

	BYTE			HwType;
	BYTE			HwVersion;
	UINT16			HwRevision;

	//
	// NDAS device location info
	//

	UCHAR			LanscsiTargetID;	// mapping to UnitNumber of NDAS device
	UCHAR			LanscsiLU;

	//
	// NDAS device lock status
	//

	LONG				AcquiredLockCount;
	LONG				LostLockCount;
	// Index value is lock ID.
	NDAS_DEVLOCK_STATUS	DevLockStatus[NDAS_NR_MAX_GPLOCK];

	//
	// Disk configuration
	//

	UINT32			LudataFlags;
	UINT32			PduDescFlags;
	UINT64			SectorCount;
	UINT16			BlockBytes;

	CHAR			Serial[20];
	CHAR			FW_Rev[8];
	CHAR			Model[40];

	//
	// Smart attributes
	//

	UINT64			InitialDevPowerRecycleCount;

} LU_HWDATA, *PLU_HWDATA;

typedef struct _LURNEXT_IDE_DEVICE {
	//
	// Lurn back-pointer
	//

	PLURELATION_NODE	Lurn;

	//
	// Thread information
	//

	HANDLE				ThreadHandle;
	PVOID				ThreadObject;
	KEVENT				ThreadReadyEvent;		// Set when Thread begin.

	//
	// CCB request
	// shared between this worker thread and requestor's thread.
	//

	LIST_ENTRY			ThreadCcbQueue;	// protected by LurnSpinLock
	KEVENT				ThreadCcbArrivalEvent;


	//
	// LURN stop at the host side.
	// Usually set by SCSIOP_START_STOP_UNIT command.
	//

	BOOLEAN				LURNHostStop;

	//
	// Disk dirty
	// TRUE if write operations occur and does not issue flush yet.
	// 
	//

	BOOLEAN				DiskWriteCacheDirty;

	//
	// Last time when accessing the disk media that make the disk stay awake.
	//

	LARGE_INTEGER		LastMediaAccessTime;

	//
	// Maximum data send/receive length
	//

	UINT32			MaxDataSendLength;
	UINT32			MaxDataRecvLength;

	//
	// Traffic control at the disk block level.
	// write block size control information	
	//

	UINT32			NrWritesAfterLastRetransmits;
	UINT32			WriteSplitSize; // in sectors

	// Read block size control information
	UINT32			NrReadAfterLastPacketLoss;
	UINT32			ReadSplitSize; // in bytes

	//
	// Retransmit and packet loss data
	//

	ULONG				Retransmits;
	ULONG				PacketLoss;

	//
	//	Lanscsi session
	//

	LANSCSI_SESSION		LanScsiSession;
	LSTRANS_TYPE		LstransType;


	//
	// LU data
	// Physical location and configuration
	//

	LU_HWDATA			LuHwData;

	//
	//	Content encrypt
	//

	UCHAR	CntEcrMethod;
	ULONG	CntEcrBufferLength;
	PBYTE	CntEcrBuffer;
	PNCIPHER_INSTANCE	CntEcrCipher;
	PNCIPHER_KEY		CntEcrKey;


	//
	// Additional Information to support RAID.
	//
	// Buffer for write-check work-around of NDAS 2.0 write bug.

	PBYTE				WriteCheckBuffer;

	
	//
	//	for ODD need checking register of target device state 
	//		and wait for busy.
	//

/* Bits of HD_STATUS */
#define ERR_STAT		0x01
#define INDEX_STAT		0x02
#define ECC_STAT		0x04	/* Corrected error */
#define DRQ_STAT		0x08
#define SEEK_STAT		0x10
#define WRERR_STAT		0x20
#define READY_STAT		0x40
#define BUSY_STAT		0x80
	BYTE	RegStatus;

/* Bits for HD_ERROR */
#define MARK_ERR		0x01	/* Bad address mark */
#define TRK0_ERR		0x02	/* couldn't find track 0 */
#define ABRT_ERR		0x04	/* Command aborted */
#define MCR_ERR			0x08	/* media change request */
#define ID_ERR			0x10	/* ID field not found */
#define MC_ERR			0x20	/* media changed */
#define ECC_ERR			0x40	/* Uncorrectable ECC error */
#define BBD_ERR			0x80	/* pre-EIDE meaning:  block marked bad */
#define ICRC_ERR		0x80	/* new meaning:  CRC error during transfer */
	BYTE	RegError;


#define NON_STATE			0x00000000
#define READY				0x00000001
#define NOT_READY			0x00000002
#define MEDIA_NOT_PRESENT	0x00000004
#define BAD_MEDIA			0x00000008
#define NOT_READY_PRESENT	0x00000010
#define RESET_POWERUP		0x00000100
#define NOT_READY_MEDIA		0x00000200
#define INVALID_COMMAND		0x00001000
#define	MEDIA_ERASE_ERROR	0x00000800				

	UINT32	ODD_STATUS;
	BYTE	PrevRequest;
	LARGE_INTEGER	DVD_Acess_Time;
#define DVD_NO_W_OP		0
#define DVD_W_OP		1
	UINT32	DVD_STATUS;

#define MAX_REFEAT_COUNT  15
	ULONG	DVD_REPEAT_COUNT;


	ULONG	DataSectorSize;
	USHORT	DVD_MEDIA_TYPE;
	ULONG	DataSectorCount;

//  device model string
#define IO_DATA_STR				"MATSHITADVD-RAM SW-9583S"
#define IO_DATA					0x00000001
#define LOGITEC_STR				"HL-DT-ST DVDRAM GSA-4082B"
#define LOGITEC					0x00000002
#define IO_DATA_STR_9573		"MATSHITADVD-RAM SW-9573S"
#define IO_DATA_9573			0x00000003

	ULONG			DVD_TYPE;

} LURNEXT_IDE_DEVICE, *PLURNEXT_IDE_DEVICE;


#define LURNIDE_INITIALIZE_PDUDESC(PLURNIDE, PDUDESC_POINTER, OPCODE, COMMAND, DESTADDR, BLOCKCNT, DATABUFFER_LENGTH, DATABUFFER_POINTER, TIMEOUT_POINTER)	\
			INITIALIZE_PDUDESC(PDUDESC_POINTER, (PLURNIDE)->LuHwData.LanscsiTargetID, (PLURNIDE)->LuHwData.LanscsiLU, OPCODE, COMMAND,	\
					(PLURNIDE)->LuHwData.PduDescFlags,																				\
				 DESTADDR, BLOCKCNT, DATABUFFER_LENGTH, DATABUFFER_POINTER, TIMEOUT_POINTER)

#define LURNIDE_ATAPI_PDUDESC(PLURNIDE, PDUDESC_POINTER, OPCODE, COMMAND, CCB_POINTER, TIMEOUT_POINTER)                             \
	        INITIALIZE_ATAPIPDUDESC(PDUDESC_POINTER, (PLURNIDE)->LuHwData.LanscsiTargetID,(PLURNIDE)->LuHwData.LanscsiLU,				\
			                     OPCODE, COMMAND, (PLURNIDE)->LuHwData.PduDescFlags, (CCB_POINTER)->PKCMD,							\
                                 (CCB_POINTER)->SecondaryBuffer, (CCB_POINTER)->SecondaryBufferLength, (PLURNIDE)->DVD_TYPE, TIMEOUT_POINTER)

extern LURN_INTERFACE LurnIdeDiskInterface;
extern LURN_INTERFACE LurnIdeODDInterface;
extern LURN_INTERFACE LurnIdeMOInterface;


//////////////////////////////////////////////////////////////////////////
//
//	common functions to IDE-specific LURNs
//
void
ConvertString(
		  PCHAR	result,
		  PCHAR	source,
		  int	size
	);

BOOLEAN
Lba_capacity_is_ok(
		struct hd_driveid *id
	);

NTSTATUS
LurnIdeUpdate(
		PLURELATION_NODE		Lurn,
		PLURNEXT_IDE_DEVICE		IdeExt,
		PCCB					Ccb
	);

VOID
LurnIdeStop(
			PLURELATION_NODE Lurn,
			PCCB Ccb
			);

NTSTATUS
LurnIdeRestart(
		   PLURELATION_NODE	Lurn,
		   PCCB				Ccb
		   );

NTSTATUS
LurnIdeAbortCommand(
		PLURELATION_NODE Lurn,
		PCCB Ccb
	);

NTSTATUS
LSLurnIdeNoop(
		PLURELATION_NODE		Lurn,
		PLURNEXT_IDE_DEVICE		IdeExt,
		PCCB					Ccb
	);

NTSTATUS
LSLurnIdeReconnect(
		   PLURELATION_NODE		Lurn,
		   PLURNEXT_IDE_DEVICE		IdeExt
	);

NTSTATUS
LurnIdeQuery(
		PLURELATION_NODE	Lurn,
		PLURNEXT_IDE_DEVICE	IdeDev,
		PCCB				Ccb
	);

NTSTATUS
LurnIdeDestroy(
		PLURELATION_NODE Lurn
	);


#endif // LANSCSI_LURN_IDE_H

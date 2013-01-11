#ifndef LANSCSI_LU_RELATION_H
#define LANSCSI_LU_RELATION_H

#include "lurdesc.h"
#include "ndasbusioctl.h"
#include "ndasscsiioctl.h"
#include "LSTransport.h"
#include "LSProto.h"
#include "LsCcb.h"
#include "lsutils.h"

#ifndef C_ASSERT
#define C_ASSERT(e) typedef char __C_ASSERT__[(e)?1:-1]
#endif
#ifndef C_ASSERT_SIZEOF
#define C_ASSERT_SIZEOF(type, size) C_ASSERT(sizeof(type) == size)
#endif

//
//	Pool tags for LUR
//

#define LURN_IOCTL_POOL_TAG						'ilSL'


#define NDAS_MAX_TRANSFER_LENGTH	(1024 * 1024)

//
//	Defines for reconnecting
//	Do not increase RECONNECTION_MAX_TRY.
//	Windows XP allows busy return within 19 times.
//
#define MAX_RECONNECTION_INTERVAL	(NANO100_PER_SEC*4)
#define RECONNECTION_MAX_TRY		19

#define	LUR_CONTENTENCRYPT_KEYLENGTH	64

//////////////////////////////////////////////////////////////////////////
//
//	LUR Ioctl
//

//
//	LUR Query
//
typedef enum _LUR_INFORMATION_CLASS {

	LurPrimaryLurnInformation=1,
	LurEnumerateLurn,
	LurRefreshLurn

} LUR_INFORMATION_CLASS, *PLUR_INFORMATION_CLASS;

typedef struct _LUR_QUERY {

	UINT32					Length;
	LUR_INFORMATION_CLASS	InfoClass;
	UINT32					QueryDataLength; // <- MUST be multiples of 4 for information to fit in align
	UCHAR					QueryData[1];

}	LUR_QUERY, * PLUR_QUERY;

#define SIZE_OF_LURQUERY(QUERY_DATA_LENGTH, RETURN_INFORMATION_SIZE) \
	(FIELD_OFFSET(LUR_QUERY, QueryData) + QUERY_DATA_LENGTH + RETURN_INFORMATION_SIZE)

#define LUR_QUERY_INITIALIZE(pLurQuery, INFO_CLASS, QUERY_DATA_LENGTH, RETURN_INFORMATION_SIZE) \
	(pLurQuery) = (PLUR_QUERY)ExAllocatePoolWithTag(NonPagedPool, SIZE_OF_LURQUERY(QUERY_DATA_LENGTH, RETURN_INFORMATION_SIZE), LURN_IOCTL_POOL_TAG); \
	if(pLurQuery)	\
	{	\
		RtlZeroMemory((pLurQuery), SIZE_OF_LURQUERY(QUERY_DATA_LENGTH, RETURN_INFORMATION_SIZE));	\
		(pLurQuery)->Length = SIZE_OF_LURQUERY(QUERY_DATA_LENGTH, RETURN_INFORMATION_SIZE);	\
		(pLurQuery)->InfoClass = INFO_CLASS;	\
		(pLurQuery)->QueryDataLength = QUERY_DATA_LENGTH;	\
	}

// address of the returned information
#define LUR_QUERY_INFORMATION(QUERY_PTR) \
	(((PBYTE)QUERY_PTR + FIELD_OFFSET(LUR_QUERY, QueryData) + (QUERY_PTR)->QueryDataLength))

#define LURN_PRIMARY_ID_LENGTH 16

//
//	Returned information
//
typedef struct _LURN_INFORMATION {

	UINT32					Length;
	UINT32					LurnId;
	UINT32					LurnType;

	union {

		UCHAR		Buffer[100];
		
		struct {

			TA_LSTRANS_ADDRESS		NetDiskAddressOld;
			TA_LSTRANS_ADDRESS		BindingAddressOld;
		};
	
		struct {

			union {

				UCHAR			Buffer1[FIELD_OFFSET(TA_ADDRESS, Address) + TDI_ADDRESS_LENGTH_LPX];				
				
				struct	{

					TA_ADDRESS		NdasNetDiskAddress;
					UCHAR			Address1[TDI_ADDRESS_LENGTH_LPX - sizeof(UCHAR)];				
				};
			};

			union {

				UCHAR			Buffer2[FIELD_OFFSET(TA_ADDRESS, Address) + TDI_ADDRESS_LENGTH_LPX];				
				
				struct	{

					TA_ADDRESS		NdasBindingAddress;
					UCHAR			Address2[TDI_ADDRESS_LENGTH_LPX - sizeof(UCHAR)];				
				};
			};
		};
	};

	UCHAR					UnitDiskId;
	UCHAR					Reserved;
	UCHAR					UserID[LSPROTO_USERID_LENGTH];
	UCHAR					Password[LSPROTO_PASSWORD_LENGTH];
	ACCESS_MASK				AccessRight;
	UINT64					UnitBlocks;
	UINT32					BlockBytes;
	UINT32					StatusFlags;

	UCHAR					PrimaryId[LURN_PRIMARY_ID_LENGTH]; // NDSC_ID. 
											// RAID set ID for RAID.
											// NetdiskAddress + Port + Unit No for single disks.

}	LURN_INFORMATION, *PLURN_INFORMATION;

C_ASSERT( sizeof(TA_LSTRANS_ADDRESS) * 2 < 100 );

typedef struct _LURN_PRIMARYINFORMATION {

	UINT32					Length;
	LURN_INFORMATION		PrimaryLurn;

}	LURN_PRIMARYINFORMATION, *PLURN_PRIMARYINFORMATION;

typedef struct _LURN_ENUM_INFORMATION {

	UINT32					Length;
	LURN_INFORMATION		Lurns[1];

}	LURN_ENUM_INFORMATION, *PLURN_ENUM_INFORMATION;

typedef struct _LURN_DVD_STATUS {
	UINT32					Length;
	LARGE_INTEGER			Last_Access_Time;
	UINT32					Status;
} LURN_DVD_STATUS, *PLURN_DVD_STATUS;

#define REFRESH_POOL_TAG 'SHFR'
typedef struct _LURN_REFRESH {
	UINT32					Length;
	ULONG					CcbStatusFlags;
} LURN_REFRESH, *PLURN_REFRESH;

#define		LURN_UPDATECLASS_WRITEACCESS_USERID		0x0001
#define		LURN_UPDATECLASS_READONLYACCESS			0x0002

typedef struct _LURN_UPDATE {

	UINT16			UpdateClass;

}	LURN_UPDATE, *PLURN_UPDATE;

//
// Device lock index
// NOTE: The number of lock index are irrelevant to NDAS chip lock index.
//

#define LURNDEVLOCK_ID_NONE		0
#define LURNDEVLOCK_ID_XIFS		1
#define LURNDEVLOCK_ID_EXTLOCK	2
#define LURNDEVLOCK_ID_BUFFLOCK	3

//
// Device lock numbers
//

#define NDAS_NR_GPLOCK		(1 + 3)	// NDAS chip 1.1, 2.0 including null lock, buffer lock, and write previlege lock.
#define NDAS_NR_ADV_GPLOCK	(1 + 8)	// NDAS chip 2.5 including null lock.

#define NDAS_NR_MAX_GPLOCK	((NDAS_NR_ADV_GPLOCK>NDAS_NR_GPLOCK)? \
							NDAS_NR_ADV_GPLOCK:NDAS_NR_GPLOCK)

//
// Lock data length
//

#define LURNDEVLOCK_LOCKDATA_LENGTH		64

#define LURNLOCK_OPCODE_ACQUIRE		0x01	// Available in NDAS chip 1.1, 2.0.
#define LURNLOCK_OPCODE_RELEASE		0x02	// Available in NDAS chip 1.1, 2.0.
#define LURNLOCK_OPCODE_QUERY_OWNER	0x03	// Available in NDAS chip 1.1, 2.0.
#define LURNLOCK_OPCODE_GETDATA		0x04	// Available in NDAS chip 1.1, 2.0.
#define LURNLOCK_OPCODE_SETDATA		0x05	// Available in NDAS chip 2.5 and later.
#define LURNLOCK_OPCODE_BREAK		0x06	// Available in NDAS chip 2.5 and later.

typedef struct _LURN_DEVLOCK_CONTROL {
	UINT32	LockId;
	BYTE	LockOpCode;
	BYTE	AdvancedLock:1;				// advanced GP lock operation. ADV_GPLOCK feature required.
	BYTE	AddressRangeValid:1;
	BYTE	RequireLockAcquisition:1;
	BYTE	Reserved1:5;
	BYTE	Reserved2[2];
	UINT64	StartingAddress;
	UINT64	EndingAddress;
	UINT64	ContentionTimeOut;

	//
	// Lock data
	//

	BYTE	LockData[LURNDEVLOCK_LOCKDATA_LENGTH];
} LURN_DEVLOCK_CONTROL, *PLURN_DEVLOCK_CONTROL;

//////////////////////////////////////////////////////////////////////////
//
//	LURN event structure
//
typedef struct _LURELATION LURELATION, *PLURELATION;
typedef struct _LURN_EVENT  LURN_EVENT, *PLURN_EVENT;

typedef VOID (*LURN_EVENT_CALLBACK)(
				PLURELATION	Lur,
				PLURN_EVENT	LurnEvent
			);

typedef	enum _LURN_EVENT_CLASS {

	LURN_REQUEST_NOOP_EVENT

} LURN_EVENT_CLASS, *PLURN_EVENT_CLASS;

typedef struct _LURN_EVENT {

	UINT32				LurnId;
	LURN_EVENT_CLASS	LurnEventClass;

} LURN_EVENT, *PLURN_EVENT;


//////////////////////////////////////////////////////////////////////////
//
//	Logical Unit Relation Node
//

typedef struct _LURELATION_NODE LURELATION_NODE, *PLURELATION_NODE;


//////////////////////////////////////////////////////////////////////////
//
//	Logical Unit Relation
//
//
//	For SCSI commands:	
//				LurId[0] = PathId
//				LurId[1] = TargetId
//				LurId[2] = Lun
//

//
//	LUR flags
//	Drivers set the default values,but users can override them with LurOptions in LurDesc.
//

//
//	LURFLAG_W2K_READONLY_PATCH	:
//		Windows 2000 NTFS does not recognize read-only disk.
//		Emulate read-only NTFS by reporting read/write disk, but not actually writable.
//		And, NDAS file system filter should deny write file operation.
//

#define LURFLAG_W2K_READONLY_PATCH		0x00000001
#define LURFLAG_WRITE_CHECK_REQUIRED		0x00000002

typedef struct _LURELATION {

	UINT16					Length;
	UINT16					Type;
	UCHAR					LurId[LURID_LENGTH];
	UINT32					LurFlags;
	PDEVICE_OBJECT			AdapterFunctionDeviceObject;
	PVOID					AdapterHardwareExtension;
	LURN_EVENT_CALLBACK		LurnEventCallback;
	UINT16					DevType;
	UINT16					DevSubtype;

	//
	// Disk ending block address
	//

	UINT64					EndingBlockAddr;

	//
	//	Lowest version number of NDAS hardware counterparts.
	//

	UINT16					LowestHwVer;


	UINT64					UnitBlocks;

	UINT64					MaxChildrenSectorCount;

	//
	//	Content encrypt
	//	CntEcrKeyLength is the key length in bytes.
	//

	UCHAR	CntEcrMethod;
	USHORT	CntEcrKeyLength;
	UCHAR	CntEcrKey[LUR_CONTENTENCRYPT_KEYLENGTH];

	//
	//	Device mode
	//

	NDAS_DEV_ACCESSMODE	DeviceMode;

	//
	//	NDAS features
	//

	NDAS_FEATURES		SupportedNdasFeatures;
	NDAS_FEATURES		EnabledNdasFeatures;
	
	//
	//	Block access control list
	//

	LSU_BLOCK_ACL			SystemBacl;
	LSU_BLOCK_ACL			UserBacl;

	//
	//	Children
	//

	UINT32					NodeCount;
	PLURELATION_NODE		Nodes[1];

} LURELATION, *PLURELATION;


//////////////////////////////////////////////////////////////////////////
//
//	exported functions
//


//
//	LURELATION
//
NTSTATUS
LurCreate (
	IN  PLURELATION_DESC	LurDesc,
	IN  BOOLEAN				EnableSecondaryOnly,
	IN  BOOLEAN				EnableW2kReadOnlyPacth,
	IN  PDEVICE_OBJECT		AdapterFunctionDeviceObject,
	IN  PVOID				AdapterHardwareExtension,
	IN  LURN_EVENT_CALLBACK	LurnEventCallback,
	OUT PLURELATION			*Lur
	);

VOID
LurClose (
	PLURELATION	Lur
	);

NTSTATUS
LurRequest (
	PLURELATION	Lur,
	PCCB		Ccb
	);

VOID
LurCallBack(
	PLURELATION	Lur,
	PLURN_EVENT	LurnEvent
	);

#endif // LANSCSI_LURN_H

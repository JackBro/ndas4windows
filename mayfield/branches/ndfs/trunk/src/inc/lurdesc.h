#ifndef LANSCSI_LURNDESC_H
#define LANSCSI_LURNDESC_H

#include "LanScsi.h"


//////////////////////////////////////////////////////////////////////////
//
//	NDAS software access mode and rights.
//	Abstracts hardware's access mode and rights.
//

//
//	= Possible NDAS device mode =
//	exclusive write, exclusive read : supported (DEVMODE_SUPER_READWRITE, Requires super user right) 
//	exclusive write, shared read    : supported (DEVMODE_EXCLUSIVE_READWRITE) 
//	exclusive write, no read        : not supported
//	shared write, exclusive read    : not supported
//	shared write, shared read       : supported (DEVMODE_SHARED_READWRITE)
//	shared write, no read           : not supported
//	no write, shared read           : supported (DEVMODE_SHARED_READONLY)
//	no write, exclusive read        : not supported
//	no write, no read               : supported (?) 
//
//	Use DEVMODE_EXCLUSIVE_READWRITE for non-sharing NDAS device such as
//	optical devices (CD/DVD drives), magnetic-optical devices (MO drives)
//
//

typedef enum _NDAS_ACCESSRIGHTS {

	NDASACCRIGHT_READ            = 0x00000001,
	NDASACCRIGHT_WRITE           = 0x00000002,
	NDASACCRIGHT_EXCLUSIVE_READ  = 0x00000004,
	NDASACCRIGHT_EXCLUSIVE_WRITE = 0x00000008

} NDAS_ACCESSRIGHTS, *PNDAS_ACCESSRIGHTS;

typedef enum _NDAS_DEV_ACCESSMODE {

	DEVMODE_SHARED_READONLY	    = NDASACCRIGHT_READ,
	DEVMODE_SHARED_READWRITE    = NDASACCRIGHT_READ | NDASACCRIGHT_WRITE,
	DEVMODE_EXCLUSIVE_READWRITE = NDASACCRIGHT_READ | NDASACCRIGHT_WRITE | NDASACCRIGHT_EXCLUSIVE_WRITE,
	DEVMODE_SUPER_READWRITE     = NDASACCRIGHT_READ | NDASACCRIGHT_WRITE | NDASACCRIGHT_EXCLUSIVE_READ | NDASACCRIGHT_EXCLUSIVE_WRITE

} NDAS_DEV_ACCESSMODE, *PNDAS_DEV_ACCESSMODE;

//////////////////////////////////////////////////////////////////////////
//
//	NDAS device feature
//	NDAS device referred as a NDAS
//	Reported by NDAS port driver
//
//
//	Supported features based on hardware version
//
// HW version 1.0: NDASFEATURE_SECONDARY
//
// HW version 1.1: NDASFEATURE_SECONDARY
//                 NDASFEATURE_OOB_WRITE
//                 NDASFEATURE_RO_FAKE_WRITE
//                 NDASFEATURE_SIMULTANEOUS_WRITE
//                 NDASFEATURE_GP_LOCK
//
// HW version 2.0: NDASFEATURE_SECONDARY
//                 NDASFEATURE_OOB_WRITE
//                 NDASFEATURE_RO_FAKE_WRITE
//                 NDASFEATURE_SIMULTANEOUS_WRITE
//                 NDASFEATURE_WRITE_CHECK
//                 NDASFEATURE_GP_LOCK
//
// HW version 2.5: NDASFEATURE_SECONDARY
//                 NDASFEATURE_OOB_WRITE
//                 NDASFEATURE_RO_FAKE_WRITE
//                 NDASFEATURE_SIMULTANEOUS_WRITE
//                 NDASFEATURE_ADV_GP_LOCK
//



typedef enum _NDAS_FEATURES {

	//
	// Secondary feature
	//
	// disallows write operations by default on shared rw mode.
	//

	NDASFEATURE_SECONDARY       = 0x00000001,

	//
	// Out-of-bound write feature
	//
	// allows writes to an NDAS device regardless of device access mode.
	// For simultaneous out-of-bound write, enable SIMULTANEOUS_WRITE.
	// NewTech NDAS product requires simultaneous out-of-bound write.
	//

	NDASFEATURE_OOB_WRITE       = 0x00000002,

	//
	// Fake write feature in read-only blocks
	//
	// Return success without actual write when the write operation
	// comes to read-only blocks.
	//

	NDASFEATURE_RO_FAKE_WRITE	= 0x00000004,

	//
	// Simultaneous write feature
	//
	// Allows multiple hosts to write blocks.
	// For simultaneous out-of-bound write, this feature must be enabled.
	// NewTech NDAS product requires simultaneous out-of-bound write.
	//
	// For NDAS software that supports only secondary no-write sharing,
	// disable this feature using LUR option for better performance.
	//
	// Implementation varies depending on NDAS chip versions.
	// NDAS chip 1.1 and 2.0 uses reserved GP lock to synchronize writes.
	// NDAS chip 2.5 uses buffer lock.
	//

	NDASFEATURE_SIMULTANEOUS_WRITE = 0x00000008,

	//
	// Write check feature
	//
	// Check if data blocks are correctly written by reading written blocks.
	//

	NDASFEATURE_WRITE_CHECK		= 0x00000010,

	//
	// General purpose lock feature
	//
	// Also known as SEMA in NDAS chip 1.1, 2.0
	// Applications can acquire locks for their functionality.
	// NOTE: Buggy. Patches are applied.
	//		 This is not buffer lock.
	//

	NDASFEATURE_GP_LOCK         = 0x00000020,

	//
	// Advanced general purpose lock feature
	//
	// New feature of NDAS chip 2.5 or later.
	// Applications can acquire locks for their functionality.
	// Each GP lock contains 64 bit data.
	// NOTE: This is not buffer lock.
	//

	NDASFEATURE_ADV_GP_LOCK     = 0x00000040

} NDAS_FEATURES, *PNDAS_FEATURES;


//////////////////////////////////////////////////////////////////////////
//
//	Logical Unit Relation Node specific extension
//

//
//	Optional init data
//
#define LURNDESC_INITDATA_BUFFERLEN		80

//
//	LUR ID ( SCSI address )
//
#define LURID_LENGTH		8
#define LURID_PATHID		0
#define LURID_TARGETID		1
#define LURID_LUN			2

//
//	LUR device types
//	Even though an LUR has a Disk LURN,
//	It can be a DVD to Windows.
//
#define LUR_DEVTYPE_HDD		0x00000001
#define LUR_DEVTYPE_ODD		0x00000002
#define LUR_DEVTYPE_MOD		0x00000003

//
//	LUR Node type
//
#define 	LURN_AGGREGATION		0x0000
#define 	LURN_MIRRORING			0x0001
#define 	LURN_IDE_DISK			0x0002
#define 	LURN_IDE_ODD			0x0003
#define		LURN_IDE_MO				0x0004
//#define 	LURN_RAID1				0x0005		obsolete
//#define 	LURN_RAID4				0x0006		obsolete
#define 	LURN_RAID0				0x0007
#define 	LURN_AOD				0x0008
#define 	LURN_RAID1R				0x0009
#define 	LURN_RAID4R				0x000A
#define 	LURN_NULL				0xFFFF

typedef	UINT16 LURN_TYPE, *PLURN_TYPE;


#include <pshpack1.h>

//
//	IDE specific information
//
//  Used in _LURELATION_NODE_DESC
//
typedef struct _LURNDESC_IDE {

	UINT32					UserID;
	UINT64					Password;
	TA_LSTRANS_ADDRESS		TargetAddress;	// sizeof(TA_LSTRANS_ADDRESS) == 26 bytes
	TA_LSTRANS_ADDRESS		BindingAddress;
	UCHAR					HWType;
	UCHAR					HWVersion;
	UCHAR					LanscsiTargetID;
	UCHAR					LanscsiLU;
	UINT64					EndBlockAddrReserved;

} LURNDESC_IDE, *PLURNDESC_IDE;


//
//	MirrorV2 specific information
//
//  Used in _LURELATION_NODE_DESC
//  Negative sector means index from last sector(ex: -1 indicates last sector, -2 for 1 before last sector...)
//
typedef struct _NDAS_LURN_RAID_INFO_V2 {
	UINT32 SectorsPerBit; // 2^7(~ 2^11)
	UINT32 nSpareDisk;    // 0~
} NDAS_LURN_RAID_INFO_V2, *PNDAS_LURN_RAID_INFO_V2;

#if !defined(LURDESC_NO_DOWNLEVEL_STRUCT)
#define INFO_RAID NDAS_LURN_RAID_INFO_V2
#define PINFO_RAID PNDAS_LURN_RAID_INFO_V2
#endif

typedef struct _NDAS_LURN_RAID_INFO_V1 {
	UINT64 SectorInfo; // sector where DIB_V2 exists
	UINT32 OffsetFlagInfo; // dirty flag's byte offset in DIB_V2
	UINT64 SectorBitmapStart; // sector where bitmap starts
	UINT32 SectorsPerBit; // 2^7(~ 2^11)
	UINT64 SectorLastWrittenSector; // sector where to write last written sectors
} NDAS_LURN_RAID_INFO_V1, *PNDAS_LURN_RAID_INFO_V1;

#if !defined(LURDESC_NO_DOWNLEVEL_STRUCT)
#define INFO_RAID_LEGACY  NDAS_LURN_RAID_INFO_V1
#define PINFO_RAID_LEGACY PNDAS_LURN_RAID_INFO_V1
#endif

#include <poppack.h>


//////////////////////////////////////////////////////////////////////////
//
//	Block access control entry ( BACE )
//	NDAS bus save the binary of the structure in the registry.
//	Must kept packed in 1 byte. If not, it might cause compatibility issue.
//

typedef UINT64	BLOCKACE_ID, *PBLOCKACE_ID;

#define NBACE_ACCESS_READ		0x0001
#define NBACE_ACCESS_WRITE		0x0002

typedef struct _NDAS_BLOCK_ACE {
	BLOCKACE_ID	BlockAceId;	// Valid only when application gets BACLs from NDAS port driver.
	UCHAR		AccessMode;
	UCHAR		Reserved[7];
	UINT64		BlockStartAddr;
	UINT64		BlockEndAddr;
} NDAS_BLOCK_ACE, *PNDAS_BLOCK_ACE;

//////////////////////////////////////////////////////////////////////////
//
//	Block access control list ( BACL )
//	NDAS bus save the binary of the structure in the registry.
//	Must kept packed in 1 byte. If not, it might cause compatibility issue.
//

typedef struct _NDAS_BLOCK_ACL {
	UINT32			Length;
	UINT32			BlockACECnt;
	NDAS_BLOCK_ACE	BlockACEs[1];
} NDAS_BLOCK_ACL, *PNDAS_BLOCK_ACL;

//////////////////////////////////////////////////////////////////////////
//
//	Logical Unit Relation Node descriptor
//
//	NOTE: whenever adding new field,
//		add new value key to the NDAS BUS's enumeration registry.
//

//
//	LU relation node options.
//	These options override default setting.
//
#define	LURNOPTION_SET_RECONNECTION		0x00000001	// Override default reconnection setting.
#define	LURNOPTION_MISSING				0x00000002	// Unit is missing when being pluged in in NDAS service.
#define	LURNOPTION_RESTRICT_UDMA		0x00000004	// Set if UDMARestrict is vaild

//
//	LU relation node descriptor
//
typedef struct _LURELATION_NODE_DESC {
	UINT16					NextOffset;

	LURN_TYPE				LurnType;
	UINT32					LurnId;
	UINT64					StartBlockAddr;
	UINT64					EndBlockAddr;
	UINT64					UnitBlocks;
	UINT32					BytesInBlock;
	UINT32					MaxBlocksPerRequest;
	ACCESS_MASK				AccessRight;

	//
	//	Override default settings.
	//
	UINT32					LurnOptions;

	//
	//	Reconnection parameters
	//
	//	ReconnTrial: Number of reconnection trial
	//	ReconnInterval:	Reconnection interval in milliseconds.
	//
	//	NOTE: Valid with LURNOPTION_SET_RECONNECTION option.
	//
	UINT32					ReconnTrial;
	UINT32					ReconnInterval;


	//
	//	Restrict UDMA mode.
	//	Set 0xff to disable UDMA
	//

	UCHAR					UDMARestrict;
	UCHAR					Reserved[7];

	//
	//	LURN-specific extension
	//
	union {
		BYTE				InitData[LURNDESC_INITDATA_BUFFERLEN];
		LURNDESC_IDE		LurnIde;
		INFO_RAID			LurnInfoRAID;
	};

	UINT32					LurnParent;
	UINT32					LurnChildrenCnt;
	UINT32					LurnChildren[1];

} LURELATION_NODE_DESC, *PLURELATION_NODE_DESC;

//////////////////////////////////////////////////////////////////////////
//
//	Logical Unit Relation Descriptor
//
//	NOTE: whenever adding new field,
//		add new value key to the NDAS BUS's enumeration registry.
//

//
//	LU relation options.
//	These options override default setting.
//
//

#define	LUROPTION_ON_OOB_WRITE				0x00000001	// Turn On the OOB write to an NDAS device
#define	LUROPTION_OFF_OOB_WRITE				0x00000002	// Turn Off the OOB write to an NDAS device
#define	LUROPTION_ON_SIMULTANEOUS_WRITE		0x00000004	// Turn On the the simultaneous write to an NDAS device
#define	LUROPTION_OFF_SIMULTANEOUS_WRITE	0x00000008	// Turn Off the the simultaneous write to an NDAS device
#define	LUROPTION_ON_NDAS_2_0_WRITE_CHECK 	0x00000010	// Turn on the write-check for NDAS 2.0
#define	LUROPTION_OFF_NDAS_2_0_WRITE_CHECK	0x00000020	// Turn off the write-check for NDAS 2.0. Default on

#define LUROPTION_DONOT_ADJUST_PRIMSEC_ROLE	0x10000000

//
//	LU relation descriptor which bears node descriptor.
//
typedef struct _PLURELATION_DESC {

	UINT16					Length;	// includes LURN descriptors and BACL.
	UINT16					Type; // LSSTRUC_TYPE_LUR
	UINT16					DevType;
	UINT16					DevSubtype;
	UCHAR					LurId[LURID_LENGTH];
	UINT32					MaxBlocksPerRequest;
	UINT32					Reserved;
	NDAS_DEV_ACCESSMODE		DeviceMode;
	UINT32					LurOptions;
	UCHAR					Reserved1[4];

	//
	//	Offset to block access control list
	//	from the beginning of the structure
	//
	UINT32					BACLLength;
	UINT32					BACLOffset;

	//
	//	Disk ending block address
	//

	UINT64					EndingBlockAddr;

	//
	//	Content encryption.
	//	Key length in bytes = CntEcrKeyLength.
	//	If CntEcrMethod is zero, there is no content encryption.
	//	If key length is zero, there is no content encryption.
	//

	UCHAR					CntEcrMethod;
	UCHAR					CntEcrKeyLength;
	UCHAR					CntEcrKey[NDAS_CONTENTENCRYPT_KEY_LENGTH];

	UINT32					LurnDescCount;
	LURELATION_NODE_DESC	LurnDesc[1];	// Each node is also variable length

} LURELATION_DESC, *PLURELATION_DESC;


#define SIZE_OF_LURELATION_DESC() (sizeof(LURELATION_DESC) - sizeof(LURELATION_NODE_DESC))
#define SIZE_OF_LURELATION_NODE_DESC(NR_LEAVES) (sizeof(LURELATION_NODE_DESC) - sizeof(LONG) + sizeof(LONG) * (NR_LEAVES))


#endif

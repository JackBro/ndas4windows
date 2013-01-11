/*
	LanDisk Version 1.0


*/

#ifndef __LANDISK_H__
#define __LANDISK_H__


#define MAX_NR_UNIT		4
#define KEY_SIZE		9

#define ND_MSGPORT		0x7000
#define	ND_BASEPORT		0x7100
#define	ND_BPORT		0x8000
//#define LD_STATUS_PORT	0x9000
//#define LD_DATA_PORT	0xA000


#ifdef _WIN32
	#include <pshpack1.h>
#endif

////////////////////////////////////////
// LanDisk Packets.
//

#define MAJOR_VERSION		0x1
#define MINOR_VERSION		0x0

// Operations
#define	OP_AUTH				0x00	// 00000000
#define OP_END				0x01	// 00000001
#define OP_INIT_LANDISK		0x02	// 00000010
#define	OP_GET_DISKS		0x80	// 10000000
#define	OP_MOUNT			0x41	// 01000001
#define	OP_UNMOUNT			0x40	// 01000000
#define	OP_SET_PASSWORD		0xc1	// 11000001
#define	OP_SET_EXPORT_LIST	0x21	// 00100001
#define	OP_GET_EXPORT_LIST	0x20	// 00100000
#define	OP_READ				0xa0	// 10100000
#define	OP_WRITE			0xa1	// 10100001

// Operation Type.
#define TYPE_REQUEST		0x0		
#define TYPE_REPLY			0x1

// Status Codes.
#define LANDISK_ERROR_SUCCESS				0x00000000
#define LANDISK_ERROR_NOT_INITIALIZED		0x10000000
#define LANDISK_ERROR_BAD_KEY				0x10000001
#define LANDISK_ERROR_IN_USE				0x10000002
#define LANDISK_ERROR_NOT_OWENED			0x10000004
#define LANDISK_ERROR_VERSION_MISMATCH		0x10000008
#define LANDISK_ERROR_BAD_REQUEST			0x10000010
//	Additional error code for PlugIn()
#define LANDISK_ERROR_NOT_COMPAT_PRIMARY	0x10000020
#define LANDISK_ERROR_NO_MORE_WRITEACCESS	0x10000021

#define	LANDISK_ERROR						0x11111111

// UnitDisk Status Codes.
#define UNITDISK_STATUS_MASK			0x0000000F

#define UNITDISK_STATUS_DISABLE			0x00000000
#define UNITDISK_STATUS_ENABLE			0x00000001
#define UNITDISK_STATUS_NO_PERMISSION	0x00000002
#define UNITDISK_STATUS_IN_USE			0x00000004
// added by ILGU HONG 2004 10 14
#define UNITDISK_STATUS_DVD_REFRESH		0x00000008
// added by ILGU HONG 2004 10 14 end
// For SNMP Operation...
#define UNITDISK_STATUS_FORCED_BIT		0x00000080
#define	UNITDISK_STATUS_REMOTE_BIT		0x00000040	// added by @hootch@ July 23 2003

#define	UNITDISK_STATUS_ERRCODE_MASK	0xFF000000
#define	UNITDISK_STATUS_ERROR			0xF0000000
#define UNITDISK_STATUS_MASTER_FAILED	0xF1000000
#define UNITDISK_STATUS_NOT_INITIALIZED	0xF2000000
#define UNITDISK_STATUS_NOT_PRESENT		0xFF000000

// Disk Type for MIRROR. (UCHAR)
#define	UNITDISK_TYPE_SINGLE				0
#define UNITDISK_TYPE_MIRROR_MASTER			1
#define UNITDISK_TYPE_AGGREGATION_FIRST		2
#define	UNITDISK_TYPE_MIRROR_SLAVE			11
#define UNITDISK_TYPE_AGGREGATION_SECOND	21
#define UNITDISK_TYPE_AGGREGATION_THIRD		22
#define UNITDISK_TYPE_AGGREGATION_FOURTH	23
#define UNITDISK_TYPE_DVD					31
#define UNITDISK_TYPE_VDVD					32
#define UNITDISK_TYPE_MO					33
#define UNITDISK_TYPE_FLASHCARD				34
#define UNITDISK_TYPE_INVALID				0x80
#define UNITDISK_TYPE_BIND					0xc0

#define	UNITDISK_TYPE_CHANGE_USAGE_TYPE		0xFE
#define	UNITDISK_TYPE_CHANGE_ACCESSRIGHT	0xFF
//#define	UNITDISK_TYPE_BYPASS				0xFF

// Disk Access Right
#define	UNITDISK_ACCESS_READ			1
#define UNITDISK_ACCESS_WRITE			2

// NetDisk Status.
#define LANDISK_STATUS_DISABLE	0x00000000
#define LANDISK_STATUS_ENABLE	0x00000001
#define LANDISK_STATUS_REMOVED	0x00000002
#define LANDISK_STATUS_BAD_KEY	0x00000004
#define LANDISK_STATUS_HIGHVER	0x00000008

// LanDisk Header.
//
typedef struct _LANDISK_HEADER {
	UCHAR	MajorVersion;
	UCHAR	MinorVersion;
	UCHAR	Operation;	
	UCHAR	OperationType;
} LANDISK_HEADER, *PLANDISK_HEADER;

////////////////////////////////////////////
// AUTH Operation.
//

// Request packets.
typedef struct _AUTH_REQUEST_HEADER {
	UCHAR	Key[KEY_SIZE];
} AUTH_REQUEST_HEADER, *PAUTH_REQUEST_HEADER;

// LanDisk Reply.
//typedef struct _AUTH_REPLY_HEADER {
//} AUTH_REPLY_HEADER, *PAUTH_REPLY_HEADER;

#define AUTH_REQUEST_PACKET_SIZE	sizeof(LANDISK_HEADER) + sizeof(AUTH_REQUEST_HEADER)
#define AUTH_REPLY_PACKET_SIZE		sizeof(LANDISK_HEADER) + sizeof(ULONG) //+ sizeof(AUTH_REPLY_HEADER)

////////////////////////////////////////////
// END Operation.
//

// Request packets.
//typedef struct _END_REQUEST_HEADER {
//} END_REQUEST_HEADER, *PEND_REQUEST_HEADER;

// LanDisk Reply.
//typedef struct _END_REPLY_HEADER {
//} END_REPLY_HEADER, *PEND_REPLY_HEADER;

#define END_REQUEST_PACKET_SIZE	sizeof(LANDISK_HEADER) //+ sizeof(END_REQUEST_HEADER)
#define END_REPLY_PACKET_SIZE	sizeof(LANDISK_HEADER) + sizeof(ULONG) //+ sizeof(AUTH_REPLY_HEADER)

////////////////////////////////////////////
// INIT_LANDISK Operation.
//

// Request packet.
typedef struct _INIT_LANDISK_REQUEST_HEADER {
	ULONGLONG	Key[KEY_SIZE];	
} INIT_LANDISK_REQUEST_HEADER, *PINIT_LANDISK_REQUEST_HEADER;

// Reply packet.
//typedef struct _INIT_LANDISK_REPLY_HEADER {
//} INIT_LANDISK_REPLY_HEADER, *PINIT_LANDISK_REPLY_HEADER;

#define INIT_LANDISK_REQUEST_PACKET_SIZE	sizeof(LANDISK_HEADER) + sizeof(INIT_LANDISK_REQUEST_HEADER)
#define INIT_LANDISK_REPLY_PACKET_SIZE		sizeof(LANDISK_HEADER) 

////////////////////////////////////////////
// GET_DISKS Operation.
//

typedef struct _GET_DISKS_UNIT_DISK {
	ULONGLONG	DiskSize;
	ULONG		Status;
} GET_DISKS_UNIT_DISK, *PGET_DISKS_UNIT_DISK;

// Request packets.
//typedef struct _GET_DISKS_REQUEST_HEADER { 
//} GET_DISKS_REQUEST_HEADER, *PGET_DISKS_REQUEST_HEADER;

// LanDisk Reply.
typedef struct _GET_DISKS_REPLY_HEADER {
	UCHAR				NrDisks;
	UCHAR				Reserved[3];
	GET_DISKS_UNIT_DISK	UnitDisks[MAX_NR_UNIT];
} GET_DISKS_REPLY_HEADER, *PGET_DISKS_REPLY_HEADER;

#define GET_DISKS_REQUEST_PACKET_SIZE	sizeof(LANDISK_HEADER) //+ sizeof(GET_DISKS_REQUEST_HEADER)
#define GET_DISKS_REPLY_PACKET_SIZE		sizeof(LANDISK_HEADER) + sizeof(ULONG) + sizeof(GET_DISKS_REPLY_HEADER)

////////////////////////////////////////////
// MOUNT Operation.
//

// Request packets.
typedef struct _MOUNT_REQUEST_HEADER {
	UCHAR	UnitNumber;
	UCHAR	Reserved[3];
} MOUNT_REQUEST_HEADER, *PMOUNT_REQUEST_HEADER;

// LanDisk Reply.
typedef struct _MOUNT_REPLY_HEADER {
	UCHAR	UnitNumber;
	UCHAR	Reserved[3];
} MOUNT_REPLY_HEADER, *PMOUNT_REPLY_HEADER;

#define MOUNT_REQUEST_PACKET_SIZE	sizeof(LANDISK_HEADER) + sizeof(MOUNT_REQUEST_HEADER)
#define MOUNT_REPLY_PACKET_SIZE		sizeof(LANDISK_HEADER) + sizeof(ULONG) + sizeof(MOUNT_REPLY_HEADER)

////////////////////////////////////////////
// UNMOUNT Operation.
//

// Request packets.
typedef struct _UNMOUNT_REQUEST_HEADER { 
	UCHAR	UnitNumber;
	UCHAR	Reserved[3];
} UNMOUNT_REQUEST_HEADER, *PUNMOUNT_REQUEST_HEADER;

// LanDisk Reply.
typedef struct _UNMOUNT_REPLY_HEADER {
	UCHAR	UnitNumber;
	UCHAR	Reserved[3];
} UNMOUNT_REPLY_HEADER, *PUNMOUNT_REPLY_HEADER;

#define UNMOUNT_REQUEST_PACKET_SIZE	sizeof(LANDISK_HEADER) + sizeof(UNMOUNT_REQUEST_HEADER)
#define UNMOUNT_REPLY_PACKET_SIZE	sizeof(LANDISK_HEADER) + sizeof(ULONG) + sizeof(UNMOUNT_REPLY_HEADER)

////////////////////////////////////////////
// READ / WRITE operation ==> DATA packet
//

// Request packet header.
typedef struct _DATA_REQUEST_HEADER {
	ULONG		Handle;
	UCHAR		UnitNumber;
	UCHAR		Reserved[3];
	ULONGLONG	From;
	ULONG		Length;
} DATA_REQUEST_HEADER, *PDATA_REQUEST_HEADER;

// LanDisk Reply.
typedef struct _DATA_REPLY_HEADER {
	ULONG		Handle;
	UCHAR		UnitNumber;
	UCHAR		Reserved[3];
	ULONGLONG	From;
	ULONG		Length;
} DATA_REPLY_HEADER, *PDATA_REPLY_HEADER;

#define DATA_REQUEST_PACKET_SIZE	sizeof(LANDISK_HEADER) + sizeof(DATA_REQUEST_HEADER)
#define DATA_REPLY_PACKET_SIZE		sizeof(LANDISK_HEADER) + sizeof(ULONG) + sizeof(DATA_REPLY_HEADER)

////////////////////////////////
// All.
typedef struct _LANDISK_REQUEST {
	
	LANDISK_HEADER	Header;

	union{
		AUTH_REQUEST_HEADER			Auth;
		//END_REQUEST_HEADER		End;
		INIT_LANDISK_REQUEST_HEADER	InitLanDisk;
		//GET_DISKS_REQUEST_HEADER	GetDisks;
		MOUNT_REQUEST_HEADER		Mount;
		UNMOUNT_REQUEST_HEADER		Unmount;
		DATA_REQUEST_HEADER			Data;
	}mu;

} LANDISK_REQUEST, *PLANDISK_REQUEST;

typedef struct _LANDISK_REPLY {
	
	LANDISK_HEADER	Header;

	ULONG	Status;

	union{
		//AUTH_REPLY_HEADER			Auth;
		//END_REPLY_HEADER			End;
		//INIT_LANDISK_REPLY_HEADER	InitLanDisk;
		GET_DISKS_REPLY_HEADER		GetDisks;
		MOUNT_REPLY_HEADER			Mount;
		UNMOUNT_REPLY_HEADER		Unmount;
		DATA_REPLY_HEADER			Data;
	}mu;

} LANDISK_REPLY, *PLANDISK_REPLY;

//
//	disk information format
//
#include <pshpack1.h>

//
// cslee:
//
// Disk Information Block should be aligned to 512-byte (1 sector size)
//
// Checksum has not been used yet, so we can safely fix the size of
// the structure at this time without increasing the version of the block structure
//
// Note when you change members of the structure, you should increase the
// version of the structure and you SHOULD provide the migration
// path from the previous version.
// 

#define DISK_INFORMATION_USAGE_TYPE_HOME	0x00
#define DISK_INFORMATION_USAGE_TYPE_OFFICE	0x10

#define DISK_INFORMATION_VERSION_MAJOR 0
#define DISK_INFORMATION_VERSION_MINOR 1

typedef struct _DISK_INFORMATION_BLOCK {
#define	DISK_INFORMATION_SIGNATURE		0xFE037A4E
	unsigned _int32	Signature;		// 4 (4)
	
	unsigned _int8	MajorVersion;	// 1 (5)
	unsigned _int8	MinorVersion;	// 1 (6)
	unsigned _int8	reserved1[2];	// 1 * 2 (8)

	unsigned _int32	Sequence;		// 4 (12)

	unsigned _int8	EtherAddress[6];	// 1 * 6 (18)
	unsigned _int8	UnitNumber;		// 1 (19)
	unsigned _int8	reserved2;		// 1 (20)

	unsigned _int8	DiskType;		// 1 (21)
	unsigned _int8	PeerAddress[6];	// 1 * 6 (27)
	unsigned _int8	PeerUnitNumber; // 1 (28)
	unsigned _int8	reserved3;		// 1 (29)

	unsigned _int8	UsageType;		// 1 (30)
	unsigned _int8	reserved4[3];	// 1 * 3 (33)

	unsigned char reserved5[512 - 37]; // should be 512 - ( 33 + 4 )

	unsigned _int32	Checksum;		// 4 (512)

} DISK_INFORMATION_BLOCK, *PDISK_INFORMATION_BLOCK;

#define MAX_UNIT_DISK_SUPPORT 128 // There is no limit in structure.

typedef struct _UNIT_DISK_LOCATION
{
	unsigned _int8 MACAddr[6];
	unsigned _int8 SlotNumber;
	unsigned _int8 reserved;
} UNIT_DISK_LOCATION, *PUNIT_DISK_LOCATION;

#define MAX_UNITS_IN_V2 32
#define MAX_UNITS_IN_SECTOR 64

typedef struct _DISK_INFORMATION_BLOCK_V2 {
	union{
		struct {
#define	DISK_INFORMATION_SIGNATURE_V2	0x3F404A9FF4495F9F
			unsigned _int64	Signature;
			
#define CURRENT_MAJOR_VERSION_V2	1
			unsigned _int32	MajorVersion;
#define CURRENT_MINOR_VERSION_V2	0
			unsigned _int32	MinorVersion;

			// sizeXArea + sizeLogicalSpace <= sizeTotalDisk
			unsigned _int64	sizeXArea; // in sectors

			unsigned _int64	sizeUserSpace; // in sectors

			unsigned _int32	iSectorsPerBit; // dirty bitmap . default : 128
#define NMT_SINGLE			1		// unbound
#define NMT_MIRROR			2		// 2 disks without repair information. need to be converted to RAID_01
#define NMT_AGGREGATE		3		// 2 or more
#define NMT_RAID_01			4		// with repair
#define NMT_VDVD			100		// virtual DVD
#define NMT_CDROM			101		// packet device, CD / DVD
#define NMT_OPMEM			102		// packet device, Magnetic Optical
#define NMT_FLASH			103		// block device, flash card
			unsigned _int32	iMediaType; // NDAS Media Type

			unsigned _int32	nDiskCount; // 1 ~ . physical disk units
			unsigned _int32	iSequence; // 0 ~

#define NDAS_DIRTY_MIRROR_DIRTY 0x00000001
#define NDAS_DIRTY_MIRROR_DEAD	0x00000002 // run time flag, so ignore data in disk
			unsigned _int32	FlagDirty;
			unsigned _int8	reserved2[4];
		};
		unsigned char bytes_256[256];
	};

	UNIT_DISK_LOCATION	UnitDisks[MAX_UNITS_IN_V2]; // 256 bytes
} DISK_INFORMATION_BLOCK_V2, *PDISK_INFORMATION_BLOCK_V2;

#define IS_WRONG_VERSION(DIBV1) ((0 != (DIBV1).MajorVersion) || (0 == (DIBV1).MajorVersion && 1 != (DIBV1).MinorVersion))
#define IS_HIGHER_VERSION_V2(DIBV2) ((CURRENT_MAJOR_VERSION_V2 < (DIBV2).MajorVersion) || (CURRENT_MAJOR_VERSION_V2 == (DIBV2).MajorVersion && CURRENT_MINOR_VERSION_V2 < (DIBV2).MinorVersion))
#define GET_TRAIL_SECTOR_COUNT_V2(DISK_COUNT) (((DISK_COUNT) > MAX_UNITS_IN_V2) ? (((DISK_COUNT) - MAX_UNITS_IN_V2) / MAX_UNITS_IN_SECTOR) +1 : 0)

#include <poppack.h>


#ifdef _WIN32
	#include <poppack.h>
#endif

#endif
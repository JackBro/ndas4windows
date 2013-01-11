/*++

Copyright (C)2002-2005 XIMETA, Inc.
All rights reserved.

--*/

#ifndef _SOCKET_LPX_H_
#define _SOCKET_LPX_H_

#define HTONS(Data)		((((Data)&0x00FF) << 8) | (((Data)&0xFF00) >> 8))
#define NTOHS(Data)		(USHORT)((((Data)&0x00FF) << 8) | (((Data)&0xFF00) >> 8))

#define HTONL(Data)		( (((Data)&0x000000FF) << 24) | (((Data)&0x0000FF00) << 8) \
						| (((Data)&0x00FF0000)  >> 8) | (((Data)&0xFF000000) >> 24))
#define NTOHL(Data)		( (((Data)&0x000000FF) << 24) | (((Data)&0x0000FF00) << 8) \
						| (((Data)&0x00FF0000)  >> 8) | (((Data)&0xFF000000) >> 24))

#define HTONLL(Data)	( (((Data)&0x00000000000000FF) << 56) | (((Data)&0x000000000000FF00) << 40) \
						| (((Data)&0x0000000000FF0000) << 24) | (((Data)&0x00000000FF000000) << 8)  \
						| (((Data)&0x000000FF00000000) >> 8)  | (((Data)&0x0000FF0000000000) >> 24) \
						| (((Data)&0x00FF000000000000) >> 40) | (((Data)&0xFF00000000000000) >> 56))

#define NTOHLL(Data)	( (((Data)&0x00000000000000FF) << 56) | (((Data)&0x000000000000FF00) << 40) \
						| (((Data)&0x0000000000FF0000) << 24) | (((Data)&0x00000000FF000000) << 8)  \
						| (((Data)&0x000000FF00000000) >> 8)  | (((Data)&0x0000FF0000000000) >> 24) \
						| (((Data)&0x00FF000000000000) >> 40) | (((Data)&0xFF00000000000000) >> 56))

#define LPX_BOUND_DEVICE_NAME_PREFIX	L"\\Device\\Lpx"
#define SOCKETLPX_DEVICE_NAME			L"\\Device\\SocketLpx"
#define SOCKETLPX_DOSDEVICE_NAME		L"\\DosDevices\\SocketLpx"

#define FSCTL_LPX_BASE     FILE_DEVICE_NETWORK

#define _LPX_CTL_CODE(function, method, access) \
            CTL_CODE(FSCTL_LPX_BASE, function, method, access)

#define IOCTL_LPX_QUERY_ADDRESS_LIST  \
            _LPX_CTL_CODE(0, METHOD_NEITHER, FILE_ANY_ACCESS)

#define IOCTL_LPX_SET_INFORMATION_EX  \
            _LPX_CTL_CODE(1, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_LPX_GET_DROP_RATE \
            _LPX_CTL_CODE(100, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_LPX_SET_DROP_RATE \
            _LPX_CTL_CODE(101, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_LPX_GET_VERSION  \
            _LPX_CTL_CODE(0x201, METHOD_NEITHER, FILE_ANY_ACCESS)


typedef struct _LPXDRV_VER {
	USHORT						VersionMajor;
	USHORT						VersionMinor;
	USHORT						VersionBuild;
	USHORT						VersionPrivate;
	UCHAR						Reserved[16];
} LPXDRV_VER, *PLPXDRV_VER;


#define AF_LPX	AF_UNSPEC

#define LPXPROTO_STREAM 214
#define LPXPROTO_DGRAM 215

#define IPPROTO_LPXTCP 214
#define IPPROTO_LPXUDP 215

typedef UNALIGNED struct _TDI_ADDRESS_LPX {
	USHORT	Port;
	UCHAR	Node[6];
	UCHAR	Reserved[10];	// To make the same size as NetBios
} TDI_ADDRESS_LPX, *PTDI_ADDRESS_LPX;


typedef struct _TA_ADDRESS_LPX {

    LONG  TAAddressCount;
    struct  _AddrLpx {
        USHORT				AddressLength;
        USHORT				AddressType;
        TDI_ADDRESS_LPX		Address[1];
    } Address [1];

} TA_LPX_ADDRESS, *PTA_LPX_ADDRESS;


#define LPXADDR_NODE_LENGTH	6

#define TDI_ADDRESS_TYPE_LPX TDI_ADDRESS_TYPE_UNSPEC
#define TDI_ADDRESS_LENGTH_LPX sizeof (TDI_ADDRESS_LPX)

#define LSTRANS_COMPARE_LPXADDRESS(PTDI_LPXADDRESS1, PTDI_LPXADDRESS2) (		\
					RtlCompareMemory( (PLPX_ADDRESS1), (PLPX_ADDRESS2),			\
					sizeof(TDI_ADDRESS_LPX)) == sizeof(TDI_ADDRESS_LPX)	)

#define LPX_ADDRESS  TDI_ADDRESS_LPX	
#define PLPX_ADDRESS PTDI_ADDRESS_LPX 

typedef UNALIGNED struct _SOCKADDR_LPX {
    SHORT					sin_family;
    LPX_ADDRESS				LpxAddress;

} SOCKADDR_LPX, *PSOCKADDR_LPX;


#define MAX_SOCKETLPX_INTERFACE		8

typedef struct _SOCKETLPX_ADDRESS_LIST {
    LONG			iAddressCount;
    SOCKADDR_LPX	SocketLpx[MAX_SOCKETLPX_INTERFACE];
} SOCKETLPX_ADDRESS_LIST, *PSOCKETLPX_ADDRESS_LIST;

//
// LPX uses DriverContext[0] and [1]  to store expiration time.
// 
//	added by hootch 02092004
//
#define GET_IRP_EXPTIME(PIRP)			(((PLARGE_INTEGER)(PIRP)->Tail.Overlay.DriverContext)->QuadPart)
#define SET_IRP_EXPTIME(PIRP, EXPTIME)	(((PLARGE_INTEGER)(PIRP)->Tail.Overlay.DriverContext)->QuadPart = (EXPTIME))

//
// LPX uses DriverContext[2] for setting protocol specific statistics per operation.
// Alway keep the same format with LS_TRANS_STAT in transport.h
//
typedef struct _TRANS_STAT {
    ULONG Retransmits;
    ULONG PacketLoss;
} TRANS_STAT, *PTRANS_STAT;

#ifdef _WIN64
// On x64, only DriverContext[0] is used for expiration time.
// And DriverContext[2] is used by someone who is not identified yet.(To do..)
// So use DriverContext[1] for time being.
#define DRIVER_CONTEXT_FOR_TRANS_STAT 1
#else
// LPX uses DriverContext[0] and [1]  to store expiration time.
// DriverContext[3] is used by someone else. To do: Find who is using this value.
#define DRIVER_CONTEXT_FOR_TRANS_STAT 2
#endif

#define INC_IRP_RETRANSMITS(PIRP, VAL)  \
    if ((PIRP)->Tail.Overlay.DriverContext[DRIVER_CONTEXT_FOR_TRANS_STAT]) \
        InterlockedExchangeAdd(&(((PTRANS_STAT)((PIRP)->Tail.Overlay.DriverContext[DRIVER_CONTEXT_FOR_TRANS_STAT]))->Retransmits), VAL)
#define GET_IRP_RETRANSMITS(PIRP) ((PIRP)->Tail.Overlay.DriverContext[DRIVER_CONTEXT_FOR_TRANS_STAT])?\
    ((PTRANS_STAT)((PIRP)->Tail.Overlay.DriverContext[DRIVER_CONTEXT_FOR_TRANS_STAT]))->Retransmits:0


#define INC_IRP_PACKET_LOSS(PIRP, VAL)  \
    if ((PIRP)->Tail.Overlay.DriverContext[DRIVER_CONTEXT_FOR_TRANS_STAT]) \
        InterlockedExchangeAdd(&(((PTRANS_STAT)((PIRP)->Tail.Overlay.DriverContext[DRIVER_CONTEXT_FOR_TRANS_STAT]))->PacketLoss), VAL)
#define GET_IRP_PACKET_LOSS(PIRP) ((PIRP)->Tail.Overlay.DriverContext[DRIVER_CONTEXT_FOR_TRANS_STAT])?\
    ((PTRANS_STAT)((PIRP)->Tail.Overlay.DriverContext[DRIVER_CONTEXT_FOR_TRANS_STAT]))->PacketLoss:0

#define IRP_TRANS_STAT(PIRP)  ((PTRANS_STAT)((PIRP)->Tail.Overlay.DriverContext[DRIVER_CONTEXT_FOR_TRANS_STAT]))

//
//	LPX Reserved Port Numbers
//	added by hootch 03022004
//
#define		LPXRP_LFS_PRIMARY		((USHORT)0x0001)
#define		LPXRP_LFS_CALLBACK		((USHORT)0x0002)
#define		LPXRP_LFS_DATAGRAM		((USHORT)0x0003)
#define		LPXRP_LSHELPER_INFOEX	((USHORT)0x0011)


#if 1
	
// Packet Drop Rate.
extern ULONG ulPacketDropRate;

#endif


#endif

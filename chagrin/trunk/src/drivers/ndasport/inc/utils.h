#pragma once
#include <ntddk.h>
#include <tdi.h>
#include <tdikrnl.h>

#ifdef __BUILDMACHINE__
#define STR2(x) #x
#define STR(x) STR2(x)
#define __BUILDMACHINE_STR__ STR(__BUILDMACHINE__)
#else
#define __BUILDMACHINE_STR__ "(none)"
#endif

#ifdef countof
#undef countof
#endif
#define countof(x) (sizeof(x) / sizeof((x)[0]))
#define min(a,b)    (((a) < (b)) ? (a) : (b))

#ifndef ExFreePoolWithTag
#define ExFreePoolWithTag(POINTER, TAG) ExFreePool(POINTER)
#endif

#if 0
#define ClearFlag(Flags, Bit) ((*(Flags)) &= ~(Bit));
#define SetFlag(Flags, Bit)   ((*(Flags)) |= (Bit));
#define TestFlag(Flags, Bit)  ((Flags)&(Bit))
#else
VOID FORCEINLINE ClearFlag(ULONG* Flags, ULONG Bit) { *Flags &= ~Bit; }
VOID FORCEINLINE SetFlag(ULONG* Flags, ULONG Bit) { *Flags |= Bit; }
BOOLEAN FORCEINLINE TestFlag(ULONG Flags, ULONG Bit) { return (Flags & Bit) != 0; }
#endif

#ifdef NDASPORT_IMP_USE_PRIVATE_BYTESWAP

SHORT FORCEINLINE ntohs(SHORT x)
{
	return ((x & 0xff) << 8) || ((x >> 8) & 0xff);
}

LONG FORCEINLINE ntohl(LONG x)
{
	return ((((x >>  0) & 0xFF) << 24) |  
		(((x >>  8) & 0xFF) << 16) | 
		(((x >> 16) & 0xFF) <<  8) | 
		(((x >> 24) & 0xFF) <<  0));
}

LONG FORCEINLINE htonl(LONG x)
{
	return (((x >> 24) & 0x000000FFL) |
		((x >>  8) & 0x0000FF00L) |
		((x <<  8) & 0x00FF0000L) |
		((x << 24) & 0xFF000000L));
}

SHORT FORCEINLINE htons(SHORT x)
{
	return (((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8));
}

#else

#define ntohl RtlUlongByteSwap
#define htonl RtlUlongByteSwap
#define ntohs RtlUshortByteSwap
#define htons RtlUshortByteSwap
#define ntohll RtlUlonglongByteSwap
#define htonll RtlUlonglongByteSwap

#endif

/*++

Routine Description:

This routine translates an srb status into an ntstatus.

Arguments:

Srb - Supplies a pointer to the failing Srb.

Return Value:

An nt status approprate for the error.

--*/
FORCEINLINE
NTSTATUS
NpTranslateScsiStatus(
    IN PSCSI_REQUEST_BLOCK Srb)
{
    switch (SRB_STATUS(Srb->SrbStatus)) 
	{
	case SRB_STATUS_PENDING: return SRB_STATUS_PENDING;
	case SRB_STATUS_INTERNAL_ERROR: return Srb->InternalStatus;
	case SRB_STATUS_SUCCESS:              return STATUS_SUCCESS;
	//case SRB_STATUS_PENDING:              return STATUS_PENDING;
    case SRB_STATUS_INVALID_LUN:
    case SRB_STATUS_INVALID_TARGET_ID: 
	case SRB_STATUS_NO_DEVICE:
    case SRB_STATUS_NO_HBA:		          return STATUS_DEVICE_DOES_NOT_EXIST;
	case SRB_STATUS_COMMAND_TIMEOUT:
    case SRB_STATUS_TIMEOUT:	          return STATUS_IO_TIMEOUT;
    case SRB_STATUS_SELECTION_TIMEOUT:    return STATUS_DEVICE_NOT_CONNECTED;
	case SRB_STATUS_BAD_FUNCTION:
    case SRB_STATUS_BAD_SRB_BLOCK_LENGTH: return STATUS_INVALID_DEVICE_REQUEST;
	case SRB_STATUS_DATA_OVERRUN:         return STATUS_BUFFER_OVERFLOW;
	default:                              return STATUS_IO_DEVICE_ERROR;
    }
}

//
// MACRO to delay the thread execution. The parameter
// gives the number of seconds to wait
//
#define DelayThreadExecution(x) {                       \
                                                        \
             LARGE_INTEGER delayTime;                   \
             delayTime.QuadPart = -10000000L * x;       \
             KeDelayExecutionThread( KernelMode,        \
                                     FALSE,             \
                                     &delayTime);       \
        }
                           
BOOLEAN
FORCEINLINE
IsEqual4Byte(CONST VOID* Source1, CONST VOID* Source2)
{
	return (BOOLEAN)(4 == RtlCompareMemory(Source1, Source2, 4));
}

BOOLEAN
FORCEINLINE
IsEqual2Byte(CONST VOID* Source1, CONST VOID* Source2)
{
	return (BOOLEAN)(2 == RtlCompareMemory(Source1, Source2, 2));
}

VOID
FORCEINLINE
Copy4Bytes(VOID UNALIGNED* Destination, CONST VOID UNALIGNED* Source)
{
	RtlCopyBytes(Destination, Source, 4);
}

VOID 
FORCEINLINE 
GetUlongFromArray(const UCHAR* UCharArray, PULONG ULongValue)
{
	*ULongValue = 0;
	*ULongValue |= UCharArray[0];
	*ULongValue <<= 8;
	*ULongValue |= UCharArray[1];
	*ULongValue <<= 8;
	*ULongValue |= UCharArray[2];
	*ULongValue <<= 8;
	*ULongValue |= UCharArray[3];
}

VOID 
FORCEINLINE 
SetUlongInArray(UCHAR* UCharArray, ULONG ULongValue)
{
	UCharArray[0] = (UCHAR)((ULongValue & 0xFF000000) >> 24); 
	UCharArray[1] = (UCHAR)((ULongValue & 0x00FF0000) >> 16);
	UCharArray[2] = (UCHAR)((ULongValue & 0x0000FF00) >> 8);
	UCharArray[3] = (UCHAR)(ULongValue & 0x000000FF);
}


VOID
FORCEINLINE
SetRelativeDueTimeInSecond(
	__inout PLARGE_INTEGER DueTime,
	__in ULONG Seconds)
{
	DueTime->QuadPart = -(10000000 * (LONGLONG) Seconds);
}

VOID
FORCEINLINE
SetRelativeDueTimeInMillisecond(
	__inout PLARGE_INTEGER DueTime,
	__in ULONG Milliseconds)
{
	DueTime->QuadPart = -(10000 * (LONGLONG) Milliseconds);
}

VOID
FORCEINLINE
SetRelativeDueTimeInMicrosecond(
	__inout PLARGE_INTEGER DueTime,
	__in ULONG Microseconds)
{
	DueTime->QuadPart = -(10 * (LONGLONG) Microseconds);
}

BOOLEAN
FORCEINLINE
IsDispatchObjectSignaled(
	__in PVOID Object)
{
	static LARGE_INTEGER ZeroTimeout = {0};
	NTSTATUS status;

	status = KeWaitForSingleObject(
		Object, 
		Executive,
		KernelMode,
		FALSE,
		&ZeroTimeout);

	return (BOOLEAN)(STATUS_SUCCESS == status);
}

/*
#define WaitForOneSecond() {                                            \
                                                                        \
                             LARGE_INTEGER delayTime;                   \
                             delayTime.QuadPart = -10000000L;           \
                             KeDelayExecutionThread( KernelMode,        \
                                                     FALSE,             \
                                                     &delayTime);       \
                           }  
                             
#define FILLUP_INQUIRYDATA(InquiryData)                  \
         InquiryData.DeviceType = 0;                     \
         InquiryData.DeviceTypeQualifier = 0;            \
         InquiryData.DeviceTypeModifier = 0;             \
         InquiryData.RemovableMedia = 0;                 \
         InquiryData.Versions = 0;                       \
         InquiryData.ResponseDataFormat = 2;             \
         RtlCopyMemory(InquiryData.VendorId,             \
                       "Seagate", 7);                    \
         RtlCopyMemory(InquiryData.ProductId,            \
                       "iSCSI Disk", 10);                \
         RtlCopyMemory(InquiryData.ProductRevisionLevel, \
                       "1.0", 3);                        

*/

BOOLEAN
FORCEINLINE
IsCleanupRequest(
	PIO_STACK_LOCATION irpStack)
{
	return (irpStack->MajorFunction == IRP_MJ_CLOSE) || 
		(irpStack->MajorFunction == IRP_MJ_CLEANUP) ||
		(irpStack->MajorFunction == IRP_MJ_SHUTDOWN) ||
		((irpStack->MajorFunction == IRP_MJ_SCSI) && 
		((irpStack->Parameters.Scsi.Srb->Function == SRB_FUNCTION_RELEASE_DEVICE) ||
			(irpStack->Parameters.Scsi.Srb->Function == SRB_FUNCTION_FLUSH_QUEUE) ||
			(TestFlag(irpStack->Parameters.Scsi.Srb->SrbFlags, SRB_FLAGS_BYPASS_FROZEN_QUEUE |
			SRB_FLAGS_BYPASS_LOCKED_QUEUE))));
}

#define GetLowWord(l)  ((USHORT)((ULONG_PTR)(l) & 0xffff))
#define GetHighWord(l) ((USHORT)((ULONG_PTR)(l) >> 16))
#define GetLowByte(w)  ((UCHAR)((ULONG_PTR)(w) & 0xff))
#define GetHighByte(w) ((UCHAR)((ULONG_PTR)(w) >> 8))


ULONG
FORCEINLINE
CDB10_LOGICAL_BLOCK_BYTE(PCDB Cdb)
{
	return 
		((ULONG)(Cdb->CDB10.LogicalBlockByte0) << (8 * 3)) +
		((ULONG)(Cdb->CDB10.LogicalBlockByte1) << (8 * 2)) +
		((ULONG)(Cdb->CDB10.LogicalBlockByte2) << (8 * 1)) +
		((ULONG)(Cdb->CDB10.LogicalBlockByte3) << (8 * 0));
}

VOID 
FORCEINLINE
CDB10_LOGICAL_BLOCK_BYTE_TO_BYTES(PCDB Cdb, ULONG LogicalBlockAddress)
{
	Cdb->CDB10.LogicalBlockByte0 = ((PFOUR_BYTE)&(LogicalBlockAddress))->Byte3;
	Cdb->CDB10.LogicalBlockByte1 = ((PFOUR_BYTE)&(LogicalBlockAddress))->Byte2;
	Cdb->CDB10.LogicalBlockByte2 = ((PFOUR_BYTE)&(LogicalBlockAddress))->Byte1;
	Cdb->CDB10.LogicalBlockByte3 = ((PFOUR_BYTE)&(LogicalBlockAddress))->Byte0;
}

ULONG
FORCEINLINE
CDB10_TRANSFER_BLOCKS(PCDB Cdb)
{
	return
		((UINT32)(Cdb->CDB10.TransferBlocksMsb) << (8 * 1)) +
		((UINT32)(Cdb->CDB10.TransferBlocksLsb) << (8 * 0));
}

VOID
FORCEINLINE
CDB10_TRANSFER_BLOCKS_TO_BYTES(PCDB Cdb, USHORT TransferBlocks)
{
	Cdb->CDB10.TransferBlocksMsb = ((PTWO_BYTE)&(TransferBlocks))->Byte1;
	Cdb->CDB10.TransferBlocksLsb = ((PTWO_BYTE)&(TransferBlocks))->Byte0;
}

//VOID
//FORCEINLINE
//NdasPortReverseBytes(
//	PFOUR_BYTE Destination,
//	CONST FOUR_BYTE* Source)
//{
//	PFOUR_BYTE d = (PFOUR_BYTE)(Destination);
//	PFOUR_BYTE s = (PFOUR_BYTE)(Source);
//	d->Byte3 = s->Byte0;
//	d->Byte2 = s->Byte1;
//	d->Byte1 = s->Byte2;
//	d->Byte0 = s->Byte3;
//}
//

//LONG64
//FORCEINLINE
//NdasPortGetLogicalBlockAddressCDB16(PCDB Cdb2)
//{
//	PCDBEXT Cdb = (PCDBEXT) Cdb2;
//	return 
//		((LONG64)Cdb->CDB16.LogicalBlock[0] << (8 * 7)) +
//		((LONG64)Cdb->CDB16.LogicalBlock[1] << (8 * 6)) +
//		((LONG64)Cdb->CDB16.LogicalBlock[2] << (8 * 5)) +
//		((LONG64)Cdb->CDB16.LogicalBlock[3] << (8 * 4)) +
//		((LONG64)Cdb->CDB16.LogicalBlock[4] << (8 * 3)) +
//		((LONG64)Cdb->CDB16.LogicalBlock[5] << (8 * 2)) +
//		((LONG64)Cdb->CDB16.LogicalBlock[6] << (8 * 1)) +
//		((LONG64)Cdb->CDB16.LogicalBlock[7] << (8 * 0));
//}
//
//LONG64
//FORCEINLINE
//NdasPortGetLogicalBlockAddressCDB10(PCDB Cdb)
//{
//	return 
//		((LONG64)Cdb->CDB10.LogicalBlockByte0 << (8 * 3)) +
//		((LONG64)Cdb->CDB10.LogicalBlockByte1 << (8 * 2)) +
//		((LONG64)Cdb->CDB10.LogicalBlockByte2 << (8 * 1)) +
//		((LONG64)Cdb->CDB10.LogicalBlockByte3 << (8 * 0));
//}
//
//LONG64
//FORCEINLINE
//NdasPortGetLogicalBlockAddressCDB6(PCDB Cdb)
//{
//	return 
//		((LONG64)Cdb->CDB6READWRITE.LogicalBlockMsb0 << (8 * 2)) +
//		((LONG64)Cdb->CDB6READWRITE.LogicalBlockMsb1 << (8 * 1)) +
//		((LONG64)Cdb->CDB6READWRITE.LogicalBlockLsb << (8 * 0));
//}
//
//ULONG
//FORCEINLINE
//NdasPortGetTransferBlockCountCDB6(PCDB Cdb)
//{
//	return
//		(ULONG)(Cdb->CDB6READWRITE.TransferBlocks << (8 * 0));
//}
//
//ULONG
//FORCEINLINE
//NdasPortGetTransferBlockCountCDB10(PCDB Cdb)
//{
//	return 
//		(ULONG)(Cdb->CDB10.TransferBlocksMsb << (8 * 1)) +
//		(ULONG)(Cdb->CDB10.TransferBlocksLsb << (8 * 0));
//}
//
//ULONG
//FORCEINLINE
//NdasPortGetTransferBlockCountCDB16(PCDB Cdb2)
//{
//	PCDBEXT Cdb = (PCDBEXT) Cdb2;
//	return
//		(ULONG)(Cdb->CDB16.TransferLength[0] << (8 * 3)) +
//		(ULONG)(Cdb->CDB16.TransferLength[1] << (8 * 2)) +
//		(ULONG)(Cdb->CDB16.TransferLength[2] << (8 * 1)) +
//		(ULONG)(Cdb->CDB16.TransferLength[3] << (8 * 0));
//}

#ifdef DebugPrint
#undef DebugPrint
#endif

#if DBG
void
NdasPortDebugPrint(
    ULONG DebugPrintLevel,
    PCCHAR DebugMessage,
    ...);

#define DebugPrint(x) NdasPortDebugPrint x
#else
#define DebugPrint(x) __noop()
#endif

//#if DBG

CONST CHAR* DbgWmiMinorFunctionStringA(UCHAR MinorFunction);
CONST CHAR* DbgPnPMinorFunctionStringA(__in UCHAR MinorFunction);

CONST CHAR* DbgDeviceRelationStringA(__in DEVICE_RELATION_TYPE Type);
CONST CHAR* DbgDeviceIDStringA(__in BUS_QUERY_ID_TYPE Type);
CONST CHAR* DbgDeviceTextTypeStringA(__in DEVICE_TEXT_TYPE Type);

CONST CHAR* DbgSrbFunctionStringA(__in UCHAR SrbFunction);
CONST CHAR* DbgScsiOpStringA(__in UCHAR ScsiOpCode);
CONST CHAR* DbgSrbStatusStringA(UCHAR SrbStatus);
CONST CHAR* DbgIoControlCodeStringA(ULONG IoControlCode);
CONST CHAR* DbgScsiMiniportIoControlCodeString(ULONG IoControlCode);

CONST CHAR* DbgStorageQueryTypeStringA(__in STORAGE_QUERY_TYPE Type);
CONST CHAR* DbgStoragePropertyIdStringA(__in STORAGE_PROPERTY_ID Id);

CONST CHAR* DbgPowerMinorFunctionString(__in UCHAR MinorFunction);
CONST CHAR* DbgSystemPowerString(__in SYSTEM_POWER_STATE Type);
CONST CHAR* DbgDevicePowerString(__in DEVICE_POWER_STATE Type);  

CONST CHAR* DbgTdiPnpOpCodeString(__in TDI_PNP_OPCODE OpCode);

CONST CHAR* AtaMinorVersionNumberString(__in USHORT MinorVersionNumber);

VOID DbgCheckScsiReturn(__in PIRP Irp, __in PSCSI_REQUEST_BLOCK Srb);

#if DBG
#define DBG_CHECK_SCSI_RETURN(Irp, Srb) DbgCheckScsiReturn(Irp, Srb)
#else
#define DBG_CHECK_SCSI_RETURN(Irp, Srb) 
#endif


#if _WIN32_WINNT <= 0x0500
//
// Supplementary Interlocked Routines
//

#if defined(_M_IX86) && !defined(RC_INVOKED) && !defined(MIDL_PASS)

#ifdef __cplusplus
extern "C" {
#endif

#if (_MSC_FULL_VER >= 14000101)

//
// Define bit test intrinsics.
//

#define BitTest _bittest
#define BitTestAndComplement _bittestandcomplement
#define BitTestAndSet _bittestandset
#define BitTestAndReset _bittestandreset
#define InterlockedBitTestAndSet _interlockedbittestandset
#define InterlockedBitTestAndReset _interlockedbittestandreset

BOOLEAN
_bittest (
    IN LONG const *Base,
    IN LONG Offset
    );

BOOLEAN
_bittestandcomplement (
    IN LONG *Base,
    IN LONG Offset
    );

BOOLEAN
_bittestandset (
    IN LONG *Base,
    IN LONG Offset
    );

BOOLEAN
_bittestandreset (
    IN LONG *Base,
    IN LONG Offset
    );

BOOLEAN
_interlockedbittestandset (
    IN LONG *Base,
    IN LONG Offset
    );

BOOLEAN
_interlockedbittestandreset (
    IN LONG *Base,
    IN LONG Offset
    );

#pragma intrinsic(_bittest)
#pragma intrinsic(_bittestandcomplement)
#pragma intrinsic(_bittestandset)
#pragma intrinsic(_bittestandreset)
#pragma intrinsic(_interlockedbittestandset)
#pragma intrinsic(_interlockedbittestandreset)

//
// Define bit scan intrinsics.
//

#define BitScanForward _BitScanForward
#define BitScanReverse _BitScanReverse

BOOLEAN
_BitScanForward (
    OUT ULONG *Index,
    IN ULONG Mask
    );

BOOLEAN
_BitScanReverse (
    OUT ULONG *Index,
    IN ULONG Mask
    );

#pragma intrinsic(_BitScanForward)
#pragma intrinsic(_BitScanReverse)

#else

#pragma warning(push)
#pragma warning(disable:4035 4793)

BOOLEAN
FORCEINLINE
InterlockedBitTestAndSet (
    IN LONG *Base,
    IN LONG Bit
    )
{
    __asm {
           mov eax, Bit
           mov ecx, Base
           lock bts [ecx], eax
           setc al
    };
}

BOOLEAN
FORCEINLINE
InterlockedBitTestAndReset (
    IN LONG *Base,
    IN LONG Bit
    )
{
    __asm {
           mov eax, Bit
           mov ecx, Base
           lock btr [ecx], eax
           setc al
    };
}
#pragma warning(pop)

#endif	/* _MSC_FULL_VER >= 14000101 */

#if !defined(_M_CEE_PURE)
#pragma warning(push)
#pragma warning(disable:4035 4793)

//BOOLEAN
//FORCEINLINE
//InterlockedBitTestAndComplement (
//    IN LONG *Base,
//    IN LONG Bit
//    )
//{
//    __asm {
//           mov eax, Bit
//           mov ecx, Base
//           lock btc [ecx], eax
//           setc al
//    };
//}
#pragma warning(pop)
#endif	/* _M_CEE_PURE */

//
// [pfx_parse]
// guard against __readfsbyte parsing error
//
#if (_MSC_FULL_VER >= 13012035) || defined(_PREFIX_) || defined(_PREFAST_)

//
// Define FS referencing intrinsics
//

UCHAR
__readfsbyte (
    IN ULONG Offset
    );
 
USHORT
__readfsword (
    IN ULONG Offset
    );
 
ULONG
__readfsdword (
    IN ULONG Offset
    );
 
VOID
__writefsbyte (
    IN ULONG Offset,
    IN UCHAR Data
    );
 
VOID
__writefsword (
    IN ULONG Offset,
    IN USHORT Data
    );
 
VOID
__writefsdword (
    IN ULONG Offset,
    IN ULONG Data
    );

#pragma intrinsic(__readfsbyte)
#pragma intrinsic(__readfsword)
#pragma intrinsic(__readfsdword)
#pragma intrinsic(__writefsbyte)
#pragma intrinsic(__writefsword)
#pragma intrinsic(__writefsdword)

#endif	/* _MSC_FULL_VER >= 13012035 */

#ifdef __cplusplus
}
#endif

#endif  /* !defined(MIDL_PASS) || defined(_M_IX86) */

#endif /* TARGETING_Win2K */

#ifndef BitScanForward
#define BitScanForward _BitScanForward

BOOLEAN
_BitScanForward (
				 OUT ULONG *Index,
				 IN ULONG Mask
				 );

#pragma intrinsic(_BitScanForward)
#endif

#ifndef BitScanReverse
#define BitScanReverse _BitScanReverse
BOOLEAN
_BitScanReverse (
				 OUT ULONG *Index,
				 IN ULONG Mask
				 );

#pragma intrinsic(_BitScanReverse)
#endif


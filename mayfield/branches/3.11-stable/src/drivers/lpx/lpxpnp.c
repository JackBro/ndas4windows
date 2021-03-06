ntext SpinLock
    //

    KSPIN_LOCK SpinLock;                // lock to manipulate this object.
                                        //  (used in KeAcquireSpinLock calls)

    //
    // the device context state, among open, closing
    //

    UCHAR State;

#if 0
    //
    // The following queue holds free TP_CONNECTION objects available for allocation.
    //

    LIST_ENTRY ConnectionPool;
#endif

    //
    // These counters keep track of resources uses by TP_CONNECTION objects.
    //

    ULONG ConnectionAllocated;
    ULONG ConnectionInitAllocated;
    ULONG ConnectionMaxAllocated;
    ULONG ConnectionInUse;
 
    //
    // following is used to keep adapter information.
    //

    PWCHAR DeviceName;
    ULONG DeviceNameLength;

    //
    // This is the Mac type we must build the packet header for and know the
    // offsets for.
    //

    ULONG MaxReceivePacketSize;         // does not include the MAC header
    ULONG MaxSendPacketSize;            // includes the MAC header

    ULONG MaxUserData;
    
    //
    // some MAC addresses we use in the transport
    //

    HARDWARE_ADDRESS LocalAddress;      // our local hardware address.

    HANDLE TdiDeviceHandle;

    //
    // The following structure contains statistics counters for use
    // by TdiQueryInformation and TdiSetInformation.  They should not
    // be used for maintenance of internal data structures.
    //

    TDI_PROVIDER_INFO Information;      // information about this provider.

    PVOID PnPContext;
    
} CONTROL_CONTEXT, *PCONTROL_CONTEXT;


//
// device context state definitions
//

#define DEVICECONTEXT_STATE_OPENING  0x00
#define DEVICECONTEXT_STATE_OPEN     0x01
#define DEVICECONTEXT_STATE_DOWN     0x02
#define DEVICECONTEXT_STATE_STOPPING 0x03
 

extern PCONTROL_CONTEXT  LpxControlDeviceContext;
extern PDEVICE_CONTEXT  LpxPrimaryDeviceContext;


//
// Structure used to interpret the TransportReserved part in the NET_PNP_EVENT
//

typedef struct _NET_PNP_EVENT_RESERVED {
    PWORK_QUEUE_ITEM PnPWorkItem;
    PDEVICE_CONTEXT DeviceContext;
} NET_PNP_EVENT_RESERVED, *PNET_PNP_EVENT_RESERVED;

#ifndef C_ASSERT
#define C_ASSERT(e) typedef char __C_ASSERT__[(e)?1:-1]
#endif
#define C_ASSERT_SIZEOF(type, size) C_ASSERT(sizeof(type) == size)

C_ASSERT(sizeof(TP_SEND_IRP_PARAMETERS) <= sizeof(((PIO_STACK_LOCATION)NULL)->Parameters));
#endif // def _LPXTYPES_

                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           ;-------------------------------------------------------------------------
; netlpx.inf -- Lean Packet Exchange Protocol
;
; Installation file (.inf) for the LPX Protocol
;
; Copyright (c) 2002-2004 XIMETA, Inc.
;
;-------------------------------------------------------------------------

[Version]
Signature   = "$Windows NT$"
Class       = NetTrans
ClassGUID   = {4d36e975-e325-11ce-bfc1-08002be10318}
Provider    = %PROVIDER%
CatalogFile = netlpx.cat
DriverVer   = 01/01/2003,1.0.0.0
DriverPackageType=Network
DriverPackageDisplayName=%DIFX_PACKAGE_NAME%

[Manufacturer]
%MANUFACTURER%=Generic,NTamd64

[Generic.NTamd64]
%LPX.Desc%=LPX.Install, NKC_LPX

[LPX.Install]
AddReg=LPX.NDI
Characteristics=0 ; Has no characterstic
CopyFiles=LPX.DriverFiles,LPX.DLLFiles,LPX.DLLx86Files

[LPX.NDI]
;HKR,Ndi,ClsID,,"{98E32798-5CF4-4165-8180-FFAE504FBDB1}"
HKR,Ndi,HelpText,,%LPX.HelpText%
HKR,Ndi,Service,,"lpx"
HKR,Ndi\Interfaces,"UpperRange",,"winsock"
HKR,Ndi\Interfaces,"LowerRange",,"ndis4,ndis5"

[LPX.Install.Services]
AddService=lpx,,LPX.Service.Install

[LPX.Service.Install]
DisplayName     = %LPX.Desc%
ServiceType     = 1 ;SERVICE_KERNEL_DRIVER
StartType       = 0 ;SERVICE_BOOT_START
ErrorControl    = 1 ;SERVICE_ERROR_NORMAL
ServiceBinary   = %12%\lpx.sys
LoadOrderGroup  = "PNP_TDI"
AddReg          = LPX.Service.Install.AddReg
Description     = %LPX.Desc%

[LPX.Service.Install.AddReg]
;HKR,"Parameters","NbProvider",,"_nb"
HKLM,"System\CurrentControlSet\Services\lpx","TextModeFlags",%REG_DWORD%,0x0001
HKR,"Parameters\winsock","UseDelayedAcceptance",%REG_DWORD%,0x0001

[LPX.Install.Winsock]
AddSock = LPX.AddSock

[LPX.AddSock]
TransportService	= lpx
;SupportedNameSpace	= 13
HelperDllName		= %11%\wshlpx.dll
MaxSockAddrLength	= 0x14
MinSockAddrLength	= 0x14

[LPX.Install.Remove]
DelReg=LPX.DelReg

[LPX.Install.Remove.Services]
DelService=lpx

[LPX.DelReg]
HKLM,"System\CurrentControlSet\Services\lpx","TextModeFlags"

[LPX.Install.Remove.Winsock]
DelSock = LPX.Remove.Winsock

[LPX.Remove.Winsock]
TransportService = LPX

[DestinationDirs]
LPX.DriverFiles = 12 ; DIRID_DRIVERS
LPX.DLLFiles    = 11 ; SYSTEM32
LPX.DLLx86Files  = 16425 ; CSIDL_SYSTEMX86

[LPX.DriverFiles]
lpx.sys

[LPX.DLLFiles]
wshlpx.dll

[LPX.DLLx86Files]
wshlpx.dll,wshlpx32.dll

[SourceDisksNames]
1 = %DiskId1%,,,

[SourceDisksFiles]
lpx.sys=1
wshlpx.dll=1
wshlpx32.dll=1

[Strings]
;
; Non-Localizable Strings
;
REG_DWORD = 0x10001

;
; Localizable Strings
;
PROVIDER     = "XIMETA"
MANUFACTURER = "XIMETA"
LPX.Desc     = "LPX Protocol"
LPX.HelpText = "Lean Packet Exchange Protocol for NDAS Devices"
DiskId1      = "NDAS Device Driver Install Disk"
DIFX_PACKAGE_NAME="NDAS Device Driver (LPX Protocol)"
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     ;-------------------------------------------------------------------------
; netlpx.inf -- Lean Packet Exchange Protocol
;
; Installation file (.inf) for the LPX Protocol
;
; Copyright (c) 2002-2004 XIMETA, Inc.
;
;-------------------------------------------------------------------------

[Version]
Signature   = "$Windows NT$"
Class       = NetTrans
ClassGUID   = {4d36e975-e325-11ce-bfc1-08002be10318}
Provider    = %PROVIDER%
CatalogFile = netlpx.cat
DriverVer   = 01/01/2003,1.0.0.0
DriverPackageType=Network
DriverPackageDisplayName=%DIFX_PACKAGE_NAME%

[Manufacturer]
%MANUFACTURER%=Generic

[Generic]
%LPX.Desc%=LPX.Install, NKC_LPX

[LPX.Install]
AddReg=LPX.NDI
Characteristics=0 ; Has no characterstic
CopyFiles=LPX.DriverFiles,LPX.DLLFiles

[LPX.NDI]
;HKR,Ndi,ClsID,,"{98E32798-5CF4-4165-8180-FFAE504FBDB1}"
HKR,Ndi,HelpText,,%LPX.HelpText%
HKR,Ndi,Service,,"lpx"
HKR,Ndi\Interfaces, UpperRange,,"winsock"
HKR,Ndi\Interfaces,"LowerRange",,"ndis4,ndis5"

[LPX.Install.Services]
AddService=lpx,,LPX.Service.Install

[LPX.Service.Install]
DisplayName     = %LPX.Desc%
ServiceType     = 1 ;SERVICE_KERNEL_DRIVER
StartType       = 0 ;SERVICE_BOOT_START
ErrorControl    = 1 ;SERVICE_ERROR_NORMAL
ServiceBinary   = %12%\lpx.sys
LoadOrderGroup  = "PNP_TDI"
AddReg          = LPX.Service.Install.AddReg
Description     = %LPX.Desc%

[LPX.Service.Install.AddReg]
;HKR,"Parameters","NbProvider",,"_nb"
HKLM,"System\CurrentControlSet\Services\lpx","TextModeFlags",%REG_DWORD%,0x0001
HKR,"Parameters\winsock","UseDelayedAcceptance",%REG_DWORD%,0x0001

[LPX.Install.Winsock]
AddSock = LPX.AddSock

[LPX.AddSock]
TransportService	= lpx
;SupportedNameSpace	= 13
HelperDllName		= %11%\wshlpx.dll
MaxSockAddrLength	= 0x14
MinSockAddrLength	= 0x14

[LPX.Install.Remove]
DelReg=LPX.DelReg

[LPX.Install.Remove.Services]
DelService=lpx

[LPX.DelReg]
HKLM,"System\CurrentControlSet\Services\lpx","TextModeFlags"

[LPX.Install.Remove.Winsock]
DelSock = LPX.Remove.Winsock

[LPX.Remove.Winsock]
TransportService = LPX

[DestinationDirs]
LPX.DriverFiles = 12 ; DIRID_DRIVERS
LPX.DLLFiles    = 11 ; SYSTEM32

[LPX.DriverFiles]
lpx.sys

[LPX.DLLFiles]
wshlpx.dll

[SourceDisksNames]
1 = %DiskId1%,,,

[SourceDisksFiles]
lpx.sys=1
wshlpx.dll=1

[Strings]
;
; Non-Localizable Strings
;
REG_DWORD = 0x10001

;
; Localizable Strings
;
PROVIDER     = "XIMETA"
MANUFACTURER = "XIMETA"
LPX.Desc     = "LPX Protocol"
LPX.HelpText = "Lean Packet Exchange Protocol for NDAS Devices"
DiskId1      = "NDAS Device Driver Install Disk"
DIFX_PACKAGE_NAME="NDAS Device Driver (LPX Protocol)"
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                /*++

Copyright (C)2002-2005 XIMETA, Inc.
All rights reserved.

--*/

#include <ntddk.h>

#include <windef.h>
#include <nb30.h>
 #include <stdio.h>
 
#include <ndis.h>                       // Physical Driver Interface.

#include <tdikrnl.h>                        // Transport Driver Interface.

#include <ndis.h>                       // Physical Driver Interface.

#if DEVL
#define STATIC
#else
#define STATIC static
#endif

// When you include module.ver file, you should include <ndasverp.h> also
// in case module.ver does not include any definitions
#include "lpx.ver"
#include <ndasverp.h>

#include "socketlpx.h"

#include "lpxconst.h"                   // private NETBEUI constants.
#include "lpxmac.h"                     // mac-specific definitions

#include "lpxproto.h"
#include "lpx.h"

#include "lpxcnfg.h"                    // configuration information.
#include "lpxtypes.h"                   // private LPX types.
#include "lpxprocs.h"                   // private LPX function prototypes.


//
// Resource and Mutex Macros
//

//
// We wrap each of these macros using
// Enter,Leave critical region macros
// to disable APCs which might occur
// while we are holding the resource
// resulting in deadlocks in the OS.
//

#define ACQUIRE_RESOURCE_EXCLUSIVE(Resource, Wait) \
    KeEnterCriticalRegion(); ExAcquireResourceExclusiveLite(Resource, Wait);
    
#define RELEASE_RESOURCE(Resource) \
    ExReleaseResourceLite(Resource); KeLeaveCriticalRegion();

#define ACQUIRE_FAST_MUTEX_UNSAFE(Mutex) \
    KeEnterCriticalRegion(); ExAcquireFastMutexUnsafe(Mutex);

#define RELEASE_FAST_MUTEX_UNSAFE(Mutex) \
    ExReleaseFastMutexUnsafe(Mutex); KeLeaveCriticalRegion();

#define ACQUIRE_SPIN_LOCK(lock,irql) KeAcquireSpinLock(lock,irql)
#define RELEASE_SPIN_LOCK(lock,irql) KeReleaseSpinLock(lock,irql)
#define ACQUIRE_DPC_SPIN_LOCK(lock) KeAcquireSpinLockAtDpcLevel(lock)
#define RELEASE_DPC_SPIN_LOCK(lock) KeReleaseSpinLockFromDpcLevel(lock)

#if DBG
extern LONG	DebugLevel;
#define DebugPrint(_l_, _x_)			\
		do{								\
			if(_l_ < DebugLevel)		\
				DbgPrint _x_;			\
		}	while(0)					\
		
#else	
#define DebugPrint(_l_, _x_)			\
		do{								\
		} while(0)
#endif

                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           /*++

Copyright (C)2002-2005 XIMETA, Inc.
All rights reserved.

--*/

#include "precomp.h"
#pragma hdrstop


NTSTATUS
LpxTdiReceive(
    IN PIRP Irp
    )

/*++

Routine Description:

    This routine performs the TdiReceive request for the transport provider.

Arguments:

    Irp - I/O Request Packet for this request.

Return Value:

    NTSTATUS - status of operation.

--*/

{
    PTP_CONNECTION connection;
     PIO_STACK_LOCATION irpSp;
    NTSTATUS status;
    //
    // verify that the operation is taking place on a connection. At the same
    // time we do this, we reference the connection. This ensures it does not
    // get removed out from under us. Note also that we do the connection
    // lookup within a try/except clause, thus protecting ourselves against
    // really bogus handles
    //

    irpSp = IoGetCurrentIrpStackLocation (Irp);
    connection = irpSp->FileObject->FsContext;

    IF_LPXDBG (LPX_DEBUG_RCVENG) {
        LpxPrint2 ("LpxTdiReceive: Received IRP %p on connection %p\n", 
                        Irp, connection);
    }

    //
    // Check that this i
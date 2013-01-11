#ifndef __SOCKETLPXPROC_H__
#define __SOCKETLPXPROC_H__

#if DBG
extern LONG	DebugLevel;

#define DebugPrint(_l_, _x_)							\
	if (_l_ <= DebugLevel) {							\
		DbgPrint ("%s %d: ", __FUNCTION__, __LINE__);	\
		DbgPrint _x_;									\
	}					

#else	
#define DebugPrint(_l_, _x_)
#endif

extern PDEVICE_CONTEXT  SocketLpxDeviceContext;
extern PDEVICE_CONTEXT  SocketLpxPrimaryDeviceContext;


VOID
SocketLpxProtocolBindAdapter(
	OUT PNDIS_STATUS	NdisStatus,
    IN  NDIS_HANDLE     BindContext,
    IN  PNDIS_STRING    DeviceName,
    IN	PVOID           SystemSpecific1,
    IN	PVOID           SystemSpecific2
    ); 

VOID
SocketLpxProtocolUnbindAdapter(
	OUT PNDIS_STATUS	NdisStatus,
    IN	NDIS_HANDLE		ProtocolBindContext,
    IN	PNDIS_HANDLE	UnbindContext
    );

NTSTATUS
SocketLpxFindDeviceContext(
    IN  PLPX_ADDRESS	NetworkName,
	OUT PDEVICE_CONTEXT *DeviceContext
	);

	
extern ULONG	PacketTxDropRate;
extern ULONG	PacketTxCountForDrop;

extern ULONG	PacketRxDropRate;
extern ULONG	PacketRxCountForDrop;


#endif //__SOCKETLPX_H__

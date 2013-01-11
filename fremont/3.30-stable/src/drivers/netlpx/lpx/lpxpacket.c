/*++

Copyright (C) 2002-2007 XIMETA, Inc.
All rights reserved.

--*/

#include "precomp.h"
#pragma hdrstop


#if __LPX__

LONG	NumberOfAllockPackets = 0;

ULONG	NumberOfSent = 0;
ULONG	NumberOfSentComplete = 0;


NTSTATUS
SendPacketAlloc (
	IN  PDEVICE_CONTEXT		DeviceContext,
	IN  PTP_ADDRESS			Address,
	IN  UCHAR				DestinationAddressNode[],
	IN	PUCHAR				UserData,
	IN	ULONG				UserDataLength,
	IN	PIO_STACK_LOCATION	IrpSp,
	IN  UCHAR				Option,
	OUT	PNDIS_PACKET		*Packet
	)
{
	NTSTATUS		status;
	PUCHAR			packetHeader = NULL;
	PNDIS_BUFFER	packetHeaderBuffer = NULL;
	ULONG			packetHeaderLength;

	PNDIS_BUFFER	userDataBuffer = NULL;
	PUCHAR			paddingData = NULL;
	PNDIS_BUFFER	paddingDataBuffer = NULL;
	PNDIS_PACKET	packet = NULL;
	USHORT			etherType;


	packetHeaderLength = ETHERNET_HEADER_LENGTH + sizeof(LPX_HEADER);

#if __LPX_OPTION_ADDRESSS__

	if (FlagOn(Option, LPX_OPTION_SOURCE_ADDRESS)) {

		packetHeaderLength += ETHERNET_ADDRESS_LENGTH;
	}

	if (FlagOn(Option, LPX_OPTION_DESTINATION_ADDRESS)) {

		packetHeaderLength += ETHERNET_ADDRESS_LENGTH;
	}

#endif

	ASSERT( packetHeaderLength + UserDataLength <= ETHERNET_HEADER_LENGTH + DeviceContext->MaxUserData );
	
	DebugPrint( 3, ("SendPacketAlloc, packetHeaderLength = %d, NumberOfAllockPackets = %d\n", packetHeaderLength, NumberOfAllockPackets) );
	
	ASSERT( DeviceContext );
	ASSERT( DeviceContext->LpxPacketPool != NULL );

	do {
	
		NdisAllocatePacket( &status, &packet, DeviceContext->LpxPacketPool );

		if (status != NDIS_STATUS_SUCCESS) {
 	
			NDAS_ASSERT( status == NDIS_STATUS_RESOURCES );
			NDAS_ASSERT( NDAS_ASSERT_INSUFFICIENT_RESOURCES );

			return status;
		}

		RtlZeroMemory( RESERVED(packet), sizeof(LPX_RESERVED) );

		ASSERT( packet->Private.NdisPacketFlags & fPACKET_ALLOCATED_BY_NDIS );

		packetHeader = (PCHAR)&RESERVED(packet)->EthernetHeader;

		NdisAllocateBuffer( &status,
							&packetHeaderBuffer,
							DeviceContext->LpxBufferPool,
							packetHeader,
							packetHeaderLength );

		if (!NT_SUCCESS(status)) {

			NDAS_ASSERT( status == NDIS_STATUS_FAILURE ); 
			NDAS_ASSERT( NDAS_ASSERT_INSUFFICIENT_RESOURCES );

			break;
		}

		if (UserData && UserDataLength) {
			
			NdisAllocateBuffer( &status,
								&userDataBuffer,
								DeviceContext->LpxBufferPool,
								UserData,
								UserDataLength );

			if (!NT_SUCCESS(status)) {

				NDAS_ASSERT( status == NDIS_STATUS_FAILURE ); 
				NDAS_ASSERT( NDAS_ASSERT_INSUFFICIENT_RESOURCES );

				break;
			}
		}

//////////////////////////////////////////////////////////////////////////
//
//	Add padding to fix Under-60byte bug of NDAS chip 2.0.
//

		if (packetHeaderLength == ETHERNET_HEADER_LENGTH + sizeof(LPX_HEADER)) {

			UINT	totalPacketLength;

			totalPacketLength = packetHeaderLength + UserDataLength;

			if (totalPacketLength >= ETHERNET_HEADER_LENGTH + sizeof(LPX_HEADER) + 4 && totalPacketLength <= 56) {

			    LONG	paddingLen = 60 - totalPacketLength;
			
				DebugPrint( 4, ("[LpxSmp]TransmitDataPacket: Adding padding to support NDAS chip 2.0\n") );

				status = LpxAllocateMemoryWithLpxTag( &paddingData, paddingLen );

				if (status != NDIS_STATUS_SUCCESS) {

					NDAS_ASSERT( status == NDIS_STATUS_FAILURE ); 
					NDAS_ASSERT( NDAS_ASSERT_INSUFFICIENT_RESOURCES );
			
					break;
				}

				NdisAllocateBuffer( &status,
									&paddingDataBuffer,
									DeviceContext->LpxBufferPool,
									paddingData,
									paddingLen );

				if (status != NDIS_STATUS_SUCCESS) {

					NDAS_ASSERT( status == NDIS_STATUS_FAILURE ); 
					NDAS_ASSERT( NDAS_ASSERT_INSUFFICIENT_RESOURCES );
			
					break;
				}

				RtlZeroMemory( paddingData, paddingLen );

				RtlCopyMemory( paddingData + paddingLen - 4, UserData + UserDataLength - 4, 4 );
			}
		}

//
//	End of padding routine.
//
//////////////////////////////////////////////////////////////////////////

	} while(0);

	if (status == STATUS_SUCCESS) {
	
		RtlCopyMemory( &packetHeader[0],
					   DestinationAddressNode,
					   ETHERNET_ADDRESS_LENGTH );

		RtlCopyMemory( &packetHeader[ETHERNET_ADDRESS_LENGTH],
					   Address->NetworkName->Node,
					   ETHERNET_ADDRESS_LENGTH );

		etherType = HTONS( ETH_P_LPX );

		RtlCopyMemory( &packetHeader[ETHERNET_ADDRESS_LENGTH*2],
					   &etherType,
					   2 );

#if __LPX_OPTION_ADDRESSS__

		if (FlagOn(Option, LPX_OPTION_DESTINATION_ADDRESS)) {

			RtlCopyMemory( RESERVED(packet)->OptionDestinationAddress,
						   DestinationAddressNode,
						   ETHERNET_ADDRESS_LENGTH );	
		}

		if (FlagOn(Option, LPX_OPTION_SOURCE_ADDRESS)) {

			if (FlagOn(Option, LPX_OPTION_DESTINATION_ADDRESS)) {
		
				RtlCopyMemory( RESERVED(packet)->OptionSourceAddress,
							   Address->NetworkName->Node,
							   ETHERNET_ADDRESS_LENGTH );	
			
			} else {
			
				RtlCopyMemory( RESERVED(packet)->OptionDestinationAddress,
							   Address->NetworkName->Node,
							   ETHERNET_ADDRESS_LENGTH );	
			}
		}

#endif

		RESERVED(packet)->LpxHeader.PacketSize = HTONS( (USHORT)(packetHeaderLength - ETHERNET_HEADER_LENGTH + UserDataLength) );
		RESERVED(packet)->LpxHeader.Option = Option;

		RESERVED(packet)->Cloned = 0;
		RESERVED(packet)->IrpSp  = IrpSp;
		RESERVED(packet)->Type   = LPX_PACKET_TYPE_SEND;
		RESERVED(packet)->Packet = packet;
		
		if (IrpSp == NULL) {

			DebugPrint( 3, ("[LPX] PacketAllocate: No IrpSp\n") ) ;
		}
	
		if (paddingDataBuffer)
			NdisChainBufferAtFront( packet, paddingDataBuffer );
		
		if (userDataBuffer)
			NdisChainBufferAtFront( packet, userDataBuffer );
		
		NdisChainBufferAtFront( packet, packetHeaderBuffer );

		InterlockedIncrement( &NumberOfAllockPackets );
	
		*Packet = packet;
	
	} else {

		if (paddingDataBuffer)
			NdisFreeBuffer( paddingDataBuffer );

		if( paddingData)
			LpxFreeMemoryWithLpxTag( paddingData );

		if (userDataBuffer)
			NdisFreeBuffer( userDataBuffer );

		if (packetHeaderBuffer)
			NdisFreeBuffer( packetHeaderBuffer );
		
		if (packet)
			NdisFreePacket( packet );

		*Packet = NULL;
		
		DebugPrint( 1, ("[LPX]PacketAllocate: Can't Allocate Buffer For CopyData!!!\n") );
	}

	return status;
}


NTSTATUS
RcvPacketAlloc (
	IN  PDEVICE_CONTEXT		DeviceContext,
	IN	ULONG				PacketDataLength,
	OUT	PNDIS_PACKET		*Packet
	)
{
	NTSTATUS		status;
	PUCHAR			packetData = NULL;
	PNDIS_BUFFER	packetDataBuffer = NULL;
	PNDIS_PACKET	packet = NULL;


	DebugPrint( 3, ("RcvPacketAlloc, PacketLength = %d, NumberOfAllockPackets = %d\n", PacketDataLength, NumberOfAllockPackets) );
	
	ASSERT( DeviceContext );
	ASSERT( DeviceContext->LpxPacketPool != NULL );

	ASSERT( PacketDataLength <= DeviceContext->MaxUserData );

	do {
	
		NdisAllocatePacket( &status, &packet, DeviceContext->LpxPacketPool );

		if (status != NDIS_STATUS_SUCCESS) {
 	
			NDAS_ASSERT( status == NDIS_STATUS_RESOURCES );
			NDAS_ASSERT( NDAS_ASSERT_INSUFFICIENT_RESOURCES );

			return status;
		}

		RtlZeroMemory( RESERVED(packet), sizeof(LPX_RESERVED) );

		ASSERT( packet->Private.NdisPacketFlags & fPACKET_ALLOCATED_BY_NDIS );

		if (PacketDataLength) {
		
			status = LpxAllocateMemoryWithLpxTag( &packetData,
												  PacketDataLength );

			if (status != NDIS_STATUS_SUCCESS) {

				NDAS_ASSERT( status == NDIS_STATUS_FAILURE ); 
				NDAS_ASSERT( NDAS_ASSERT_INSUFFICIENT_RESOURCES );
			
				break;
			}

			NdisAllocateBuffer( &status,
								&packetDataBuffer,
								DeviceContext->LpxBufferPool,
								packetData,
								PacketDataLength );

			if (!NT_SUCCESS(status)) {

				NDAS_ASSERT( status == NDIS_STATUS_FAILURE ); 
				NDAS_ASSERT( NDAS_ASSERT_INSUFFICIENT_RESOURCES );

				break;
			}

			NdisChainBufferAtFront( packet, packetDataBuffer );
		}
	
	} while(0);

	if (status == STATUS_SUCCESS) {
	
		RESERVED(packet)->Cloned = 0;
		RESERVED(packet)->Type = LPX_PACKET_TYPE_RECEIVE;
		RESERVED(packet)->Packet = packet;
	
		InterlockedIncrement( &NumberOfAllockPackets );
	
		*Packet = packet;
	
#if __LPX_STATISTICS__
		
		KeQuerySystemTime( &RESERVED(packet)->RecvTime2 );
		RESERVED(packet)->DeviceContext = DeviceContext;

#endif

	} else {

		if (packetDataBuffer)
			NdisFreeBuffer( packetDataBuffer );
		
		if (packetData)
			LpxFreeMemoryWithLpxTag( packetData );

		if (packet)
			NdisFreePacket( packet );

		*Packet = NULL;
		
		DebugPrint( 1, ("[LPX]RcvPacketAlloc: Can't Allocate Buffer For CopyData!!!\n") );
	}

	return status;
}



VOID
PacketFree (
	IN PDEVICE_CONTEXT  DeviceContext,
	IN PNDIS_PACKET		Packet
	)
{
	PLPX_RESERVED	reserved = RESERVED(Packet);
	PUCHAR			packetData;
	PNDIS_BUFFER	pNdisBuffer;
	UINT			uiLength;
	LONG			clone;
	LONG			BufferSeq;
	BOOLEAN			allocMiniport = FALSE;
	PLPX_RESERVED	externalReserved;

	UNREFERENCED_PARAMETER( DeviceContext );

	DebugPrint( 3, ("PacketFree reserved->type = %d\n", reserved->Type) );

	ASSERT( Packet->Private.NdisPacketFlags & fPACKET_ALLOCATED_BY_NDIS );

	switch (reserved->Type) {

	case LPX_PACKET_TYPE_SEND:

		clone = InterlockedDecrement( &reserved->Cloned );
		
		if(clone >= 0) {
		
			return;
		}
	
		pNdisBuffer = NULL;
		BufferSeq = 0;
		NdisUnchainBufferAtFront( Packet, &pNdisBuffer );

		while (pNdisBuffer) {

			//
			//	Assuming the first data buffer comes from user application
			//			the others are created in LPX for padding, etc.
			//	Free the memory of the others.
			//
			
			if (BufferSeq == 0) {

#if DBG
				NdisQueryBufferSafe( pNdisBuffer, &packetData, &uiLength, HighPagePriority );
				ASSERT( packetData == (PCHAR)&RESERVED(Packet)->EthernetHeader );
#endif

			} else if (BufferSeq == 1) {

				// UserBuffer

			} else if (BufferSeq == 2) {

				// Padding
					
				NdisQueryBufferSafe( pNdisBuffer, &packetData, &uiLength, HighPagePriority );
				LpxFreeMemoryWithLpxTag( packetData );
			
			} else {

				ASSERT( FALSE );
			}

			NdisFreeBuffer( pNdisBuffer );

			pNdisBuffer = NULL;
			NdisUnchainBufferAtFront( Packet, &pNdisBuffer );
			BufferSeq ++;
		}
		
		if (reserved->IrpSp != NULL) {
			
			PIRP _Irp = IRP_SEND_IRP( reserved->IrpSp );

			NDAS_ASSERT( reserved->NdisStatus == NDIS_STATUS_SUCCESS			|| 
						 reserved->NdisStatus == STATUS_UNSUCCESSFUL			||
						 reserved->NdisStatus == NDIS_STATUS_NO_CABLE			||
						 reserved->NdisStatus == NDIS_STATUS_REQUEST_ABORTED	||
						 reserved->NdisStatus == NDIS_STATUS_NOT_ACCEPTED		||
						 reserved->NdisStatus == 0xC001002a						||
						 reserved->NdisStatus == NDIS_STATUS_ADAPTER_NOT_READY );

			if (!NT_SUCCESS(reserved->NdisStatus)) {

				_Irp->IoStatus.Status = reserved->NdisStatus;
			}

			LpxDereferenceSendIrp( "Destroy packet", reserved->IrpSp, RREF_PACKET );
		
		} else {
		
			DebugPrint( 3, ("[LPX] PacketFree: No IrpSp\n") ) ;
		}

		break;

	case LPX_PACKET_TYPE_RECEIVE:

		//
		// If the packet allocated by NIC miniport, break here.
		//
		if (RESERVED(Packet)->RecvFlags & LPX_RESERVED_RECVFLAG_ALLOC_MINIPORT) {

			allocMiniport = TRUE;
			break;
		}

		pNdisBuffer = NULL;	

		NdisUnchainBufferAtFront( Packet, &pNdisBuffer );

#if __LPX_STATISTICS__
		{
			LARGE_INTEGER systemTime;

			KeQuerySystemTime( &systemTime );

			RESERVED(Packet)->DeviceContext->NumberOfRecvPackets ++;
			RESERVED(Packet)->DeviceContext->FreeTimeOfRecvPackets.QuadPart += systemTime.QuadPart - RESERVED(Packet)->RecvTime2.QuadPart;
			RESERVED(Packet)->DeviceContext->BytesOfRecvPackets.QuadPart += sizeof(LPX_HEADER) + RESERVED(Packet)->PacketRawDataLength;

			if (RESERVED(Packet)->PacketRawDataLength) {

				RESERVED(Packet)->DeviceContext->NumberOfLargeRecvPackets ++;
				RESERVED(Packet)->DeviceContext->FreeTimeOfLargeRecvPackets.QuadPart += systemTime.QuadPart - RESERVED(Packet)->RecvTime2.QuadPart;
				RESERVED(Packet)->DeviceContext->BytesOfLargeRecvPackets.QuadPart += sizeof(LPX_HEADER) + RESERVED(Packet)->PacketRawDataLength;
			
			} else {

				RESERVED(Packet)->DeviceContext->NumberOfSmallRecvPackets ++;
				RESERVED(Packet)->DeviceContext->FreeTimeOfSmallRecvPackets.QuadPart += systemTime.QuadPart - RESERVED(Packet)->RecvTime2.QuadPart;
				RESERVED(Packet)->DeviceContext->BytesOfSmallRecvPackets.QuadPart += sizeof(LPX_HEADER);
			}
		}
#endif

		if (pNdisBuffer) {

			NdisQueryBufferSafe( pNdisBuffer, &packetData, &uiLength, HighPagePriority );
			
			LpxFreeMemoryWithLpxTag( packetData );
			NdisFreeBuffer( pNdisBuffer );
		}

		break;

	default:
		
		NDAS_ASSERT(FALSE);
		return;
	}

	//
	// Free external protocol reserved context.
	//

	externalReserved = ((PLPX_RESERVED)(Packet->ProtocolReserved))->ExternalReserved;
	
	if (externalReserved) {
	
		NdisFreeMemory(externalReserved, sizeof(LPX_RESERVED), 0);
	}

	ASSERT( Packet->Private.NdisPacketFlags & fPACKET_ALLOCATED_BY_NDIS );

	if (allocMiniport) {
	
		// Return the packet allocated by NIC miniport
	
		NdisReturnPackets(&Packet, 1);
	
	} else {

		NdisFreePacket( Packet );

		InterlockedDecrement( &NumberOfAllockPackets );
		DebugPrint( 3, ("Packet REALLY Freed NumberOfAllockPackets = %d\n", NumberOfAllockPackets) );
	}
}


PNDIS_PACKET
PacketCopy (
	IN	PNDIS_PACKET	Packet,
	OUT	PLONG			CloneCount
	)
{
	if (CloneCount)
		*CloneCount = InterlockedIncrement( &(RESERVED(Packet)->Cloned) );
	else
		InterlockedIncrement( &(RESERVED(Packet)->Cloned) );

	return Packet;
}

//////////////////////////////////////////////////////////////////////////
//
//	Packet queue utility functions
//
//

PNDIS_PACKET
PacketDequeue (
	IN PLIST_ENTRY	PacketQueue,
	IN PKSPIN_LOCK	QSpinLock
	)
{
	PLIST_ENTRY		packetListEntry = NULL;
	PLPX_RESERVED	reserved;
	PNDIS_PACKET	packet;
	
	DebugPrint( 4, ("PacketDequeue\n") );
	
	if (QSpinLock) {
	
		packetListEntry = ExInterlockedRemoveHeadList( PacketQueue,
													   QSpinLock );
		
	} else {

		if (IsListEmpty(PacketQueue))
			packetListEntry = NULL;
		else
			packetListEntry = RemoveHeadList(PacketQueue);
	}
	
	if (packetListEntry) {
	
		reserved = CONTAINING_RECORD( packetListEntry, LPX_RESERVED, ListEntry );
		packet = reserved->Packet;

	} else {

		packet = NULL;
	}

	return packet;
}


BOOLEAN
PacketQueueEmpty(
	PLIST_ENTRY	PacketQueue,
	PKSPIN_LOCK	QSpinLock
	)
{
	PLIST_ENTRY		packetListEntry;
	KIRQL			oldIrql;

	if (QSpinLock) {
	
		ACQUIRE_SPIN_LOCK( QSpinLock, &oldIrql );
		packetListEntry = PacketQueue->Flink;
		RELEASE_SPIN_LOCK( QSpinLock, oldIrql );
	
	} else {

		packetListEntry = PacketQueue->Flink;
	}

	return( packetListEntry == PacketQueue );
}

#endif

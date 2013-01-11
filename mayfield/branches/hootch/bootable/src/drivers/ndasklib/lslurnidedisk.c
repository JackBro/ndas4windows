#ifdef __NDASBOOT__
#ifdef __ENABLE_LOADER__
#include "ntkrnlapi.h"
#endif
#include "ndasboot.h"
#endif

#include <ntddk.h>
#include <stdio.h>

#include "ver.h"
#include "LSKLib.h"
#include "basetsdex.h"
#include "cipher.h"
#include "hdreg.h"
#include "binparams.h"
#include "hash.h"

#include "KDebug.h"
#include "LSProto.h"
#include "LSLurn.h"
#include "LSLurnIde.h"

#ifdef __MODULE__
#undef __MODULE__
#endif // __MODULE__
#define __MODULE__ "LurnIdeDisk"


#ifdef __NDASBOOT__
#include "repos.h"
#include "lsutils.h"
#include "ndasboot.h"

#ifdef __ENABLE_LOADER__
NTSTATUS
LurnIdeDiskDispatchCcb(
		PLURELATION_NODE		Lurn,
		PLURNEXT_IDE_DEVICE		IdeDisk
	) ;
#endif

#endif __NDASBOOT__

//////////////////////////////////////////////////////////////////////////
//
//	IDE disk interfaces
//
NTSTATUS
LurnIdeDiskInitialize(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
	);

NTSTATUS
LurnIdeDiskRequest(
		PLURELATION_NODE Lurn,
		PCCB Ccb
	);

LURN_INTERFACE LurnIdeDiskInterface = {
					LSSTRUC_TYPE_LURN_INTERFACE,
					sizeof(LURN_INTERFACE),
					LURN_IDE_DISK,
					0, {
						LurnIdeDiskInitialize,
						LurnIdeDestroy,
						LurnIdeDiskRequest
				}
	};



//////////////////////////////////////////////////////////////////////////
//
//	IDE disk interface
//

static VOID
LurnIdeDiskThreadProc(
		IN	PVOID	Context
	);

static
LONG
LurnIdeDiskGetWriteSplit(
	PLANSCSI_SESSION LanScsiSession)
{
//	return LanScsiSession->MaxBlocks; // temp
	return LanScsiSession->WriteSplitSize;
}

static
LONG
LurnIdeDiskAdjustWriteSplit(
	PLANSCSI_SESSION LanScsiSession)
{
#ifdef RETRANS_FRE_LOG
	UCHAR	buffer[257];
#endif // RETRANS_FRE_LOG
	ULONG Retranmits;
	ULONG PacketLoss;
	
	// Clear packet loss count not make read-error during writing cause changes read-split.
	PacketLoss = InterlockedExchange(&LanScsiSession->TransStat.PacketLoss, 0);
//	LanScsiSession->TransStat.PacketLoss = 0; 
	if (PacketLoss) {
		KDPrintM(DBG_LURN_ERROR, ("Cleared packet loss count\n"));
	}
	
	Retranmits = InterlockedExchange(&LanScsiSession->TransStat.Retransmits, 0);
	if(Retranmits > 0)
	{
		LanScsiSession->NrWritesAfterLastRetransmits = 0;
		LanScsiSession->WriteSplitSize = 
			(LanScsiSession->WriteSplitSize >= 128) ? 64 :
			(LanScsiSession->WriteSplitSize >= 64) ? 43 :
			(LanScsiSession->WriteSplitSize >= 43) ? 32 :
			(LanScsiSession->WriteSplitSize >= 32) ? 26 :
			(LanScsiSession->WriteSplitSize >= 26) ? 22 :
			(LanScsiSession->WriteSplitSize >= 22) ? 19 :
			(LanScsiSession->WriteSplitSize >= 19) ? 16 :
			(LanScsiSession->WriteSplitSize >= 16) ? 13 :
			(LanScsiSession->WriteSplitSize >= 13) ? 11 :
			(LanScsiSession->WriteSplitSize >= 11) ? 10 :
			(LanScsiSession->WriteSplitSize >= 10) ? 8 :
			(LanScsiSession->WriteSplitSize >= 8) ? 6 :
			(LanScsiSession->WriteSplitSize >= 6) ? 4 : 2;
		KDPrintM(DBG_LURN_ERROR, ("Retransmit detected(%p). down to %d\n", LanScsiSession, LanScsiSession->WriteSplitSize));
#ifdef RETRANS_FRE_LOG
		_snprintf(buffer, 257, "[%s:%04d] Retransmit detected(%p). down to %d\n", __MODULE__, __LINE__, LanScsiSession, LanScsiSession->WriteSplitSize);
		DbgPrint(buffer);
#endif // RETRANS_FRE_LOG
	}
	else
	{
		LanScsiSession->NrWritesAfterLastRetransmits++;
		if(LanScsiSession->NrWritesAfterLastRetransmits > LanScsiSession->WriteSplitSize * 8)
		{
			LanScsiSession->NrWritesAfterLastRetransmits = 0;
	
			if(LanScsiSession->WriteSplitSize < LanScsiSession->MaxBlocksPerRequest)
			{
				LanScsiSession->WriteSplitSize =
					(LanScsiSession->WriteSplitSize >= 64) ? 128 :
					(LanScsiSession->WriteSplitSize >= 43) ? 64 :
					(LanScsiSession->WriteSplitSize >= 32) ? 43 :
					(LanScsiSession->WriteSplitSize >= 26) ? 32 :
					(LanScsiSession->WriteSplitSize >= 22) ? 26 :
					(LanScsiSession->WriteSplitSize >= 19) ? 22 :
					(LanScsiSession->WriteSplitSize >= 16) ? 19 :
					(LanScsiSession->WriteSplitSize >= 13) ? 16 :
					(LanScsiSession->WriteSplitSize >= 11) ? 13 :
					(LanScsiSession->WriteSplitSize >= 10) ? 11 :
					(LanScsiSession->WriteSplitSize >= 8) ? 10 :
					(LanScsiSession->WriteSplitSize >= 6) ? 8 : 6;

				if(LanScsiSession->WriteSplitSize > LanScsiSession->MaxBlocksPerRequest)
					LanScsiSession->WriteSplitSize = LanScsiSession->MaxBlocksPerRequest;

//                              DbgPrint("Write split up to %d\n", LanScsiSession->WriteSplitSize);			// temp message
				KDPrintM(DBG_LURN_ERROR, ("Stable status(%p). up to %d\n", LanScsiSession, LanScsiSession->WriteSplitSize));
#ifdef RETRANS_FRE_LOG
				_snprintf(buffer, 257, "[%s:%04d] Stable status(%p). up to %d\n", __MODULE__, __LINE__, LanScsiSession, LanScsiSession->WriteSplitSize);
				DbgPrint(buffer);
#endif // RETRANS_FRE_LOG
			}
		}
	}
	ASSERT(LanScsiSession->WriteSplitSize <= 128);
	if (LanScsiSession->WriteSplitSize > 128) { // Safeguard for ndas service passed invalid value.
		LanScsiSession->WriteSplitSize = 128;
	}

	return LanScsiSession->WriteSplitSize;
}


#define MINUMUN_READ_SPILIC_SIZE 8 // 4kbytes
	
static 
LONG
LurnIdeDiskAdjustReadSplit(
	PLANSCSI_SESSION LanScsiSession)
{
	ULONG PacketLoss;
	ULONG Retransmits;
	// Clear retransmit not to make retransmit during reading cause changes write-split.
	Retransmits = InterlockedExchange(&LanScsiSession->TransStat.Retransmits, 0);
	if (Retransmits) {
		KDPrintM(DBG_LURN_ERROR, ("Cleared retrasmits count\n"));
	}
	
	PacketLoss = InterlockedExchange(&LanScsiSession->TransStat.PacketLoss, 0);
	if(PacketLoss > 0)
	{
		LanScsiSession->NrReadAfterLastPacketLoss = 0;
		LanScsiSession->ReadSplitSize = LanScsiSession->ReadSplitSize * 3 /4;
		if (LanScsiSession->ReadSplitSize < MINUMUN_READ_SPILIC_SIZE)
		    LanScsiSession->ReadSplitSize =MINUMUN_READ_SPILIC_SIZE;

		KDPrintM(DBG_LURN_ERROR, ("Packet loss detected(%p). Read block size is downed to %d\n", LanScsiSession, LanScsiSession->ReadSplitSize));
	}
	else
	{
		LanScsiSession->NrReadAfterLastPacketLoss++;
		if(LanScsiSession->NrReadAfterLastPacketLoss > LanScsiSession->ReadSplitSize * 8) 
		{

			LanScsiSession->NrReadAfterLastPacketLoss = 0;

			if(LanScsiSession->ReadSplitSize < LanScsiSession->MaxBlocksPerRequest)
			{
				LanScsiSession->ReadSplitSize++;

				if(LanScsiSession->ReadSplitSize > LanScsiSession->MaxBlocksPerRequest)
					LanScsiSession->ReadSplitSize = LanScsiSession->MaxBlocksPerRequest;
				KDPrintM(DBG_LURN_ERROR, ("Stable status(%p). Increased read block size to %d\n", LanScsiSession, LanScsiSession->ReadSplitSize));
			}
		}
	}
	ASSERT(LanScsiSession->ReadSplitSize <= 128);
	if (LanScsiSession->ReadSplitSize > 128) { // Safeguard for ndas service passed invalid value.
		LanScsiSession->ReadSplitSize = 128;
	}
	return LanScsiSession->ReadSplitSize;
}

static
LONG
LurnIdeDiskGetReadSplit(
	PLANSCSI_SESSION LanScsiSession)
{
//	return LanScsiSession->MaxBlocksPerRequest; // temp
	return LanScsiSession->ReadSplitSize;
}

#ifdef __DIVIDED_MULTI_BOOT__

#define MAX_PARTITION 4

extern PUCHAR BootParameters;
BYTE HostID[MAX_HOST_ID_LEN] = "HOST1";

extern PUCHAR tstrstr(PUCHAR str, PUCHAR ptn);
extern PUCHAR tstrchr(PUCHAR str, UCHAR c);

UCHAR *ParseHostID(PUCHAR BootOpstions, PUCHAR HostID)
{
	PUCHAR pCurrent = BootOpstions;
	int i = 0;
	UCHAR s = 0;	

	if( (pCurrent = tstrstr(pCurrent, "HOSTID")) != NULL ) {
		if( (pCurrent = tstrchr(pCurrent, '=')) != NULL ) {
			pCurrent ++; // skip '='
			while(i<MAX_HOST_ID_LEN) {				
				if(*pCurrent == 0 || *pCurrent == ' ') break;
				*HostID = *pCurrent;
				HostID ++;
				pCurrent ++;
			}
		}
	}
	HostID++;
	*HostID = 0;
	
	return pCurrent;
}

/* HBA (virtual) geometry for the drive */
ULONG	hba_Numcyl;		/* number of cylinders */
ULONG	hba_heads;		/* number of heads */
ULONG	hba_sectors;		/* number of sectors per track */

#define	MAX_SECT	(63)
#define	MAX_CYL		(1022)
#define	MAX_HEAD	(254)

void
Set_CHS_Values(
	IN	PPARTITION				Part
			 )
{
 	ULONG	lba, cy, hd, sc;
 
	lba = (ULONG)Part->Relsect;
  	if (lba >= hba_heads * hba_sectors * MAX_CYL) {
  		/*
 		 * the lba address cannot be expressed in CHS value
 		 * so store the maximum CHS field values in the CHS fields.
 		 */
 		cy = MAX_CYL + 1;
  		hd = MAX_HEAD;
  		sc = MAX_SECT;
  	} else {
 		cy = lba / hba_sectors / hba_heads;
 		hd = lba / hba_sectors % hba_heads;
 		sc = lba % hba_sectors + 1;
	}

	Part->Begcyl = (UCHAR)(cy & 0xff);
	Part->Beghead = (UCHAR)hd;
	Part->Begsect = (UCHAR)(((cy >> 2) & 0xc0) | sc);
 
 	/*
 	 * This code is identical to the code above
 	 * except that it works on ending CHS values
 	 */
	lba = (ULONG)(Part->Relsect + Part->Numsect - 1);
 	if (lba >= hba_heads * hba_sectors * MAX_CYL) {
 		cy = MAX_CYL + 1;
 		hd = MAX_HEAD;
 		sc = MAX_SECT;
 	} else {
 		cy = lba / hba_sectors / hba_heads;
 		hd = lba / hba_sectors % hba_heads;
 		sc = lba % hba_sectors + 1;
 	}
	Part->Endcyl = (UCHAR)(cy & 0xff);
	Part->Endhead = (UCHAR)hd;
	Part->Endsect = (UCHAR)(((cy >> 2) & 0xc0) | sc);
}

BOOLEAN
MultiBootCheckPartition(
		IN	PNDAS_DMBI_INFORMATION	pDMBI,
		IN	PPARTITION				Part
		)
{
	ULONG i, j;
	PHOST_DISK_LOCATION Host = pDMBI->Host;
	ULONG	StartSect, NumOfSect, OffSect, NameLen;

	OffSect = 0;
	for(i=0; i< pDMBI->HostCount; i++) {
		if(Host[i].Valid) {
			if(Host[i].Sectors & 0x3F) return FALSE;

			if(Host[i].Shared) {
				StartSect = (ULONG)Host[i].Start;
				NumOfSect = (ULONG)Host[i].Sectors;
				break;
			}
			NameLen = strlen(HostID);
			if(RtlCompareMemory(HostID, pDMBI->Host[i].HostID, NameLen) == NameLen) {
				OffSect += (ULONG)Host[i].Sectors;
			}
		}
	}

	if(i == pDMBI->HostCount) return TRUE;

	for(i=0; i<MAX_PARTITION; i++) {
		if(Part[i].Numsect & 0x3F) return FALSE;
		for(j=i+1; j<MAX_PARTITION; j++) {
			if(Part[i].Relsect < Part[j].Relsect) {
				if((Part[i].Relsect + Part[i].Numsect) > Part[j].Relsect) {
					return FALSE;
				}
			} else {
				if((Part[j].Relsect + Part[j].Numsect) > Part[i].Relsect) {
					return FALSE;
				}
			}					
		}
	}

	NbDebugPrint(0, ("MultiBootCheckPartition: Boundary Check Complete !!\n"));

	for(i=0; i<MAX_PARTITION; i++) {
		if((Part[i].Relsect == OffSect) &&	(Part[i].Numsect == NumOfSect)) {
			break;
		}
	}
	if(i==MAX_PARTITION) return FALSE;

	NbDebugPrint(0, ("MultiBootCheckPartition: Boundary Check2 Complete !!\n"));

	return TRUE;

}


ULONG MultiBootUpdatePartition(
		IN PNDAS_DMBI_INFORMATION	pDMBI,		
		IN OUT PPARTITION			Part
		)
{
	ULONG i;
	PHOST_DISK_LOCATION Host = pDMBI->Host;
	ULONG OffSect;
	ULONG PartIndex;
	ULONG NameLen;


	KDPrintM(DBG_LURN_INFO, ("MultiBootUpdatePartition Entered\n"));

	OffSect = 0;
	PartIndex = 0;

	for(i=0; i<MAX_PARTITION; i++) {
		RtlZeroMemory(&Part[i], sizeof(PARTITION));
	}

	for(i=0; i<MAX_HOST_ID; i++) {
		NameLen = strlen(HostID);
		if(Host[i].Valid && 
			(Host[i].Shared || (RtlCompareMemory(HostID, pDMBI->Host[i].HostID, NameLen) == NameLen))) {			
			
			if(PartIndex == 0) {
				Part[PartIndex].Bootid = 0x80;
				Part[PartIndex].Relsect = (ULONG) OffSect + 1;
				Part[PartIndex].Numsect = (ULONG) (pDMBI->Host[i].Sectors - 1);
			}
			else {
				Part[PartIndex].Bootid = 0x00;
				Part[PartIndex].Relsect = (ULONG) OffSect;
				Part[PartIndex].Numsect = (ULONG) pDMBI->Host[i].Sectors;
			}

			Set_CHS_Values(&Part[PartIndex]);

			if(Host[i].Shared) {
				Part[PartIndex].Systid = 0x0C;	// FAT32 partition
			}
			else {
				Part[PartIndex].Systid = 0x07;	// NTFS/HPFS partition
			}

			OffSect += (ULONG)Host[i].Sectors;
			PartIndex ++;

			KDPrintM(DBG_LURN_INFO, ("OffSect = %08X, PartIndex = %02X\n", OffSect, PartIndex));
		}
	}

	return OffSect;
}

ULONG MultiBootGetDiskSize(
	  	IN	PNDAS_DMBI_INFORMATION	pDMBI		
	   )
{
	ULONG i;
	PHOST_DISK_LOCATION Host = pDMBI->Host;
	ULONG	OffSect;
	ULONG	NameLen;

	OffSect = 0;
	for(i=0; i<MAX_HOST_ID; i++) {
		NameLen = strlen(HostID);
		if(Host[i].Valid && 
			(Host[i].Shared || (RtlCompareMemory(HostID, pDMBI->Host[i].HostID, NameLen) == NameLen))) {			
			OffSect += (ULONG)Host[i].Sectors;
		}
	}

	return OffSect;
}

#endif __DIVIDED_MULTI_BOOT__


//
//	confiure IDE disk.
//
static
NTSTATUS
LurnIdeDiskConfigure(
		IN	PLURELATION_NODE	Lurn,
		OUT	PBYTE				PduResponse
	)
{
	struct hd_driveid	info;
	char				buffer[41];
	NTSTATUS			ntStatus;
	LANSCSI_PDUDESC		PduDesc;
	PLANSCSI_SESSION	LSS;
	PLURNEXT_IDE_DEVICE	IdeDisk;
	BOOLEAN				SetDmaMode;
	LONG				retryRequest;

	//
	// Init.
	//
	ASSERT(Lurn->LurnExtension);

	IdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;
	LSS = &IdeDisk->LanScsiSession;
	IdeDisk->LuData.PduDescFlags &= ~(PDUDESC_FLAG_LBA|PDUDESC_FLAG_LBA48|PDUDESC_FLAG_PIO|PDUDESC_FLAG_DMA|PDUDESC_FLAG_UDMA);

	KDPrintM(DBG_LURN_INFO, ("Target ID %d\n", IdeDisk->LuData.LanscsiTargetID));

	// identify.

	for(retryRequest = 0; retryRequest < 2; retryRequest ++) {
		LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &PduDesc, IDE_COMMAND, WIN_IDENTIFY, 0, 1, &info);
		ntStatus = LspRequest(LSS, &PduDesc, PduResponse);
		if(NT_SUCCESS(ntStatus) && *PduResponse == LANSCSI_RESPONSE_SUCCESS) {
			break;
		}
		KDPrintM(DBG_LURN_ERROR, ("Identify Failed...\n"));
	}
	if(!NT_SUCCESS(ntStatus) || *PduResponse != LANSCSI_RESPONSE_SUCCESS) {
		KDPrintM(DBG_LURN_ERROR, ("Give up..\n"));
		return ntStatus;
	}


	//
	// DMA/PIO Mode.
	//
	KDPrintM(DBG_LURN_INFO, ("Major 0x%x, Minor 0x%x, Capa 0x%x\n",
							info.major_rev_num,
							info.minor_rev_num,
							info.capability));
	KDPrintM(DBG_LURN_INFO, ("DMA 0x%x, U-DMA 0x%x\n",
							info.dma_mword,
							info.dma_ultra));

	//
	//	determine IO mode ( UltraDMA, DMA, and PIO ) according to hardware versions and disk capacity.
	//
	SetDmaMode = FALSE;

	do {
		BOOLEAN	disableUDMA;
		UCHAR	DmaFeature;
		UCHAR	DmaMode;

		DmaFeature = 0;
		DmaMode = 0;


		//
		// Check UDMA option
		//

		if(Lurn->UDMARestrictValid &&
			Lurn->UDMARestrict == 0xff) {

			disableUDMA = TRUE;
		} else {
			disableUDMA = FALSE;
		}

		//
		// Ultra DMA if NDAS chip is 2.0 or higher.
		//
		if(IdeDisk->HwVersion >= LANSCSIIDE_VERSION_2_0 &&
			(info.dma_ultra & 0x00ff) &&
			disableUDMA == FALSE) {
			// Find Fastest Mode.
			if(info.dma_ultra & 0x0001)
				DmaMode = 0;
			if(info.dma_ultra & 0x0002)
				DmaMode = 1;
			if(info.dma_ultra & 0x0004)
				DmaMode = 2;
			//	if Cable80, try higher Ultra Dma Mode.
#ifdef __DETECT_CABLE80__
			if(info.hw_config & 0x2000) {
#endif
				if(info.dma_ultra & 0x0008)
					DmaMode = 3;
				if(info.dma_ultra & 0x0010)
					DmaMode = 4;
				if (IdeDisk->HwVersion != LANSCSIIDE_VERSION_2_0) {
					// Limit UDMA mode to 4 only to reduce write-failure.
					if(info.dma_ultra & 0x0020)
						DmaMode = 5;
					if(info.dma_ultra & 0x0040)
						DmaMode = 6;
					if(info.dma_ultra & 0x0080)
						DmaMode = 7;
				}
				//
				//	GIGA NDAS board does not seem to be compatible with
				//		NRAID's UDMA mode.
				//

				if(Lurn->UDMARestrictValid) {
					if(DmaMode > Lurn->UDMARestrict) {
						DmaMode = Lurn->UDMARestrict;
						KDPrintM(DBG_LURN_INFO, ("UDMA restriction applied. UDMA=%d\n", (ULONG)DmaMode));
					}
				}

#ifdef __DETECT_CABLE80__
			}
#endif
			KDPrintM(DBG_LURN_INFO, ("Ultra DMA %d detected.\n", (int)DmaMode));
			DmaFeature = DmaMode | 0x40;	// Ultra DMA mode.
			IdeDisk->LuData.PduDescFlags |= PDUDESC_FLAG_DMA|PDUDESC_FLAG_UDMA;

#if 0
			// Set Ultra DMA mode if needed
			if(!(info.dma_ultra & (0x0100 << DmaMode))) {
				SetDmaMode = TRUE;
			}
#else
			// Always set DMA mode because NDAS chip and HDD may have different DMA setting.
			SetDmaMode = TRUE;
#endif

		//
		//	DMA
		//
		} else if(info.dma_mword & 0x00ff) {
			if(info.dma_mword & 0x0001)
				DmaMode = 0;
			if(info.dma_mword & 0x0002)
				DmaMode = 1;
			if(info.dma_mword & 0x0004)
				DmaMode = 2;

			KDPrintM(DBG_LURN_INFO, ("DMA mode %d detected.\n", (int)DmaMode));
			DmaFeature = DmaMode | 0x20;
			IdeDisk->LuData.PduDescFlags |= PDUDESC_FLAG_DMA;

#if 0
			// Set DMA mode if needed
			if(!(info.dma_mword & (0x0100 << DmaMode))) {
				SetDmaMode = TRUE;
			}
#else
			// Always set DMA mode because NDAS chip and HDD may have different DMA setting.
			SetDmaMode = TRUE;
#endif
		}

		// Set DMA mode if needed.
		if(SetDmaMode) {
			LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &PduDesc, IDE_COMMAND, WIN_SETFEATURES, 0, 0, NULL);
			PduDesc.Feature = 0x03;
			PduDesc.Param8[0] = DmaFeature;
			ntStatus = LspRequest(LSS, &PduDesc, PduResponse);
			if(!NT_SUCCESS(ntStatus) || *PduResponse != LANSCSI_RESPONSE_SUCCESS) {
				KDPrintM(DBG_LURN_ERROR, ("Set Feature Failed...\n"));
				return ntStatus;
			}

			// identify.
			LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &PduDesc, IDE_COMMAND, WIN_IDENTIFY, 0, 1, &info);
			ntStatus = LspRequest(LSS, &PduDesc, PduResponse);
			if(!NT_SUCCESS(ntStatus) || *PduResponse != LANSCSI_RESPONSE_SUCCESS) {
				KDPrintM(DBG_LURN_ERROR, ("Identify Failed...\n"));
				return ntStatus;
			}
			KDPrintM(DBG_LURN_INFO, ("After Set Feature DMA 0x%x, U-DMA 0x%x\n",
							info.dma_mword,
							info.dma_ultra));
		}
		if(IdeDisk->LuData.PduDescFlags & PDUDESC_FLAG_DMA) {
			break;
		}
		//
		//	PIO.
		//
		KDPrintM(DBG_LURN_ERROR, ("NetDisk does not support DMA mode. Turn to PIO mode.\n"));
		IdeDisk->LuData.PduDescFlags |= PDUDESC_FLAG_PIO;

	} while(0);


	//
	//	Product strings.
	//
	ConvertString((PCHAR)buffer, (PCHAR)info.serial_no, 20);
	RtlCopyMemory(IdeDisk->LuData.Serial, buffer, 20);
	KDPrintM(DBG_LURN_INFO, ("Serial No: %s\n", buffer));

	ConvertString((PCHAR)buffer, (PCHAR)info.fw_rev, 8);
	RtlCopyMemory(IdeDisk->LuData.FW_Rev, buffer, 8);
	KDPrintM(DBG_LURN_INFO, ("Firmware rev: %s\n", buffer));

	ConvertString((PCHAR)buffer, (PCHAR)info.model, 40);
	RtlCopyMemory(IdeDisk->LuData.Model, buffer, 40);
	KDPrintM(DBG_LURN_INFO, ("Model No: %s\n", buffer));

	//
	// Support LBA?
	//
	if(!(info.capability & 0x02)) {
		IdeDisk->LuData.PduDescFlags &= ~PDUDESC_FLAG_LBA;
		ASSERT(FALSE);
	} else {
		IdeDisk->LuData.PduDescFlags |= PDUDESC_FLAG_LBA;
	}

	//
	// Support LBA48.
	// Calc Capacity.
	//
	if((info.command_set_2 & 0x0400) && (info.cfs_enable_2 & 0x0400)) {	// Support LBA48bit
		IdeDisk->LuData.PduDescFlags |= PDUDESC_FLAG_LBA48;
		IdeDisk->LuData.SectorCount = info.lba_capacity_2;
	} else {
		IdeDisk->LuData.PduDescFlags &= ~PDUDESC_FLAG_LBA48;

		if((info.capability & 0x02) && Lba_capacity_is_ok(&info)) {
			IdeDisk->LuData.SectorCount = info.lba_capacity;
		} else
			IdeDisk->LuData.SectorCount = info.cyls * info.heads * info.sectors;
	}

	KDPrintM(DBG_LURN_INFO, ("LBA %d, LBA48 %d, Number of Sectors: %I64d\n",
				(IdeDisk->LuData.PduDescFlags & PDUDESC_FLAG_LBA) != 0,
				(IdeDisk->LuData.PduDescFlags & PDUDESC_FLAG_LBA48) != 0,
				IdeDisk->LuData.SectorCount
		));

#ifdef __DIVIDED_MULTI_BOOT__

#define KB_SECTORS	(1024 / BLOCK_SIZE)
#define MB_SECTORS	(1024 * 1024 / BLOCK_SIZE)
#define GB_SECTORS	(1024 * 1024 * 1024 / BLOCK_SIZE) 

#define MAX_RETRY_COUNT	5


	if (info.cur_cyls == 0 ||
		info.cur_heads == 0 ||
    	info.cur_sectors == 0 ||
    	info.cur_cyls > MAX_CYL ||
    	info.cur_heads > MAX_HEAD ||
    	info.cur_sectors > MAX_SECT) {    
    		hba_sectors	= MAX_SECT;
    		hba_heads	= MAX_HEAD + 1;
    		hba_Numcyl	= (info.cur_cyls * info.cur_heads * info.cur_sectors) / (hba_sectors * hba_heads);
    }
	else {
		/* save virtual geometry values obtained by ioctl */
    	hba_Numcyl = info.cur_cyls;
    	hba_heads = info.cur_heads;
    	hba_sectors = info.cur_sectors;
	}

	if(IdeDisk->HwVersion == LANSCSIIDE_VERSION_1_1 || IdeDisk->HwVersion == LANSCSIIDE_VERSION_2_0) {
		ULONG				logicalBlockAddress;
		USHORT				transferBlocks;
		BYTE				response;		
		LANSCSI_PDUDESC		PduDesc;
		BYTE				OneSector[BLOCK_SIZE];
		PNDAS_DMBI_INFORMATION pDMBI = (PNDAS_DMBI_INFORMATION)IdeDisk->LuData.OneSector;
		PMASTER_BOOT_RECORD	pMBR;
		PPARTITION			Part;
		BOOLEAN				Valid;
		ULONG				i ;
		ULONG				RetryCount = 0;
		ULONG				NameLen = 0;

		KDPrintM(DBG_LURN_INFO, ("Number of Sectors = %08X\n", IdeDisk->LuData.SectorCount));

		ParseHostID(BootParameters, HostID);
		KDPrintM(DBG_LURN_INFO, ("HostID = %s\n", HostID));

/* Write Default */
/*
		RtlZeroMemory(pDMBI, BLOCK_SIZE);
		pDMBI->HostCount = 6;
		pDMBI->MajorVersion = NDAS_DMBI_VERSION_MAJOR;
		pDMBI->MajorVersion = NDAS_DMBI_VERSION_MINOR;
		pDMBI->Signature = NDAS_DMBI_SIGNATURE;

		pDMBI->Host[0].Valid = TRUE;
		pDMBI->Host[0].Shared = FALSE;
		NameLen = strlen("HOST1");
		RtlCopyMemory(pDMBI->Host[0].HostID, "HOST1", NameLen+1);
		pDMBI->Host[0].Start = 0;
		pDMBI->Host[0].Sectors = 10 * GB_SECTORS;

		pDMBI->Host[1].Valid = TRUE;
		pDMBI->Host[1].Shared = FALSE;
		NameLen = strlen("HOST2");
		RtlCopyMemory(pDMBI->Host[1].HostID, "HOST2", NameLen+1);
		pDMBI->Host[1].Start = 10 * GB_SECTORS;
		pDMBI->Host[1].Sectors = 10 * GB_SECTORS;

		pDMBI->Host[2].Valid = TRUE;
		pDMBI->Host[2].Shared = FALSE;
		NameLen = strlen("HOST3");
		RtlCopyMemory(pDMBI->Host[2].HostID, "HOST3", NameLen+1);
		pDMBI->Host[2].Start = 20 * GB_SECTORS;
		pDMBI->Host[2].Sectors = 10 * GB_SECTORS;

		pDMBI->Host[3].Valid = TRUE;
		pDMBI->Host[3].Shared = FALSE;
		NameLen = strlen("HOST4");
		RtlCopyMemory(pDMBI->Host[3].HostID, "HOST4", NameLen+1);
		pDMBI->Host[3].Start = 30 * GB_SECTORS;
		pDMBI->Host[3].Sectors = 10 * GB_SECTORS;

		pDMBI->Host[4].Valid = TRUE;
		pDMBI->Host[4].Shared = FALSE;
		NameLen = strlen("HOST5");
		RtlCopyMemory(pDMBI->Host[4].HostID, "HOST5", NameLen+1);		
		pDMBI->Host[4].Start = 40 * GB_SECTORS;
		pDMBI->Host[4].Sectors = 10 * GB_SECTORS;

		pDMBI->Host[5].Valid = TRUE;
		pDMBI->Host[5].Shared = TRUE;
		RtlCopyMemory(pDMBI->Host[5].HostID, "", 1);		
		pDMBI->Host[5].Start = 100 * GB_SECTORS;
		pDMBI->Host[5].Sectors = 50 * GB_SECTORS;

		logicalBlockAddress = (ULONG)(IdeDisk->LuData.SectorCount + NDAS_BLOCK_LOCATION_DMBI);
		transferBlocks = 1;
		LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &PduDesc, IDE_COMMAND, WIN_WRITE, logicalBlockAddress, transferBlocks, pDMBI);
		ntStatus = LspRequest(
						&IdeDisk->LanScsiSession,
						&PduDesc,
						&response
					);

		if(!NT_SUCCESS(ntStatus) || response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR,
				("WIN_WRITE Error: logicalBlockAddress = %x, transferBlocks = %x\n",
				logicalBlockAddress, transferBlocks));
			return STATUS_UNSUCCESSFUL;
		} else {
			KDPrintM(DBG_LURN_INFO,
				("WIN_WRITE : read a sector to logicalBlockAddress = %x, transferBlocks = %x\n",
				logicalBlockAddress, transferBlocks));
		}		

		KDPrintM(DBG_LURN_INFO, ("Divided Multiboot Information Block Write Complete !!\n"));
		while(1);
*/
	/* Write Default End */
/*
		for(i=0; i<pDMBI->HostCount; i++ ) {		
			NameLen = strlen(HostID);
			if(pDMBI->Host[i].Valid && 
				(pDMBI->Host[i].Shared || RtlCompareMemory(HostID, pDMBI->Host[i].HostID, NameLen) == NameLen)) {
				break;
			}			
		}

		RtlZeroMemory(OneSector, BLOCK_SIZE);
		logicalBlockAddress = (ULONG)pDMBI->Host[i].Start;
		transferBlocks = 1;
		LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &PduDesc, IDE_COMMAND, WIN_READ, logicalBlockAddress, transferBlocks, OneSector);
		ntStatus = LspRequest(
						&IdeDisk->LanScsiSession,
						&PduDesc,
						&response
					);

		if(!NT_SUCCESS(ntStatus) || response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR,
				("WIN_READ Error: logicalBlockAddress = %x, transferBlocks = %x\n",
				logicalBlockAddress, transferBlocks));
			return STATUS_UNSUCCESSFUL;
		} else {
			KDPrintM(DBG_LURN_INFO,
				("WIN_READ : read a sector to logicalBlockAddress = %x, transferBlocks = %x\n",
				logicalBlockAddress, transferBlocks));
		}

		KDPrintM(DBG_LURN_INFO, ("MBR size = %d\n", sizeof(MASTER_BOOT_RECORD)));
		pMBR = (PMASTER_BOOT_RECORD) &OneSector[0];
		Part = (PPARTITION) &pMBR->Parts[0];

		Part[3].Begcyl = 0xFF;
		Part[3].Beghead = 0x00;
		Part[3].Begsect = 0xC1;
		Part[3].Bootid = 0x00;
		Part[3].Endcyl = 0xFF;
		Part[3].Endhead = 0x00;
		Part[3].Endsect = 0xC1;
		Part[3].Relsect = 12 * GB_SECTORS;
		Part[3].Numsect = 20 * GB_SECTORS;
		Part[3].Systid = 0x00;

		logicalBlockAddress = (ULONG)pDMBI->Host[i].Start;
		transferBlocks = 1;
		LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &PduDesc, IDE_COMMAND, WIN_WRITE, logicalBlockAddress, transferBlocks, OneSector);
		ntStatus = LspRequest(
						&IdeDisk->LanScsiSession,
						&PduDesc,
						&response
					);

		if(!NT_SUCCESS(ntStatus) || response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR,
				("WIN_WRITE Error: logicalBlockAddress = %x, transferBlocks = %x\n",
				logicalBlockAddress, transferBlocks));
			return STATUS_UNSUCCESSFUL;
		} else {
			KDPrintM(DBG_LURN_INFO,
				("WIN_WRITE : read a sector to logicalBlockAddress = %x, transferBlocks = %x\n",
				logicalBlockAddress, transferBlocks));
		}

*/

retry:
		RtlZeroMemory(pDMBI, BLOCK_SIZE);
		logicalBlockAddress = (ULONG)(IdeDisk->LuData.SectorCount + NDAS_BLOCK_LOCATION_DMBI);
		transferBlocks = 1;
		LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &PduDesc, IDE_COMMAND, WIN_READ, logicalBlockAddress, transferBlocks, pDMBI);
		ntStatus = LspRequest(
						&IdeDisk->LanScsiSession,
						&PduDesc,
						&response
					);

		if(!NT_SUCCESS(ntStatus) || response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR,
				("WIN_READ Error: logicalBlockAddress = %x, transferBlocks = %x\n",
				logicalBlockAddress, transferBlocks));
			if(RetryCount < MAX_RETRY_COUNT) {
				RetryCount ++;
				goto retry;
			}
			return STATUS_UNSUCCESSFUL;
		} else {
			KDPrintM(DBG_LURN_INFO,
				("WIN_READ : read a sector to logicalBlockAddress = %x, transferBlocks = %x\n",
				logicalBlockAddress, transferBlocks));
		}

		KDPrintM(DBG_LURN_INFO, ("Signature = %I64X\n", pDMBI->Signature));		
		if(pDMBI->Signature == NDAS_DMBI_SIGNATURE) 
		{			
			for(i=0; i<pDMBI->HostCount; i++ ) {
				KDPrintM(DBG_LURN_INFO, ("Host[%d]:Valid = %d, Shared = %d, HostID = %s, Start = %I64X, Sectors = %I64X\n", 
					i, pDMBI->Host[i].Valid, pDMBI->Host[i].Shared, pDMBI->Host[i].HostID, pDMBI->Host[i].Start, pDMBI->Host[i].Sectors));		
			}

			for(i=0; i<pDMBI->HostCount; i++ ) {		
				NameLen = strlen(HostID);
				if(pDMBI->Host[i].Valid && 
					(pDMBI->Host[i].Shared || RtlCompareMemory(HostID, pDMBI->Host[i].HostID, NameLen) == NameLen)) {
					break;
				}			
			}

			if(i == pDMBI->HostCount) {
				KDPrintM(DBG_LURN_ERROR, ("%s didn't have any valid space !!\n", pDMBI->Signature));	
				return STATUS_UNSUCCESSFUL;
			}

			RtlZeroMemory(OneSector, BLOCK_SIZE);			
			logicalBlockAddress = (ULONG)pDMBI->Host[i].Start;
			transferBlocks = 1;

			LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &PduDesc, IDE_COMMAND, WIN_READ, logicalBlockAddress, transferBlocks, OneSector);
			ntStatus = LspRequest(
							&IdeDisk->LanScsiSession,
							&PduDesc,
							&response
						);

			if(!NT_SUCCESS(ntStatus) || response != LANSCSI_RESPONSE_SUCCESS) {
				KDPrintM(DBG_LURN_ERROR,
					("WIN_READ Error: logicalBlockAddress = %x, transferBlocks = %x\n",
					logicalBlockAddress, transferBlocks));
				return STATUS_UNSUCCESSFUL;
			} else {
				KDPrintM(DBG_LURN_INFO,
					("WIN_READ : read a sector to logicalBlockAddress = %x, transferBlocks = %x\n",
					logicalBlockAddress, transferBlocks));
			}

			KDPrintM(DBG_LURN_INFO, ("C/H/S = %04X/%04X/%04X\n", info.cyls, info.heads, info.sectors));
			KDPrintM(DBG_LURN_INFO, ("current C/H/S = %04X/%04X/%04X\n", info.cur_cyls, info.cur_heads, info.cur_sectors));

#ifdef __ENABLE_LOADER__
			UNREFERENCED_PARAMETER(pMBR);
			UNREFERENCED_PARAMETER(Part);
			UNREFERENCED_PARAMETER(Valid);
			IdeDisk->LuData.SectorCount = MultiBootGetDiskSize(pDMBI);
#else 
			pMBR = (PMASTER_BOOT_RECORD) &OneSector[0];
			Part = (PPARTITION) &pMBR->Parts[0];

			pMBR->Signature = 0xAA55;
			Valid = MultiBootCheckPartition(pDMBI, Part);
			if(FALSE == Valid) {
				IdeDisk->LuData.SectorCount = MultiBootUpdatePartition(pDMBI, Part);
			}
			else {
				IdeDisk->LuData.SectorCount = MultiBootGetDiskSize(pDMBI);
			}

			KDPrintM(DBG_LURN_INFO, ("Number of Sectors = %08X\n", IdeDisk->LuData.SectorCount));

			for(i=0;i<4;i++) {
				KDPrintM(DBG_LURN_INFO, ("Start(%02X, %02X, %02X), Bootid:%02X, End(%02X, %02X, %02X), NumSect:%08X, Relsect:%08X, SysID:%02X\n", 
					Part[i].Begcyl, Part[i].Beghead, Part[i].Begsect, 
					Part[i].Bootid, 
					Part[i].Endcyl, Part[i].Endhead, Part[i].Endsect, 
					Part[i].Numsect, Part[i].Relsect, Part[i].Systid));			
			}	

			KDPrintM(DBG_LURN_INFO, ("Added Shared Partition\n"));
			
			for(i=0;i<4;i++) {
				KDPrintM(DBG_LURN_INFO, ("Start(%02X, %02X, %02X), Bootid:%02X, End(%02X, %02X, %02X), NumSect:%08X, Reselect:%08X, SysID:%02X\n", 
					Part[i].Begcyl, Part[i].Beghead, Part[i].Begsect, 
					Part[i].Bootid, 
					Part[i].Endcyl, Part[i].Endhead, Part[i].Endsect, 
					Part[i].Numsect, Part[i].Relsect, Part[i].Systid));
			}

			do {
				UINT64	Parameter = 0;
				UINT64	Result = 0;

				ntStatus = LsuSetSemaphore(&IdeDisk->LanScsiSession, Parameter, &Result);
	//			KDPrintM(DBG_LURN_INFO, ("Set Semaphore: ntStaus = %08X, Parameter = %I64X, Result = %I64X\n", ntStatus, Parameter, Result));						
			} while(ntStatus == STATUS_DEVICE_BUSY);

			LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &PduDesc, IDE_COMMAND, WIN_WRITE, logicalBlockAddress, transferBlocks, OneSector);
			ntStatus = LspRequest(
							&IdeDisk->LanScsiSession,
							&PduDesc,
							&response
						);

			if(!NT_SUCCESS(ntStatus) || response != LANSCSI_RESPONSE_SUCCESS) {
				KDPrintM(DBG_LURN_ERROR,
					("WIN_WRITE Error: logicalBlockAddress = %x, transferBlocks = %x\n",
					logicalBlockAddress, transferBlocks));
				return STATUS_UNSUCCESSFUL;
			} else {
				KDPrintM(DBG_LURN_INFO,
					("WIN_WRITE : write a sector to logicalBlockAddress = %x, transferBlocks = %x\n",
					logicalBlockAddress, transferBlocks));
			}

			do {
				UINT64	Parameter = 0;
				UINT64	Result = 0;

				ntStatus = LsuFreeSemaphore(&IdeDisk->LanScsiSession, Parameter, &Result);
	//			KDPrintM(DBG_LURN_INFO, ("Set Semaphore: ntStaus = %08X, Parameter = %I64X, Result = %I64X\n", ntStatus, Parameter, Result));						
			} while(ntStatus == STATUS_DEVICE_BUSY);
#endif

			KDPrintM(DBG_LURN_INFO, ("Major = %d, Minor = %d\n", pDMBI->MajorVersion, pDMBI->MinorVersion));
		}	else {
			KDPrintM(DBG_LURN_INFO, ("Signature is not found= %I64X\n", pDMBI->Signature));
		}	
	}
#endif __DIVIDED_MULTI_BOOT__


#ifdef __NDASCHIP20_ALPHA_SUPPORT__

	KDPrintM(DBG_LURN_INFO, ("NDASCHIP20_ALPHA_SUPPORT enabled.!!!\n"));
	//
	//	Write one sector into last 4096th sector form the last sector.
	//
	if(IdeDisk->HwVersion == LANSCSIIDE_VERSION_2_0 && IdeDisk->LuData.PduDescFlags & PDUDESC_FLAG_UDMA) {
		ULONG				logicalBlockAddress;
		USHORT				transferBlocks;
		BYTE				response;
		BYTE				OneSector[BLOCK_SIZE];
		LANSCSI_PDUDESC		PduDesc;

		RtlZeroMemory(OneSector, BLOCK_SIZE);
		logicalBlockAddress = (ULONG)IdeDisk->LuData.SectorCount - 4097;
		transferBlocks = 1;

#ifdef __DIVIDED_MULTI_BOOT__
		{
			UINT32	LockContention;

			LockContention = 0;
			while(1) {


				//
				//	Acquire a write lock allocated for this unit device.
				//

				ntStatus = LspAcquireLock(
								&IdeDisk->LanScsiSession,
								SCSISTRG_WRITE_LOCKIDX_BASE + IdeDisk->LuData.LanscsiTargetID,
								NULL);
				if(!NT_SUCCESS(ntStatus)) {
					break;
				}
				if(ntStatus == STATUS_WAS_LOCKED) {
					KDPrintM(DBG_LURN_ERROR, ("Lock contention #%u!!!\n", LockContention));
				} else {
					break;
				}
				LockContention ++;


				//
				//	Clean up the lock on the NDAS device.
				//

				if(	LockContention % 1000 == 0) {

						LspCleanupLock(&IdeDisk->LanScsiSession,
							SCSISTRG_WRITE_LOCKIDX_BASE + IdeDisk->LuData.LanscsiTargetID);
				}

				if(LockContention >= 10000) {
					ntStatus = STATUS_UNSUCCESSFUL;
					break;
				}
			}

			if(!NT_SUCCESS(ntStatus)) {
				KDPrintM(DBG_LURN_ERROR, ("Failed to acquire lock #0\n"));
				return STATUS_UNSUCCESSFUL;
			}
		}

#endif __DIVIDED_MULTI_BOOT__

		LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &PduDesc, IDE_COMMAND, WIN_WRITE,logicalBlockAddress,transferBlocks, OneSector);
		ntStatus = LspRequest(
						&IdeDisk->LanScsiSession,
						&PduDesc,
						&response
					);
		if(!NT_SUCCESS(ntStatus) || response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR,
				("WIN_WRITE Error: logicalBlockAddress = %x, transferBlocks = %x\n",
				logicalBlockAddress, transferBlocks));
			return STATUS_UNSUCCESSFUL;
		} else {
			KDPrintM(DBG_LURN_INFO,
				("WIN_WRITE : wrote a sector to logicalBlockAddress = %x, transferBlocks = %x\n",
				logicalBlockAddress, transferBlocks));
		}

#ifdef __DIVIDED_MULTI_BOOT__
		LspReleaseLock(&IdeDisk->LanScsiSession, 0, NULL);
#endif __DIVIDED_MULTI_BOOT__

	}
#endif

#ifdef __NDASBOOT__	

	Lurn->UnitBlocks = IdeDisk->LuData.SectorCount;
	Lurn->StartBlockAddr = 0;
	Lurn->EndBlockAddr = IdeDisk->LuData.SectorCount - 1;

#endif

	return STATUS_SUCCESS;
}

#ifndef __NDASBOOT__
static
#endif
NTSTATUS
LurnIdeDiskExecute(
		PLURELATION_NODE		Lurn,
		PLURNEXT_IDE_DEVICE		IdeDisk,
		IN	PCCB				Ccb
	);

//
//	Initilize IDE Disk
//
NTSTATUS
LurnIdeDiskInitialize(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
	)
{
	PLURNEXT_IDE_DEVICE		IdeDisk;
	NTSTATUS				status;
	OBJECT_ATTRIBUTES		objectAttributes;
	LARGE_INTEGER			timeout;
	BOOLEAN					Connection;
	BOOLEAN					LogIn;
	BOOLEAN					ThreadRef;
	LSSLOGIN_INFO			LoginInfo;
	LSPROTO_TYPE			LSProto;
	BYTE					response;
	LARGE_INTEGER			GenericTimeOut;
	BOOLEAN					LssEncBuff;

	Connection = FALSE;
	LogIn = FALSE;
	ThreadRef = FALSE;

	// already alive
	if(LURN_STATUS_RUNNING == Lurn->LurnStatus)
		return STATUS_SUCCESS;

	if(!LurnDesc)
	{
		// revive cycle
		KIRQL oldIrql;

		ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);
		if(LURN_IS_RUNNING(Lurn->LurnStatus))
		{
			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);
			KDPrintM(DBG_LURN_ERROR,("Lurn already running.\n"));
			return STATUS_UNSUCCESSFUL;
		}

		RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

		// resurrection cycle
		ASSERT(Lurn->LurnExtension);

		IdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;

		ASSERT(IdeDisk->Lurn == Lurn);

		if(IdeDisk->CntEcrBufferLength && IdeDisk->CntEcrBuffer) {
			LssEncBuff = FALSE;
		} else {
			LssEncBuff = TRUE;
		}

		status = LspReconnectAndLogin(&IdeDisk->LanScsiSession, IdeDisk->LstransType, LssEncBuff);

		if(!NT_SUCCESS(status)) {
			KDPrintM(DBG_LURN_ERROR,("failed.\n"));
			return status;
		}
		if(NT_SUCCESS(status)) {
			//
			//	Reconnecting succeeded.
			//	Reset Stall counter.
			//
			ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);
//			ASSERT(Lurn->LurnStatus == LURN_STATUS_STALL);
			Lurn->LurnStatus = LURN_STATUS_RUNNING;
			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);
			InterlockedExchange(&Lurn->NrOfStall, 0);
		} else {
			goto error_out;
		}
	}
	else
	{
		// initializing cycle

		ASSERT(!Lurn->LurnExtension);

	IdeDisk = ExAllocatePoolWithTag(NonPagedPool, sizeof(LURNEXT_IDE_DEVICE), LURNEXT_POOL_TAG);
	if(!IdeDisk) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	//
	//	Initialize fields
	//
	Lurn->LurnExtension = IdeDisk;
	RtlZeroMemory(IdeDisk, sizeof(LURNEXT_IDE_DEVICE) );

	IdeDisk->Lurn = Lurn;
	IdeDisk->HwType = LurnDesc->LurnIde.HWType;
	IdeDisk->HwVersion = LurnDesc->LurnIde.HWVersion;
	IdeDisk->LuData.LanscsiTargetID = LurnDesc->LurnIde.LanscsiTargetID;
	IdeDisk->LuData.LanscsiLU = LurnDesc->LurnIde.LanscsiLU;

	//
	//	Lowest h/w version among NDAS devices
	//

	if(Lurn->Lur->LowestHwVer > LurnDesc->LurnIde.HWVersion) {
		Lurn->Lur->LowestHwVer = LurnDesc->LurnIde.HWVersion;
	}

	KeInitializeEvent(
			&IdeDisk->ThreadCcbArrivalEvent,
			NotificationEvent,
			FALSE
		);
	InitializeListHead(&IdeDisk->ThreadCcbQueue);

	KeInitializeEvent(
			&IdeDisk->ThreadReadyEvent,
			NotificationEvent,
			FALSE
		);

	//
	//	Confirm address type.
	//	Connect to the Lanscsi Node.
	//
	status = LstransAddrTypeToTransType( LurnDesc->LurnIde.BindingAddress.Address[0].AddressType, &IdeDisk->LstransType);
	if(!NT_SUCCESS(status)) {
		KDPrintM(DBG_LURN_ERROR, ("LstransAddrTypeToTransType(), Can't Connect to the LS node. STATUS:0x%08x\n", status));
		goto error_out;
	}

	//
	//	Set timeouts.
	//
	GenericTimeOut.QuadPart = LURNIDE_GENERIC_TIMEOUT;
	LspSetTimeOut(&IdeDisk->LanScsiSession, &GenericTimeOut);

	status = LspConnect(
					&IdeDisk->LanScsiSession,
					&LurnDesc->LurnIde.BindingAddress,
					&LurnDesc->LurnIde.TargetAddress
				);
	if(!NT_SUCCESS(status)) {
		KDPrintM(DBG_LURN_ERROR, ("LspConnect(), Can't Connect to the LS node. STATUS:0x%08x\n", status));
		goto error_out;
	}
	Connection = TRUE;

	//
	//	Login to the Lanscsi Node.
	//
	Lurn->AccessRight = LurnDesc->AccessRight;
	if(!(LurnDesc->AccessRight & GENERIC_WRITE)) {
		LurnDesc->LurnIde.UserID = CONVERT_TO_ROUSERID(LurnDesc->LurnIde.UserID);
	}

	LoginInfo.LoginType	= LOGIN_TYPE_NORMAL;
	RtlCopyMemory(&LoginInfo.UserID, &LurnDesc->LurnIde.UserID, LSPROTO_USERID_LENGTH);
	RtlCopyMemory(&LoginInfo.Password, &LurnDesc->LurnIde.Password, LSPROTO_PASSWORD_LENGTH);
	LoginInfo.MaxBlocksPerRequest = LurnDesc->MaxBlocksPerRequest;
	ASSERT(LurnDesc->MaxBlocksPerRequest<=128);
	
	LoginInfo.LanscsiTargetID = LurnDesc->LurnIde.LanscsiTargetID;
	LoginInfo.LanscsiLU = LurnDesc->LurnIde.LanscsiLU;
	LoginInfo.HWType = LurnDesc->LurnIde.HWType;
	LoginInfo.HWVersion = LurnDesc->LurnIde.HWVersion;
	LoginInfo.BlockInBytes = BLOCK_SIZE;
	status = LspLookupProtocol(LurnDesc->LurnIde.HWType, LurnDesc->LurnIde.HWVersion, &LSProto);
	if(!NT_SUCCESS(status)) {
		goto error_out;
	}
	if(Lurn->Lur->CntEcrMethod) {
		//
		//	IdeLurn will have a encryption buffer.
		//
		LoginInfo.IsEncryptBuffer = FALSE;
		KDPrintM(DBG_LURN_INFO, ("IDE Lurn extension is supposed to have a encryption buffer.\n"));
	} else {
		//
		//	IdeLurn will NOT have a encryption buffer.
		//	LSS should have a buffer to speed up.
		//
		LoginInfo.IsEncryptBuffer = TRUE;
		KDPrintM(DBG_LURN_INFO, ("LSS is supposed to have a encryption buffer.\n"));
	}

	status = LspLogin(
					&IdeDisk->LanScsiSession,
					&LoginInfo,
					LSProto,
					TRUE
				);
	if(!NT_SUCCESS(status)) {
		KDPrintM(DBG_LURN_ERROR, ("LspLogin(), Can't login to the LS node"
			" with UserID:%08lx. STATUS:0x%08x.\n", LoginInfo.UserID, status));
		status = STATUS_ACCESS_DENIED;
		goto error_out;
	}
	LogIn = TRUE;

	//
	//	Configure the disk.
	//
	status = LurnIdeDiskConfigure(Lurn, &response);
	if(!NT_SUCCESS(status)) {
		KDPrintM(DBG_LURN_ERROR,
			("LurnIdeDiskConfigure failed. Response:%d. STATUS:%08lx\n", response, status));
		goto error_out;
	}
	if(response != LANSCSI_RESPONSE_SUCCESS) {
		KDPrintM(DBG_LURN_ERROR,
			("LurnIdeDiskConfigure failed. Response:%d. STATUS:%08lx\n", response, status));
		status = STATUS_UNSUCCESSFUL;
		goto error_out;
	}

	//
	//	Content encryption.
	//
	IdeDisk->CntEcrMethod		= Lurn->Lur->CntEcrMethod;
//	IdeDisk->CntEcrKeyLength	= Lurn->Lur->CntEcrKeyLength;
//	IdeDisk->CntEcrKey			= Lurn->Lur->CntEcrKey;

	if(IdeDisk->CntEcrMethod) {
		PUCHAR	pPassword;
		PUCHAR	pKey;

		pPassword = (PUCHAR)&(LurnDesc->LurnIde.Password);
		pKey = Lurn->Lur->CntEcrKey;

		status = CreateCipher(
						&IdeDisk->CntEcrCipher,
						IdeDisk->CntEcrMethod,
						NCIPHER_MODE_ECB,
						HASH_KEY_LENGTH,
						pPassword
					);
		if(!NT_SUCCESS(status)) {
			KDPrintM(DBG_LURN_ERROR, ("Could not create a cipher instance.\n"));
			goto error_out;
		}

		status = CreateCipherKey(
							IdeDisk->CntEcrCipher,
							&IdeDisk->CntEcrKey,
							Lurn->Lur->CntEcrKeyLength,
							pKey
						);
		if(!NT_SUCCESS(status)) {
			KDPrintM(DBG_LURN_ERROR, ("Could not create a cipher key.\n"));
			goto error_out;
		}

		//
		//	Allocate the encryption buffer
		//
		IdeDisk->CntEcrBuffer = ExAllocatePoolWithTag(NonPagedPool, IdeDisk->LanScsiSession.MaxBlocksPerRequest * LurnDesc->BytesInBlock, LURNEXT_ENCBUFF_TAG);
		if(IdeDisk->CntEcrBuffer) {
			IdeDisk->CntEcrBufferLength = IdeDisk->LanScsiSession.MaxBlocksPerRequest * LurnDesc->BytesInBlock;
		} else {
			IdeDisk->CntEcrBufferLength = 0;
			KDPrintM(DBG_LURN_ERROR, ("Could not allocate a encryption buffer.\n"));
		}
	}
	}

	//
	//	create worker thread
	//
	InitializeObjectAttributes(&objectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
	status = PsCreateSystemThread(
						&IdeDisk->ThreadHandle,
						THREAD_ALL_ACCESS,
						&objectAttributes,
						NULL,
						NULL,
						LurnIdeDiskThreadProc,
						Lurn
					);
	if(!NT_SUCCESS(status)) {
		KDPrintM(DBG_LURN_ERROR, ("Can't Create Process. STATUS:0x%08x\n", status));
		goto error_out;
	}

	ASSERT(IdeDisk->ThreadHandle);
	status = ObReferenceObjectByHandle(
					IdeDisk->ThreadHandle,
					GENERIC_ALL,
					NULL,
					KernelMode,
					&IdeDisk->ThreadObject,
					NULL
				);
	if(status != STATUS_SUCCESS) {
		KDPrintM(DBG_LURN_ERROR, ("ObReferenceObjectByHandle() Failed. 0x%x\n", status));
		status = STATUS_UNSUCCESSFUL;
		goto error_out;
	}

	ThreadRef = TRUE;

	timeout.QuadPart = -LURNIDE_THREAD_START_TIME_OUT;

	status = KeWaitForSingleObject(
				&IdeDisk->ThreadReadyEvent,
				Executive,
				KernelMode,
				FALSE,
				&timeout
				);
	if(status != STATUS_SUCCESS) {
		KDPrintM(DBG_LURN_ERROR, ("Wait for event Failed. 0x%x\n", status));
		status = STATUS_UNSUCCESSFUL;
		goto error_out;
	}


	return STATUS_SUCCESS;

error_out:
	if(ThreadRef)
		ObDereferenceObject(&IdeDisk->ThreadObject);
	if(LogIn) {
		LspLogout(&IdeDisk->LanScsiSession);
	}
	if(Connection) {
		LspDisconnect(&IdeDisk->LanScsiSession);
	}

	if(Lurn->LurnExtension) {
		PLURNEXT_IDE_DEVICE		tmpIdeDisk;

		tmpIdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;

		if(tmpIdeDisk->CntEcrKey) {
			CloseCipherKey(tmpIdeDisk->CntEcrKey);
			tmpIdeDisk->CntEcrKey = NULL;
		}

		if(tmpIdeDisk->CntEcrCipher) {
			CloseCipher(tmpIdeDisk->CntEcrCipher);
			tmpIdeDisk->CntEcrCipher = NULL;
		}

		ExFreePoolWithTag(Lurn->LurnExtension, LURNEXT_POOL_TAG);
		Lurn->LurnExtension = NULL;
	}

	return status;
}



//
//	Process requests from parents
//
NTSTATUS
LurnIdeDiskRequest(
		PLURELATION_NODE Lurn,
		PCCB Ccb
	) {
	PLURNEXT_IDE_DEVICE		IdeDisk;
	KIRQL					oldIrql;
	NTSTATUS				status;

	IdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;

	//
	//	dispatch a request
	//
	switch(Ccb->OperationCode) {
	case CCB_OPCODE_RESETBUS:
		//
		//	clear all CCB in the queue and queue the CCB to the thread.
		//
		KDPrintM(DBG_LURN_ERROR, ("CCB_OPCODE_RESETBUS, Ccb->Srb = %08x, Ccb->AssociateID = %d\n",
			Ccb->Srb, Ccb->AssociateID));
		ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);

		CcbCompleteList(&IdeDisk->ThreadCcbQueue, CCB_STATUS_RESET, 0);
		if(	LURN_IS_RUNNING(Lurn->LurnStatus) ) {
			InsertHeadList(&IdeDisk->ThreadCcbQueue, &Ccb->ListEntry);
			KeSetEvent(&IdeDisk->ThreadCcbArrivalEvent, IO_NO_INCREMENT, FALSE);
			LSCcbMarkCcbAsPending(Ccb);

			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

			return STATUS_PENDING;
		}

		RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		LSCcbCompleteCcb(Ccb);
		KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_RESETBUS: Completed Ccb:%p\n", Ccb));
		return STATUS_SUCCESS;

	case CCB_OPCODE_STOP:

		KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_STOP\n"));


		//
		//	Stop request must succeed unconditionally.
		//	clear all CCB in the queue and set LURN_STATUS_STOP_PENDING
		//

		ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
		ASSERT(Ccb->Flags & CCB_FLAG_SYNCHRONOUS);

		if(IdeDisk == NULL) {
			KDPrintM(DBG_LURN_ERROR, ("CCB_OPCODE_STOP: IdeDisk NULL!\n"));
			Ccb->CcbStatus =CCB_STATUS_SUCCESS;
			LSCcbCompleteCcb(Ccb);
			return STATUS_SUCCESS;
		}

		ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);

		CcbCompleteList(&IdeDisk->ThreadCcbQueue, CCB_STATUS_NOT_EXIST, CCBSTATUS_FLAG_LURN_STOP);

		if(	LURN_IS_RUNNING(Lurn->LurnStatus)) {

			LARGE_INTEGER			TimeOut;

			KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_STOP: queue CCB_OPCODE_STOP to the thread. LurnStatus=%08lx\n",
															Lurn->LurnStatus
														));

			//
			//	Hand over Stop CCB to the worker thread.
			//	The worker thread will complete the CCB.
			//

			InsertHeadList(&IdeDisk->ThreadCcbQueue, &Ccb->ListEntry);
			KeSetEvent(&IdeDisk->ThreadCcbArrivalEvent, IO_NO_INCREMENT, FALSE);
			Lurn->LurnStatus = LURN_STATUS_STOP_PENDING;
			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);


			//
			//	Wait until the worker thread is terminated.
			//

			TimeOut.QuadPart = - NANO100_PER_SEC * 120;
			status = KeWaitForSingleObject(
						IdeDisk->ThreadObject,
						Executive,
						KernelMode,
						FALSE,
						&TimeOut
					);
			ASSERT(status == STATUS_SUCCESS);


			//
			//	Dereference the thread object.
			//

			ObDereferenceObject(IdeDisk->ThreadObject);

			ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);

			IdeDisk->ThreadObject = NULL;
			IdeDisk->ThreadHandle = NULL;

			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

			LspLogout(&IdeDisk->LanScsiSession);
			LspDisconnect(&IdeDisk->LanScsiSession);

		} else {
			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);


			//
			//	Complete the STOP CCB.
			//

			Ccb->CcbStatus =CCB_STATUS_SUCCESS;
			LSCcbCompleteCcb(Ccb);
		}

		return STATUS_SUCCESS;

	case CCB_OPCODE_QUERY:
		KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_QUERY\n"));
		status = LurnIdeQuery(Lurn, IdeDisk, Ccb);
		if(	status != STATUS_MORE_PROCESSING_REQUIRED) {
#if DBG
			if(NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_QUERY: Completed Ccb:%p\n", Ccb));
			}
#endif
			LSCcbCompleteCcb(Ccb);
			return STATUS_SUCCESS;
		}
	case CCB_OPCODE_RESTART:
		KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_RESTART\n"));
		Ccb->CcbStatus =CCB_STATUS_COMMAND_FAILED;
		LSCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;

	case CCB_OPCODE_ABORT_COMMAND:
		KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_ABORT_COMMAND\n"));
		Ccb->CcbStatus =CCB_STATUS_COMMAND_FAILED;
		LSCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;
	case CCB_OPCODE_DVD_STATUS:
		{
			Ccb->CcbStatus =CCB_STATUS_COMMAND_FAILED;
			LSCcbCompleteCcb(Ccb);
			return STATUS_SUCCESS;
		}
		break;
	default:
		break;
	}


	//
	//	check the status and queue a Ccb to the worker thread
	//
	ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);

	if(		!LURN_IS_RUNNING(Lurn->LurnStatus)
		) {


		if(Lurn->LurnStatus == LURN_STATUS_STOP_PENDING) {
			KDPrintM(DBG_LURN_ERROR, ("LURN%d(STATUS %08lx)"
						" is in STOP_PENDING. Return with CCB_STATUS_STOP.\n", Lurn->LurnId, Lurn->LurnStatus));
			LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_BUSRESET_REQUIRED|CCBSTATUS_FLAG_LURN_STOP);
			Ccb->CcbStatus = CCB_STATUS_STOP;
			Lurn->LurnStatus = LURN_STATUS_STOP;
		}
		else {
			KDPrintM(DBG_LURN_NOISE, ("LURN%d(STATUS %08lx)"
						" is not running. Return with NOT_EXIST.\n", Lurn->LurnId, Lurn->LurnStatus));
			LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_LURN_STOP);
			Ccb->CcbStatus = CCB_STATUS_NOT_EXIST;
		}
		RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

		LSCcbCompleteCcb(Ccb);

		return STATUS_SUCCESS;

	}

	if(Ccb->Flags & CCB_FLAG_URGENT) {
		KDPrintM(DBG_LURN_INFO, ("URGENT. Queue to the head and mark pending.\n"));

		InsertHeadList(&IdeDisk->ThreadCcbQueue, &Ccb->ListEntry);
#ifdef __ENABLE_LOADER__
		RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);
		return LurnIdeDiskDispatchCcb(Lurn, IdeDisk);
		ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);
#else
		KeSetEvent(&IdeDisk->ThreadCcbArrivalEvent, IO_DISK_INCREMENT, FALSE);
		LSCcbMarkCcbAsPending(Ccb);
#endif
	} else {
		KDPrintM(DBG_LURN_TRACE, ("Queue to the tail and mark pending.\n"));

		InsertTailList(&IdeDisk->ThreadCcbQueue, &Ccb->ListEntry);
#ifdef __ENABLE_LOADER__
		RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);
		return LurnIdeDiskDispatchCcb(Lurn, IdeDisk);
		ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);
#else
		KeSetEvent(&IdeDisk->ThreadCcbArrivalEvent, IO_DISK_INCREMENT, FALSE);
		LSCcbMarkCcbAsPending(Ccb);
#endif

	}

	RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

	return STATUS_PENDING;
}

#ifndef __NDASBOOT__
static
#endif

NTSTATUS
LurnIdeDiskDispatchCcb(
		PLURELATION_NODE		Lurn,
		PLURNEXT_IDE_DEVICE		IdeDisk
	) {
	PLIST_ENTRY	ListEntry;
	KIRQL		oldIrql;
	PCCB		Ccb;
	NTSTATUS	status;
	BYTE		PduResponse;

	status = STATUS_SUCCESS;

	while(1) {
		//
		//	dequeue one entry.
		//
		ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);

		if(	!(Lurn->LurnStatus == LURN_STATUS_RUNNING) &&
			!(Lurn->LurnStatus == LURN_STATUS_STALL)
			) {
			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

			KDPrintM(DBG_LURN_ERROR, ("not LURN_STATUS_RUNNING nor LURN_STATUS_STALL.\n"));

			return STATUS_UNSUCCESSFUL;
		}

		ListEntry = RemoveHeadList(&IdeDisk->ThreadCcbQueue);

		if(ListEntry == (&IdeDisk->ThreadCcbQueue)) {
			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

			KDPrintM(DBG_LURN_TRACE, ("Empty.\n"));

			break;
		}

		Ccb = CONTAINING_RECORD(ListEntry, CCB, ListEntry);

#ifdef __NDASBOOT__
		if(STATUS_LPX_DRIVER_BEFORE_BIND == ProtoStatus) {
			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

			KDPrintM(DBG_LURN_TRACE, ("Trying to reconnect with LPX driver.\n"));

			Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
			goto try_reconnect;
		}
#endif

		if(Lurn->LurnStatus == LURN_STATUS_STALL &&
			!LSCcbIsFlagOn(Ccb, CCB_FLAG_RETRY_NOT_ALLOWED)) {
			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

			Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
			status = STATUS_PORT_DISCONNECTED;

			KDPrintM(DBG_LURN_ERROR, ("LURN_STATUS_STALL! going to reconnecting process.\n"));

			goto try_reconnect;
		}

		RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

		if(Ccb->CascadeEvent)
		{
			KDPrintM(DBG_LURN_INFO, ("KeWaitForSingleObject(Ccb[%d]->CascadeEvent(%p)...\n",
				Ccb->AssociateID,
				Ccb->CascadeEvent));
			status = KeWaitForSingleObject(
				Ccb->CascadeEvent,
				Executive,
				KernelMode,
				FALSE,
				NULL);

			if(!NT_SUCCESS(status))
			{
				KDPrintM(DBG_LURN_ERROR, ("KeWaitForSingleObject Failed(%x)\n", status));
				ASSERT(FALSE);
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				status = STATUS_UNSUCCESSFUL;
//					Ccb->ForceFail = TRUE;
				goto try_reconnect;
			}
			KDPrintM(DBG_LURN_ERROR, ("Cascade : #%d. OpCode : %08x\n", Ccb->AssociateID, Ccb->OperationCode));
		}

		//
		//	dispatch a CCB
		//
/*		if(Ccb->AssociateCascade && Ccb->ForceFail)
		{
			status = STATUS_SUCCESS;
			KDPrintM(DBG_LURN_ERROR, ("Ccb->AssociateCascade && Ccb->ForceFail\n"));
			LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
		}
		else
		{
*/		switch(Ccb->OperationCode)
		{
		case CCB_OPCODE_UPDATE:

			KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_UPDATE!\n"));
			status = LurnIdeUpdate(Lurn, IdeDisk, Ccb);
			//
			//	Configure the disk
			//
			if(NT_SUCCESS(status) && Ccb->CcbStatus == CCB_STATUS_SUCCESS) {
				LurnIdeDiskConfigure(Lurn, &PduResponse);
			}
			break;

		case CCB_OPCODE_RESETBUS:

			KDPrintM(DBG_LURN_ERROR, ("CCB_OPCODE_RESETBUS!\n"));
			status = LurnIdeResetBus(Lurn, Ccb);
			break;

		case CCB_OPCODE_STOP:

			KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_STOP!\n"));
			LurnIdeStop(Lurn, Ccb);
			return STATUS_UNSUCCESSFUL;
			break;

		case CCB_OPCODE_RESTART:

			KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_RESTART!\n"));
			status = LurnIdeRestart(Lurn, Ccb);
			break;

		case CCB_OPCODE_ABORT_COMMAND:

			KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_ABORT_COMMAND!\n"));
			status = LurnIdeAbortCommand(Lurn, Ccb);
			break;

		case CCB_OPCODE_EXECUTE:

			KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_EXECUTE!\n"));
			status = LurnIdeDiskExecute(Lurn, Lurn->LurnExtension, Ccb);
			break;

		case CCB_OPCODE_NOOP:

			KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_NOOP!\n"));
			status = LSLurnIdeNoop(Lurn, Lurn->LurnExtension, Ccb);
			break;

		default:
			break;
		}
//		}

try_reconnect:

		//
		//	detect disconnection
		//
		if(	!LSCcbIsFlagOn(Ccb, CCB_FLAG_RETRY_NOT_ALLOWED) &&
			(status == STATUS_PORT_DISCONNECTED ||
			Ccb->CcbStatus == CCB_STATUS_COMMUNICATION_ERROR)) {
			//
			//	Complete with Busy status
			//	so that ScsiPort will send the CCB again and prevent SRB timeout.
			//
			//
			KDPrintM(DBG_LURN_ERROR, ("RECONN: try reconnecting!\n"));

			//
			//	Set the reconnection flag to notify reconnection to Miniport.
			//	Complete the CCB.
			//
			if(Lurn->ReconnTrial) {
				//
				//	If this is the first try to reconnect,
				//	then do not notify the reconnection event.
				//	The communication error could be caused by standby mode of the NDAS device.
				//
				if(Lurn->NrOfStall != 0)
					LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_RECONNECTING);
			}

			ASSERT(Ccb->Srb || Ccb->OperationCode == CCB_OPCODE_RESETBUS || Ccb->CompletionEvent);
			Ccb->CcbStatus = CCB_STATUS_BUSY;
#ifdef __NDASBOOT__
#ifndef __ENABLE_LOADER__
			status = InsertToReposite(Ccb);
#endif
#endif
			LSCcbCompleteCcb(Ccb);
			KDPrintM(DBG_LURN_ERROR, ("RECONN: completed Ccb:%x.\n", Ccb));

			status = LSLurnIdeReconnect(Lurn, IdeDisk);
			if(status == STATUS_CONNECTION_COUNT_LIMIT) {
				//
				//	terminate the thread
				//
				return STATUS_UNSUCCESSFUL;
			} else if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_ERROR, ("RECONN: reconnection failed.\n"));

			} else {
				//
				//	Configure the disk and get the next CCB
				//
#ifdef __NDASBOOT__
				if( ProtoStatus != STATUS_LPX_DRIVER_BEFORE_BIND ) {
#endif
					status = LurnIdeDiskConfigure(Lurn, &PduResponse);
					if(!NT_SUCCESS(status)) {
						ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);
						Lurn->LurnStatus = LURN_STATUS_STALL;
						RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);
						KDPrintM(DBG_LURN_ERROR, ("RECONN: LurnIdeDiskConfigure() failed.\n"));

					}
#ifdef __NDASBOOT__
				} else {
					KDPrintM(DBG_LURN_TRACE, ("Trying to reconnect with LPX driver.\n"));
				}
#endif

			}

			continue;
		}

		//
		//	complete a CCB
		//
		LSCcbCompleteCcb(Ccb);
		KDPrintM(DBG_LURN_TRACE, ("Completed Ccb:%p\n", Ccb));
	}

	return STATUS_SUCCESS;
}


static
void
LurnIdeDiskThreadProc(
		IN	PVOID	Context
)
{
	PLURELATION_NODE		Lurn;
	PLURNEXT_IDE_DEVICE		IdeDisk;
	NTSTATUS				status;
	KIRQL					oldIrql;
	LARGE_INTEGER			TimeOut;

	KDPrintM(DBG_LURN_TRACE, ("Entered\n"));

	Lurn = (PLURELATION_NODE)Context;
	IdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;
	TimeOut.QuadPart = - LURNIDE_IDLE_TIMEOUT;

	//
	//	Raise the thread's current priority level (same level to Modified page writer).
	//	Set ready signal.
	//
	KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);
	KeSetEvent (&IdeDisk->ThreadReadyEvent, IO_NO_INCREMENT, FALSE);

	do {
		ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

		status = KeWaitForSingleObject(
							&IdeDisk->ThreadCcbArrivalEvent,
							Executive,
							KernelMode,
							FALSE,
							&TimeOut
						);
		if(!NT_SUCCESS(status)){
			KDPrintM(DBG_LURN_ERROR, ("KeWaitForSingleObject Fail\n"));
			break;
		}

		switch(status)
		{
		case STATUS_SUCCESS:
			KeClearEvent(&IdeDisk->ThreadCcbArrivalEvent);
			status = LurnIdeDiskDispatchCcb(Lurn, IdeDisk);
			if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_ERROR, ("LurnIdeDiskDispatchCcb() failed. NTSTATUS:%08lx\n", status));
				goto terminate_thread;
			}
		break;

		case STATUS_TIMEOUT: {
			BYTE	pdu_response;

			//
			//	Send NOOP to make sure that the Lanscsi Node is reachable
			//	Except when the LURN has stopped at the host side.
			//

#ifdef __NDASBOOT__
			if( ProtoStatus != STATUS_LPX_DRIVER_BEFORE_BIND) {
			status = LspNoOperation(&IdeDisk->LanScsiSession, IdeDisk->LuData.LanscsiTargetID, &pdu_response);
			}
			else {
				KDPrintM(DBG_LURN_TRACE, ("Trying to bind with LPX driver. Skip sending NOOP.\n"));
				status = STATUS_SUCCESS;
			}
#endif

			if(IdeDisk->LURNHostStop) {
				KDPrintM(DBG_LURN_ERROR, ("Lurn%d:"
					" IDLE_TIMEOUT: stopped at host side. skip pinging.\n",
					Lurn->LurnId));
				break;
			}
/*			ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);
			if(Lurn->LurnStatus != LURN_STATUS_RUNNING) {
				RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

				KDPrintM(DBG_LURN_ERROR, ("Lurn%d:"
					" IDLE_TIMEOUT: Not running. not send noop.\n",
					Lurn->LurnId));

				break;
			} else {
				RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);
			}*/

			status = LspNoOperation(&IdeDisk->LanScsiSession, IdeDisk->LuData.LanscsiTargetID, &pdu_response);
			if(!NT_SUCCESS(status) || pdu_response != LANSCSI_RESPONSE_SUCCESS) {
				ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);
				if(Lurn->LurnStatus != LURN_STATUS_STALL) {
					LURN_EVENT	LurnEvent;

					Lurn->LurnStatus = LURN_STATUS_STALL;
					RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

					//
					//	Set a event to make LanscsiMiniport fire NoOperation.
					//
					LurnEvent.LurnId = Lurn->LurnId;
					LurnEvent.LurnEventClass = LURN_REQUEST_NOOP_EVENT;
					LurCallBack(Lurn->Lur, &LurnEvent);

					KDPrintM(DBG_LURN_ERROR, ("Lurn%d:"
											" IDLE_TIMEOUT: NoOperation failed. Start reconnecting.\n",
											Lurn->LurnId));
				} else {
					RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);
				}
			} else {
				KDPrintM(DBG_LURN_TRACE, ("Lurn%d:"
											" IDLE_TIMEOUT: NoOperation is successful.\n", Lurn->LurnId));
			}

		break;
		}
		default:
		break;
		}


	} while(1);


terminate_thread:
	//
	//	Set LurnStatus and empty ccb queue
	//
	KDPrintM(DBG_LURN_TRACE, ("The thread will be terminiated.\n"));
	ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);
	Lurn->LurnStatus = LURN_STATUS_STOP_PENDING;
	CcbCompleteList(
			&IdeDisk->ThreadCcbQueue,
			CCB_STATUS_NOT_EXIST,
			CCBSTATUS_FLAG_LURN_STOP
		);
	RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

	KDPrintM(DBG_LURN_ERROR, ("Terminated\n"));
	PsTerminateSystemThread(STATUS_SUCCESS);

	return;
}

static
NTSTATUS
LurnIdeDiskRead(
		IN OUT	PCCB					Ccb,
		IN		PLURNEXT_IDE_DEVICE		IdeDisk,
		OUT		PVOID					DataBuffer,
		IN		UINT64					logicalBlockAddress,
		IN		ULONG					transferBlocks,
		IN		ULONG					splitBlocks
)
{
	BYTE				response;
	LANSCSI_PDUDESC		PduDesc;
	NTSTATUS			ntStatus;
	ULONG				transferedBlocks;

#ifdef __ENABLE_PERFORMACE_CHECK__
	LARGE_INTEGER		PFStartTime;
	LARGE_INTEGER		PFCurrentTime;

	PFStartTime = KeQueryPerformanceCounter(NULL);
	PFCurrentTime.QuadPart = PFStartTime.QuadPart;
#endif

	// AING : some disks (Maxtor 80GB supports LBA only)
	//		ASSERT(IdeDisk->LuData.PduDescFlags&PDUDESC_FLAG_LBA48);
	KDPrintM(DBG_LURN_NOISE, ("0x%08x(%d)\n", logicalBlockAddress, transferBlocks));

	if(0 == transferBlocks)
	{
		KDPrintM(DBG_LURN_ERROR, ("Return for transferBlocks == 0\n"));
		return STATUS_SUCCESS;
	}

#ifdef __ENABLE_LOCKED_READ__
	//
	//	Acquire a write lock allocated for this unit device.
	//

	if(	IdeDisk->Lurn->Lur->LurFlags & LURFLAG_LOCKEDWRITE && IdeDisk->Lurn->Lur->LowestHwVer >= LANSCSIIDE_VERSION_1_1) {

		UINT32	LockContention;

		LockContention = 0;
		while(1) {
			ntStatus = LspAcquireLock(	&IdeDisk->LanScsiSession,
										SCSISTRG_WRITE_LOCKIDX_BASE + IdeDisk->LuData.LanscsiTargetID,
										NULL);
			if(!NT_SUCCESS(ntStatus)) {
				break;
			}
			if(ntStatus == STATUS_WAS_LOCKED) {
				KDPrintM(DBG_LURN_ERROR, ("Lock contention #%u!!!\n", LockContention));
			} else {
				break;
			}
			LockContention ++;


			//
			//	Clean up the lock on the NDAS device.
			//

			if(	LockContention % 1000 == 0) {

				LspCleanupLock(&IdeDisk->LanScsiSession,
					SCSISTRG_WRITE_LOCKIDX_BASE + IdeDisk->LuData.LanscsiTargetID);
			}

			if(LockContention >= 10000) {
				ntStatus = STATUS_UNSUCCESSFUL;
				break;
			}
		}
		if(!NT_SUCCESS(ntStatus)) {
			KDPrintM(DBG_LURN_ERROR, ("Failed to acquire lock #0\n"));
			return STATUS_UNSUCCESSFUL;
		}
	}
#endif

	//
	//	Send read LSP requests split by SplitBlock factor.
	//

	for(transferedBlocks = 0; transferedBlocks < transferBlocks; transferedBlocks += splitBlocks)
	{
		LURNIDE_INITIALIZE_PDUDESC(
			IdeDisk, 
			&PduDesc, 
			IDE_COMMAND, 
			WIN_READ, 
			logicalBlockAddress + transferedBlocks,
			(transferBlocks - transferedBlocks <= splitBlocks) ? transferBlocks - transferedBlocks : splitBlocks, 
			(PBYTE)DataBuffer + (transferedBlocks << BLOCK_SIZE_BITS));

		ntStatus = LspRequest(
						&IdeDisk->LanScsiSession,
						&PduDesc,
						&response
					);
#if DBG
		if(!NT_SUCCESS(ntStatus) || response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR,
					("logicalBlockAddress = %I64x, transferBlocks = %d, transferedBlocks = %d\n",
					logicalBlockAddress, transferBlocks, transferedBlocks));
		}
#endif
		if(!NT_SUCCESS(ntStatus)) {

			KDPrintM(DBG_LURN_ERROR, ("Error when LspRequest 0x%x\n", ntStatus));

			Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
			break;
		} else {
#if DBG
			if(response != LANSCSI_RESPONSE_SUCCESS)
				KDPrintM(DBG_LURN_ERROR, ("Device Report Error 0x%x\n", response));
#endif
			switch(response) {
			case LANSCSI_RESPONSE_SUCCESS:
				Ccb->CcbStatus = CCB_STATUS_SUCCESS;
				break;
			case LANSCSI_RESPONSE_T_NOT_EXIST:
				Ccb->CcbStatus = CCB_STATUS_NOT_EXIST;

				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: returned LANSCSI_RESPONSE_T_NOT_EXIST\n"));
				goto diskread_exit;
				break;
			case LANSCSI_RESPONSE_T_BAD_COMMAND:
				Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;

				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: returned LANSCSI_RESPONSE_T_BAD_COMMAND\n"));
				goto diskread_exit;	
				break;
			case LANSCSI_RESPONSE_T_COMMAND_FAILED:
				{
					UINT64	BadSector, i;

					KDPrintM(DBG_LURN_ERROR,
						("CcbStatus: returned LANSCSI_RESPONSE_T_COMMAND_FAILED\n"));

					//
					//	Figure out exact error status.
					//	Command will have status register's value,
					//	least bit has a validity of error register.
					//

//					if(PduDesc.Command & 0x01) {

						//
						// ID not found error!
						//	( Disk logical address not found )
						//

						if((!(PduDesc.Feature & ID_ERR) && (PduDesc.Feature & ABRT_ERR)) ||
							((PduDesc.Feature & ID_ERR) && !(PduDesc.Feature & ABRT_ERR))){
							PSENSE_DATA	senseData;

							KDPrintM(DBG_LURN_ERROR,
								("IDE error: Illegal address.\n"));

							senseData = Ccb->SenseBuffer;

							senseData->ErrorCode = 0x70;
							senseData->Valid = 1;
							senseData->SenseKey = SCSI_SENSE_VOL_OVERFLOW;
							senseData->AdditionalSenseLength = 0xb;
							senseData->AdditionalSenseCode = SCSI_ADSENSE_NO_SENSE;
							senseData->AdditionalSenseCodeQualifier = 0;

							Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
							ntStatus = STATUS_IO_DEVICE_ERROR;
							goto diskread_exit;
						}
//					}

					KDPrintM(DBG_LURN_ERROR,
						("CcbStatus: verifying...\n"));

					//
					// Search Bad Sector.
					//
					BadSector = logicalBlockAddress + transferedBlocks;

					for(i = 0; i < splitBlocks; i++) {

						LURNIDE_INITIALIZE_PDUDESC(
											IdeDisk,
											&PduDesc,
											IDE_COMMAND,
											WIN_READ,
											BadSector +i,
											1,
											&((PUCHAR)DataBuffer +
											(transferedBlocks << BLOCK_SIZE_BITS))[i]);
						ntStatus = LspRequest(
										&IdeDisk->LanScsiSession,
										&PduDesc,
										&response
									);
						if(!NT_SUCCESS(ntStatus)) {

							KDPrintM(DBG_LURN_ERROR, ("Error when LspRequest for find Bad Sector. 0x%x\n", ntStatus));

							Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
							goto diskread_exit;
						}


						if(response != LANSCSI_RESPONSE_SUCCESS) {
							KDPrintM(DBG_LURN_ERROR,
								("Error: logicalBlockAddress = %x, transferBlocks = %x\n",
								BadSector, transferBlocks));
							break;
						} else {
							BadSector++;
						}
					}

					if(BadSector > logicalBlockAddress + transferBlocks) {
						KDPrintM(DBG_LURN_ERROR, ("No Bad Sector!!!\n"));

						Ccb->CcbStatus = CCB_STATUS_SUCCESS;

						ntStatus = STATUS_SUCCESS;
						goto diskread_exit;
					} else {

						KDPrintM(DBG_LURN_ERROR, ("Bad Sector is 0x%x Sense 0x%x pData 0x%x\n",
							BadSector, Ccb->SenseDataLength, Ccb->SenseBuffer));

						// Calc residual size.
						Ccb->ResidualDataLength = (ULONG)(transferBlocks - (BadSector - logicalBlockAddress)) * BLOCK_SIZE;

						if(Ccb->SenseBuffer != NULL) {
							PSENSE_DATA	senseData;

							senseData = Ccb->SenseBuffer;

							senseData->ErrorCode = 0x70;
							senseData->Valid = 1;
							//senseData->SegmentNumber = 0;
							senseData->SenseKey = SCSI_SENSE_MEDIUM_ERROR;
							//senseData->IncorrectLength = 0;
							//senseData->EndOfMedia = 0;
							//senseData->FileMark = 0;

							senseData->AdditionalSenseLength = 0xb;
							senseData->AdditionalSenseCode = SCSI_ADSENSE_NO_SENSE;
							senseData->AdditionalSenseCodeQualifier = 0;

						}
					}
				}

				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				ntStatus = STATUS_IO_DEVICE_ERROR;
				goto diskread_exit;
				break;


			case LANSCSI_RESPONSE_T_BROKEN_DATA:
			default:
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				ntStatus = STATUS_IO_DEVICE_ERROR;

				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: returned LANSCSI_RESPONSE_T_BROKEN_DATA or failure..\n"));
				goto diskread_exit; // to do: fix to retry this operation again.
				break;
			}

		}
	}

diskread_exit:
#ifdef __ENABLE_LOCKED_READ__
	if(IdeDisk->Lurn->Lur->LurFlags & LURFLAG_LOCKEDWRITE)
		LspReleaseLock(&IdeDisk->LanScsiSession, 0, NULL);
#endif

	//
	//	Decrypt the content
	//
	if(	NT_SUCCESS(ntStatus) &&
		IdeDisk->CntEcrMethod
	) {
		DecryptBlock(
				IdeDisk->CntEcrCipher,
				IdeDisk->CntEcrKey,
				PduDesc.DataBufferLength << BLOCK_SIZE_BITS,
				PduDesc.DataBuffer,
				PduDesc.DataBuffer
			);
	}

#ifdef __ENABLE_PERFORMACE_CHECK__

	PFCurrentTime = KeQueryPerformanceCounter(NULL);
	if(PFCurrentTime.QuadPart - PFStartTime.QuadPart > 10000) {
		DbgPrint("=PF= LurnIdeDiskRead: addr:%d blk=%d elaps:%I64d\n", 
								logicalBlockAddress,
								transferBlocks,
								PFCurrentTime.QuadPart - PFStartTime.QuadPart
							);
	}

#endif

	return ntStatus;
}

static
NTSTATUS
LurnIdeDiskWrite(
		IN OUT	PCCB					Ccb,
		IN		PLURNEXT_IDE_DEVICE		IdeDisk,
		IN		PVOID					DataBuffer,
		IN		UINT64					logicalBlockAddress,
		IN		ULONG					transferBlocks,
		IN		ULONG					splitBlocks
)
{
	BYTE				response;
	LANSCSI_PDUDESC		PduDesc;
	NTSTATUS			ntStatus;
	BOOLEAN				RecoverBuffer;
	PBYTE				l_DataBuffer;
	UINT32				l_Flags;
	ULONG				transferedBlocks;

#ifdef __ENABLE_PERFORMACE_CHECK__
	LARGE_INTEGER		PFStartTime;
	LARGE_INTEGER		PFCurrentTime;

	PFStartTime = KeQueryPerformanceCounter(NULL);
	PFCurrentTime.QuadPart = PFStartTime.QuadPart;
#endif

	KDPrintM(DBG_LURN_NOISE, ("0x%08x(%d)\n", logicalBlockAddress, transferBlocks));

	if(0 == transferBlocks)
	{
		KDPrintM(DBG_LURN_ERROR, ("Return for transferBlocks == 0\n"));
		return STATUS_SUCCESS;
	}

	RecoverBuffer = FALSE;

	//
	//	Try to encrypt the content.
	//
	l_Flags = 0;
	l_DataBuffer = DataBuffer;
	if(IdeDisk->CntEcrMethod) {

		//
		//	If the primary buffer is volatile, encrypt it directly.
		//
		if(LSCcbIsFlagOn(Ccb, CCB_FLAG_VOLATILE_PRIMARY_BUFFER)) {
			EncryptBlock(
					IdeDisk->CntEcrCipher,
					IdeDisk->CntEcrKey,
					transferBlocks << BLOCK_SIZE_BITS,
					DataBuffer,
					DataBuffer
				);
		//
		//	If the encryption buffer is available, encrypt it by copying to the encryption buffer.
		//
		} else if(	IdeDisk->CntEcrBuffer &&
			IdeDisk->CntEcrBufferLength >= (transferBlocks << BLOCK_SIZE_BITS)
			) {

			EncryptBlock(
					IdeDisk->CntEcrCipher,
					IdeDisk->CntEcrKey,
					transferBlocks << BLOCK_SIZE_BITS,
					DataBuffer,
					IdeDisk->CntEcrBuffer
				);
			l_DataBuffer = IdeDisk->CntEcrBuffer;
			l_Flags = PDUDESC_FLAG_VOLATILE_BUFFER;

		//
		//	There is no usable buffer, encrypt it directly and decrypt it later.
		//
		} else {

			EncryptBlock(
					IdeDisk->CntEcrCipher,
					IdeDisk->CntEcrKey,
					transferBlocks << BLOCK_SIZE_BITS,
					DataBuffer,
					DataBuffer
				);
			RecoverBuffer = TRUE;
		}
#if DBG
		if(	!IdeDisk->CntEcrBuffer ||
			IdeDisk->CntEcrBufferLength < (transferBlocks << BLOCK_SIZE_BITS)
			) {
		KDPrintM(DBG_LURN_ERROR,
			("Encryption buffer too small! %p %d\n",
			IdeDisk->CntEcrBuffer, IdeDisk->CntEcrBufferLength));
		}
#endif
	}

	//
	//	Try to acquire the lock in the NDAS device.
	//

	if(	IdeDisk->Lurn->Lur->LurFlags & LURFLAG_LOCKEDWRITE && IdeDisk->Lurn->Lur->LowestHwVer >= LANSCSIIDE_VERSION_1_1) {

		UINT32	LockContention;

		LockContention = 0;
		while(1) {


			//
			//	Acquire a write lock allocated for this unit device.
			//

			ntStatus = LspAcquireLock(
							&IdeDisk->LanScsiSession,
							SCSISTRG_WRITE_LOCKIDX_BASE + IdeDisk->LuData.LanscsiTargetID,
							NULL);
			if(!NT_SUCCESS(ntStatus)) {
				break;
			}
			if(ntStatus == STATUS_WAS_LOCKED) {
				KDPrintM(DBG_LURN_ERROR, ("Lock contention #%u!!!\n", LockContention));
			} else {
				break;
			}
			LockContention ++;


			//
			//	Clean up the lock on the NDAS device.
			//

			if(	LockContention % 1000 == 0) {

					LspCleanupLock(&IdeDisk->LanScsiSession,
						SCSISTRG_WRITE_LOCKIDX_BASE + IdeDisk->LuData.LanscsiTargetID);
			}

			if(LockContention >= 10000) {
				ntStatus = STATUS_UNSUCCESSFUL;
				break;
			}
		}
		if(!NT_SUCCESS(ntStatus)) {
			KDPrintM(DBG_LURN_ERROR, ("Failed to acquire lock #0\n"));
			return STATUS_UNSUCCESSFUL;
		}
	}


	//
	//	Send write LSP requests split by SplitBlock factor.
	//

	for(transferedBlocks = 0; transferedBlocks < transferBlocks; transferedBlocks += splitBlocks)
	{
		UINT32 BlocksToTransfer = (transferBlocks - transferedBlocks <= splitBlocks) ? transferBlocks - transferedBlocks : splitBlocks;
		UINT32 RetryCount = 0;
retry:
		LURNIDE_INITIALIZE_PDUDESC(
			IdeDisk, 
			&PduDesc, 
			IDE_COMMAND, 
			WIN_WRITE,
			logicalBlockAddress + transferedBlocks, 
			BlocksToTransfer,
			l_DataBuffer + (transferedBlocks << BLOCK_SIZE_BITS));
		PduDesc.Flags |= l_Flags;

		KDPrintM(DBG_LURN_NOISE,
			("logicalBlockAddress = %I64u, transferBlocks = %d\n",
			logicalBlockAddress + transferedBlocks, BlocksToTransfer));

		ntStatus = LspRequest(
						&IdeDisk->LanScsiSession,
						&PduDesc,
						&response
					);
		if ( IdeDisk->Lurn && IdeDisk->Lurn->Lur 
			&& (IdeDisk->Lurn->Lur->LurFlags & LURFLAG_NDAS_2_0_WRITE_CHECK)
			&& IdeDisk->LanScsiSession.HWVersion == LANSCSIIDE_VERSION_2_0
//			&& IdeDisk->LanScsiSession.HWRevision == 0
			&& NT_SUCCESS(ntStatus) && response == LANSCSI_RESPONSE_SUCCESS
			&& (logicalBlockAddress + transferedBlocks-1) < IdeDisk->Lurn->UnitBlocks) {
			// 2.0 rev 0 chip bug fix.
			// Check written data is correct and rewrite if it is incorrect.
			LURNIDE_INITIALIZE_PDUDESC(
				IdeDisk, 
				&PduDesc, 
				IDE_COMMAND, 
				WIN_READ,
				logicalBlockAddress + transferedBlocks, 
				BlocksToTransfer,
				IdeDisk->LanScsiSession.WriteCheckBuffer);
			PduDesc.Flags |= l_Flags;

			ntStatus = LspRequest(
							&IdeDisk->LanScsiSession,
							&PduDesc,
							&response
						);
			if (NT_SUCCESS(ntStatus) && response == LANSCSI_RESPONSE_SUCCESS) {
				// read success. Compare written data
				if (RtlCompareMemory(
					IdeDisk->LanScsiSession.WriteCheckBuffer,
					l_DataBuffer + (transferedBlocks << BLOCK_SIZE_BITS),
					BlocksToTransfer<< BLOCK_SIZE_BITS ) != (BlocksToTransfer<< BLOCK_SIZE_BITS)) {
					RetryCount++;
					if (RetryCount>3) {
						KDPrintM(DBG_LURN_ERROR,("Write-check failed multiple times. There may some other reason to fail.\n"));
					} else {
						KDPrintM(DBG_LURN_ERROR,
								("Write-check failed. Retrying %d\n", RetryCount));
						goto retry;
					}
				}
			}
		}

#if DBG
		if(!NT_SUCCESS(ntStatus) || response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR,
					("logicalBlockAddress = %I64x, transferBlocks = %d, transferedBlocks = %d\n",
					logicalBlockAddress, transferBlocks, transferedBlocks));
		}
#endif
		if(!NT_SUCCESS(ntStatus)) {

			KDPrintM(DBG_LURN_ERROR, ("Error when LspRequest 0x%x\n", ntStatus));

			Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
			goto diskwrite_exit;

		} else {
#if DBG
			if(response != LANSCSI_RESPONSE_SUCCESS)
				KDPrintM(DBG_LURN_ERROR, ("Device Report Error 0x%x\n", response));
#endif
			switch(response)
			{
			case LANSCSI_RESPONSE_SUCCESS:
				Ccb->CcbStatus = CCB_STATUS_SUCCESS;
				break;
			case LANSCSI_RESPONSE_T_NOT_EXIST:
				Ccb->CcbStatus = CCB_STATUS_NOT_EXIST;

				KDPrintM(DBG_LURN_ERROR,
				("CcbStatus: returned LANSCSI_RESPONSE_T_NOT_EXIST.\n"));
				goto diskwrite_exit;
				break;
			case LANSCSI_RESPONSE_T_BAD_COMMAND:
				Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
				KDPrintM(DBG_LURN_ERROR,
				("CcbStatus: returned LANSCSI_RESPONSE_T_BAD_COMMAND..\n"));
				goto diskwrite_exit;
				break;
			case LANSCSI_RESPONSE_T_COMMAND_FAILED:
				{
					UINT64	BadSector, i;

					KDPrintM(DBG_LURN_ERROR,
						("CcbStatus: returned LANSCSI_RESPONSE_T_COMMAND_FAILED\n"));

					//
					//	Figure out exact error status.
					//	Command will have status register's value,
					//	least bit has a validity of error register.
					//

//					if(PduDesc.Command & 0x01) {

						//
						// ID not found error!
						//	( Disk logical address not found )
						//

						if((!(PduDesc.Feature & ID_ERR) && (PduDesc.Feature & ABRT_ERR)) ||
							((PduDesc.Feature & ID_ERR) && !(PduDesc.Feature & ABRT_ERR))) {
							PSENSE_DATA	senseData;

							KDPrintM(DBG_LURN_ERROR,
								("IDE error: ID not found.\n"));

							senseData = Ccb->SenseBuffer;

							senseData->ErrorCode = 0x70;
							senseData->Valid = 1;
							senseData->SenseKey = SCSI_SENSE_VOL_OVERFLOW;
							senseData->AdditionalSenseLength = 0xb;
							senseData->AdditionalSenseCode = SCSI_ADSENSE_NO_SENSE;
							senseData->AdditionalSenseCodeQualifier = 0;

							Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
							ntStatus = STATUS_IO_DEVICE_ERROR;
							goto diskwrite_exit;
						}
//					}

					KDPrintM(DBG_LURN_ERROR,
						("CcbStatus: verifying...\n"));

					//
					// Search Bad Sector.
					//
					BadSector = logicalBlockAddress + transferedBlocks;

					for(i = 0; i < splitBlocks; i++) {

						LURNIDE_INITIALIZE_PDUDESC(
											IdeDisk,
											&PduDesc,
											IDE_COMMAND,
											WIN_WRITE,
											BadSector +i,
											1,
											&((PUCHAR)DataBuffer + 
												(transferedBlocks << BLOCK_SIZE_BITS))[i]);
						ntStatus = LspRequest(
										&IdeDisk->LanScsiSession,
										&PduDesc,
										&response
									);
						if(!NT_SUCCESS(ntStatus)) {

							KDPrintM(DBG_LURN_ERROR, ("Error when LspRequest to find Bad Sector. 0x%x\n", ntStatus));

							Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
							goto diskwrite_exit;
						}


						if(response != LANSCSI_RESPONSE_SUCCESS) {
							KDPrintM(DBG_LURN_ERROR,
								("Error: logicalBlockAddress = %x, transferBlocks = %x\n",
								BadSector, transferBlocks));
							break;
						} else {
							BadSector++;
						}
					}

					if(BadSector > logicalBlockAddress + transferBlocks) {
						KDPrintM(DBG_LURN_ERROR, ("No Bad Sector!!!\n"));

						Ccb->CcbStatus = CCB_STATUS_SUCCESS;

						ntStatus = STATUS_SUCCESS;
						goto diskwrite_exit;
					} else {

						KDPrintM(DBG_LURN_ERROR, ("Bad Sector is 0x%x Sense 0x%x pData 0x%x\n",
							BadSector, Ccb->SenseDataLength, Ccb->SenseBuffer));

						// Calc residual size.
						Ccb->ResidualDataLength = (ULONG)(transferBlocks - (BadSector - logicalBlockAddress)) * BLOCK_SIZE;

						if(Ccb->SenseBuffer != NULL) {
							PSENSE_DATA	senseData;

							senseData = Ccb->SenseBuffer;

							senseData->ErrorCode = 0x70;
							senseData->Valid = 1;
							//senseData->SegmentNumber = 0;
							senseData->SenseKey = SCSI_SENSE_MEDIUM_ERROR;
							//senseData->IncorrectLength = 0;
							//senseData->EndOfMedia = 0;
							//senseData->FileMark = 0;

							senseData->AdditionalSenseLength = 0xb;
							senseData->AdditionalSenseCode = SCSI_ADSENSE_NO_SENSE;
							senseData->AdditionalSenseCodeQualifier = 0;

						}
					}
				}

				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				ntStatus = STATUS_IO_DEVICE_ERROR;
				goto diskwrite_exit;
				break;

			case LANSCSI_RESPONSE_T_BROKEN_DATA:
			default:
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				ntStatus = STATUS_IO_DEVICE_ERROR;

				KDPrintM(DBG_LURN_ERROR,
				("CcbStatus: returned LANSCSI_RESPONSE_T_BROKEN_DATA or failure..\n"));
				goto diskwrite_exit;
				break;
			}
		}
	}

diskwrite_exit:

	//
	//	Release the lock in the NDAS device.
	//

	if(	IdeDisk->Lurn->Lur->LurFlags & LURFLAG_LOCKEDWRITE) {
		LspReleaseLock(&IdeDisk->LanScsiSession, 0, NULL);
	}

	if(RecoverBuffer && PduDesc.DataBuffer) {
		ASSERT(FALSE);
		DecryptBlock(
				IdeDisk->CntEcrCipher,
				IdeDisk->CntEcrKey,
				transferBlocks << BLOCK_SIZE_BITS,
				DataBuffer,
				DataBuffer
			);
	}

#ifdef __ENABLE_PERFORMACE_CHECK__

	PFCurrentTime = KeQueryPerformanceCounter(NULL);
	if(PFCurrentTime.QuadPart - PFStartTime.QuadPart > 10000) {
		DbgPrint("=PF= LurnIdeDiskWrite: addr:%d blk=%d elaps=%I64d\n", 
			logicalBlockAddress,
			transferBlocks,
			PFCurrentTime.QuadPart - PFStartTime.QuadPart
			);
	}

#endif

	return ntStatus;
}

NTSTATUS
LurnIdeDiskReserveRelease(
	IN PLURELATION_NODE		Lurn,
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN	PCCB				Ccb
){
	UNREFERENCED_PARAMETER(Lurn);
	UNREFERENCED_PARAMETER(IdeDisk);

	Ccb->CcbStatus = CCB_STATUS_SUCCESS;
	return STATUS_SUCCESS;
}


//
//	Execute CCB.
//

#ifndef __NDASBOOT__
static
#endif 

NTSTATUS
LurnIdeDiskExecute(
	PLURELATION_NODE		Lurn,
	PLURNEXT_IDE_DEVICE		IdeDisk,
	IN	PCCB				Ccb
)
{
	NTSTATUS			ntStatus;
	LANSCSI_PDUDESC		PduDesc;
	PCMD_COMMON			ext_cmd;

	KDPrintM(DBG_LURN_NOISE,
			("Entered\n"));
#if 0
	if(Ccb->Cdb[0] != SCSIOP_READ
		&& Ccb->Cdb[0] != SCSIOP_WRITE
		&& Ccb->Cdb[0] != SCSIOP_VERIFY)
	{
		KDPrintM(DBG_LURN_INFO,	("SCSIOP = 0x%x %s \n",
									Ccb->Cdb[0],
									CdbOperationString(Ccb->Cdb[0]))
								);
	}
#endif
	ntStatus = STATUS_SUCCESS;

	ASSERT(IdeDisk);

	// process extended command
	ext_cmd = Ccb->pExtendedCommand;
	while(NULL != ext_cmd)
	{
		// linked list
		switch(ext_cmd->Operation)
		{
		case CCB_EXT_READ:
		case CCB_EXT_WRITE:
		case CCB_EXT_READ_OPERATE_WRITE:
			{
				PCMD_BYTE_OP ext_cmd_op = (PCMD_BYTE_OP)ext_cmd;
				ULONG logicalBlockAddress;
				BYTE one_sector[BLOCK_SIZE];

				logicalBlockAddress = (ext_cmd_op->CountBack) ? 
					(ULONG)(IdeDisk->LuData.SectorCount) - ext_cmd_op->logicalBlockAddress : ext_cmd_op->logicalBlockAddress;
				switch(ext_cmd_op->Operation)
				{
				case CCB_EXT_READ:
					{
						switch(ext_cmd_op->ByteOperation)
						{
						case EXT_BLOCK_OPERATION:
							ntStatus = LurnIdeDiskRead(
										Ccb,
										IdeDisk,
										ext_cmd_op->pByteData,
										logicalBlockAddress,
										ext_cmd_op->LengthBlock,
										IdeDisk->LanScsiSession.MaxBlocksPerRequest);
							break;
						case EXT_BYTE_OPERATION_COPY:
							ntStatus = LurnIdeDiskRead(
										Ccb,
										IdeDisk,
										one_sector,
										logicalBlockAddress,
										1,
										IdeDisk->LanScsiSession.MaxBlocksPerRequest);
							RtlCopyMemory(ext_cmd_op->pByteData, one_sector + ext_cmd_op->Offset, ext_cmd_op->LengthByte);
							// write back
							break;
						default:
							ASSERT(FALSE);
							break;
						}

						KDPrintM(DBG_LURN_NOISE,	("CCB_EXT_READ\n"));

						if(!NT_SUCCESS(ntStatus))
						{
							return ntStatus;
						}
					}

					break;
				case CCB_EXT_WRITE:
					{
						//
						//	If the LUR is in Locked read/write mode,
						//	skip write access check.
						//

						if(!(Lurn->Lur->LurFlags & LURFLAG_SHAREDWRITE)) {

							if(!(Lurn->AccessRight & GENERIC_WRITE)){

								if(Lurn->Lur->LurFlags & LURFLAG_FAKEWRITE) {
									KDPrintM(DBG_LURN_INFO, ("CCB_EXT_WRITE: Fake Write. Address:%lu\n", logicalBlockAddress));

									break;

								} else {
									ASSERT(FALSE);
									Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
									return STATUS_UNSUCCESSFUL;
								}
							}
						}

						KDPrintM(DBG_LURN_NOISE,	("CCB_EXT_WRITE\n"));

						//				RtlZeroMemory(one_sector, sizeof(one_sector));

						switch(ext_cmd_op->ByteOperation)
						{
						case EXT_BLOCK_OPERATION:
							ntStatus = LurnIdeDiskWrite(
											Ccb,
											IdeDisk,
											ext_cmd_op->pByteData,
											logicalBlockAddress,
											ext_cmd_op->LengthBlock,
											IdeDisk->LanScsiSession.MaxBlocksPerRequest);
							break;
						case EXT_BYTE_OPERATION_COPY:
							RtlZeroMemory(one_sector, BLOCK_SIZE);
							RtlCopyMemory(one_sector + ext_cmd_op->Offset, ext_cmd_op->pByteData, ext_cmd_op->LengthByte);
							// write back
							ntStatus = LurnIdeDiskWrite(
												Ccb,
												IdeDisk,
												one_sector,
												logicalBlockAddress,
												1,
												IdeDisk->LanScsiSession.MaxBlocksPerRequest);
							break;
						case EXT_BYTE_OPERATION_AND:
						case EXT_BYTE_OPERATION_OR:
						default:
							ASSERT(FALSE);
							// NA JUNG YE
							break;
						}

						if(!NT_SUCCESS(ntStatus))
						{
							return ntStatus;
						}
					}

					break;
				case CCB_EXT_READ_OPERATE_WRITE:
					{
						KDPrintM(DBG_LURN_ERROR,	("CCB_EXT_READ_OPERATE_WRITE\n"));


						// AING : It costs lots of time for hard disk to read - write serially.
						// Do not use this operation often.

						// read sector

						ntStatus = LurnIdeDiskRead(
										Ccb,
										IdeDisk,
										one_sector,
										logicalBlockAddress,
										1,
										IdeDisk->LanScsiSession.MaxBlocksPerRequest);
						KDPrintM(DBG_LURN_ERROR,	("CCB_EXT_READ_OPERATE_WRITE read status %x\n", ntStatus));

						if(!NT_SUCCESS(ntStatus))
						{
							return ntStatus;
						}

						//
						//	If the LUR is in Locked read/write mode,
						//	skip write access check.
						//

						if(!(Lurn->Lur->LurFlags & LURFLAG_SHAREDWRITE)) {

							if(!(Lurn->AccessRight & GENERIC_WRITE)){

								if(Lurn->Lur->LurFlags & LURFLAG_FAKEWRITE) {
									KDPrintM(DBG_LURN_ERROR, ("CCB_EXT_WRITE: Fake Write. Address:%lu\n", logicalBlockAddress));
									break;

								} else {
									ASSERT(FALSE);
									Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
									return STATUS_UNSUCCESSFUL;
								}
							}
						}

						switch(ext_cmd_op->ByteOperation)
						{
						case EXT_BYTE_OPERATION_COPY:
							RtlCopyMemory(one_sector + ext_cmd_op->Offset, ext_cmd_op->pByteData, ext_cmd_op->LengthByte);
							break;
						case EXT_BYTE_OPERATION_AND:
						case EXT_BYTE_OPERATION_OR:
						default:
							ASSERT(FALSE);
							// NA JUNG YE
							break;
						}

						// write back
						ntStatus = LurnIdeDiskWrite(
										Ccb,
										IdeDisk,
										one_sector,
										logicalBlockAddress,
										1,
										IdeDisk->LanScsiSession.MaxBlocksPerRequest);

						KDPrintM(DBG_LURN_ERROR,	("CCB_EXT_READ_OPERATE_WRITE write status %x\n", ntStatus));

						if(!NT_SUCCESS(ntStatus))
						{
							return ntStatus;
						}
					}

					break;
				}
			}
			break;
		}

		ext_cmd = (PCMD_COMMON)ext_cmd->pNextCmd;
	}

	switch(Ccb->Cdb[0]) {

	case SCSIOP_INQUIRY:
	{
		INQUIRYDATA			inquiryData;

		KDPrintM(DBG_LURN_INFO,("SCSIOP_INQUIRY Ccb->Lun = 0x%x\n", Ccb->LurId[2]));


		//
		//	We don't support EVPD(enable vital product data).
		//
		if(Ccb->Cdb[1]  & CDB_INQUIRY_EVPD) {
			KDPrintM(DBG_LURN_ERROR,("SCSIOP_INQUIRY: got EVPD. Not supported.\n"));

			LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
			LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
			return STATUS_SUCCESS;
		}

		RtlZeroMemory(Ccb->DataBuffer, Ccb->DataBufferLength);
		RtlZeroMemory(&inquiryData, sizeof(INQUIRYDATA));

		inquiryData.DeviceType = DIRECT_ACCESS_DEVICE;
		inquiryData.DeviceTypeQualifier = DEVICE_QUALIFIER_ACTIVE;
		inquiryData.DeviceTypeModifier;
		inquiryData.RemovableMedia = FALSE;
		inquiryData.Versions = 2;
		inquiryData.ResponseDataFormat = 2;
		inquiryData.HiSupport;
		inquiryData.NormACA;
//		inquiryData.TerminateTask;
		inquiryData.AERC;
		inquiryData.AdditionalLength = 31;	// including ProductRevisionLevel.
//		inquiryData.MediumChanger;
//		inquiryData.MultiPort;
//		inquiryData.EnclosureServices;
		inquiryData.SoftReset;
		inquiryData.CommandQueue;
		inquiryData.LinkedCommands;
		inquiryData.RelativeAddressing;

		RtlCopyMemory(
			inquiryData.VendorId,
			NDAS_DISK_VENDOR_ID,
			(strlen(NDAS_DISK_VENDOR_ID)+1) < 8 ? (strlen(NDAS_DISK_VENDOR_ID)+1) : 8
			);

		RtlCopyMemory(
			inquiryData.ProductId,
			IdeDisk->LuData.Model,
			16
			);

		_snprintf(inquiryData.ProductRevisionLevel, 4, "%d.0", (int)IdeDisk->LanScsiSession.HWVersion);

		RtlMoveMemory (
					Ccb->DataBuffer,
					&inquiryData,
					Ccb->DataBufferLength > sizeof (INQUIRYDATA) ?
					sizeof (INQUIRYDATA) :
					Ccb->DataBufferLength
				);

		LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;

		return STATUS_SUCCESS;
	}

	case SCSIOP_READ_CAPACITY:
	{
		PREAD_CAPACITY_DATA	readCapacityData;
		ULONG				blockSize;
		UINT64				sectorCount;
		UINT64				logicalBlockAddress;

		sectorCount = Lurn->UnitBlocks;

		readCapacityData = (PREAD_CAPACITY_DATA)Ccb->DataBuffer;

		logicalBlockAddress = sectorCount - 1;

		if(logicalBlockAddress < 0xffffffff) {
			REVERSE_BYTES(&readCapacityData->LogicalBlockAddress, &logicalBlockAddress);
		} else {
			readCapacityData->LogicalBlockAddress =	0xffffffff;
		}

		blockSize = BLOCK_SIZE;
		REVERSE_BYTES(&readCapacityData->BytesPerBlock, &blockSize);

		Ccb->CcbStatus = CCB_STATUS_SUCCESS;

		KDPrintM(DBG_LURN_ERROR, ("SCSIOP_READ_CAPACITY: %I64u : %u\n", logicalBlockAddress, blockSize));

		return STATUS_SUCCESS;

	}

	case SCSIOP_READ_CAPACITY16:
	{
		PREAD_CAPACITY_DATA_EX		readCapacityDataEx;
		ULONG				blockSize;
		UINT64				sectorCount;
		UINT64				logicalBlockAddress;

		sectorCount = Lurn->UnitBlocks;

		readCapacityDataEx = (PREAD_CAPACITY_DATA_EX)Ccb->DataBuffer;

		logicalBlockAddress = sectorCount - 1;
		REVERSE_BYTES_QUAD(&readCapacityDataEx->LogicalBlockAddress.QuadPart, &logicalBlockAddress);

		blockSize = BLOCK_SIZE;
		REVERSE_BYTES(&readCapacityDataEx->BytesPerBlock, &blockSize);

		Ccb->CcbStatus = CCB_STATUS_SUCCESS;

		KDPrintM(DBG_LURN_ERROR, ("SCSIOP_READ_CAPACITY16: %I64u : %u\n", logicalBlockAddress, blockSize));

		return STATUS_SUCCESS;

	}

	case SCSIOP_RESERVE_UNIT:
		KDPrintM(DBG_LURN_INFO, ("RESERVE(6): %02x %02x %02x %02x %02x %02x\n",
												Ccb->Cdb[0], Ccb->Cdb[1],
												Ccb->Cdb[2], Ccb->Cdb[3],
												Ccb->Cdb[4], Ccb->Cdb[5]));
		ntStatus = LurnIdeDiskReserveRelease(Lurn,IdeDisk, Ccb);
		return STATUS_SUCCESS;

	case 0x56:					// RESERVE_UNIT(10)

		KDPrintM(DBG_LURN_INFO, ("RESERVE(10): %02x %02x %02x %02x %02x %02x\n",
												Ccb->Cdb[0], Ccb->Cdb[1],
												Ccb->Cdb[2], Ccb->Cdb[3],
												Ccb->Cdb[4], Ccb->Cdb[5],
												Ccb->Cdb[6], Ccb->Cdb[7],
												Ccb->Cdb[8], Ccb->Cdb[9]));

		LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_ILLEGAL_COMMAND, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		ntStatus = STATUS_SUCCESS;
		return STATUS_SUCCESS;

	case SCSIOP_RELEASE_UNIT:
		KDPrintM(DBG_LURN_INFO, ("RELEASE(6): %02x %02x %02x %02x %02x %02x\n",
												Ccb->Cdb[0], Ccb->Cdb[1],
												Ccb->Cdb[2], Ccb->Cdb[3],
												Ccb->Cdb[4], Ccb->Cdb[5]));
		ntStatus = LurnIdeDiskReserveRelease(Lurn,IdeDisk, Ccb);
		break;

	case 0x57:					// RELEASE_UNIT(10)
		KDPrintM(DBG_LURN_INFO, ("RELEASE(10): %02x %02x %02x %02x %02x %02x\n",
												Ccb->Cdb[0], Ccb->Cdb[1],
												Ccb->Cdb[2], Ccb->Cdb[3],
												Ccb->Cdb[4], Ccb->Cdb[5],
												Ccb->Cdb[6], Ccb->Cdb[7],
												Ccb->Cdb[8], Ccb->Cdb[9]));

		LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_ILLEGAL_COMMAND, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		ntStatus = STATUS_SUCCESS;
		return STATUS_SUCCESS;

	case SCSIOP_TEST_UNIT_READY:

		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		return STATUS_SUCCESS;

	case SCSIOP_VERIFY:
	case SCSIOP_VERIFY16:
	{
		UINT64	logicalBlockAddress;
		UINT64	endSector;
		UINT64	sectorCount;
		ULONG	transferBlocks;
		ULONG	totalTransferBlocks;
		BYTE	response;

		sectorCount = Lurn->UnitBlocks;

		if(Ccb->Cdb[0] == SCSIOP_VERIFY) {

			logicalBlockAddress = CDB10_LOGICAL_BLOCK_BYTE((PCDB)Ccb->Cdb);
			totalTransferBlocks = CDB10_TRANSFER_BLOCKS((PCDB)Ccb->Cdb);

		} else {

			logicalBlockAddress = CDB16_LOGICAL_BLOCK_BYTE((PCDB)Ccb->Cdb);
			totalTransferBlocks = CDB16_TRANSFER_BLOCKS((PCDB)Ccb->Cdb);

		}

		endSector = logicalBlockAddress + totalTransferBlocks - 1;

		if(endSector > sectorCount - 1) {
			KDPrintM(DBG_LURN_ERROR,
					("SCSIOP_VERIFY: block Overflows endSector = 0x%x, SectorCount = 0x%x\n",
					endSector, sectorCount));

			Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
			return STATUS_SUCCESS;
		}
		KDPrintM(DBG_LURN_INFO,
						("SCSIOP_VERIFY[%d,%d,%d]  Address:%I64u DataBufferLength = %lu, Sectors = %lu\n",
						(int)Ccb->LurId[0], (int)Ccb->LurId[1], (int)Ccb->LurId[2],
						logicalBlockAddress, Ccb->DataBufferLength, totalTransferBlocks));

DO_MORE:
		transferBlocks = (totalTransferBlocks > 128) ? 128 : totalTransferBlocks;
		totalTransferBlocks -= transferBlocks;

#ifdef __DIVIDED_MULTI_BOOT__
		{
			ULONG i;
			PNDAS_DMBI_INFORMATION pDMBI = (PNDAS_DMBI_INFORMATION)IdeDisk->LuData.OneSector;
			ULONG NameLen;

			KDPrintM(DBG_LURN_INFO,
						("SCSIOP_VERIFY_R[%d,%d,%d]  Address:%lu DataBufferLength = %lu, Sectors = %lu\n",
						(int)Ccb->LurId[0], (int)Ccb->LurId[1], (int)Ccb->LurId[2],
						logicalBlockAddress, Ccb->DataBufferLength, (ULONG)(transferBlocks)));
			
			for(i=0; i<pDMBI->HostCount; i++ ) {
				NameLen = strlen(HostID);
				if(pDMBI->Host[i].Valid && 
					(pDMBI->Host[i].Shared || RtlCompareMemory(HostID, pDMBI->Host[i].HostID, NameLen) == NameLen)) {
					if(logicalBlockAddress > (ULONG) pDMBI->Host[i].Sectors) {
						logicalBlockAddress = logicalBlockAddress - (ULONG)pDMBI->Host[i].Sectors;
					} else {
						break;
					}
				}			
			}
			if(i<pDMBI->HostCount) {
				logicalBlockAddress = (ULONG)pDMBI->Host[i].Start + logicalBlockAddress;
			}
			else {
				KDPrintM(DBG_LURN_ERROR, ("Can't translate Address:%lu\n", logicalBlockAddress ));
			}

			KDPrintM(DBG_LURN_INFO,
						("SCSIOP_VERIFY_T[%d,%d,%d]  Address:%lu DataBufferLength = %lu, Sectors = %lu\n",
						(int)Ccb->LurId[0], (int)Ccb->LurId[1], (int)Ccb->LurId[2],
						logicalBlockAddress, Ccb->DataBufferLength, (ULONG)(transferBlocks)));
		}
#endif __DIVIDED_MULTI_BOOT__

		LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &PduDesc, IDE_COMMAND, WIN_VERIFY,logicalBlockAddress,transferBlocks, 0);
		ntStatus = LspRequest(
							&IdeDisk->LanScsiSession,
							&PduDesc,
							&response
						);

		if(!NT_SUCCESS(ntStatus) || response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR,
				("SCSIOP_VERIFY Error: logicalBlockAddress = %x, transferBlocks = %x\n",
				logicalBlockAddress, transferBlocks));
		}


		if(!NT_SUCCESS(ntStatus)) {

			KDPrintM(DBG_LURN_ERROR, ("SCSIOP_VERIFY: Error when LspRequest 0x%x\n", ntStatus));

			Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
			return ntStatus;

		} else {

			if(response != LANSCSI_RESPONSE_SUCCESS)
				KDPrintM(DBG_LURN_ERROR, ("SCSIOP_VERIFY: Device Report Error 0x%x\n", response));

			switch(response) {
			case LANSCSI_RESPONSE_SUCCESS:
				Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;
				break;
			case LANSCSI_RESPONSE_T_NOT_EXIST:
				Ccb->CcbStatus = CCB_STATUS_NOT_EXIST;	//SRB_STATUS_NO_DEVICE;
				return STATUS_SUCCESS;

			case LANSCSI_RESPONSE_T_BAD_COMMAND:
				Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;	//SRB_STATUS_INVALID_REQUEST;
				return STATUS_SUCCESS;

			case LANSCSI_RESPONSE_T_COMMAND_FAILED:
				{
					UINT64	BadSector, i;

					//
					// Search Bad Sector.
					//
					BadSector = logicalBlockAddress;

					for(i = 0; i < transferBlocks; i++) {

						LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &PduDesc, IDE_COMMAND, WIN_VERIFY,BadSector,transferBlocks, 0);
						ntStatus = LspRequest(
										&IdeDisk->LanScsiSession,
										&PduDesc,
										&response
									);
						if(!NT_SUCCESS(ntStatus)) {

							KDPrintM(DBG_LURN_ERROR, ("SCSIOP_VERIFY: Error when LspRequest() for find Bad Sector. 0x%x\n", ntStatus));

							Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;

							return ntStatus;
						}


						if(response != LANSCSI_RESPONSE_SUCCESS) {
							KDPrintM(DBG_LURN_ERROR,
								("SCSIOP_VERIFY Error: logicalBlockAddress = %x, transferBlocks = %x\n",
								BadSector, transferBlocks));
							break;
						} else {
							BadSector++;
						}
					}

					if(BadSector > logicalBlockAddress + transferBlocks) {
						KDPrintM(DBG_LURN_ERROR, ("SCSIOP_VERIFY: No Bad Sector!!!\n"));

						Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;

						if(totalTransferBlocks)
							goto DO_MORE;

						return STATUS_SUCCESS;
					} else {

						KDPrintM(DBG_LURN_ERROR, ("SCSIOP_VERIFY: Bad Sector is 0x%x Sense 0x%x pData 0x%x\n",
							BadSector, Ccb->SenseDataLength, Ccb->SenseBuffer));

						// Calc residual size.
						Ccb->ResidualDataLength = (ULONG)(transferBlocks - (BadSector - logicalBlockAddress)) * BLOCK_SIZE;

						if(Ccb->SenseBuffer != NULL) {
							PSENSE_DATA	senseData;

							senseData = Ccb->SenseBuffer;

							senseData->ErrorCode = 0x70;
							senseData->Valid = 1;
							//senseData->SegmentNumber = 0;
							senseData->SenseKey = SCSI_SENSE_MEDIUM_ERROR;	//SCSI_SENSE_MISCOMPARE;
							//senseData->IncorrectLength = 0;
							//senseData->EndOfMedia = 0;
							//senseData->FileMark = 0;

							senseData->AdditionalSenseLength = 0xb;
							senseData->AdditionalSenseCode = SCSI_ADSENSE_NO_SENSE;
							senseData->AdditionalSenseCodeQualifier = 0;

						}
					}
				}

				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				return STATUS_SUCCESS;

			case LANSCSI_RESPONSE_T_BROKEN_DATA:
			default:
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;	//SRB_STATUS_ERROR;
				return STATUS_SUCCESS;
				break;
			}

			if(totalTransferBlocks)
				goto DO_MORE;
			return STATUS_SUCCESS;
		}

		if(totalTransferBlocks)
			goto DO_MORE;

		Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;
		return STATUS_SUCCESS;
	}

	case SCSIOP_START_STOP_UNIT:
	{
		PCDB		cdb = (PCDB)(Ccb->Cdb);
		UCHAR		response;
		NTSTATUS	status;

		if(cdb->START_STOP.Start == START_UNIT_CODE) {

			//
			//	Reset stop flag.
			//

			IdeDisk->LURNHostStop = FALSE;

			//
			//	get the disk information to get into  reconnecting process.
			//
			status = LurnIdeDiskConfigure(Lurn, &response);
#if DBG
			if(!NT_SUCCESS(status) ||
				response != LANSCSI_RESPONSE_SUCCESS) {
				KDPrintM(DBG_LURN_ERROR,
					("SCSIOP_START_STOP_UNIT: GetDiskInfo() failed."
					" We succeeded to raise the disconnect event."
					" Response:%d.\n", response));
			}
#endif

			KDPrintM(DBG_LURN_ERROR,
				("SCSIOP_START_STOP_UNIT: Start Unit %d %d.\n", Ccb->LurId[1], cdb->START_STOP.LogicalUnitNumber));
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;

		} else if(cdb->START_STOP.Start == STOP_UNIT_CODE) {

			//
			//	Set stop flag.
			//

			IdeDisk->LURNHostStop = TRUE;


			KDPrintM(DBG_LURN_ERROR,
				("SCSIOP_START_STOP_UNIT: Stop Unit %d %d.\n", Ccb->LurId[1], cdb->START_STOP.LogicalUnitNumber));
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;
		} else {
			KDPrintM(DBG_LURN_ERROR,
				("SCSIOP_START_STOP_UNIT: Invaild operation!!! %d %d.\n", Ccb->LurId[1], cdb->START_STOP.LogicalUnitNumber));
			Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;	//SRB_STATUS_INVALID_REQUEST;
		}

		//
		// CCB_STATUS_SUCCESS_TIMER will make
		// MiniportCompletionRoutine use timer for its completion.
		//
		LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		KDPrintM(DBG_LURN_ERROR, ("SCSIOP_START_STOP_UNIT CcbStatus:%d!!\n", Ccb->CcbStatus));

		return STATUS_SUCCESS;
	}

	case SCSIOP_MODE_SENSE:
	{
		PCDB	Cdb;
		PMODE_PARAMETER_HEADER	parameterHeader = (PMODE_PARAMETER_HEADER)Ccb->DataBuffer;
		PMODE_PARAMETER_BLOCK	parameterBlock =  (PMODE_PARAMETER_BLOCK)((PUCHAR)Ccb->DataBuffer + sizeof(MODE_PARAMETER_HEADER));

		RtlZeroMemory(
			Ccb->DataBuffer,
			sizeof(MODE_PARAMETER_HEADER) + sizeof(MODE_PARAMETER_BLOCK)
			);
		Cdb = (PCDB)Ccb->Cdb;
		if(	Cdb->MODE_SENSE.PageCode == MODE_SENSE_RETURN_ALL) {	// all pages
			ULONG	BlockCount;
			//
			// Make Header.
			//
			parameterHeader->ModeDataLength = sizeof(MODE_PARAMETER_HEADER) + sizeof(MODE_PARAMETER_BLOCK) - sizeof(parameterHeader->ModeDataLength);
			parameterHeader->MediumType = 00;	// Default medium type.

			if(!(Lurn->AccessRight & GENERIC_WRITE)) {
				KDPrintM(DBG_LURN_TRACE,
				("SCSIOP_MODE_SENSE: MODE_DSP_WRITE_PROTECT\n"));
				parameterHeader->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;

				if((Lurn->Lur->LurFlags & LURFLAG_FAKEWRITE) || (Lurn->Lur->LurFlags & LURFLAG_SHAREDWRITE))
					parameterHeader->DeviceSpecificParameter &= ~MODE_DSP_WRITE_PROTECT;
			} else
				parameterHeader->DeviceSpecificParameter &= ~MODE_DSP_WRITE_PROTECT;

			parameterHeader->BlockDescriptorLength = sizeof(MODE_PARAMETER_BLOCK);

			//
			// Make Block.
			//
			BlockCount = (ULONG)(Lurn->EndBlockAddr - Lurn->StartBlockAddr + 1);
			parameterBlock->DensityCode = 0;	// It is Reserved for direct access devices.
			parameterBlock->BlockLength[0] = (BYTE)(BLOCK_SIZE>>16);
			parameterBlock->BlockLength[1] = (BYTE)(BLOCK_SIZE>>8);
			parameterBlock->BlockLength[2] = (BYTE)(BLOCK_SIZE);

			Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;
			ntStatus = STATUS_SUCCESS;
		} else {
			KDPrintM(DBG_LURN_ERROR,
						("SCSIOP_MODE_SENSE: unsupported pagecode:%x\n", (ULONG)Cdb->MODE_SENSE.PageCode));
			LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
			Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;	//SRB_STATUS_SUCCESS;
			ntStatus = STATUS_SUCCESS;
		}

		break;
	}

	case 0x3E:		// READ_LONG
	case SCSIOP_READ:
	case SCSIOP_READ16:
	{
		UINT64	logicalBlockAddress;
		UINT64	endSector;
		UINT64	sectorCount;
		ULONG	transferBlocks;
		LONG	SplitSize;

		sectorCount = Lurn->UnitBlocks;

		if( Ccb->Cdb[0] == SCSIOP_READ ||
			Ccb->Cdb[0] == 0x3E) {
			logicalBlockAddress = CDB10_LOGICAL_BLOCK_BYTE((PCDB)Ccb->Cdb);
			transferBlocks = CDB10_TRANSFER_BLOCKS((PCDB)Ccb->Cdb);
		} else {
			logicalBlockAddress = CDB16_LOGICAL_BLOCK_BYTE((PCDB)Ccb->Cdb);
			transferBlocks = CDB16_TRANSFER_BLOCKS((PCDB)Ccb->Cdb);
		}

		// Check Buffer size.
		if(Ccb->DataBufferLength < (ULONG)(transferBlocks*BLOCK_SIZE)) {
			KDPrintM(DBG_LURN_ERROR,
						("SCSIOP_READ CCB Ccb->DataBufferLength = %d, Trans*BLOCK_SIZE = %d\n",
						Ccb->DataBufferLength, (ULONG)(transferBlocks*BLOCK_SIZE)));

			Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;	//SRB_STATUS_INVALID_REQUEST;
			return STATUS_SUCCESS;
		}
#if DBG
		if(LSCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE)) {
			KDPrintM(DBG_LURN_ERROR,
						("SCSIOP_READ[%d,%d,%d]  Address:%lu DataBufferLength = %lu, Sectors = %lu\n",
						(int)Ccb->LurId[0], (int)Ccb->LurId[1], (int)Ccb->LurId[2],
						logicalBlockAddress, Ccb->DataBufferLength, (ULONG)(transferBlocks)));
		}
#endif
		endSector = logicalBlockAddress + transferBlocks - 1;

		if(transferBlocks == 0) {
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;
			return STATUS_SUCCESS;
		}


		//
		//	If an IO is within barrier area,
		//	fail it.
		//

		if(endSector >= sectorCount) {
			if(endSector < sectorCount + 512) {
			KDPrintM(DBG_LURN_ERROR,
					("SCSIOP_READ: Block Overflows 0x%x, luExtension->SectorCount = 0x%x\n",
					endSector, sectorCount));

			Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;	//SRB_STATUS_INVALID_REQUEST;
			return STATUS_SUCCESS;
			}
		}

		//////////////////////////////////////////////////
		//
		// Send Read Request.
		//

		SplitSize = LurnIdeDiskGetReadSplit(&IdeDisk->LanScsiSession);

#ifdef __DIVIDED_MULTI_BOOT__
		if(pDMBI->Signature == NDAS_DMBI_SIGNATURE) 
		{
			ULONG i;
			ULONG NameLen;
			
			for(i=0; i<pDMBI->HostCount; i++ ) {
				NameLen = strlen(HostID);
				if(pDMBI->Host[i].Valid && 
					(pDMBI->Host[i].Shared || RtlCompareMemory(HostID, pDMBI->Host[i].HostID, NameLen) == NameLen)) {
					if(logicalBlockAddress > (ULONG) pDMBI->Host[i].Sectors - 1) {
						logicalBlockAddress = logicalBlockAddress - (ULONG)pDMBI->Host[i].Sectors;
					} else {
						break;
					}
				}			
			}
			if(i<pDMBI->HostCount) {
				logicalBlockAddress = (ULONG)pDMBI->Host[i].Start + logicalBlockAddress;
			}
			else {
				KDPrintM(DBG_LURN_ERROR, ("Can't translate Address:%lu\n", logicalBlockAddress ));
			}
			
		}
#endif __DIVIDED_MULTI_BOOT__

		ntStatus = LurnIdeDiskRead(
							Ccb,
							IdeDisk,
							Ccb->DataBuffer,
							logicalBlockAddress,
							transferBlocks,
							SplitSize);
		if(!NT_SUCCESS(ntStatus))
		{
			return ntStatus;
		}
		else
		{

			LurnIdeDiskAdjustReadSplit(&IdeDisk->LanScsiSession);
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;

			return STATUS_SUCCESS;
		}

	}

	case SCSIOP_WRITE:
	case SCSIOP_WRITE16:
	{
		UINT64	logicalBlockAddress;
		UINT64	endSector;
		UINT64	sectorCount;
		ULONG	transferBlocks;
		ULONG	SplitSize, i;
		BOOLEAN	overRange;

		sectorCount = (UINT32)Lurn->UnitBlocks;

		if(Ccb->Cdb[0] == SCSIOP_WRITE) {
			logicalBlockAddress = CDB10_LOGICAL_BLOCK_BYTE((PCDB)Ccb->Cdb);
			transferBlocks = CDB10_TRANSFER_BLOCKS((PCDB)Ccb->Cdb);
		} else {
			logicalBlockAddress = CDB16_LOGICAL_BLOCK_BYTE((PCDB)Ccb->Cdb);
			transferBlocks = CDB16_TRANSFER_BLOCKS((PCDB)Ccb->Cdb);
		}

#if DBG
		if(LSCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE)) {
			KDPrintM(DBG_LURN_ERROR,
						("SCSIOP_WRITE[%d,%d,%d]  Address:%lu DataBufferLength = %lu, Sectors = %lu\n",
						(int)Ccb->LurId[0], (int)Ccb->LurId[1], (int)Ccb->LurId[2],
						logicalBlockAddress, Ccb->DataBufferLength, (ULONG)(transferBlocks)));
		}
#endif

		if(transferBlocks == 0) {
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;
			return STATUS_SUCCESS;
		}
		// Check buffer size.
		if(Ccb->DataBufferLength < (ULONG)(transferBlocks * BLOCK_SIZE)) {
			KDPrintM(DBG_LURN_ERROR,
						("SCSIOP_WRITE CCB Ccb->DataBufferLength = %d, Trans*BLOCK_SIZE = %d\n",
						Ccb->DataBufferLength, (ULONG)(transferBlocks*BLOCK_SIZE)));

			Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;	//SRB_STATUS_INVALID_REQUEST;
			return STATUS_SUCCESS;
		}

		endSector = logicalBlockAddress + transferBlocks - 1;


		//
		//	If an IO is within barrier area,
		//	fail it.
		//

		if(endSector >= sectorCount) {
			if(endSector < sectorCount + 512) {

				KDPrintM(DBG_LURN_ERROR,
					("SCSIOP_WRITE: Block Overflows 0x%x, luExtension->SectorCount = 0x%x\n",
						endSector, sectorCount));
				ASSERT(FALSE);

				Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;	//SRB_STATUS_INVALID_REQUEST;
				return STATUS_SUCCESS;
			}
			overRange = TRUE;
		} else {
			overRange = FALSE;
		}

		//
		// If the LURN has rw access right,
		//	skip write filter.
		//

		if(!(Lurn->AccessRight & GENERIC_WRITE)){	// Read-only access write
			BOOLEAN	fakeWriteFilter = FALSE;

			if(Lurn->Lur->LurFlags & LURFLAG_SHAREDWRITE) {


				//
				// Fully shared write mode
				// Skip any write restriction.
				//

			} else {
				//
				//	PS write-share mode or else.
				//	Filter any writes.
				//

				fakeWriteFilter = TRUE;

				if(Lurn->Lur->LurFlags & LURFLAG_OOB_SHAREDWRITE) {


					//
					//	Shared OOB write mode.
					//	Filter only in-band writes.
					//

					if(overRange) {
						fakeWriteFilter = FALSE;
					}

				}
			}

			//
			//	Filter writes according to flags.
			//

			if(fakeWriteFilter) {

				if(Lurn->Lur->LurFlags & LURFLAG_FAKEWRITE) {	// Fake write
					KDPrintM(DBG_LURN_ERROR, ("SCSIOP_WRITE: Fake Write."
						"  Address:%lu DataBufferLength = %lu, Trans = %lu\n",
							logicalBlockAddress, Ccb->DataBufferLength,
							(ULONG)(transferBlocks)));

					Ccb->CcbStatus = CCB_STATUS_SUCCESS;
					return STATUS_SUCCESS;

				} else {
					KDPrintM(DBG_LURN_ERROR, ("WRITE: WRITE command"
						" received without Write accessright.\n"));
					Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
					return STATUS_UNSUCCESSFUL;
				}
			}
		}


		//////////////////////////////////////////////////
		//
		// Send Write Request.
		//

		SplitSize = LurnIdeDiskGetWriteSplit(&IdeDisk->LanScsiSession);

		i = 0;

#ifdef __DIVIDED_MULTI_BOOT__
		if(pDMBI->Signature == NDAS_DMBI_SIGNATURE) 
		{
			ULONG i;
			ULONG NameLen;			
			
			for(i=0; i<pDMBI->HostCount; i++ ) {
				NameLen = strlen(HostID);
				if(pDMBI->Host[i].Valid && 
					(pDMBI->Host[i].Shared || RtlCompareMemory(HostID, pDMBI->Host[i].HostID, NameLen) == NameLen)) {
					if(logicalBlockAddress > pDMBI->Host[i].Sectors - 1) {
						logicalBlockAddress = logicalBlockAddress - (ULONG)pDMBI->Host[i].Sectors;
					} else {
						break;
					}
				}			
			}

			if(i<pDMBI->HostCount) {
				logicalBlockAddress = (ULONG)pDMBI->Host[i].Start + logicalBlockAddress;
			}
			else {
				KDPrintM(DBG_LURN_ERROR, ("Can't translate Address:%lu\n", logicalBlockAddress ));
			}			
		}
#endif __DIVIDED_MULTI_BOOT__

		ntStatus = LurnIdeDiskWrite(Ccb, IdeDisk,
						(PVOID)((PCHAR)Ccb->DataBuffer),
						logicalBlockAddress,
						transferBlocks, SplitSize);

		if(!NT_SUCCESS(ntStatus))
		{
			return ntStatus;
		}
		else
		{
			LurnIdeDiskAdjustWriteSplit(&IdeDisk->LanScsiSession);
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;

			return STATUS_SUCCESS;
		}
	}

	//
	//	Upper devices send SCSIOP_SYNCHRONIZE_CACHE before hibernation.
	//	But, flush operation may not succeed because
	//	NICs always power down just before that time.
	//
	case SCSIOP_SYNCHRONIZE_CACHE:
	case SCSIOP_SYNCHRONIZE_CACHE16:
		//
		// CCB_STATUS_SUCCESS_TIMER will make MiniportCompletionRoutine use timer for its completion.
		//
		KDPrintM(DBG_LURN_TRACE, ("SCSIOP_SYNCHRONIZE_CACHE IN\n"));

		{
			LANSCSI_PDUDESC		PduDescTemp;
			BYTE				response;

			LURNIDE_INITIALIZE_PDUDESC(
				IdeDisk, 
				&PduDescTemp, 
				IDE_COMMAND, 
				WIN_FLUSH_CACHE, 
				0,
				0, 
				0);

			ntStatus = LspRequest(
				&IdeDisk->LanScsiSession,
				&PduDescTemp,
				&response
				);

			if(!NT_SUCCESS(ntStatus) || response != LANSCSI_RESPONSE_SUCCESS) {
				// just print error and ignore
				KDPrintM(DBG_LURN_ERROR,
					("SCSIOP_SYNCHRONIZE_CACHE : LspRequest failed : status - %x, response - %x\n", ntStatus, response));
			}
		}

		LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		ntStatus = STATUS_SUCCESS;

		KDPrintM(DBG_LURN_TRACE, ("SCSIOP_SYNCHRONIZE_CACHE OUT\n"));
		break;

	case SCSIOP_SEEK:
		LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		ntStatus = STATUS_SUCCESS;
		KDPrintM(DBG_LURN_ERROR, ("SCSIOP_SEEK CcbStatus:%d!!\n", Ccb->CcbStatus));
		break;

	//
	//	Non-supported commands.
	//
	case SCSIOP_REASSIGN_BLOCKS:
	case SCSIOP_REQUEST_SENSE:
	case SCSIOP_FORMAT_UNIT:
	case SCSIOP_MEDIUM_REMOVAL:
	case 0xF0:					// Vendor-specific commands
	case 0xF1:					// Vendor-specific commands
		LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_ILLEGAL_COMMAND, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		ntStatus = STATUS_SUCCESS;

#if DBG
		if(Ccb->Cdb[0] == SCSIOP_MEDIUM_REMOVAL) {
			PCDB	Cdb = (PCDB)Ccb->Cdb;

			KDPrintM(DBG_LURN_ERROR, ("SCSIOP_MEDIUM_REMOVAL: Prevent:%d Persistant:%d Control:0x%x\n",
								(int)Cdb->MEDIA_REMOVAL.Prevent,
								(int)Cdb->MEDIA_REMOVAL.Persistant,
								(int)Cdb->MEDIA_REMOVAL.Control
						));
		}
#endif
		KDPrintM(DBG_LURN_ERROR, ("%s: CcbStatus:%d!!\n", CdbOperationString(Ccb->Cdb[0]), Ccb->CcbStatus));
		break;

	//
	//	Invalid commands.
	//
	default:

		KDPrintM(DBG_LURN_ERROR, ("Bad Request.\n"));

		Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
		ntStatus = STATUS_SUCCESS;

		KDPrintM(DBG_LURN_ERROR, ("%s: CcbStatus:%d!!\n", CdbOperationString(Ccb->Cdb[0]), Ccb->CcbStatus));

		break;
	}

	return ntStatus;
}

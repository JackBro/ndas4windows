#include "ndasscsiproc.h"

#ifdef __MODULE__
#undef __MODULE__
#endif // __MODULE__
#define __MODULE__ "LurnIdeDisk"


#if !__NDAS_SCSI_NEW_LURN_IDE_DISK__

C_ASSERT(FIELD_OFFSET(struct hd_driveid, words71_74) == 71 * sizeof(unsigned short));
C_ASSERT(FIELD_OFFSET(struct hd_driveid, words130_155) == 130 * sizeof(unsigned short));
C_ASSERT(FIELD_OFFSET(struct hd_driveid, integrity_word) == 255 * sizeof(unsigned short));

//////////////////////////////////////////////////////////////////////////
//
//	IDE disk interfaces
//
NTSTATUS
IdeDiskLurnInitialize(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
	);

NTSTATUS
IdeDiskLurnRequest(
		PLURELATION_NODE Lurn,
		PCCB Ccb
	);

LURN_INTERFACE LurnIdeDiskInterface = {
					LSSTRUC_TYPE_LURN_INTERFACE,
					sizeof(LURN_INTERFACE),
					LURN_IDE_DISK,
					0, {
						IdeDiskLurnInitialize,
						IdeLurnDestroy,
						IdeDiskLurnRequest
				}
	};



//////////////////////////////////////////////////////////////////////////
//
//	Internal functions
//

static
VOID
LurnIdeDiskThreadProc(
		IN	PVOID	Context
	);


static
NTSTATUS
LurnIdeDiskCheckPowerMode(
		IN PLURELATION_NODE		Lurn,
		IN PLURNEXT_IDE_DEVICE	IdeDisk,
		IN PCCB					Ccb
);

static
NTSTATUS
LurnIdeDiskExecute(
	IN PLURELATION_NODE		Lurn,
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN	PCCB				Ccb
);

static
NTSTATUS
LurnIdeDiskSmart(
	IN PLURELATION_NODE		Lurn,
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN	PCCB				Ccb
);

static
NTSTATUS
LurnIdeDiskReadAttributes(
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN PATA_SMART_DATA		AtaSmartBuffer,
	OUT PCHAR				PduResponse
);

static
BOOLEAN
LurnIdeDiskFindAttribute(
	IN PATA_SMART_DATA			AtaSmartBuffer,
	IN UCHAR					AttributeId,
	OUT PATA_SMART_ATTRIBUTE	*AtaSmartAttribute
);

static
NTSTATUS
LurnIdeDiskGetPowerRecycleCount(
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	OUT PUINT64				PowerRecycleCount
);

//////////////////////////////////////////////////////////////////////////
//
//	configure IDE disk.
//
static
NTSTATUS
LurnIdeDiskConfigure(
		IN	PLURELATION_NODE	Lurn,
		OUT	PBYTE				PduResponse
	)
{
	NTSTATUS			status;
	PLURNEXT_IDE_DEVICE	IdeDisk;
	PLANSCSI_SESSION	LSS;
	struct hd_driveid	info;
	char				buffer[41];
	LANSCSI_PDUDESC		pduDesc;
	BOOLEAN				setDmaMode;
	LONG				retryRequest;
	UINT32				oldLudataFlsgs;
	UINT32				oldPduDescFlags;

	//
	// Init.
	//
	ASSERT(Lurn->LurnExtension);

	IdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;
	LSS = IdeDisk->LanScsiSession;
	oldLudataFlsgs = IdeDisk->LuHwData.LudataFlags;
	oldPduDescFlags = IdeDisk->LuHwData.PduDescFlags;
	IdeDisk->LuHwData.LudataFlags = 0;
	IdeDisk->LuHwData.PduDescFlags &= ~(PDUDESC_FLAG_LBA|PDUDESC_FLAG_LBA48|PDUDESC_FLAG_PIO|PDUDESC_FLAG_DMA|PDUDESC_FLAG_UDMA);

	KDPrintM(DBG_LURN_INFO, ("Target ID %d\n", IdeDisk->LuHwData.LanscsiTargetID));

	//
	// Issue identify command.
	//
	// Try identify command several times.
	// A hard disk with long spin-up time might make the NDAS chip non-responsive.
	// Hard disk spin-down -> NDASSCSI requests sector IO -> Hard disk spin-up ->
	// -> NDAS chip not response -> NDASSCSI enters reconnection ->
	// -> Identify in LurnIdeDiskConfigure() -> May be error return.
	//

	for(retryRequest = 0; retryRequest < 2; retryRequest ++) {
		LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &pduDesc, IDE_COMMAND, WIN_IDENTIFY, 0, 1, sizeof(info), &info, NULL);
		status = LspRequest(LSS, &pduDesc, PduResponse);
		if(NT_SUCCESS(status) && *PduResponse == LANSCSI_RESPONSE_SUCCESS) {
			break;
		}
		KDPrintM(DBG_LURN_ERROR, ("Identify Failed.Retry..\n"));
	}
	if(!NT_SUCCESS(status) || *PduResponse != LANSCSI_RESPONSE_SUCCESS) {
		KDPrintM(DBG_LURN_ERROR, ("Give up..\n"));
		// Recover the original flags.
		// 'Cause we did not get valid identify data.
		IdeDisk->LuHwData.LudataFlags = oldLudataFlsgs;
		IdeDisk->LuHwData.PduDescFlags = oldPduDescFlags;
		LurnRecordFault(Lurn, LURN_ERR_DISK_OP, LURN_FAULT_IDENTIFY, NULL);
		return NT_SUCCESS(status)?STATUS_UNSUCCESSFUL:status;
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
	KDPrintM(DBG_LURN_INFO, ("CFS1 0x%x, CFS1 enabled 0x%x\n",
							info.command_set_1,
							info.cfs_enable_1));
	KDPrintM(DBG_LURN_INFO, ("CFS2 0x%x, CFS2 enabled 0x%x\n",
							info.command_set_2,
							info.cfs_enable_2));
	KDPrintM(DBG_LURN_INFO, ("CFS ext 0x%x, CFS default 0x%x\n",
							info.cfsse,
							info.csf_default));

	//
	//	determine IO mode ( UltraDMA, DMA, and PIO ) according to hardware versions and disk capacity.
	//
	setDmaMode = FALSE;

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
		/*
			We don't support UDMA for 2.0 rev 0 due to the bug.
			The bug : Written data using UDMA will be corrupted
		*/
		if ((disableUDMA == FALSE) &&
			(info.dma_ultra & 0x00ff) &&
			(
				(LANSCSIIDE_VERSION_2_0 < IdeDisk->LuHwData.HwVersion) ||
				((LANSCSIIDE_VERSION_2_0 == IdeDisk->LuHwData.HwVersion) && (0 != IdeDisk->LuHwData.HwRevision))
			))
		{
			// Find Fastest Mode.
			if(info.dma_ultra & 0x0001)
				DmaMode = 0;
			if(info.dma_ultra & 0x0002)
				DmaMode = 1;
			if(info.dma_ultra & 0x0004)
				DmaMode = 2;
			//	if Cable80, try higher Ultra Dma Mode.
#if __DETECT_CABLE80__
			if(info.hw_config & 0x2000) {
#endif
				if(info.dma_ultra & 0x0008)
					DmaMode = 3;
				if(info.dma_ultra & 0x0010)
					DmaMode = 4;
				// Limit UDMA mode to 4 only to reduce write-failure.
				if(info.dma_ultra & 0x0020)
					DmaMode = 5;
				if(info.dma_ultra & 0x0040)
					DmaMode = 6;
				if(info.dma_ultra & 0x0080)
					DmaMode = 7;

				//
				// If the ndas device is version 2.0 revision 100Mbps,
				// Restrict UDMA to mode 2.
				//

				if (IdeDisk->LuHwData.HwVersion == LANSCSIIDE_VERSION_2_0 && 
					IdeDisk->LuHwData.HwRevision == LANSCSIIDE_VER20_REV_100M) {

					if(DmaMode > 2)
						DmaMode = 2;
				}

				//
				// Restrict UDMA mode when requested.
				//

				if(Lurn->UDMARestrictValid) {
					if(DmaMode > Lurn->UDMARestrict) {
						DmaMode = Lurn->UDMARestrict;
						KDPrintM(DBG_LURN_INFO, ("UDMA restriction applied. UDMA=%d\n", (ULONG)DmaMode));
					}
				}

#if __DETECT_CABLE80__
			}
#endif
			KDPrintM(DBG_LURN_INFO, ("Ultra DMA %d detected.\n", (int)DmaMode));
			DmaFeature = DmaMode | 0x40;	// Ultra DMA mode.
			IdeDisk->LuHwData.PduDescFlags |= PDUDESC_FLAG_DMA|PDUDESC_FLAG_UDMA;

#if 0
			// Set Ultra DMA mode if needed
			if(!(info.dma_ultra & (0x0100 << DmaMode))) {
				SetDmaMode = TRUE;
			}
#else
			// Always set DMA mode because NDAS chip and HDD may have different DMA setting.
			setDmaMode = TRUE;
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
			IdeDisk->LuHwData.PduDescFlags |= PDUDESC_FLAG_DMA;

#if 0
			// Set DMA mode if needed
			if(!(info.dma_mword & (0x0100 << DmaMode))) {
				SetDmaMode = TRUE;
			}
#else
			// Always set DMA mode because NDAS chip and HDD may have different DMA setting.
			setDmaMode = TRUE;
#endif
		}

		// Set DMA mode if needed.
		if(setDmaMode) {
			LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &pduDesc, IDE_COMMAND, WIN_SETFEATURES, 0, 0, 0, NULL, NULL);
			pduDesc.Feature = SETFEATURES_XFER;
			pduDesc.BlockCount = DmaFeature;
			status = LspRequest(LSS, &pduDesc, PduResponse);
			if(!NT_SUCCESS(status) || *PduResponse != LANSCSI_RESPONSE_SUCCESS) {
				KDPrintM(DBG_LURN_ERROR, ("Set Feature Failed...\n"));
				return status;
			}

			// identify.
			LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &pduDesc, IDE_COMMAND, WIN_IDENTIFY, 0, 1, sizeof(info), &info, NULL);
			status = LspRequest(LSS, &pduDesc, PduResponse);
			if(!NT_SUCCESS(status) || *PduResponse != LANSCSI_RESPONSE_SUCCESS) {
				KDPrintM(DBG_LURN_ERROR, ("Identify Failed...\n"));
				LurnRecordFault(Lurn, LURN_ERR_DISK_OP, LURN_FAULT_IDENTIFY, NULL);
				return status;
			}
			KDPrintM(DBG_LURN_INFO, ("After Set Feature DMA 0x%x, U-DMA 0x%x\n",
							info.dma_mword,
							info.dma_ultra));
		}
		if(IdeDisk->LuHwData.PduDescFlags & PDUDESC_FLAG_DMA) {
			break;
		}
		//
		//	PIO.
		//
		KDPrintM(DBG_LURN_ERROR, ("NetDisk does not support DMA mode. Turn to PIO mode.\n"));
		IdeDisk->LuHwData.PduDescFlags |= PDUDESC_FLAG_PIO;

	} while(0);


	//
	//	Product strings.
	//
	ConvertString((PCHAR)buffer, (PCHAR)info.serial_no, 20);
	RtlCopyMemory(IdeDisk->LuHwData.Serial, buffer, 20);
	KDPrintM(DBG_LURN_INFO, ("Serial No: %s\n", buffer));

	ConvertString((PCHAR)buffer, (PCHAR)info.fw_rev, 8);
	RtlCopyMemory(IdeDisk->LuHwData.FW_Rev, buffer, 8);
	KDPrintM(DBG_LURN_INFO, ("Firmware rev: %s\n", buffer));

	ConvertString((PCHAR)buffer, (PCHAR)info.model, 40);
	RtlCopyMemory(IdeDisk->LuHwData.Model, buffer, 40);
	KDPrintM(DBG_LURN_INFO, ("Model No: %s\n", buffer));

	//
	// Bytes of sector
	//

	IdeDisk->LuHwData.BlockBytes = (UINT16)AtaGetBytesPerBlock(&info);

	//
	// Support LBA?
	//

	if(!(info.capability & 0x02)) {
		IdeDisk->LuHwData.PduDescFlags &= ~PDUDESC_FLAG_LBA;
		ASSERT(FALSE);
	} else {
		IdeDisk->LuHwData.PduDescFlags |= PDUDESC_FLAG_LBA;
	}

	//
	// Support LBA48.
	// Calc Capacity.
	//
	if((info.command_set_2 & 0x0400) && (info.cfs_enable_2 & 0x0400)) {	// Support LBA48bit
		IdeDisk->LuHwData.PduDescFlags |= PDUDESC_FLAG_LBA48;
		IdeDisk->LuHwData.SectorCount = info.lba_capacity_2;
	} else {
		IdeDisk->LuHwData.PduDescFlags &= ~PDUDESC_FLAG_LBA48;

		if((info.capability & 0x02) && Lba_capacity_is_ok(&info)) {
			IdeDisk->LuHwData.SectorCount = info.lba_capacity;
		} else
			IdeDisk->LuHwData.SectorCount = info.cyls * info.heads * info.sectors;
	}

	KDPrintM(DBG_LURN_INFO, ("LBA %d, LBA48 %d, Number of Sectors: %I64d\n",
				(IdeDisk->LuHwData.PduDescFlags & PDUDESC_FLAG_LBA) != 0,
				(IdeDisk->LuHwData.PduDescFlags & PDUDESC_FLAG_LBA48) != 0,
				IdeDisk->LuHwData.SectorCount
		));

	//
	// Support smart?
	//

	if(info.command_set_1 & 0x0001) {
		KDPrintM(DBG_LURN_INFO, ("Smart supported.\n"));
		IdeDisk->LuHwData.LudataFlags |= LUDATA_FLAG_SMART_SUPPORT;
		if(info.cfs_enable_1 & 0x0001) {
			KDPrintM(DBG_LURN_INFO, ("Smart enabled.\n"));
			IdeDisk->LuHwData.LudataFlags |= LUDATA_FLAG_SMART_ENABLED;
		}
	}

	//
	// Power management feature set enabled?
	//

	if(info.command_set_1 & 0x0008) {
		KDPrintM(DBG_LURN_INFO, ("Power management supported\n"));
		IdeDisk->LuHwData.LudataFlags |= LUDATA_FLAG_POWERMGMT_SUPPORT;
		if(info.cfs_enable_1 & 0x0008) {
			IdeDisk->LuHwData.LudataFlags |= LUDATA_FLAG_POWERMGMT_ENABLED;
			KDPrintM(DBG_LURN_INFO, ("Power management enabled\n"));
		}
	}

	//
	// Write cache enabled?
	//

	if(info.command_set_1 & 0x20) {
		IdeDisk->LuHwData.LudataFlags |= LUDATA_FLAG_WCACHE_SUPPORT;
		KDPrintM(DBG_LURN_INFO, ("Write cache supported\n"));
		if(info.cfs_enable_1 & 0x20) {
			IdeDisk->LuHwData.LudataFlags |= LUDATA_FLAG_WCACHE_ENABLED;
			KDPrintM(DBG_LURN_INFO, ("Write cache enabled\n"));
		}
	}

	//
	// FUA ( force unit access ) feature set enabled?
	//

	if(info.cfsse & 0x0040) {
		KDPrintM(DBG_LURN_INFO, ("Native force unit access support.\n"));
		IdeDisk->LuHwData.LudataFlags |= LUDATA_FLAG_FUA_SUPPORT;
		if(info.csf_default & 0x0040) {
			IdeDisk->LuHwData.LudataFlags |= LUDATA_FLAG_FUA_ENABLED;
			KDPrintM(DBG_LURN_INFO, ("Native force unit access enabled.\n"));
		}
	}

	return STATUS_SUCCESS;
}


//
//	Initialize IDE Disk
//

NTSTATUS
IdeDiskLurnInitialize(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
	)
{
	NTSTATUS				status;
	PLURNEXT_IDE_DEVICE		IdeDisk;
	// Indicate job done.
	BOOLEAN					Connection;
	BOOLEAN					LogIn;
	BOOLEAN					ThreadRef;
	BOOLEAN					LssEncBuff;
	BOOLEAN					lockMgmtInit;

	OBJECT_ATTRIBUTES		objectAttributes;
	LSSLOGIN_INFO			loginInfo;
	LSPROTO_TYPE			LSProto;
	BYTE					response;
	LARGE_INTEGER			defaultTimeOut;
	ULONG					maxTransferLength;
	BOOLEAN					FirstInit;
	BOOLEAN					initialBuffControlState;

	KDPrintM(DBG_LURN_ERROR,("IN Lurn = %p, LurnDesc = %p\n", Lurn, LurnDesc) );


	Connection = FALSE;
	LogIn = FALSE;
	ThreadRef = FALSE;
	LssEncBuff = FALSE;
	lockMgmtInit = FALSE;

	// already alive
	if(LURN_STATUS_RUNNING == Lurn->LurnStatus)
		return STATUS_SUCCESS;

	if(!LurnDesc)
	{
		//
		// revive cycle
		//
		KIRQL oldIrql;
		FirstInit = FALSE;
		
		ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);
		if(LURN_IS_RUNNING(Lurn->LurnStatus))
		{
			RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);
			KDPrintM(DBG_LURN_ERROR,("Lurn already running.\n"));
			return STATUS_UNSUCCESSFUL;
		}

		RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

		// resurrection cycle
		ASSERT(Lurn->LurnExtension);

		IdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;

		ASSERT(IdeDisk->Lurn == Lurn);

		//
		// Reset stop reason status flags.
		//

		Lurn->LurnStopReasonCcbStatusFlags = 0;

		//
		// If the IDE extension has a content encryption buffer,
		// LSS does not need a encryption buffer.
		//

		if(IdeDisk->CntEcrBufferLength && IdeDisk->CntEcrBuffer) {
			LssEncBuff = FALSE;
		} else {
			LssEncBuff = TRUE;
		}

		status = LspReconnect(IdeDisk->LanScsiSession, NULL, NULL, NULL);

		if(!NT_SUCCESS(status)) {
			LurnRecordFault(Lurn, LURN_ERR_CONNECT, status, NULL);
			KDPrintM(DBG_LURN_ERROR,("failed.\n"));
			// Nothing to clean up. Just return
			return status;
		}
	
		Connection = TRUE;

		if ((Lurn->AccessRight & GENERIC_WRITE) && !(IdeDisk->LanScsiSession->UserID & 0xffff0000)) {
			KDPrintM(DBG_LURN_ERROR,("LSS don't have write user ID but Lurn has write access. Updating user ID\n"));
			//
			//	Add Write access.
			//
			IdeDisk->LanScsiSession->UserID = IdeDisk->LanScsiSession->UserID | (IdeDisk->LanScsiSession->UserID << 16);
		}

		status = LspRelogin(IdeDisk->LanScsiSession, LssEncBuff, NULL);
		
		if(NT_SUCCESS(status)) {
			//
			//	Reconnecting succeeded.
			//	Reset Stall counter.
			//
			LogIn = TRUE;

			ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);
//			ASSERT(Lurn->LurnStatus == LURN_STATUS_STALL);
			Lurn->LurnStatus = LURN_STATUS_RUNNING;
			RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);
			InterlockedExchange(&Lurn->NrOfStall, 0);
		} else {
			LurnRecordFault(Lurn, LURN_ERR_LOGIN, IdeDisk->LanScsiSession->LastLoginError, NULL);
			goto error_out;
		}

		//
		// Save power recycle count to detect power recycle during reconnection.
		//

		IdeDisk->DiskWriteCacheDirty = FALSE;

		if(IdeDisk->LuHwData.LudataFlags & LUDATA_FLAG_SMART_ENABLED) {
			status = LurnIdeDiskGetPowerRecycleCount(
										IdeDisk,
										&IdeDisk->LuHwData.InitialDevPowerRecycleCount
				);
			if(status == STATUS_NOT_SUPPORTED) {
				KDPrintM(DBG_LURN_ERROR, ("No Power recycle count. Initial power recycle count = zero\n"));
			} else if(!NT_SUCCESS(status) /*|| response != LANSCSI_RESPONSE_SUCCESS*/) {
				KDPrintM(DBG_LURN_ERROR,
					("LurnIdeDiskGetPowerRecycleCount() failed. STATUS:%08lx\n", status));
				goto error_out;
			} else {
				KDPrintM(DBG_LURN_ERROR, ("Initial power recycle count = %I64u\n", IdeDisk->LuHwData.InitialDevPowerRecycleCount));
				ASSERT(IdeDisk->LuHwData.InitialDevPowerRecycleCount);
			}
		} else {
			//
			// If SMART disabled, continue without power recycle count.
			// Disable power recycle detection using SMART.
			//
			IdeDisk->LuHwData.InitialDevPowerRecycleCount = 0;
			KDPrintM(DBG_LURN_ERROR, ("SMART disabled. Initial power recycle count = zero\n"));
		}
	}
	else
	{
		//
		// initializing cycle
		//
		FirstInit = TRUE;
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
		IdeDisk->LuHwData.HwType = LurnDesc->LurnIde.HWType;
		IdeDisk->LuHwData.HwVersion = LurnDesc->LurnIde.HWVersion;
		IdeDisk->LuHwData.HwRevision = LurnDesc->LurnIde.HWRevision;
		IdeDisk->LuHwData.LanscsiTargetID = LurnDesc->LurnIde.LanscsiTargetID;
		IdeDisk->LuHwData.LanscsiLU = LurnDesc->LurnIde.LanscsiLU;
		IdeDisk->MaxDataSendLength = LurnDesc->MaxDataSendLength;
		IdeDisk->MaxDataRecvLength = LurnDesc->MaxDataRecvLength;

		IdeDisk->LanScsiSession	= &IdeDisk->CommLanScsiSession;

		//
		// Data transfer length used for allocation of LSS encryption,
		// content encryption, and write check buffer.
		//

		maxTransferLength = IdeDisk->MaxDataSendLength > IdeDisk->MaxDataRecvLength ? 
				IdeDisk->MaxDataSendLength:IdeDisk->MaxDataRecvLength;

		//
		// Write check buffer if needed
		//

		if(	IdeDisk->LuHwData.HwVersion == LANSCSIIDE_VERSION_2_0 &&
			IdeDisk->LuHwData.HwRevision == LANSCSIIDE_VER20_REV_1G_ORIGINAL) {

			IdeDisk->WriteCheckBuffer = ExAllocatePoolWithTag( NonPagedPool, maxTransferLength, LURNEXT_WRITECHECK_BUFFER_POOLTAG);
			if(!IdeDisk->WriteCheckBuffer) {
				// Continue without write check function
				KDPrintM(DBG_PROTO_ERROR, ("Error! Could not allocate the write-check buffer!\n"));
			}
		} else {
			IdeDisk->WriteCheckBuffer = NULL;
		}

		//
		//	Update lowest h/w version among NDAS devices
		//

		if(Lurn->Lur->LowestHwVer > LurnDesc->LurnIde.HWVersion) {
			Lurn->Lur->LowestHwVer = LurnDesc->LurnIde.HWVersion;
		}

#if __NDAS_SCSI_BOUNDARY_CHECK__

		if (Lurn->Lur->MaxChildrenSectorCount < IdeDisk->LuHwData.SectorCount) {

			Lurn->Lur->MaxChildrenSectorCount = IdeDisk->LuHwData.SectorCount;
		}

#endif

		KeInitializeEvent(
				&IdeDisk->ThreadCcbArrivalEvent,
				NotificationEvent,
				FALSE
			);
		InitializeListHead(&IdeDisk->CcbQueue);

		KeInitializeEvent(
				&IdeDisk->ThreadReadyEvent,
				NotificationEvent,
				FALSE
			);

		//
		//	Confirm address type.
		//	Connect to the NDAS Node.
		//
#if !__NDAS_SCSI_LPXTDI_V2__
		status = LstransAddrTypeToTransType( LurnDesc->LurnIde.BindingAddress.Address[0].AddressType, &IdeDisk->LstransType);
		if(!NT_SUCCESS(status)) {
			KDPrintM(DBG_LURN_ERROR, ("LstransAddrTypeToTransType(), Can't Connect to the LS node. STATUS:0x%08x\n", status));
			goto error_out;
		}
#else
		IdeDisk->AddressType = LurnDesc->LurnIde.BindingAddress.Address[0].AddressType;
#endif

		//
		// Set default time out
		//

		defaultTimeOut.QuadPart = -LURNIDE_GENERIC_TIMEOUT;
		LspSetDefaultTimeOut(IdeDisk->LanScsiSession, &defaultTimeOut);

#if !__NDAS_SCSI_LPXTDI_V2__
	
		status = LspConnectMultiBindAddr(
						IdeDisk->LanScsiSession,
						&LurnDesc->LurnIde.BindingAddress,
						&LurnDesc->LurnIde.BindingAddress,
						NULL,
						&LurnDesc->LurnIde.TargetAddress,
						TRUE,
						NULL,
						NULL,
						NULL
					);

#else

		status = LspConnectMultiBindAddr( IdeDisk->LanScsiSession,
										  &LurnDesc->LurnIde.BindingAddress,
										  NULL,
										  NULL,
										  &LurnDesc->LurnIde.TargetAddress,
										  TRUE,
										  NULL );

#endif

		if(!NT_SUCCESS(status)) {
			LurnRecordFault(Lurn, LURN_ERR_CONNECT, status, NULL);			
			KDPrintM(DBG_LURN_ERROR, ("LspConnectMultiBindAddr(), Can't Connect to the LS node. STATUS:0x%08x\n", status));
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

		loginInfo.LoginType	= LOGIN_TYPE_NORMAL;
		RtlCopyMemory(&loginInfo.UserID, &LurnDesc->LurnIde.UserID, LSPROTO_USERID_LENGTH);
		RtlCopyMemory(&loginInfo.Password, &LurnDesc->LurnIde.Password, LSPROTO_PASSWORD_LENGTH);
		loginInfo.MaxDataTransferLength = maxTransferLength;
		ASSERT(loginInfo.MaxDataTransferLength<=65536);

		loginInfo.LanscsiTargetID = LurnDesc->LurnIde.LanscsiTargetID;
		loginInfo.LanscsiLU = LurnDesc->LurnIde.LanscsiLU;
		loginInfo.HWType = LurnDesc->LurnIde.HWType;
		loginInfo.HWVersion = LurnDesc->LurnIde.HWVersion;
		loginInfo.HWRevision = LurnDesc->LurnIde.HWRevision;
		status = LspLookupProtocol(LurnDesc->LurnIde.HWType, LurnDesc->LurnIde.HWVersion, &LSProto);
		if(!NT_SUCCESS(status)) {
			goto error_out;
		}
		if(Lurn->Lur->CntEcrMethod) {
			//
			//	IdeLurn will have a encryption buffer.
			//
			loginInfo.IsEncryptBuffer = FALSE;
			KDPrintM(DBG_LURN_INFO, ("IDE Lurn extension is supposed to have a encryption buffer.\n"));
		} else {
			//
			//	IdeLurn will NOT have a encryption buffer.
			//	LSS should have a buffer to speed up.
			//
			loginInfo.IsEncryptBuffer = TRUE;
			KDPrintM(DBG_LURN_INFO, ("LSS is supposed to have a encryption buffer.\n"));
		}

		status = LspLogin(
						IdeDisk->LanScsiSession,
						&loginInfo,
						LSProto,
						NULL,
						TRUE
					);
		if(!NT_SUCCESS(status)) {
			LurnRecordFault(Lurn, LURN_ERR_LOGIN, IdeDisk->LanScsiSession->LastLoginError, NULL);
			KDPrintM(DBG_LURN_ERROR, ("LspLogin(), Can't login to the LS node"
				" with UserID:%08lx. STATUS:0x%08x.\n", loginInfo.UserID, status));
			status = STATUS_ACCESS_DENIED;
			goto error_out;
		}
		LogIn = TRUE;

		//
		//	Configure the disk.
		//
		status = LurnIdeDiskConfigure(Lurn, &response);
		if(!NT_SUCCESS(status) || response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR,
					("LurnIdeDiskConfigure() failed. Response:%d. STATUS:%08lx\n", response, status));
			goto error_out;
		}


		//
		// Save power recycle count to detect power recycle during reconnection.
		//

		if(IdeDisk->LuHwData.LudataFlags & LUDATA_FLAG_SMART_ENABLED) {
			status = LurnIdeDiskGetPowerRecycleCount(
										IdeDisk,
										&IdeDisk->LuHwData.InitialDevPowerRecycleCount
				);
			if(status == STATUS_NOT_SUPPORTED) {
				KDPrintM(DBG_LURN_ERROR, ("No Power recycle count. Initial power recycle count = zero\n"));
			} else if(!NT_SUCCESS(status) /*|| response != LANSCSI_RESPONSE_SUCCESS*/) {
				KDPrintM(DBG_LURN_ERROR,
					("LurnIdeDiskGetPowerRecycleCount() failed. Response:%d. STATUS:%08lx\n", response, status));
				goto error_out;
			} else {
				KDPrintM(DBG_LURN_ERROR, ("Initial power recycle count = %I64u\n", IdeDisk->LuHwData.InitialDevPowerRecycleCount));
				ASSERT(IdeDisk->LuHwData.InitialDevPowerRecycleCount);
			}
		} else {
			//
			// If SMART disabled, continue without power recycle count.
			// Disable power recycle detection using SMART.
			//
			IdeDisk->LuHwData.InitialDevPowerRecycleCount = 0;
			KDPrintM(DBG_LURN_ERROR, ("SMART disabled. Initial power recycle count = zero\n"));
		}

		//
		// Set bytes of block returned by configuration function.
		//

		Lurn->ChildBlockBytes = IdeDisk->LuHwData.BlockBytes;
		Lurn->BlockBytes = Lurn->ChildBlockBytes;
		
		Lurn->MaxDataSendLength = IdeDisk->MaxDataSendLength;
		Lurn->MaxDataRecvLength = IdeDisk->MaxDataRecvLength;

		//
		// Traffic control configuration for read/write operation
		// Start with max possible blocks.
		//

		IdeDisk->WriteSplitSize	= IdeDisk->MaxDataSendLength;
		IdeDisk->ReadSplitSize	= IdeDisk->MaxDataRecvLength;
		ASSERT(IdeDisk->WriteSplitSize <= 65536);
		ASSERT(IdeDisk->WriteSplitSize >0);
		ASSERT(IdeDisk->ReadSplitSize <= 65536);
		ASSERT(IdeDisk->ReadSplitSize >0);

		//
		//	Content encryption.
		//
		IdeDisk->CntEcrMethod		= Lurn->Lur->CntEcrMethod;
		if(IdeDisk->CntEcrMethod) {
			PUCHAR	pPassword;
			PUCHAR	pKey;
			ULONG	encBuffLen;

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
			encBuffLen = IdeDisk->MaxDataSendLength > IdeDisk->MaxDataRecvLength ?
				IdeDisk->MaxDataSendLength:IdeDisk->MaxDataRecvLength;
			IdeDisk->CntEcrBuffer = ExAllocatePoolWithTag(NonPagedPool, encBuffLen, LURNEXT_ENCBUFF_TAG);
			if(IdeDisk->CntEcrBuffer) {
				IdeDisk->CntEcrBufferLength = encBuffLen;
			} else {
				IdeDisk->CntEcrBufferLength = 0;
				KDPrintM(DBG_LURN_ERROR, ("Could not allocate a content encryption buffer.\n"));
			}
		}
	}

	//
	// Initialize Lock management
	//

	if(FlagOn(LurnDesc->LurnOptions, LURNOPTION_NO_BUFF_CONTROL)) {
		initialBuffControlState = FALSE;
	} else {
		initialBuffControlState = TRUE;
	}
	status = LMInitialize(Lurn, &IdeDisk->BuffLockCtl, initialBuffControlState);
	if(!NT_SUCCESS(status)) {
		KDPrintM(DBG_LURN_ERROR, ("Can't Create Process. STATUS:0x%08x\n", status));
		goto error_out;
	}
	
	lockMgmtInit = TRUE;

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

	status = KeWaitForSingleObject(
				&IdeDisk->ThreadReadyEvent,
				Executive,
				KernelMode,
				FALSE,
				NULL
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
	if(lockMgmtInit) {
		LMDestroy(&IdeDisk->BuffLockCtl);
	}
	if(LogIn) {
		LspLogout(IdeDisk->LanScsiSession, NULL);
	}
	if(Connection) {
		LspDisconnect(IdeDisk->LanScsiSession);
	}

	if(FirstInit && Lurn->LurnExtension) {
		PLURNEXT_IDE_DEVICE		tmpIdeDisk;

		tmpIdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;

		if(tmpIdeDisk->WriteCheckBuffer) {
			ExFreePoolWithTag(tmpIdeDisk->WriteCheckBuffer, LURNEXT_WRITECHECK_BUFFER_POOLTAG);
			tmpIdeDisk->WriteCheckBuffer = NULL;
		}

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
IdeDiskLurnRequest(
		PLURELATION_NODE Lurn,
		PCCB Ccb
	) {
	PLURNEXT_IDE_DEVICE		IdeDisk;
	KIRQL					oldIrql;
	NTSTATUS				status;

	//
	//	dispatch a request
	//
	switch(Ccb->OperationCode) {
	case CCB_OPCODE_FLUSH:
		KDPrintM(DBG_LURN_ERROR, ("CCB_OPCODE_FLUSH, Ccb->Srb = %08x, Ccb->AssociateID = %d\n",
			Ccb->Srb, Ccb->AssociateID));
		//
		// Nothing to do for IDE disk. This flush is sent to controller side cache.
		//
		Ccb->CcbStatus =CCB_STATUS_SUCCESS;
		LsCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;
		
	case CCB_OPCODE_SHUTDOWN:
		KDPrintM(DBG_LURN_ERROR, ("CCB_OPCODE_SHUTDOWN, Ccb->Srb = %08x, Ccb->AssociateID = %d\n",
			Ccb->Srb, Ccb->AssociateID));
		// Nothing to do for IDE disk		
		Ccb->CcbStatus =CCB_STATUS_SUCCESS;
		LsCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;
		
	case CCB_OPCODE_RESETBUS:
		//
		//	clear all CCB in the queue and queue the CCB to the thread.
		//
		KDPrintM(DBG_LURN_ERROR, ("CCB_OPCODE_RESETBUS, Ccb->Srb = %08x, Ccb->AssociateID = %d\n",
			Ccb->Srb, Ccb->AssociateID));

		ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);

		IdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;

		//
		// If the extension does not exist,
		// complete the CCB with an error.
		//
		if(IdeDisk == NULL) {
			RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);
			KDPrintM(DBG_LURN_ERROR, ("IdeDisk NULL!\n"));
			Ccb->CcbStatus =CCB_STATUS_NOT_EXIST;
			LsCcbCompleteCcb(Ccb);
			return STATUS_SUCCESS;
		}

		CcbCompleteList(&IdeDisk->CcbQueue, CCB_STATUS_RESET, Lurn->LurnStopReasonCcbStatusFlags);
		if(	LURN_IS_RUNNING(Lurn->LurnStatus) ) {
			InsertHeadList(&IdeDisk->CcbQueue, &Ccb->ListEntry);
			KeSetEvent(&IdeDisk->ThreadCcbArrivalEvent, IO_NO_INCREMENT, FALSE);
			LsCcbMarkCcbAsPending(Ccb);

			RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

			return STATUS_PENDING;
		}

		RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		LsCcbCompleteCcb(Ccb);
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

		ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);

		IdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;

		if(IdeDisk == NULL) {
			RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);
			KDPrintM(DBG_LURN_ERROR, ("CCB_OPCODE_STOP: IdeDisk NULL!\n"));
			Ccb->CcbStatus =CCB_STATUS_SUCCESS;
			LsCcbCompleteCcb(Ccb);
			return STATUS_SUCCESS;
		}


		CcbCompleteList(&IdeDisk->CcbQueue, CCB_STATUS_NOT_EXIST, CCBSTATUS_FLAG_LURN_STOP |
			Lurn->LurnStopReasonCcbStatusFlags);

		if(	LURN_IS_RUNNING(Lurn->LurnStatus)) {

			KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_STOP: queue CCB_OPCODE_STOP to the thread. LurnStatus=%08lx\n",
															Lurn->LurnStatus
														));

			//
			//	Hand over Stop CCB to the worker thread.
			//	The worker thread will complete the CCB.
			//

			InsertHeadList(&IdeDisk->CcbQueue, &Ccb->ListEntry);
			KeSetEvent(&IdeDisk->ThreadCcbArrivalEvent, IO_NO_INCREMENT, FALSE);
			Lurn->LurnStatus = LURN_STATUS_STOP_PENDING; 
			RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);


			//
			//	Wait until the worker thread is terminated.
			//

			status = KeWaitForSingleObject(
						IdeDisk->ThreadObject,
						Executive,
						KernelMode,
						FALSE,
						NULL
					);
			ASSERT(status == STATUS_SUCCESS);


			//
			// Now we can assume the stop request is completed.
			// Dereference the thread object.
			//

			ObDereferenceObject(IdeDisk->ThreadObject);

			ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);

			IdeDisk->ThreadObject = NULL;
			IdeDisk->ThreadHandle = NULL;

			RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

			LspLogout(IdeDisk->LanScsiSession, NULL);
			LspDisconnect(IdeDisk->LanScsiSession);

		} else {
			RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);


			//
			//	Complete the STOP CCB.
			//

			Ccb->CcbStatus =CCB_STATUS_SUCCESS;
			LsCcbCompleteCcb(Ccb);
		}

		return STATUS_SUCCESS;

	case CCB_OPCODE_QUERY:
		KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_QUERY\n"));
		ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);

		IdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;

#if 0	// IdeDisk can be null if mounted in degraded mode.
		//
		// If the extension does not exist,
		// complete the CCB with an error.
		//
		if(IdeDisk == NULL) {
			RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);
			KDPrintM(DBG_LURN_ERROR, ("IdeDisk NULL!\n"));
			Ccb->CcbStatus =CCB_STATUS_NOT_EXIST;
			LsCcbCompleteCcb(Ccb);
			return STATUS_SUCCESS;
		}
#endif		
		RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);
		status = LurnIdeQuery(Lurn, IdeDisk, Ccb);
		if(	status != STATUS_MORE_PROCESSING_REQUIRED) {
#if DBG
			if(NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_QUERY: Completed Ccb:%p\n", Ccb));
			}
#endif
			LsCcbCompleteCcb(Ccb);
			return STATUS_SUCCESS;
		}
		break;
	case CCB_OPCODE_RESTART:
		KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_RESTART\n"));
		Ccb->CcbStatus =CCB_STATUS_COMMAND_FAILED;
		LsCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;

	case CCB_OPCODE_ABORT_COMMAND:
		KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_ABORT_COMMAND\n"));
		Ccb->CcbStatus =CCB_STATUS_COMMAND_FAILED;
		LsCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;
	case CCB_OPCODE_DVD_STATUS:
		{
			Ccb->CcbStatus =CCB_STATUS_COMMAND_FAILED;
			LsCcbCompleteCcb(Ccb);
			return STATUS_SUCCESS;
		}
	default:
		break;
	}


	//
	//	check the status and queue a Ccb to the worker thread
	//
	ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);

	IdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;
	
	if(		!LURN_IS_RUNNING(Lurn->LurnStatus)
		) {


		if(Lurn->LurnStatus == LURN_STATUS_STOP_PENDING) {
			KDPrintM(DBG_LURN_ERROR, ("LURN%d(STATUS %08lx)"
						" is in STOP_PENDING. Return with CCB_STATUS_STOP.\n", Lurn->LurnId, Lurn->LurnStatus));
			LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_BUSCHANGE|CCBSTATUS_FLAG_LURN_STOP);
			Ccb->CcbStatus = CCB_STATUS_STOP;
			Lurn->LurnStatus = LURN_STATUS_STOP;
		} else {
			KDPrintM(DBG_LURN_NOISE, ("LURN%d(STATUS %08lx)"
						" is not running. Return with NOT_EXIST.\n", Lurn->LurnId, Lurn->LurnStatus));
			LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_LURN_STOP);
			Ccb->CcbStatus = CCB_STATUS_NOT_EXIST;
		}
		RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

		LsCcbCompleteCcb(Ccb);

		return STATUS_SUCCESS;

	}

	if (IdeDisk) {
		if(Ccb->Flags & CCB_FLAG_URGENT) {
			KDPrintM(DBG_LURN_INFO, ("URGENT. Queue to the head and mark pending.\n"));

			InsertHeadList(&IdeDisk->CcbQueue, &Ccb->ListEntry);
			KeSetEvent(&IdeDisk->ThreadCcbArrivalEvent, IO_DISK_INCREMENT, FALSE);
			LsCcbMarkCcbAsPending(Ccb);
		} else {
			KDPrintM(DBG_LURN_TRACE, ("Queue to the tail and mark pending.\n"));

			InsertTailList(&IdeDisk->CcbQueue, &Ccb->ListEntry);
			KeSetEvent(&IdeDisk->ThreadCcbArrivalEvent, IO_DISK_INCREMENT, FALSE);
			LsCcbMarkCcbAsPending(Ccb);
		}
		RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);
	} else {
		RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);
		//
		// IdeDisk is NULL
		//
		Ccb->CcbStatus = CCB_STATUS_NOT_EXIST;
		Lurn->LurnStatus = LURN_STATUS_STOP;
		LsCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;
	}

	return STATUS_PENDING;
}


//
// Dispatch a CCB from the queue.
//

static
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
		ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);

		if(Lurn->LurnStatus == LURN_STATUS_STOP_PENDING) {
			//
			// Even if a stop request is pending,
			// try to process all the queued requests
			// until the stop request is dequeued.
			//
			KDPrintM(DBG_LURN_ERROR, ("Stop pending. Go ahead to process the rest of requests.\n"));
		} else if(	!(Lurn->LurnStatus == LURN_STATUS_RUNNING) &&
			!(Lurn->LurnStatus == LURN_STATUS_STALL)
			) {
			RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

			KDPrintM(DBG_LURN_ERROR, ("not LURN_STATUS_RUNNING nor LURN_STATUS_STALL.\n"));

			return STATUS_UNSUCCESSFUL;
		}

		ListEntry = RemoveHeadList(&IdeDisk->CcbQueue);

		if(ListEntry == (&IdeDisk->CcbQueue)) {
			RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

			KDPrintM(DBG_LURN_TRACE, ("Empty.\n"));

			break;
		}

		Ccb = CONTAINING_RECORD(ListEntry, CCB, ListEntry);

		//
		// Reconnection not allowed in non-running status
		//

		if(!LURN_IS_RUNNING(Lurn->LurnStatus)) {
			KDPrintM(DBG_LURN_ERROR, ("Reconnection not allowed in non-running status\n"));
			LsCcbSetFlag(Ccb, CCB_FLAG_RETRY_NOT_ALLOWED);
		}

		//
		// Try reconnect except stop request.
		// Stop or must-succeed request must be performed any time.
		//

		if(Ccb->OperationCode != CCB_OPCODE_STOP &&
			!LsCcbIsFlagOn(Ccb, CCB_FLAG_MUST_SUCCEED)){

			//
			// Try reconnect if the condition is met.
			//
			if(Lurn->LurnStatus == LURN_STATUS_STALL &&
				!LsCcbIsFlagOn(Ccb, CCB_FLAG_RETRY_NOT_ALLOWED)) {
					RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

					Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
					status = STATUS_PORT_DISCONNECTED;

					KDPrintM(DBG_LURN_ERROR, ("LURN_STATUS_STALL! going to reconnecting process.\n"));

					goto try_reconnect;
			}
		}


		RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

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
			if (Ccb->FailCascadedCcbCode != CCB_STATUS_SUCCESS) {
				KDPrintM(DBG_LURN_ERROR, ("Previous cascade CCB has force failed this ccb\n"));
				if (Lurn->LurnStatus == LURN_STATUS_STOP ||
					Lurn->LurnStatus == LURN_STATUS_STOP_PENDING) {
					KDPrintM(DBG_LURN_ERROR, ("Node is already stopped.\n"));
					Ccb->CcbStatus = CCB_STATUS_STOP;
				} else {
					Ccb->CcbStatus = Ccb->FailCascadedCcbCode;
				}
				LsCcbCompleteCcb(Ccb);	
				return STATUS_SUCCESS;
			}
		}

		//
		//	dispatch a CCB
		//
/*		if(Ccb->AssociateCascade && Ccb->ForceFail)
		{
			status = STATUS_SUCCESS;
			KDPrintM(DBG_LURN_ERROR, ("Ccb->AssociateCascade && Ccb->ForceFail\n"));
			LsCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
		}
		else
		{
*/		switch(Ccb->OperationCode)
		{
		case CCB_OPCODE_UPDATE:

			KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_UPDATE!\n"));
			status = LurnIdeUpdate(Lurn, IdeDisk, Ccb);
			//
			//	Configure the disk
			//
			if(NT_SUCCESS(status) && Ccb->CcbStatus == CCB_STATUS_SUCCESS) {
				LurnIdeDiskConfigure(Lurn, &PduResponse);
			}
			break;

		case CCB_OPCODE_RESETBUS:

			KDPrintM(DBG_LURN_ERROR, ("CCB_OPCODE_RESETBUS! Nothing to do.\n"));
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;
			status = STATUS_SUCCESS;
			break;

		case CCB_OPCODE_STOP:

			KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_STOP!\n"));
			LurnIdeStop(Lurn, Ccb);
			//
			//	complete a CCB
			//
			LsCcbCompleteCcb(Ccb);
			return STATUS_UNSUCCESSFUL;

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
//			ASSERT(status != STATUS_IO_DEVICE_ERROR);
			break;

		case CCB_OPCODE_NOOP:

			KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_NOOP!\n"));
			status = LSLurnIdeNoop(Lurn, Lurn->LurnExtension, Ccb);
			break;

		case CCB_OPCODE_SMART:
			KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_SMART\n"));
			status = LurnIdeDiskSmart(Lurn, Lurn->LurnExtension, Ccb);
			break;
		case CCB_OPCODE_DEVLOCK:
			KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_SMART\n"));
			status = LurnIdeDiskDeviceLockControl(Lurn, Lurn->LurnExtension, Ccb);
			break;

		default:
			break;
		}

try_reconnect:

		//
		//	detect disconnection
		//

		if((status == STATUS_PORT_DISCONNECTED ||
			Ccb->CcbStatus == CCB_STATUS_COMMUNICATION_ERROR)) 
		{
			if (LsCcbIsFlagOn(Ccb, CCB_FLAG_RETRY_NOT_ALLOWED) ||
				(Lurn->LurnParent && LURN_REDUNDENT_TYPE(Lurn->LurnParent->LurnType) && 
				IdeDisk->DiskWriteCacheDirty && 
				IdeDisk->LuHwData.InitialDevPowerRecycleCount ==0)) {
				//
				// Reconnection is not allowed 
				//	if CCB flag is on 
				//		or
				//	if potential unflushed write exists on redundant member disk and 
				//		smart is not available for disk power cycle check.
				//		(we cannot tell network down from power-down anyway. Give it up right now.)
				//
				KDPrintM(DBG_LURN_ERROR, ("Reconnection is not allowed. Terminating.\n"));

				LspLogout(
					IdeDisk->LanScsiSession,
					NULL
				);
				LspDisconnect(
					IdeDisk->LanScsiSession
				);
				Ccb->CcbStatus = CCB_STATUS_STOP;
				LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_RECONNECT_NOTALLOWED);
				Lurn->LurnStopReasonCcbStatusFlags |= CCBSTATUS_FLAG_RECONNECT_NOTALLOWED;
				LsCcbCompleteCcb(Ccb);
				KDPrintM(DBG_LURN_TRACE, ("Completed Ccb:%p\n", Ccb));
				// This return value will terminate Ide thread.
				return STATUS_UNSUCCESSFUL;
			}
			//
			//	Set the reconnection flag to notify reconnection to Miniport.
			//	Complete the CCB.
			//
			if(Lurn->ReconnTrial) {
				//
				//	If this is the first & second try to reconnect,
				//	then do not notify the reconnection event.
				//	The communication error could be caused by standby mode of the NDAS device.
				//
				if(Lurn->NrOfStall >1) {
					LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_RECONNECTING);
				} else {
					KDPrintM(DBG_LURN_INFO, ("Do not set reconnecting flag for first, second reconnection.\n"));
				}
			}
			//
			//	Complete with Busy status
			//	so that ScsiPort will send the CCB again and prevent SRB timeout.
			//
			//
			KDPrintM(DBG_LURN_ERROR, ("RECONN: try reconnecting!\n"));

			//
			// Lost all locks
			//

			LockCacheAllLocksLost(&IdeDisk->LuHwData);

			//
			//	Set the reconnection flag to notify reconnection to Miniport.
			//	Complete the CCB.
			//
			if(Lurn->ReconnTrial) {
				//
				//	If this is the first & second try to reconnect,
				//	then do not notify the reconnection event.
				//	The communication error could be caused by standby mode of the NDAS device.
				//
				if(Lurn->NrOfStall >1) {
					LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_RECONNECTING);
				} else {
					KDPrintM(DBG_LURN_INFO, ("Do not set reconnecting flag for first, second reconnection.\n"));
				}
			}

			ASSERT( Ccb->Srb											|| 
					Ccb->OperationCode == CCB_OPCODE_RESETBUS			|| 
					Ccb->OperationCode == CCB_OPCODE_NOOP				||
					Ccb->CompletionEvent								||
					LsCcbIsFlagOn(Ccb, CCB_FLAG_CALLED_INTERNEL) );
			
			Ccb->CcbStatus = CCB_STATUS_BUSY;
			LsCcbCompleteCcb(Ccb);
			KDPrintM(DBG_LURN_ERROR, ("RECONN: completed Ccb:%x.\n", Ccb));

			status = LSLurnIdeReconnect(Lurn, IdeDisk);
			if(status == STATUS_CONNECTION_COUNT_LIMIT) {
				//
				//	terminate the thread
				//
				Lurn->LurnStopReasonCcbStatusFlags |= CCBSTATUS_FLAG_RECONNECT_REACH_MAX;
				KDPrintM(DBG_LURN_ERROR, ("RECONN: reconnection count limit. Terminating.\n"));
				return STATUS_UNSUCCESSFUL;
			} else if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_ERROR, ("RECONN: reconnection failed.\n"));
				// Will try later.
			} else {
				UINT64			SectorCount;
				CHAR			Serial[20];
				CHAR			FW_Rev[8];
				CHAR			Model[40];
			
				//
				// Save HDD's info before reconfiguration.
				//
				SectorCount = IdeDisk->LuHwData.SectorCount;
				RtlCopyMemory(Serial, IdeDisk->LuHwData.Serial, sizeof(Serial));
				RtlCopyMemory(FW_Rev, IdeDisk->LuHwData.FW_Rev, sizeof(FW_Rev));
				RtlCopyMemory(Model, IdeDisk->LuHwData.Model, sizeof(Model));				
				//
				//	Configure the disk and get the next CCB
				//
				status = LurnIdeDiskConfigure(Lurn, &PduResponse);
				if(!NT_SUCCESS(status)) {
					ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);
					Lurn->LurnStatus = LURN_STATUS_STALL;
					RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);
					KDPrintM(DBG_LURN_ERROR, ("RECONN: LurnIdeDiskConfigure() failed.\n"));
				}

				//
				// Check reconnected disk is not replaced.
				//

				if (IdeDisk->LuHwData.SectorCount != SectorCount ||
					RtlCompareMemory(Serial, IdeDisk->LuHwData.Serial, sizeof(Serial)) != sizeof(Serial) ||
					RtlCompareMemory(FW_Rev, IdeDisk->LuHwData.FW_Rev, sizeof(FW_Rev)) != sizeof(FW_Rev) ||					
					RtlCompareMemory(Model, IdeDisk->LuHwData.Model, sizeof(Model)) != sizeof(Model)) {
					KDPrintM(DBG_LURN_ERROR, ("RECONN: HDD is replaced.Disconnecting\n"));
					LspLogout(
						IdeDisk->LanScsiSession,
						NULL
					);
					LspDisconnect(
						IdeDisk->LanScsiSession
					);						
					Lurn->LurnStopReasonCcbStatusFlags |= CCBSTATUS_FLAG_DISK_REPLACED;
					return STATUS_UNSUCCESSFUL;
				}

				//
				// If the disk is dirty, determine if power recycle occurs after disconnection.
				// When the disk is dirty and power recycle occurs,
				// We think the data in the disk write cache might be lost.
				//
				// We can also assume whether the disk write cache is disabled by checking
				// LU hardware data, but other host might enable the cache.
				// Therefore, we should not assume the LU hardware data has a exact information.
				//
				// If the count is zero that means there was no initial power recycle count,
				// Can not perform power recycle detection using SMART.
				//
#if DBG
				if(IdeDisk->DiskWriteCacheDirty) {
					KDPrintM(DBG_LURN_ERROR, ("Disk write cache is dirty.\n"));
				} else {
					KDPrintM(DBG_LURN_ERROR, ("Disk write cache is clean.\n"));
				}
#endif
				if (IdeDisk->LuHwData.InitialDevPowerRecycleCount != 0) {

					UINT64		powerRecycleCount;

					status = LurnIdeDiskGetPowerRecycleCount( IdeDisk, &powerRecycleCount );

					if (!NT_SUCCESS(status)) {

						KDPrintM( DBG_LURN_ERROR, ("RECONN: Power recycle detection"
												   "failed. STATUS=%08lx. Continue to the next.\n",
												   status) );
						status = STATUS_SUCCESS;

						ASSERT( FALSE );

					} else {
						
						KDPrintM(DBG_LURN_ERROR, ("RECONN: Current power recycle count = %I64u\n",
												  powerRecycleCount) );

						if (IdeDisk->DiskWriteCacheDirty) {

							if (IdeDisk->LuHwData.InitialDevPowerRecycleCount != powerRecycleCount) {

								KDPrintM( DBG_LURN_ERROR, ("RECONN: HDD got power-recycled. Stop the LURN.\n") );
					
								LspLogout( IdeDisk->LanScsiSession, NULL );
							
								LspDisconnect( IdeDisk->LanScsiSession );
								
								//
								//	terminate the thread
								//
							
								Lurn->LurnStopReasonCcbStatusFlags |= CCBSTATUS_FLAG_POWERRECYLE_OCCUR;

								return STATUS_UNSUCCESSFUL;
							}
						}

						IdeDisk->LuHwData.InitialDevPowerRecycleCount = powerRecycleCount;
					}
				}
			}

			continue;				
		}// Ended handling of disconnection.

		//
		//	complete a CCB
		//
		LsCcbCompleteCcb(Ccb);
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
	PVOID					Events[2];
#if DBG
	LURN_TYPE				parentLurnType;
#endif

	KDPrintM(DBG_LURN_TRACE, ("Entered\n"));

	Lurn = (PLURELATION_NODE)Context;
	IdeDisk = (PLURNEXT_IDE_DEVICE)Lurn->LurnExtension;
#if DBG
	parentLurnType = Lurn->LurnParent->LurnType;
#endif

	Events[0] = &IdeDisk->ThreadCcbArrivalEvent;
	Events[1] = &IdeDisk->BuffLockCtl.IoIdleTimer;
	TimeOut.QuadPart = -LURNIDE_IDLE_TIMEOUT;

	//
	//	Raise the thread's current priority level (same level to Modified page writer).
	//	Set ready signal.
	//
	KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);
	KeSetEvent (&IdeDisk->ThreadReadyEvent, IO_NO_INCREMENT, FALSE);

	do {
		ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

		status = KeWaitForMultipleObjects(
			2,
			Events,
			WaitAny,
			Executive,
			KernelMode,
			FALSE,
			&TimeOut,
			NULL
			);
		if(!NT_SUCCESS(status)){
			KDPrintM(DBG_LURN_ERROR, ("KeWaitForSingleObject Fail\n"));
			break;
		}

		switch(status)
		{
		case STATUS_WAIT_0:
			KeClearEvent(Events[0]);
			status = LurnIdeDiskDispatchCcb(Lurn, IdeDisk);
			if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_ERROR, ("LurnIdeDiskDispatchCcb() failed. NTSTATUS:%08lx\n", status));
				goto terminate_thread;
			}
		break;
		case STATUS_WAIT_1:
			KeClearEvent(Events[1]);
			status = EnterBufferLockIoIdle(
							&IdeDisk->BuffLockCtl,
							IdeDisk->LanScsiSession,
							&IdeDisk->LuHwData);
			if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_ERROR, ("EnterBufferLockIoIdle() failed. NTSTATUS:%08lx\n", status));
			}
		break;
		case STATUS_TIMEOUT: {
			BYTE	pdu_response;

			//
			//	Send NOOP to make sure that the Lanscsi Node is reachable
			//	Except when the LURN has stopped at the host side.
			//
			if(IdeDisk->LURNHostStop) {
				KDPrintM(DBG_LURN_TRACE, ("Lurn%d:"
					" IDLE_TIMEOUT: stopped at host side. skip pinging.\n",
					Lurn->LurnId));
				break;
			}
/*			ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);
			if(Lurn->LurnStatus != LURN_STATUS_RUNNING) {
				RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

				KDPrintM(DBG_LURN_ERROR, ("Lurn%d:"
					" IDLE_TIMEOUT: Not running. not send noop.\n",
					Lurn->LurnId));

				break;
			} else {
				RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);
			}*/

			status = LspNoOperation(IdeDisk->LanScsiSession, IdeDisk->LuHwData.LanscsiTargetID, &pdu_response, NULL);
			if(!NT_SUCCESS(status) || pdu_response != LANSCSI_RESPONSE_SUCCESS) {
				ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);
				if(Lurn->LurnStatus != LURN_STATUS_STALL) {
					LURN_EVENT	LurnEvent;

					Lurn->LurnStatus = LURN_STATUS_STALL;
					RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

					//
					//	Set a event to make NDASSCSI fire NoOperation.
					//
					LurnEvent.LurnId = Lurn->LurnId;
					LurnEvent.LurnEventClass = LURN_REQUEST_NOOP_EVENT;
					LurCallBack(Lurn->Lur, &LurnEvent);

					KDPrintM(DBG_LURN_ERROR, ("Lurn%d:"
											" IDLE_TIMEOUT: NoOperation failed. Start reconnecting.\n",
											Lurn->LurnId));
				} else {
					RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);
				
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
	KDPrintM(DBG_LURN_TRACE, ("The thread will be terminated.\n"));
	ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);

#if DBG
	if (!LUR_IS_PRIMARY(Lurn->Lur) || LURN_IS_ROOT_NODE(Lurn) || !LURN_REDUNDENT_TYPE(parentLurnType)) {

		ASSERT(IsListEmpty(&IdeDisk->CcbQueue));
	}
#endif

	Lurn->LurnStatus = LURN_STATUS_STOP_PENDING;
	CcbCompleteList(
			&IdeDisk->CcbQueue,
			CCB_STATUS_STOP,
			CCBSTATUS_FLAG_LURN_STOP |
			CCBSTATUS_FLAG_BUSCHANGE |
			Lurn->LurnStopReasonCcbStatusFlags
		);

	RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

	KDPrintM(DBG_LURN_ERROR, ("Terminated\n"));
	PsTerminateSystemThread(STATUS_SUCCESS);

	return;
}


//////////////////////////////////////////////////////////////////////////
//
// SMART operation execution
//

#ifndef SMART_READ_LOG
#define SMART_READ_LOG			0xd5
#endif
#define SMART_WRITE_LOG			0xd6

// Log sector size
#define SMART_LOG_SECTOR_SIZE	512

//
// Read SMART Attribute
//

static
NTSTATUS
LurnIdeDiskReadAttributes(
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN PATA_SMART_DATA		AtaSmartBuffer,
	OUT PCHAR				PduResponse
){
	NTSTATUS		status;
	LANSCSI_PDUDESC	pduDesc;

	if(IdeDisk->LuHwData.HwVersion <= LANSCSIIDE_VERSION_1_0) {
		return STATUS_NOT_SUPPORTED;
	}
	if(!(IdeDisk->LuHwData.LudataFlags & LUDATA_FLAG_SMART_SUPPORT)) {
		return STATUS_NOT_SUPPORTED;
	}
	if(!(IdeDisk->LuHwData.LudataFlags & LUDATA_FLAG_SMART_ENABLED)) {
		return STATUS_DEVICE_NOT_READY;
	}

	//
	// Init PDU descriptor.
	//

	LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &pduDesc, IDE_COMMAND, WIN_SMART, 0, 0, READ_ATTRIBUTE_BUFFER_SIZE, (PUCHAR)AtaSmartBuffer, NULL);
	pduDesc.Flags &= ~(PDUDESC_FLAG_LBA | PDUDESC_FLAG_LBA48 | PDUDESC_FLAG_DMA | PDUDESC_FLAG_UDMA);
	pduDesc.Flags |= PDUDESC_FLAG_DATA_IN| PDUDESC_FLAG_PIO;

	//
	// Beside WIN_SMART set in command register, smart command needs
	// six more register values.
	//

	pduDesc.Feature = READ_ATTRIBUTES; // set sub-command
	pduDesc.BlockCount = 1;
	pduDesc.Param8[0] = 1;
	pduDesc.Param8[1] = 0x4f;
	pduDesc.Param8[2] = 0xc2;
	pduDesc.Param8[3] = 0xa0 | 
		((IdeDisk->LuHwData.LanscsiTargetID&0x01) << 4); // master/slave select

	// Send smart command PDU
	status = LspRequest(IdeDisk->LanScsiSession, &pduDesc, PduResponse);

	return status;
}

//
// Find the specified attribute in the smart data buffer.
//

static
BOOLEAN
LurnIdeDiskFindAttribute(
	IN PATA_SMART_DATA			AtaSmartBuffer,
	IN UCHAR					AttributeId,
	OUT PATA_SMART_ATTRIBUTE	*AtaSmartAttribute
){
	LONG	idx_attr;

	for(idx_attr = 0; idx_attr < ATA_SMART_NR_ATTR; idx_attr ++) {
	
		if(AtaSmartBuffer->vendor_attributes[idx_attr].id == 0)
			continue;
		if(AtaSmartBuffer->vendor_attributes[idx_attr].id == AttributeId) {
			*AtaSmartAttribute = &AtaSmartBuffer->vendor_attributes[idx_attr];
			return TRUE;
		}
	}

	return FALSE;
}

//
// Get the disk power cycle count
//

static
NTSTATUS
LurnIdeDiskGetPowerRecycleCount(
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	OUT PUINT64				PowerRecycleCount
){
	NTSTATUS		status;
	ATA_SMART_DATA	smartData;
	UCHAR			pduResponse;
	BOOLEAN			found;
	PATA_SMART_ATTRIBUTE	smartAttr;

	status = LurnIdeDiskReadAttributes(IdeDisk, &smartData, &pduResponse);
	if(!NT_SUCCESS(status) && pduResponse != LANSCSI_RESPONSE_SUCCESS) {
		return status;
	}

	found = LurnIdeDiskFindAttribute(
						&smartData,
						0x0C,		// Device power cycle count
						&smartAttr);
	if(found == FALSE)
		return STATUS_NOT_SUPPORTED;

	//
	// set return values
	//

	*PowerRecycleCount =
				((UINT64)smartAttr->raw[0]) +
				((UINT64)smartAttr->raw[1] << 8) +
				((UINT64)smartAttr->raw[2] << 16) +
				((UINT64)smartAttr->raw[3] << 24) +
				((UINT64)smartAttr->raw[4] << 32) +
				((UINT64)smartAttr->raw[5] << 40);

	return STATUS_SUCCESS;
}

//
// Identify for SMART command
//

static
NTSTATUS
LurnIdeDiskIdentify(
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN PCCB					Ccb,
	OUT PSENDCMDOUTPARAMS	SendCmdOut
){
	NTSTATUS			status;
	LANSCSI_PDUDESC		pduDesc;
	UCHAR				pduResponse;

	//
	// Verify buffer
	//

	if(Ccb->DataBufferLength < FIELD_OFFSET(SENDCMDOUTPARAMS, bBuffer) + IDENTIFY_BUFFER_SIZE) {
		Ccb->CcbStatus = CCB_STATUS_DATA_OVERRUN;
		Ccb->ResidualDataLength = Ccb->DataBufferLength;
		KDPrintM(DBG_LURN_ERROR, ("Buffer too small. Supplied=%u Required=%u\n",
					Ccb->DataBufferLength,
					FIELD_OFFSET(SENDCMDINPARAMS,bBuffer) + IDENTIFY_BUFFER_SIZE));
		return STATUS_SUCCESS;
	}

	// identify.

	LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &pduDesc, IDE_COMMAND, WIN_IDENTIFY, 0, 1, IDENTIFY_BUFFER_SIZE, SendCmdOut->bBuffer, NULL);
	status = LspRequest(IdeDisk->LanScsiSession, &pduDesc, &pduResponse);
	if(!NT_SUCCESS(status)) {
		SendCmdOut->DriverStatus.bDriverError = SMART_IDE_ERROR;
		SendCmdOut->DriverStatus.bIDEError = pduDesc.Feature;
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		KDPrintM(DBG_LURN_ERROR, ("Identify comm Failed...\n"));
		return status;
	}
	if(pduResponse != LANSCSI_RESPONSE_SUCCESS) {
		SendCmdOut->DriverStatus.bDriverError = SMART_IDE_ERROR;
		SendCmdOut->DriverStatus.bIDEError = pduDesc.Feature;
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		KDPrintM(DBG_LURN_ERROR, ("Identify device Failed...\n"));
		return STATUS_SUCCESS;
	}

	SendCmdOut->DriverStatus.bDriverError = 0;
	SendCmdOut->DriverStatus.bIDEError = 0;
	Ccb->CcbStatus = CCB_STATUS_SUCCESS;

	return STATUS_SUCCESS;
}

static
ULONG
GetRequiredBufferLength(
	IN PSENDCMDINPARAMS		SendCmdIn
){
	ULONG	requiredLength;

	switch (SendCmdIn->irDriveRegs.bFeaturesReg)
	{
		//
		// SMART_RCV_DRIVE_DATA
		//
	case READ_ATTRIBUTES:
	case READ_THRESHOLDS:
		requiredLength = READ_ATTRIBUTE_BUFFER_SIZE;
		break;
	case SMART_READ_LOG:
	case SMART_WRITE_LOG:
		requiredLength = SendCmdIn->irDriveRegs.bSectorCountReg * SMART_LOG_SECTOR_SIZE;
		break;
		//
		// SMART_SEND_DRIVE_COMMAND
		//
	case ENABLE_SMART:
	case DISABLE_SMART:
	case RETURN_SMART_STATUS:
	case ENABLE_DISABLE_AUTOSAVE:
	case EXECUTE_OFFLINE_DIAGS:
	case SAVE_ATTRIBUTE_VALUES:
	case ENABLE_DISABLE_AUTO_OFFLINE:
	default:
		requiredLength = 0;
		break;
	}

	return requiredLength;
}
static
NTSTATUS
LurnIdeDiskSmartCommand(
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN PCCB					Ccb,
	IN PSENDCMDINPARAMS		SendCmdIn,
	OUT PSENDCMDOUTPARAMS	SendCmdOut
){
	NTSTATUS			status;
	LANSCSI_PDUDESC		pduDesc;
	UCHAR				pduResponse;
	ULONG				requiredLength;
	LARGE_INTEGER		longTimeout;

	KDPrintM(DBG_LURN_ERROR, ("SendCmdIn: BuffSz=%x DrvNo=%x F=%x SC=%x SN=%x CL=%x CH=%x DH=%x CM=%x\n",
						SendCmdIn->cBufferSize,
						(ULONG)SendCmdIn->bDriveNumber,
						(ULONG)SendCmdIn->irDriveRegs.bFeaturesReg,
						(ULONG)SendCmdIn->irDriveRegs.bSectorCountReg,
						(ULONG)SendCmdIn->irDriveRegs.bSectorNumberReg,
						(ULONG)SendCmdIn->irDriveRegs.bCylLowReg,
						(ULONG)SendCmdIn->irDriveRegs.bCylHighReg,
						(ULONG)SendCmdIn->irDriveRegs.bDriveHeadReg,
						(ULONG)SendCmdIn->irDriveRegs.bCommandReg));


	//
	// We look into the registers not the IO control code itself!
	//

	requiredLength = GetRequiredBufferLength(SendCmdIn);

	//
	// Buffer size check
	//

	if(Ccb->DataBufferLength < FIELD_OFFSET(SENDCMDOUTPARAMS,bBuffer) + requiredLength) {
		KDPrintM(DBG_LURN_ERROR, ("Buffer too small. Supplied=%u Required=%u\n",
					Ccb->DataBufferLength,
					FIELD_OFFSET(SENDCMDINPARAMS,bBuffer) + requiredLength));
		Ccb->CcbStatus = CCB_STATUS_DATA_OVERRUN;
		return STATUS_SUCCESS;
	}


	//
	// Init PDU descriptor.
	//

	LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &pduDesc, IDE_COMMAND, WIN_SMART, 0, 0, requiredLength, SendCmdOut->bBuffer, NULL);
	pduDesc.Flags &= ~(PDUDESC_FLAG_LBA | PDUDESC_FLAG_LBA48 | PDUDESC_FLAG_DMA | PDUDESC_FLAG_UDMA);
	if(requiredLength) {
		pduDesc.Flags |= PDUDESC_FLAG_DATA_IN;
		pduDesc.Flags  |= PDUDESC_FLAG_PIO;
	}
	// Long time-out requested?
	if(Ccb->CcbStatusFlags & CCBSTATUS_FLAG_LONG_COMM_TIMEOUT) {
		longTimeout.QuadPart = LURNIDE_GENERIC_TIMEOUT * 3;
		pduDesc.TimeOut = &longTimeout;
	}

	//
	// Beside WIN_SMART set in command register, smart command needs
	// six more register values.
	//

	pduDesc.Feature = SendCmdIn->irDriveRegs.bFeaturesReg; // set sub-command
	pduDesc.BlockCount = SendCmdIn->irDriveRegs.bSectorCountReg;
	pduDesc.Param8[0] = SendCmdIn->irDriveRegs.bSectorNumberReg;
	pduDesc.Param8[1] = SendCmdIn->irDriveRegs.bCylLowReg;
	pduDesc.Param8[2] = SendCmdIn->irDriveRegs.bCylHighReg;
	pduDesc.Param8[3] = SendCmdIn->irDriveRegs.bDriveHeadReg;

	// Send smart command PDU
	status = LspRequest(IdeDisk->LanScsiSession, &pduDesc, &pduResponse);
	if(NT_SUCCESS(status) && pduResponse == LANSCSI_RESPONSE_SUCCESS) {
		SendCmdOut->DriverStatus.bDriverError = 0;
		SendCmdOut->DriverStatus.bIDEError = 0;
	} else {
		SendCmdOut->DriverStatus.bDriverError = SMART_IDE_ERROR;
		SendCmdOut->DriverStatus.bIDEError = pduDesc.Feature;
		if(!NT_SUCCESS(status)) {
			KDPrintM(DBG_LURN_ERROR, ("SMART command Failed: Communication error.\n"));
			// Trigger reconnecting
			Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
			return status;
		} else {
			// Return error to the requester
			KDPrintM(DBG_LURN_ERROR, ("SMART command Failed: NDAS chip failed.\n"));
			Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
			return STATUS_SUCCESS;
		}
	}


	//
	// Set up command-result.
	//

	SendCmdOut->cBufferSize = requiredLength;

	//
	// NDAS chip 1.1 and 2.0 does not return error register correctly.
	// Do not rely on the status register too much.
	//

	SendCmdOut->DriverStatus.bDriverError = 
		(pduDesc.Command & ERR_STAT)? SMART_IDE_ERROR : 0;


	SendCmdOut->DriverStatus.bIDEError = pduDesc.Feature;
#if DBG
	if(pduDesc.Command & ERR_STAT) {
		KDPrintM(DBG_LURN_ERROR, ("ErrReg=%x StatReg=%x\n", pduDesc.Command, pduDesc.Feature));
	}
#endif


	//
	// If SMART sub-command is RETURN_SMART_STATUS,
	// extract the smart status from cylinder low and high.
	//

	if(SendCmdIn->irDriveRegs.bFeaturesReg == RETURN_SMART_STATUS) {
			PIDEREGS returnIdeRegs;

			returnIdeRegs = (PIDEREGS) SendCmdOut->bBuffer;
			returnIdeRegs->bFeaturesReg = RETURN_SMART_STATUS;
			returnIdeRegs->bSectorCountReg = (BYTE)pduDesc.BlockCount;
			returnIdeRegs->bSectorNumberReg = pduDesc.Param8[0];
			returnIdeRegs->bCylLowReg =  pduDesc.Param8[1];
			returnIdeRegs->bCylHighReg = pduDesc.Param8[2];
			returnIdeRegs->bDriveHeadReg = pduDesc.Param8[3];
			returnIdeRegs->bCommandReg = SMART_CMD;
			SendCmdOut->cBufferSize = sizeof(IDEREGS); // 7 + reserved = 8
	}

	return status;
}

static
NTSTATUS
LurnIdeDiskSmart(
	IN PLURELATION_NODE		Lurn,
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN PCCB					Ccb
){
	NTSTATUS			status;
	SENDCMDINPARAMS		sendCmdIn; /* a copy of SENDCMDINPARAMS */
	PSENDCMDOUTPARAMS	sendCmdOut;
	UCHAR				selectDev;

	UNREFERENCED_PARAMETER(Lurn);

	//
	// Version check
	//

	if(IdeDisk->LuHwData.HwVersion <= LANSCSIIDE_VERSION_1_0) {
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		return STATUS_SUCCESS;
	}

	//
	// Check disk power mode.
	//

	status = LurnIdeDiskCheckPowerMode(Lurn, IdeDisk, Ccb);
	if(!NT_SUCCESS(status)) {
		KDPrintM(DBG_LURN_ERROR, ("LurnIdeDiskCheckPowerMode() failed. continue to execute. STATUS=%08lx\n", status));
		// Continue even if power mode check failed.
		status = STATUS_SUCCESS;
	}

	//
	// Get the register values
	//

	sendCmdIn = *(PSENDCMDINPARAMS)Ccb->DataBuffer;
	sendCmdOut = (PSENDCMDOUTPARAMS)Ccb->DataBuffer;

	status = STATUS_SUCCESS;
	switch(sendCmdIn.irDriveRegs.bCommandReg) {
		case ATAPI_ID_CMD:
			Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
			break;
		case ID_CMD:
			status = LurnIdeDiskIdentify(IdeDisk, Ccb, sendCmdOut);
			ASSERT((Ccb->CcbStatus == CCB_STATUS_SUCCESS && NT_SUCCESS(status)) ||
				(Ccb->CcbStatus != CCB_STATUS_SUCCESS));
			break;
		case SMART_CMD:

			//
			// Check smart is supported
			//

			if(!(IdeDisk->LuHwData.LudataFlags & LUDATA_FLAG_SMART_SUPPORT)) {
				Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
				KDPrintM(DBG_LURN_ERROR, ("SMART not supported.\n"));
				break;
			}

			//
			// Check device select
			//

			selectDev = (sendCmdIn.irDriveRegs.bDriveHeadReg >> 4) & 0x01;
			if(selectDev != IdeDisk->LuHwData.LanscsiTargetID) {
				// Active Smart application send wrong device select.
				// It might be confused using SCSI-based disk.
				KDPrintM(DBG_LURN_ERROR, ("Wrong device selected. Select dev=%d. Correct it to %d\n",
					selectDev, IdeDisk->LuHwData.LanscsiTargetID));

				//
				// Correct device select.
				//
				sendCmdIn.irDriveRegs.bDriveHeadReg &= 0xef;
				sendCmdIn.irDriveRegs.bDriveHeadReg |= (IdeDisk->LuHwData.LanscsiTargetID & 0x01) << 4;
			}

			status = LurnIdeDiskSmartCommand(IdeDisk, Ccb, &sendCmdIn, sendCmdOut);
			ASSERT((Ccb->CcbStatus == CCB_STATUS_SUCCESS && NT_SUCCESS(status)) ||
					(Ccb->CcbStatus != CCB_STATUS_SUCCESS));
			break;
		default:
			Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
	}

	return status;
}

//////////////////////////////////////////////////////////////////////////
//
// Flow control
//
#define MINUMUN_READ_SPLIT_SIZE 4096 // 4kbytes

static 
VOID
LurnIdeDiskResetProtocolCounters(
	PLURNEXT_IDE_DEVICE IdeDisk
) {
	IdeDisk->PacketLoss = 0;	
	IdeDisk->Retransmits = 0;
}

static 
LONG
LurnIdeDiskAdjustReadSplit(
	PLURNEXT_IDE_DEVICE IdeDisk
) {
	ULONG packetLoss;
#if 0	
	ULONG retransmits;

	// Clear retransmit not to make retransmit during reading cause changes write-split.
	retransmits = InterlockedExchange(&IdeDisk->Retransmits, 0);
	if (retransmits) {
		KDPrintM(DBG_LURN_ERROR, ("Cleared retransmit count\n"));
	}
#endif

#if __NDAS_SCSI_LPXTDI_V2__
	return IdeDisk->MaxDataRecvLength;
#endif

	packetLoss = InterlockedExchange(&IdeDisk->PacketLoss, 0);
	if(packetLoss > 0)
	{
		IdeDisk->NrReadAfterLastPacketLoss = 0;
		IdeDisk->ReadSplitSize = IdeDisk->ReadSplitSize * 3 /4;
		if (IdeDisk->ReadSplitSize < MINUMUN_READ_SPLIT_SIZE) {
		    IdeDisk->ReadSplitSize = MINUMUN_READ_SPLIT_SIZE;
		}
		KDPrintM(DBG_LURN_ERROR, ("Packet loss detected(%p). Read block size is downed to %d\n", IdeDisk, IdeDisk->ReadSplitSize));
	}
	else
	{
		IdeDisk->NrReadAfterLastPacketLoss++;
		if(IdeDisk->NrReadAfterLastPacketLoss > IdeDisk->ReadSplitSize / 64) 
		{

			IdeDisk->NrReadAfterLastPacketLoss = 0;

			if(IdeDisk->ReadSplitSize < IdeDisk->MaxDataRecvLength)
			{
				IdeDisk->ReadSplitSize += 512;

				if(IdeDisk->ReadSplitSize > IdeDisk->MaxDataRecvLength)
					IdeDisk->ReadSplitSize = IdeDisk->MaxDataRecvLength;
				KDPrintM(DBG_LURN_ERROR, ("Stable status(%p). Increased read block size to %d bytes\n", IdeDisk, IdeDisk->ReadSplitSize));
			}
		}
	}
	ASSERT(IdeDisk->ReadSplitSize <= IdeDisk->MaxDataRecvLength);
	if (IdeDisk->ReadSplitSize > IdeDisk->MaxDataRecvLength) { // Safeguard for ndas service passed invalid value.
		IdeDisk->ReadSplitSize = IdeDisk->MaxDataRecvLength;
	}

	return IdeDisk->ReadSplitSize;
}


static
LONG
LurnIdeDiskAdjustWriteSplit(
	PLURNEXT_IDE_DEVICE IdeDisk)
{
#ifdef RETRANS_FRE_LOG
	UCHAR	buffer[257];
#endif // RETRANS_FRE_LOG
	ULONG retranmits;
#if 0
	ULONG packetLoss;

	// Clear packet loss count not make read-error during writing cause changes read-split.
	packetLoss = InterlockedExchange(&IdeDisk->PacketLoss, 0);
	if (packetLoss) {
		KDPrintM(DBG_LURN_ERROR, ("Packet loss detected while writing\n")); // Can be happen if write verify is on.
	}
#endif

#if __NDAS_SCSI_LPXTDI_V2__
	return IdeDisk->MaxDataSendLength;
#endif

	retranmits = InterlockedExchange(&IdeDisk->Retransmits, 0);
	if(retranmits > 0)
	{
		IdeDisk->NrWritesAfterLastRetransmits = 0;
		IdeDisk->WriteSplitSize = 
			(IdeDisk->WriteSplitSize >= 128*512) ? 64*512 :
			(IdeDisk->WriteSplitSize >=  64*512) ? 43*512 :
			(IdeDisk->WriteSplitSize >=  43*512) ? 32*512 :
			(IdeDisk->WriteSplitSize >=  32*512) ? 26*512 :
			(IdeDisk->WriteSplitSize >=  26*512) ? 22*512 :
			(IdeDisk->WriteSplitSize >=  22*512) ? 19*512 :
			(IdeDisk->WriteSplitSize >=  19*512) ? 16*512 :
			(IdeDisk->WriteSplitSize >=  16*512) ? 13*512 :
			(IdeDisk->WriteSplitSize >=  13*512) ? 11*512 :
			(IdeDisk->WriteSplitSize >=  11*512) ? 10*512 :
			(IdeDisk->WriteSplitSize >=  10*512) ?  8*512 :
			(IdeDisk->WriteSplitSize >=   8*512) ?  6*512 :
			(IdeDisk->WriteSplitSize >=   6*512) ?  4*512 : 2*512;
		KDPrintM(DBG_LURN_ERROR, ("Retransmit detected(%p). down to %d\n", IdeDisk, IdeDisk->WriteSplitSize));
#ifdef RETRANS_FRE_LOG
		_snprintf(buffer, 257, "[%s:%04d] Retransmit detected(%p). down to %d\n", __MODULE__, __LINE__, IdeDisk, IdeDisk->WriteSplitSize);
		DbgPrint(buffer);
#endif // RETRANS_FRE_LOG
	}
	else
	{
		IdeDisk->NrWritesAfterLastRetransmits++;
		if(IdeDisk->NrWritesAfterLastRetransmits > IdeDisk->WriteSplitSize / 64)
		{
			IdeDisk->NrWritesAfterLastRetransmits = 0;
	
			if(IdeDisk->WriteSplitSize < IdeDisk->MaxDataSendLength)
			{
				IdeDisk->WriteSplitSize =
					(IdeDisk->WriteSplitSize >= 64*512) ? 128*512 :
					(IdeDisk->WriteSplitSize >= 43*512) ?  64*512 :
					(IdeDisk->WriteSplitSize >= 32*512) ?  43*512 :
					(IdeDisk->WriteSplitSize >= 26*512) ?  32*512 :
					(IdeDisk->WriteSplitSize >= 22*512) ?  26*512 :
					(IdeDisk->WriteSplitSize >= 19*512) ?  22*512 :
					(IdeDisk->WriteSplitSize >= 16*512) ?  19*512 :
					(IdeDisk->WriteSplitSize >= 13*512) ?  16*512 :
					(IdeDisk->WriteSplitSize >= 11*512) ?  13*512 :
					(IdeDisk->WriteSplitSize >= 10*512) ?  11*512 :
					(IdeDisk->WriteSplitSize >=  8*512) ?  10*512 :
					(IdeDisk->WriteSplitSize >=  6*512) ?   8*512 : 6*512;

				if(IdeDisk->WriteSplitSize > IdeDisk->MaxDataSendLength)
					IdeDisk->WriteSplitSize = IdeDisk->MaxDataSendLength;

				KDPrintM(DBG_LURN_ERROR, ("Stable status(%p). up to %d\n", IdeDisk, IdeDisk->WriteSplitSize));
#ifdef RETRANS_FRE_LOG
				_snprintf(buffer, 257, "[%s:%04d] Stable status(%p). up to %d\n", __MODULE__, __LINE__, IdeDisk, IdeDisk->WriteSplitSize);
				DbgPrint(buffer);
#endif // RETRANS_FRE_LOG
			}
		}
	}
	ASSERT(IdeDisk->WriteSplitSize <= IdeDisk->MaxDataSendLength);
	if (IdeDisk->WriteSplitSize > IdeDisk->MaxDataSendLength) { // Safeguard for ndas service passed invalid value.
		IdeDisk->WriteSplitSize = IdeDisk->MaxDataSendLength;
	}

	return IdeDisk->WriteSplitSize;
}

//////////////////////////////////////////////////////////////////////////
//
// Read/write/verify commands
// SCSIOP_READ /  WRITE / VERIFY
//

static
NTSTATUS
LurnIdeDiskVerify(
	IN		PLURELATION_NODE		Lurn,
	IN		PLURNEXT_IDE_DEVICE		IdeDisk,
	IN OUT	PCCB					Ccb,
	IN		UINT64					LogicalBlockAddress,
	IN		ULONG					TransferBlocks
);

static
NTSTATUS
LurnIdeDiskRead(
		IN OUT	PCCB					Ccb,
		IN		PLURNEXT_IDE_DEVICE		IdeDisk,
		OUT		PVOID					DataBuffer,
		IN		UINT64					LogicalBlockAddress,
		IN		ULONG					TransferBlocks,
		IN		ULONG					SplitBlockBytes // in bytes
)
{
	NTSTATUS			status;
	BYTE				response;
	LANSCSI_PDUDESC		pduDesc;
	ULONG				transferedBlocks;
	ULONG				blocksToTransfer;
	UINT32				retryCount;
	UINT32				splitBlocks;
	BOOLEAN				isLongTimeout;
	LARGE_INTEGER		longTimeout;
	BOOLEAN				lostLockIO;

#if __ENABLE_PERFORMACE_CHECK__
	LARGE_INTEGER		PFStartTime;
	LARGE_INTEGER		PFCurrentTime;

	PFStartTime = KeQueryPerformanceCounter(NULL);
	PFCurrentTime.QuadPart = PFStartTime.QuadPart;
#endif

	// AING : some disks (Maxtor 80GB supports LBA only)
	//		ASSERT(IdeDisk->LuData.PduDescFlags&PDUDESC_FLAG_LBA48);
	KDPrintM(DBG_LURN_NOISE, ("0x%08x(%d)\n", LogicalBlockAddress, TransferBlocks));

	if(0 == TransferBlocks)
	{
		KDPrintM(DBG_LURN_ERROR, ("Return for transferBlocks == 0\n"));
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		return STATUS_SUCCESS;
	}
	status = STATUS_SUCCESS;

	//
	// Check to see if this IO requires a lost device lock.
	//

	lostLockIO = LockCacheCheckLostLockIORange(&IdeDisk->LuHwData, LogicalBlockAddress, LogicalBlockAddress + TransferBlocks - 1);
	if(lostLockIO) {
		KDPrintM(DBG_LURN_ERROR, ("Blocked by a lost lock.\n"));
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		return STATUS_SUCCESS;
	}

	//
	// Calculate the number of split blocks
	//

	splitBlocks = SplitBlockBytes / IdeDisk->LuHwData.BlockBytes;
	if(splitBlocks == 0)
		splitBlocks = 1;

	//
	// Long time out
	//

	isLongTimeout = (Ccb->CcbStatusFlags & CCBSTATUS_FLAG_LONG_COMM_TIMEOUT) != 0;

	//
	// Clear flow control stats.
	//
	LurnIdeDiskResetProtocolCounters(IdeDisk);
	//
	// Stop the IO idle timer
	//
	StopIoIdleTimer(&IdeDisk->BuffLockCtl);

	//
	//	Send read LSP requests split by SplitBlock factor.
	//

#if __NDAS_SCSI_LPXTDI_V2__
{
	UINT32			chooseDataLength;
	LARGE_INTEGER	startTime;
	LARGE_INTEGER	endTime;

	NDASSCSI_ASSERT( SplitBlockBytes == (64*1024) );

	chooseDataLength = NdasFcChooseSendSize( &IdeDisk->LanScsiSession->RecvNdasFcStatistics, 
											 TransferBlocks * IdeDisk->LuHwData.BlockBytes );

	startTime = LpxTdiV2CurrentTime();
	//KeQuerySystemTime( &startTime );

	splitBlocks = chooseDataLength / IdeDisk->LuHwData.BlockBytes;
#endif


	for(transferedBlocks = 0; transferedBlocks < TransferBlocks; transferedBlocks += splitBlocks)
	{
		retryCount = 0;
retry_io:
		blocksToTransfer = (TransferBlocks - transferedBlocks <= splitBlocks) ? (TransferBlocks - transferedBlocks) : splitBlocks;
		ASSERT(blocksToTransfer>0);

#if __ENABLE_LOCKED_READ__
		//
		//	Acquire the buffer lock in the NDAS device.
		//

		if(LsCcbIsFlagOn(Ccb, CCB_FLAG_ACQUIRE_BUFLOCK)) {

			//
			//	Acquire the buffer lock allocated for this unit device.
			//

			status = NdasAcquireBufferLock(
				&IdeDisk->BuffLockCtl,
				IdeDisk->LanScsiSession,
				&IdeDisk->LuHwData,
				NULL,
				NULL);
			if (status == STATUS_NOT_SUPPORTED) {
				KDPrintM(DBG_LURN_ERROR, ("Lock is not supported\n"));
			} else if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_ERROR, ("Failed to acquire the buffer lock\n"));
				Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
				return STATUS_UNSUCCESSFUL;
			}
		}
#endif

		LURNIDE_INITIALIZE_PDUDESC(
			IdeDisk, 
			&pduDesc, 
			IDE_COMMAND, 
			WIN_READ, 
			LogicalBlockAddress + transferedBlocks,
			blocksToTransfer,
			blocksToTransfer * IdeDisk->LuHwData.BlockBytes,
			(PBYTE)DataBuffer + (transferedBlocks * IdeDisk->LuHwData.BlockBytes),
			NULL);
		if(isLongTimeout && transferedBlocks == 0) {

			longTimeout.QuadPart = LURNIDE_GENERIC_TIMEOUT * 3;
			pduDesc.TimeOut = &longTimeout;
		} else {
			pduDesc.TimeOut = NULL;
		}

		KDPrintM(DBG_LURN_TRACE,
				("Address = %I64d, Databuffer length = %d\n",
					LogicalBlockAddress + transferedBlocks,
					pduDesc.DataBufferLength)
		);

		status = LspRequest(
						IdeDisk->LanScsiSession,
						&pduDesc,
						&response
					);

#if __ENABLE_LOCKED_READ__
		//
		//	Release the lock in the NDAS device.
		//

		if(	LsCcbIsFlagOn(Ccb, CCB_FLAG_ACQUIRE_BUFLOCK)) {
			NdasReleaseBufferLock(
				&IdeDisk->BuffLockCtl,
				IdeDisk->LanScsiSession,
				&IdeDisk->LuHwData,
				NULL,
				NULL,
				FALSE,
				blocksToTransfer * IdeDisk->LuHwData.BlockBytes);
		}
#endif

#if DBG
		if(!NT_SUCCESS(status) || response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR,
					("logicalBlockAddress = %x, transferBlocks = %d, transferedBlocks = %d\n",
					LogicalBlockAddress, TransferBlocks, transferedBlocks));
			ASSERT(TransferBlocks>0);
		}
#endif
		//
		// Get the transfer log
		//
		IdeDisk->Retransmits += pduDesc.Retransmits;
		IdeDisk->PacketLoss	+= pduDesc.PacketLoss;
		if(!NT_SUCCESS(status)) {
			LURN_FAULT_IO Lfi;
			Lfi.Address = LogicalBlockAddress;
			Lfi.Length = blocksToTransfer;
			LurnRecordFault(IdeDisk->Lurn, LURN_ERR_READ, LURN_FAULT_COMMUNICATION, &Lfi);

#if 0
			//
			// If fault occured same location again, handle this case as bad sector.
			//
			if (LurnGetCauseOfFault(IdeDisk->Lurn) & LURN_FCAUSE_BAD_SECTOR) {
				KDPrintM(DBG_LURN_ERROR,
					("Multiple failure of reading same place. Handle as bad sector address start=%I64x, length=%x \n",
					LogicalBlockAddress + transferedBlocks,
					blocksToTransfer)
				);

				Ccb->ResidualDataLength = (ULONG)(TransferBlocks - transferedBlocks) * IdeDisk->LuHwData.BlockBytes;

				if(Ccb->SenseBuffer != NULL) {
					PSENSE_DATA	senseData;

					senseData = Ccb->SenseBuffer;

					senseData->ErrorCode = 0x70;
					senseData->Valid = 1;
					senseData->SenseKey = SCSI_SENSE_MEDIUM_ERROR;

					senseData->AdditionalSenseLength = 0xb;
					senseData->AdditionalSenseCode = SCSI_ADSENSE_NO_SENSE;
					senseData->AdditionalSenseCodeQualifier = 0;
				} else {
					KDPrintM(DBG_LURN_ERROR, ("Error SenseBuffer is NULL\n"));
				}
				LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_DEFECTIVE);
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				status = STATUS_IO_DEVICE_ERROR;
				goto diskread_exit;
			}
#endif
			KDPrintM(DBG_LURN_ERROR, ("Error when LspRequest 0x%x\n", status));
			Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
			break;
		} else {
			LURN_FAULT_IO Lfi;
			Lfi.Address = LogicalBlockAddress;
			Lfi.Length = blocksToTransfer;
		
#if DBG
			if(response != LANSCSI_RESPONSE_SUCCESS)
				KDPrintM(DBG_LURN_ERROR, ("Device Report Error 0x%x\n", response));
#endif
			switch(response) {
			case LANSCSI_RESPONSE_SUCCESS:
				Ccb->CcbStatus = CCB_STATUS_SUCCESS;
				LurnRecordFault(IdeDisk->Lurn, LURN_ERR_SUCCESS, 0, NULL);
				break;
			case LANSCSI_RESPONSE_T_NOT_EXIST:

				Ccb->CcbStatus = CCB_STATUS_NOT_EXIST;

				LurnRecordFault(IdeDisk->Lurn, LURN_ERR_DISK_OP, LURN_FAULT_NOT_EXIST, &Lfi);
				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: returned LANSCSI_RESPONSE_T_NOT_EXIST\n"));
				
				goto diskread_exit;
			case LANSCSI_RESPONSE_T_BAD_COMMAND: // this is not used by HW. 2.5 HW may use as no target. Need to change name.

//				Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
				Ccb->CcbStatus = CCB_STATUS_NOT_EXIST;
				LurnRecordFault(IdeDisk->Lurn, LURN_ERR_DISK_OP, LURN_FAULT_NOT_EXIST, &Lfi);
				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: returned LANSCSI_RESPONSE_T_BAD_COMMAND\n"));
				goto diskread_exit;	
			case LANSCSI_RESPONSE_T_COMMAND_FAILED:
				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: returned LANSCSI_RESPONSE_T_COMMAND_FAILED\n"));
			
				//
				//	Figure out exact error status.
				//	Command will have status register's value,
				//	least bit has a validity of error register. - but it is implemented incorrectly at 1.1~2.0.
				//	After 2.5 status field is just IDE register's status value

				//
				// IDE reply's Feature contains error field of IDE registers

				if((!(pduDesc.Feature & ID_ERR) && (pduDesc.Feature & ABRT_ERR)) ||
					((pduDesc.Feature & ID_ERR) && !(pduDesc.Feature & ABRT_ERR))){

					//
					// ID not found error!
					//	( Disk logical address not found )
					//

					KDPrintM(DBG_LURN_ERROR,
								("IDE error: Illegal address.\n"));
					

					LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_VOLUME_OVERFLOW, 0);
#if DBG
					if (!Ccb->SenseBuffer) {
						KDPrintM(DBG_LURN_ERROR, ("Error SenseBuffer is NULL\n"));
					}
#endif
					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
					status = STATUS_IO_DEVICE_ERROR;
					LurnRecordFault(IdeDisk->Lurn, LURN_ERR_READ, LURN_FAULT_OUT_OF_BOUND_ACCESS, &Lfi);
					
					goto diskread_exit;
				}

				if (pduDesc.Feature & ICRC_ERR) {

					// 
					// Detected error during transfer. Cable or PCB board may be bad.
					// Hope it is okay when retrying.
					//
					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
					status = STATUS_IO_DEVICE_ERROR;
					LurnRecordFault(IdeDisk->Lurn, LURN_ERR_READ, LURN_FAULT_TRANSFER_CRC, &Lfi);	
					retryCount++;
					if (retryCount >5) {
						Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
						status = STATUS_IO_DEVICE_ERROR;
						goto diskread_exit;
					}					
					goto retry_io;
				}
				if (pduDesc.Feature & ECC_ERR) {

					//
					// Bad sector is detected
					//
					KDPrintM(DBG_LURN_ERROR,
						("CcbStatus: Bad sector detected in address start=%I64x, length=%x while reading\n", 
						LogicalBlockAddress + transferedBlocks,
						blocksToTransfer)
					);

					LurnRecordFault(IdeDisk->Lurn, LURN_ERR_READ, LURN_FAULT_BAD_SECTOR, &Lfi);
					
					Ccb->ResidualDataLength = (ULONG)(TransferBlocks - transferedBlocks) * IdeDisk->LuHwData.BlockBytes;

					LSCCB_SETSENSE(Ccb, SCSI_SENSE_MEDIUM_ERROR, SCSI_ADSENSE_NO_SENSE, 0);
#if DBG
					if(!Ccb->SenseBuffer) {
						KDPrintM(DBG_LURN_ERROR, ("No sense buffer.\n"));
					}
#endif
				}

				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				status = STATUS_IO_DEVICE_ERROR;
				goto diskread_exit;

			case LANSCSI_RESPONSE_T_BROKEN_DATA:
			default:
				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: returned LANSCSI_RESPONSE_T_BROKEN_DATA or failure.\n"));
				retryCount++;
				LurnRecordFault(IdeDisk->Lurn, LURN_ERR_DIGEST, 0, NULL);
				if (retryCount >5) {
					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
					status = STATUS_IO_DEVICE_ERROR;
					goto diskread_exit;
				}
				goto retry_io;
			}

		}
	}

#if __NDAS_SCSI_LPXTDI_V2__
	endTime = LpxTdiV2CurrentTime();
	//KeQuerySystemTime( &endTime );

	NdasFcUpdateSendSize( &IdeDisk->LanScsiSession->RecvNdasFcStatistics, 
						  chooseDataLength, 
						  TransferBlocks * IdeDisk->LuHwData.BlockBytes,
						  startTime, 
						  endTime );

	}
#endif

	LurnIdeDiskAdjustReadSplit(IdeDisk);

diskread_exit:

	//
	// Start the IO idle timer
	//
	StartIoIdleTimer(&IdeDisk->BuffLockCtl);

	//
	//	Decrypt the content
	//
	if(	NT_SUCCESS(status) &&
		IdeDisk->CntEcrMethod
	) {
		DecryptBlock(
				IdeDisk->CntEcrCipher,
				IdeDisk->CntEcrKey,
				pduDesc.DataBufferLength,
				pduDesc.DataBuffer,
				pduDesc.DataBuffer
			);
	}

#if __ENABLE_PERFORMACE_CHECK__

	PFCurrentTime = KeQueryPerformanceCounter(NULL);
	if(PFCurrentTime.QuadPart - PFStartTime.QuadPart > 10000) {
		DbgPrint("=PF= addr:%d blk=%d elaps:%I64d\n", 
								LogicalBlockAddress,
								TransferBlocks,
								PFCurrentTime.QuadPart - PFStartTime.QuadPart
							);
	}

#endif
	IdeDisk->Lurn->LastAccessedAddress = LogicalBlockAddress + TransferBlocks;
	return status;
}


//
// Verify the written data is correct by reading and comparing it.
//

static
NTSTATUS
LurnIdeDiskVerifyWrite(
	IN OUT PCCB				Ccb,
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN PVOID				ComparedDataBuffer,
	IN UINT64				LogicalBlockAddress,
	IN ULONG				TransferBlocks,
	IN ULONG				SplitBlockBytes // in bytes
){
	NTSTATUS			status;
	BYTE				response;
	LANSCSI_PDUDESC		pduDesc;
	ULONG				transferedBlocks;
	ULONG				blocksToTransfer;
	UINT32				splitBlocks;
	BOOLEAN				isLongTimeout;
	LARGE_INTEGER		longTimeout;


	// AING : some disks (Maxtor 80GB supports LBA only)
	//		ASSERT(IdeDisk->LuData.PduDescFlags&PDUDESC_FLAG_LBA48);
	KDPrintM(DBG_LURN_NOISE, ("0x%08x(%d)\n", LogicalBlockAddress, TransferBlocks));

	if(0 == TransferBlocks)
	{
		KDPrintM(DBG_LURN_ERROR, ("Return for transferBlocks == 0\n"));
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		return STATUS_SUCCESS;
	}
	status = STATUS_SUCCESS;

	//
	// Calculate the number of split blocks
	//

	splitBlocks = SplitBlockBytes / IdeDisk->LuHwData.BlockBytes;
	if(splitBlocks == 0)
		splitBlocks = 1;

	//
	// Long time out
	//

	isLongTimeout = (Ccb->CcbStatusFlags & CCBSTATUS_FLAG_LONG_COMM_TIMEOUT) != 0;


	//
	//	Send read LSP requests split by SplitBlock factor.
	//

#if __NDAS_SCSI_LPXTDI_V2__
	NDASSCSI_ASSERT( SplitBlockBytes == (64*1024) );
#endif

	for(transferedBlocks = 0; transferedBlocks < TransferBlocks; transferedBlocks += splitBlocks)
	{

		blocksToTransfer = (TransferBlocks - transferedBlocks <= splitBlocks) ? (TransferBlocks - transferedBlocks) : splitBlocks;
		ASSERT(blocksToTransfer>0);
		ASSERT(blocksToTransfer * IdeDisk->LuHwData.BlockBytes <= 65536);

		LURNIDE_INITIALIZE_PDUDESC(
			IdeDisk, 
			&pduDesc, 
			IDE_COMMAND, 
			WIN_READ, 
			LogicalBlockAddress + transferedBlocks,
			blocksToTransfer,
			blocksToTransfer * IdeDisk->LuHwData.BlockBytes,
			IdeDisk->WriteCheckBuffer,
			NULL);
		if(isLongTimeout && transferedBlocks == 0) {

			longTimeout.QuadPart = LURNIDE_GENERIC_TIMEOUT * 3;
			pduDesc.TimeOut = &longTimeout;
		} else {
			pduDesc.TimeOut = NULL;
		}

		KDPrintM(DBG_LURN_TRACE,
			("Address = %I64d, Databuffer length = %d\n",
			LogicalBlockAddress + transferedBlocks,
			pduDesc.DataBufferLength)
			);

		status = LspRequest(
			IdeDisk->LanScsiSession,
			&pduDesc,
			&response
			);
#if DBG
		if(!NT_SUCCESS(status) || response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR,
				("logicalBlockAddress = %I64x, transferBlocks = %d, transferedBlocks = %d\n",
				LogicalBlockAddress, TransferBlocks, transferedBlocks));
			ASSERT(TransferBlocks>0);
		}
#endif
		//
		// Get the transfer log
		//
		IdeDisk->Retransmits += pduDesc.Retransmits;
		IdeDisk->PacketLoss	+= pduDesc.PacketLoss;


		if(!NT_SUCCESS(status)) {

			KDPrintM(DBG_LURN_ERROR, ("Error when LspRequest 0x%x\n", status));

			Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
			break;
		} else {
#if DBG
			if(response != LANSCSI_RESPONSE_SUCCESS)
				KDPrintM(DBG_LURN_ERROR, ("Device Report Error 0x%x\n", response));
#endif
			switch(response) {
			case LANSCSI_RESPONSE_SUCCESS:
				// read success. Compare written data
				if (RtlCompareMemory(
					IdeDisk->WriteCheckBuffer,
					(PBYTE)ComparedDataBuffer + (transferedBlocks *IdeDisk->LuHwData.BlockBytes),
					(blocksToTransfer*IdeDisk->LuHwData.BlockBytes)) != (blocksToTransfer*IdeDisk->LuHwData.BlockBytes)) {
							KDPrintM(DBG_LURN_ERROR,
								("Compare error. Retry.\n"));
							return STATUS_RETRY;
				} else {
					Ccb->CcbStatus = CCB_STATUS_SUCCESS;
				}
				break;
			case LANSCSI_RESPONSE_T_NOT_EXIST:
				Ccb->CcbStatus = CCB_STATUS_NOT_EXIST;

				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: returned LANSCSI_RESPONSE_T_NOT_EXIST\n"));
				goto diskverifywrite_exit;

			case LANSCSI_RESPONSE_T_BAD_COMMAND:
				Ccb->CcbStatus = CCB_STATUS_NOT_EXIST;
				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: returned LANSCSI_RESPONSE_T_BAD_COMMAND\n"));
				goto diskverifywrite_exit;	

			case LANSCSI_RESPONSE_T_COMMAND_FAILED:
				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: returned LANSCSI_RESPONSE_T_COMMAND_FAILED\n"));

				//
				//	Figure out exact error status.
				//	Command will have status register's value,
				//	least bit has a validity of error register. - but it is implemented incorrectly at 1.1~2.0.
				//	After 2.5 status field is just IDE register's status value

				//
				// IDE reply's Feature contains error field of IDE registers

				if((!(pduDesc.Feature & ID_ERR) && (pduDesc.Feature & ABRT_ERR)) ||
					((pduDesc.Feature & ID_ERR) && !(pduDesc.Feature & ABRT_ERR))){

						//
						// ID not found error!
						//	( Disk logical address not found )
						//

						KDPrintM(DBG_LURN_ERROR,
							("IDE error: Illegal address.\n"));

						LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_VOLUME_OVERFLOW, 0);
#if DBG
						if (!Ccb->SenseBuffer) {
							KDPrintM(DBG_LURN_ERROR, ("Error SenseBuffer is NULL\n"));
						}
#endif
						Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
						status = STATUS_IO_DEVICE_ERROR;

						goto diskverifywrite_exit;
				}
				if (pduDesc.Feature & ICRC_ERR) {

					// 
					// Detected error during transfer. Cable or PCB board may be bad.

					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
					goto diskverifywrite_exit;
				}
				if (pduDesc.Feature & ECC_ERR) {

					//
					// Bad sector is detected
					//
					KDPrintM(DBG_LURN_ERROR,
						("CcbStatus: Bad sector detected in address start=%I64x, length=%x while reading\n", 
						LogicalBlockAddress + transferedBlocks,
						blocksToTransfer)
						);


					Ccb->ResidualDataLength = (ULONG)(TransferBlocks - transferedBlocks) * IdeDisk->LuHwData.BlockBytes;
					LSCCB_SETSENSE(Ccb, SCSI_SENSE_MEDIUM_ERROR, SCSI_ADSENSE_NO_SENSE, 0);
#if DBG
					if(!Ccb->SenseBuffer) {
						KDPrintM(DBG_LURN_ERROR, ("No sense buffer.\n"));
					}
#endif
				}

				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				status = STATUS_IO_DEVICE_ERROR;
				goto diskverifywrite_exit;

			case LANSCSI_RESPONSE_T_BROKEN_DATA:
			default:
				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: returned LANSCSI_RESPONSE_T_BROKEN_DATA or failure.\n"));
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
			}

		}
	}

diskverifywrite_exit:


	return status;
}

static
NTSTATUS
LurnIdeDiskWriteNoFua(
	IN OUT PCCB				Ccb,
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN PVOID				DataBuffer,
	IN UINT64				LogicalBlockAddress,
	IN ULONG				TransferBlocks,
	IN ULONG				SplitBlockBytes // in bytes
){
	BYTE				response;
	LANSCSI_PDUDESC		pduDesc;
	NTSTATUS			status;
	BOOLEAN				recoverBuffer;
	PBYTE				l_DataBuffer;
	UINT32				l_Flags;
	ULONG				transferedBlocks;
	UINT32				retryCount;
	ULONG				blocksToTransfer;
	UINT32				splitBlocks;
	BOOLEAN				isLongTimeout;
	LARGE_INTEGER		longTimeout;
	BOOLEAN				lostLockIO;
#if __ENABLE_PERFORMACE_CHECK__
	LARGE_INTEGER		PFStartTime;
	LARGE_INTEGER		PFCurrentTime;

	PFStartTime = KeQueryPerformanceCounter(NULL);
	PFCurrentTime.QuadPart = PFStartTime.QuadPart;
#endif

	KDPrintM(DBG_LURN_NOISE, ("0x%08x(%d)\n", LogicalBlockAddress, TransferBlocks));

	if(0 == TransferBlocks)
	{
		KDPrintM(DBG_LURN_ERROR, ("Return for transferBlocks == 0\n"));
		return STATUS_SUCCESS;
	}
	status = STATUS_SUCCESS;
	response = LANSCSI_RESPONSE_SUCCESS;
	pduDesc.DataBuffer = NULL;

	//
	// Reset flow control stats
	//

	LurnIdeDiskResetProtocolCounters(IdeDisk);

	//
	// Stop the IO idle timer
	//

	StopIoIdleTimer(&IdeDisk->BuffLockCtl);
	
	//
	// Check to see if this IO requires a lost device lock.
	//

	lostLockIO = LockCacheCheckLostLockIORange(&IdeDisk->LuHwData, LogicalBlockAddress, LogicalBlockAddress + TransferBlocks - 1);
	if(lostLockIO) {
		KDPrintM(DBG_LURN_ERROR, ("Blocked by a lost lock.\n"));
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		return STATUS_SUCCESS;
	}

	recoverBuffer = FALSE;

	//
	// Calculate the number of split blocks
	//

	splitBlocks = SplitBlockBytes / IdeDisk->LuHwData.BlockBytes;
	if(splitBlocks == 0)
		splitBlocks = 1;

	//
	// Long time out
	//

	isLongTimeout = (Ccb->CcbStatusFlags & CCBSTATUS_FLAG_LONG_COMM_TIMEOUT) != 0;

	//
	//	Try to encrypt the content.
	//
	l_Flags = 0;
	l_DataBuffer = DataBuffer;
	if(IdeDisk->CntEcrMethod) {

		//
		//	Encrypt the content before writing.
		//	If the primary buffer is volatile, encrypt it directly.
		//
		if(LsCcbIsFlagOn(Ccb, CCB_FLAG_VOLATILE_PRIMARY_BUFFER)) {
			EncryptBlock(
					IdeDisk->CntEcrCipher,
					IdeDisk->CntEcrKey,
					TransferBlocks *IdeDisk->LuHwData.BlockBytes,
					DataBuffer,
					DataBuffer
				);
		//
		//	If the encryption buffer is available, encrypt it by copying to the encryption buffer.
		//
		} else if(	IdeDisk->CntEcrBuffer &&
			IdeDisk->CntEcrBufferLength >= (TransferBlocks *IdeDisk->LuHwData.BlockBytes)
			) {

			EncryptBlock(
					IdeDisk->CntEcrCipher,
					IdeDisk->CntEcrKey,
					TransferBlocks *IdeDisk->LuHwData.BlockBytes,
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
					TransferBlocks *IdeDisk->LuHwData.BlockBytes,
					DataBuffer,
					DataBuffer
				);
			recoverBuffer = TRUE;
		}
#if DBG
		if(	!IdeDisk->CntEcrBuffer ||
			IdeDisk->CntEcrBufferLength < (TransferBlocks *IdeDisk->LuHwData.BlockBytes)
			) {
		KDPrintM(DBG_LURN_ERROR,
			("Encryption buffer too small! %p %d\n",
			IdeDisk->CntEcrBuffer, IdeDisk->CntEcrBufferLength));
		}
#endif
	}

	//
	//	Send write LSP requests split by SplitBlock factor.
	//

	retryCount = 0;
retry:

	for(transferedBlocks = 0; transferedBlocks < TransferBlocks; transferedBlocks += splitBlocks)
	{		
		//
		//	Acquire the buffer lock in the NDAS device.
		//

		if(LsCcbIsFlagOn(Ccb, CCB_FLAG_ACQUIRE_BUFLOCK)) {

			//
			//	Acquire the buffer lock allocated for this unit device.
			//

			status = NdasAcquireBufferLock(
				&IdeDisk->BuffLockCtl,
				IdeDisk->LanScsiSession,
				&IdeDisk->LuHwData,
				NULL,
				NULL);
			if (status == STATUS_NOT_SUPPORTED) {
				KDPrintM(DBG_LURN_ERROR, ("Lock is not supported\n"));
			} else if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_ERROR, ("Failed to acquire the buffer lock\n"));
				Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
				return STATUS_UNSUCCESSFUL;
			}
		}


		//
		// Send Write command.
		//

		blocksToTransfer = (TransferBlocks - transferedBlocks <= splitBlocks) ? (TransferBlocks - transferedBlocks) : splitBlocks;

		LURNIDE_INITIALIZE_PDUDESC(
			IdeDisk, 
			&pduDesc, 
			IDE_COMMAND, 
			WIN_WRITE,
			LogicalBlockAddress + transferedBlocks, 
			blocksToTransfer,
			blocksToTransfer * IdeDisk->LuHwData.BlockBytes,
			l_DataBuffer + (transferedBlocks  * IdeDisk->LuHwData.BlockBytes),
			NULL);
		if(isLongTimeout && transferedBlocks == 0) {

			longTimeout.QuadPart = LURNIDE_GENERIC_TIMEOUT * 3;
			pduDesc.TimeOut = &longTimeout;
		} else {
			pduDesc.TimeOut = NULL;
		}
		pduDesc.Flags |= l_Flags;

		ASSERT(TransferBlocks >0);
		ASSERT(blocksToTransfer > 0);

		KDPrintM(DBG_LURN_NOISE,
			("logicalBlockAddress = %I64u, transferBlocks = %d\n",
			LogicalBlockAddress + transferedBlocks, blocksToTransfer));

		status = LspRequest(
						IdeDisk->LanScsiSession,
						&pduDesc,
						&response
					);

		//
		//	Release the lock in the NDAS device.
		//

		if(	LsCcbIsFlagOn(Ccb, CCB_FLAG_ACQUIRE_BUFLOCK)) {
			NdasReleaseBufferLock(
				&IdeDisk->BuffLockCtl,
				IdeDisk->LanScsiSession,
				&IdeDisk->LuHwData,
				NULL,
				NULL,
				FALSE,
				blocksToTransfer * IdeDisk->LuHwData.BlockBytes);
		}

		//
		// Get the transfer log
		//
		IdeDisk->Retransmits += pduDesc.Retransmits;
		IdeDisk->PacketLoss	+= pduDesc.PacketLoss;

#if DBG
		if(!NT_SUCCESS(status) || response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR,
					("logicalBlockAddress = %I64x, transferBlocks = %d, transferedBlocks = %d\n",
					LogicalBlockAddress, TransferBlocks, transferedBlocks));
		}
#endif
		//
		// Error check
		//

		if(!NT_SUCCESS(status)) {
			LURN_FAULT_IO Lfi;
			Lfi.Address = LogicalBlockAddress;
			Lfi.Length = TransferBlocks;

			LurnRecordFault(IdeDisk->Lurn, LURN_ERR_WRITE, LURN_FAULT_COMMUNICATION, &Lfi);

#if 0
			//
			// If fault occurred same location again, handle this case as bad sector.
			//
			if (LurnGetCauseOfFault(IdeDisk->Lurn) & LURN_FCAUSE_BAD_SECTOR) {
				KDPrintM(DBG_LURN_ERROR,
					("LurnIdeDiskWrite: Multiple failure of reading same place. Handle as bad sector address start=%I64x, length=%x \n",
					LogicalBlockAddress + transferedBlocks,
					TransferBlocks)
				);
				
				Ccb->ResidualDataLength = (ULONG)(TransferBlocks - transferedBlocks) * IdeDisk->LuHwData.BlockBytes;

				if(Ccb->SenseBuffer != NULL) {
					PSENSE_DATA	senseData;

					senseData = Ccb->SenseBuffer;

					senseData->ErrorCode = 0x70;
					senseData->Valid = 1;
					senseData->SenseKey = SCSI_SENSE_MEDIUM_ERROR;

					senseData->AdditionalSenseLength = 0xb;
					senseData->AdditionalSenseCode = SCSI_ADSENSE_NO_SENSE;
					senseData->AdditionalSenseCodeQualifier = 0;
				}
				LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_DEFECTIVE);
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				status = STATUS_IO_DEVICE_ERROR;
				goto diskwrite_exit;
			}
#endif
			KDPrintM(DBG_LURN_ERROR, ("Error when LspRequest 0x%x\n", status));

			Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
			goto diskwrite_exit;

		} else {
			LURN_FAULT_IO Lfi;
			Lfi.Address = LogicalBlockAddress;
			Lfi.Length = TransferBlocks;
		
#if DBG
			if(response != LANSCSI_RESPONSE_SUCCESS)
				KDPrintM(DBG_LURN_ERROR, ("Device Report Error 0x%x\n", response));
#endif
			switch(response)
			{
			case LANSCSI_RESPONSE_SUCCESS:
				Ccb->CcbStatus = CCB_STATUS_SUCCESS;
				LurnRecordFault(IdeDisk->Lurn, LURN_ERR_SUCCESS, 0, NULL);
				break;
			case LANSCSI_RESPONSE_T_NOT_EXIST:
				Ccb->CcbStatus = CCB_STATUS_NOT_EXIST;
				LurnRecordFault(IdeDisk->Lurn, LURN_ERR_DISK_OP, LURN_FAULT_NOT_EXIST, &Lfi);

				KDPrintM(DBG_LURN_ERROR,
				("CcbStatus: returned LANSCSI_RESPONSE_T_NOT_EXIST.\n"));
				goto diskwrite_exit;
			case LANSCSI_RESPONSE_T_BAD_COMMAND: // not used by HW
				Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
				KDPrintM(DBG_LURN_ERROR,
				("CcbStatus: returned LANSCSI_RESPONSE_T_BAD_COMMAND..\n"));
				LurnRecordFault(IdeDisk->Lurn, LURN_ERR_DISK_OP, LURN_FAULT_NOT_EXIST, &Lfi);
				
				goto diskwrite_exit;
			case LANSCSI_RESPONSE_T_COMMAND_FAILED:
				{
				        KDPrintM(DBG_LURN_ERROR,
					        ("CcbStatus: returned LANSCSI_RESPONSE_T_COMMAND_FAILED\n"));
        
				        //
				        //	Figure out exact error status.
				        //	Command will have status register's value,
				        //	least bit has a validity of error register. - but it is implemented incorrectly at 1.1~2.0.
				        //	After 2.5 status field is just IDE register's status value
        
				        //
				        // IDE reply's Feature contains error field of IDE registers
 
					if((!(pduDesc.Feature & ID_ERR) && (pduDesc.Feature & ABRT_ERR)) ||
						((pduDesc.Feature & ID_ERR) && !(pduDesc.Feature & ABRT_ERR))) {

						KDPrintM(DBG_LURN_ERROR,
							("IDE error: ID not found.\n"));

						LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_NO_SENSE, 0);

#if DBG
						if(!Ccb->SenseBuffer) {
							KDPrintM(DBG_LURN_ERROR, ("Error SenseBuffer is NULL\n"));
						}
#endif
						Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
						status = STATUS_IO_DEVICE_ERROR;
					        LurnRecordFault(IdeDisk->Lurn, LURN_ERR_WRITE, LURN_FAULT_OUT_OF_BOUND_ACCESS, &Lfi);
						goto diskwrite_exit;
					}


					if (pduDesc.Feature & ICRC_ERR) {
						// 
						// Detected error during transfer. Cable or PCB board may be bad.
						// Hope it is okay when retrying.
						//
						Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
						status = STATUS_IO_DEVICE_ERROR;
					        LurnRecordFault(IdeDisk->Lurn, LURN_ERR_WRITE, LURN_FAULT_TRANSFER_CRC, &Lfi);					
        
						retryCount++;
						if (retryCount >5) {
								Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
								status = STATUS_IO_DEVICE_ERROR;
								goto diskwrite_exit;
						}
						goto retry;
					}

					if (pduDesc.Feature & ECC_ERR) {
						//
						// Bad sector is detected
						//
						KDPrintM(DBG_LURN_ERROR,
							("CcbStatus: Bad sector detected in address start=%I64x, length=%x while writing\n", 
							LogicalBlockAddress + transferedBlocks,
							blocksToTransfer)
						);

					        LurnRecordFault(IdeDisk->Lurn, LURN_ERR_WRITE, LURN_FAULT_BAD_SECTOR, &Lfi);

						Ccb->ResidualDataLength = (ULONG)(TransferBlocks - transferedBlocks) * IdeDisk->LuHwData.BlockBytes;
						LSCCB_SETSENSE(Ccb, SCSI_SENSE_MEDIUM_ERROR, SCSI_ADSENSE_NO_SENSE, 0);
#if DBG
						if(!Ccb->SenseBuffer) {
							KDPrintM(DBG_LURN_ERROR, ("No Sense buffer.\n"));
						}
#endif
					}
					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
					status = STATUS_IO_DEVICE_ERROR;
					goto diskwrite_exit;
				}
			case LANSCSI_RESPONSE_T_BROKEN_DATA:
				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: returned LANSCSI_RESPONSE_T_BROKEN_DATA or failure..\n"));

			default:
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				status = STATUS_IO_DEVICE_ERROR;
				LurnRecordFault(IdeDisk->Lurn, LURN_ERR_DIGEST, 0, NULL);

				if (retryCount >5) {
					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
					status = STATUS_IO_DEVICE_ERROR;
					goto diskwrite_exit;
				}

				goto retry;
			}
		}
	}  // for()

	//
	// Adjust write split size after write operation.
	//

	LurnIdeDiskAdjustWriteSplit(IdeDisk);

#if 0 // As we don't use UDMA mode for 2.0 rev 0, we don't need to use LurnIdeDiskVerifyWrite.
	//
	// 2.0 rev 0 chip bug fix.
	// Check written data is correct and rewrite if it is incorrect.
	//

	if (LsCcbIsFlagOn(Ccb, CCB_FLAG_WRITE_CHECK) &&
		IdeDisk->LuHwData.HwVersion == LANSCSIIDE_VERSION_2_0 &&
		IdeDisk->LuHwData.HwRevision == LANSCSIIDE_VER20_REV_1G_ORIGINAL &&
		NT_SUCCESS(status) && response == LANSCSI_RESPONSE_SUCCESS &&
		(LogicalBlockAddress + transferedBlocks-1) < IdeDisk->Lurn->UnitBlocks) {
		LurnIdeDiskResetProtocolCounters(IdeDisk);
		status = LurnIdeDiskVerifyWrite(Ccb, IdeDisk, l_DataBuffer, LogicalBlockAddress, TransferBlocks, IdeDisk->ReadSplitSize);
		if (!NT_SUCCESS(status) || Ccb->CcbStatus != CCB_STATUS_SUCCESS) {
			retryCount++;
			if (retryCount>3) {
				KDPrintM(DBG_LURN_ERROR,("Write-check failed multiple times. There may some other reason to fail.\n"));
			} else {
				KDPrintM(DBG_LURN_ERROR,
					("Write-check failed. Retrying %d\n", retryCount));
				goto retry;
			}
		} else {
			// go through. handle as if it is writing failure..	
		}
		LurnIdeDiskAdjustReadSplit(IdeDisk);
	}
#endif

diskwrite_exit:
	//
	// Start the IO idle timer
	//
	StartIoIdleTimer(&IdeDisk->BuffLockCtl);

	if(recoverBuffer && pduDesc.DataBuffer) {
		ASSERT(FALSE);
		DecryptBlock(
				IdeDisk->CntEcrCipher,
				IdeDisk->CntEcrKey,
				TransferBlocks *IdeDisk->LuHwData.BlockBytes,
				DataBuffer,
				DataBuffer
			);
	}

#if __ENABLE_PERFORMACE_CHECK__

	PFCurrentTime = KeQueryPerformanceCounter(NULL);
	if(PFCurrentTime.QuadPart - PFStartTime.QuadPart > 10000) {
		DbgPrint("=PF= addr:%d blk=%d elaps=%I64d\n", 
			LogicalBlockAddress,
			TransferBlocks,
			PFCurrentTime.QuadPart - PFStartTime.QuadPart
			);
	}

#endif
	IdeDisk->Lurn->LastAccessedAddress = LogicalBlockAddress + TransferBlocks;
	return status;
}

static
NTSTATUS
LurnIdeDiskWrite(
	IN OUT PCCB				Ccb,
	IN PLURELATION_NODE		Lurn,
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN PVOID				DataBuffer,
	IN UINT64				LogicalBlockAddress,
	IN ULONG				TransferBlocks,
	IN ULONG				SplitBlockBytes, // in bytes
	IN BOOLEAN				ForceUnitAccess
){
	NTSTATUS	status;

	status = LurnIdeDiskWriteNoFua(
			Ccb,
			IdeDisk,
			DataBuffer,
			LogicalBlockAddress,
			TransferBlocks,
			SplitBlockBytes
		);
	if(NT_SUCCESS(status) && Ccb->CcbStatus == CCB_STATUS_SUCCESS) {
		//
		// Verify the disk to have force-unit-access effect
		// Most of ATA disks do not implement FUA.
		//
		// We can also assume whether the disk write cache is disabled by checking
		// LU hardware data, so we don't need to flush the write cache in the case.
		// But other host might enable the cache.
		// Therefore, we should not rely on write cache information of the LU hardware data.
		//

		if(!ForceUnitAccess) {

			//
			// Set the disk dirty flag.
			//

			IdeDisk->DiskWriteCacheDirty = TRUE;

		} else {

			//
			// Write with FUA.
			// Do not touch the disk write cache flag.
			// 

			status = LurnIdeDiskVerify(Lurn, IdeDisk, Ccb, LogicalBlockAddress, TransferBlocks);
			if(!NT_SUCCESS(status) || Ccb->CcbStatus != CCB_STATUS_SUCCESS) {
				//
				// Set the disk dirty flag.
				//

				IdeDisk->DiskWriteCacheDirty = TRUE;

				KDPrintM(DBG_LURN_ERROR,
					("WRITE:FUA: LspRequest FLUSH failed : status - %x, CcbStatus - %x\n", status, Ccb->CcbStatus));
			}
		}
	}
	//
	// The write operation failed.
	// The requester will get the error,
	// or re-issue the write operation by reconnecting process.
	// No need to set the dirty flag here.
	//
#if 0
	else {
		//
		// Set the disk dirty flag.
		//

		IdeDisk->DiskWriteCacheDirty = TRUE;
	}
#endif
	return status;
}


static
NTSTATUS
LurnIdeDiskVerify(
	IN		PLURELATION_NODE		Lurn,
	IN		PLURNEXT_IDE_DEVICE		IdeDisk,
	IN OUT	PCCB					Ccb,
	IN		UINT64					LogicalBlockAddress,
	IN		ULONG					TransferBlocks
){
	NTSTATUS	status;
	UINT64	endSector;
	UINT64	sectorCount;
	ULONG	transferBlocks;
	ULONG	totalTransferBlocks;
	BYTE	response;
	LANSCSI_PDUDESC	pduDesc;
	BOOLEAN	isLongTimeout;
	LARGE_INTEGER	longTimeout;

	UNREFERENCED_PARAMETER(Lurn);
	
	sectorCount = IdeDisk->LuHwData.SectorCount;
	totalTransferBlocks = TransferBlocks;
	endSector = LogicalBlockAddress + totalTransferBlocks - 1;

	if (TransferBlocks == 0) {
		KDPrintM(DBG_LURN_ERROR,
			("Zero transfer block\n"));
		ASSERT(FALSE);
		Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
		return STATUS_SUCCESS;
	}

	if(endSector > sectorCount - 1) {
		KDPrintM(DBG_LURN_ERROR,
			("block Overflows endSector = 0x%x, SectorCount = 0x%x\n",
			endSector, sectorCount));

		Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
		return STATUS_SUCCESS;
	}

	//
	// Long time out
	//

	isLongTimeout = (Ccb->CcbStatusFlags & CCBSTATUS_FLAG_LONG_COMM_TIMEOUT) != 0;

DO_MORE:
	transferBlocks = (totalTransferBlocks > 128) ? 128 : totalTransferBlocks;
	totalTransferBlocks -= transferBlocks;

	LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &pduDesc, IDE_COMMAND, WIN_VERIFY,LogicalBlockAddress, transferBlocks, 0, NULL, NULL);
	if(isLongTimeout && totalTransferBlocks == TransferBlocks) {

		longTimeout.QuadPart = LURNIDE_GENERIC_TIMEOUT * 3;
		pduDesc.TimeOut = &longTimeout;
	} else {
		pduDesc.TimeOut = NULL;
	}
	status = LspRequest(
		IdeDisk->LanScsiSession,
		&pduDesc,
		&response
		);

	if(!NT_SUCCESS(status) || response != LANSCSI_RESPONSE_SUCCESS) {
		KDPrintM(DBG_LURN_ERROR,
			("Error: logicalBlockAddress = %x, transferBlocks = %x\n",
			LogicalBlockAddress, transferBlocks));
	}


	if(!NT_SUCCESS(status)) {

		KDPrintM(DBG_LURN_ERROR, ("Error when LspRequest 0x%x\n", status));

		Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
		return status;

	} else {
#if DBG
		if(response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR, ("Device Report Error 0x%x\n", response));
		}
#endif
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
			KDPrintM(DBG_LURN_ERROR,
				("CcbStatus: Verify returned LANSCSI_RESPONSE_T_COMMAND_FAILED\n"));

			if((!(pduDesc.Feature & ID_ERR) && (pduDesc.Feature & ABRT_ERR)) ||
				((pduDesc.Feature & ID_ERR) && !(pduDesc.Feature & ABRT_ERR))){

					//
					// ID not found error!
					//	( Disk logical address not found )
					//

					KDPrintM(DBG_LURN_ERROR,
						("IDE error: Illegal address.\n"));

					LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_VOLUME_OVERFLOW, 0);
#if DBG
					if (!Ccb->SenseBuffer) {
						KDPrintM(DBG_LURN_ERROR, ("Error SenseBuffer is NULL\n"));
					}
#endif
					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
					status = STATUS_IO_DEVICE_ERROR;
			}

#if 1
			if (pduDesc.Feature & ECC_ERR) {
				LURN_FAULT_IO Lfi;
				Lfi.Address = LogicalBlockAddress;
				Lfi.Length = TransferBlocks;

				//
				// Bad sector is detected
				//
				KDPrintM(DBG_LURN_ERROR,
					("CcbStatus: Bad sector detected in address start=%I64x, length=%x while verifying\n", 
					LogicalBlockAddress,
					transferBlocks)
					);

				Ccb->ResidualDataLength = transferBlocks * IdeDisk->LuHwData.BlockBytes;
				LSCCB_SETSENSE(Ccb, SCSI_SENSE_MEDIUM_ERROR, SCSI_ADSENSE_NO_SENSE, 0);
				LurnRecordFault(IdeDisk->Lurn, LURN_ERR_VERIFY, LURN_FAULT_BAD_SECTOR, &Lfi);
#if DBG
				if (!Ccb->SenseBuffer) {
					KDPrintM(DBG_LURN_ERROR, ("Error SenseBuffer is NULL\n"));
				}
#endif
			}
#else
			{
				UINT64	BadSector, i;

				//
				// Search Bad Sector.
				//
				BadSector = logicalBlockAddress;

				for(i = 0; i < transferBlocks; i++) {

					LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &pduDesc, IDE_COMMAND, WIN_VERIFY,BadSector,transferBlocks, 0);
					status = LspRequest(
						IdeDisk->LanScsiSession,
						&pduDesc,
						&response
						);
					if(!NT_SUCCESS(status)) {

						KDPrintM(DBG_LURN_ERROR, ("Error when LspRequest() for find Bad Sector. 0x%x\n", status));

						Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;

						return status;
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
					KDPrintM(DBG_LURN_ERROR, (" No Bad Sector!!!\n"));

					Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;

					if(totalTransferBlocks)
						goto DO_MORE;

					return STATUS_SUCCESS;
				} else {

					KDPrintM(DBG_LURN_ERROR, ("Bad Sector is 0x%x Sense 0x%x pData 0x%x\n",
						BadSector, Ccb->SenseDataLength, Ccb->SenseBuffer));

					// Calc residual size.
					Ccb->ResidualDataLength = (ULONG)(transferBlocks - (BadSector - logicalBlockAddress)) * IdeDisk->LuData.BlockBytes;
					LSCCB_SETSENSE(Ccb, SCSI_SENSE_MEDIUM_ERROR, SCSI_ADSENSE_NO_SENSE, 0);

#if DBG
					if (!Ccb->SenseBuffer) {
						KDPrintM(DBG_LURN_ERROR, ("Error SenseBuffer is NULL\n"));
					}
#endif
				}
			}
#endif
			Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
			return STATUS_SUCCESS;

		case LANSCSI_RESPONSE_T_BROKEN_DATA:
		default:
			Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
			return STATUS_SUCCESS;
		}
	}

	if(totalTransferBlocks)
		goto DO_MORE;

	Ccb->CcbStatus = CCB_STATUS_SUCCESS;
	return STATUS_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
//
// reserve/release command
// SCSIOP_RESERVE_UNIT/SCSIOP_RELEASE_UNIT
//

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

//////////////////////////////////////////////////////////////////////////
//
// Check IDE power mode
//

#define POWERMODE_CHECK_TIMEOUT		(NANO100_PER_SEC * 10)	// 10 sec

typedef enum _ATA_POWERMODE {
	ATA_STANDBY_MODE = 1,			// P2
	ATA_IDLE_MODE,					// P1
	ATA_ACTIVE_OR_IDLE_MODE,		// P0 or P1
} ATA_POWERMODE, *PATA_POWERMODE;


//
// Determine if the CCB is a media-access command that makes the disk wake up.
//

static
BOOLEAN
IsMediaAccessCommand(
	IN PCCB Ccb
){
	switch(Ccb->OperationCode) {
		case CCB_OPCODE_EXECUTE: {
			switch(Ccb->Cdb[0]) {
				case SCSIOP_READ:
				case SCSIOP_READ16:
				case 0x3E:		// READ_LONG
				case SCSIOP_WRITE:
				case SCSIOP_WRITE16:
				case SCSIOP_VERIFY:
				case SCSIOP_VERIFY16:
					return TRUE;
				default:;
			}
			// Extended commands access the disk media.
			if(Ccb->pExtendedCommand != NULL)
				return TRUE;
		}
		break;
		case  CCB_OPCODE_SMART: {
			PSENDCMDINPARAMS sendCmdIn;
			
			sendCmdIn = (PSENDCMDINPARAMS)Ccb->DataBuffer;
			switch(sendCmdIn->irDriveRegs.bCommandReg) {
			case SMART_CMD:
				switch(sendCmdIn->irDriveRegs.bFeaturesReg) {
					case READ_ATTRIBUTES:
					case READ_THRESHOLDS:
					case SAVE_ATTRIBUTE_VALUES:
					case EXECUTE_OFFLINE_DIAGS:
					case SMART_READ_LOG:
					case SMART_WRITE_LOG:
					case ENABLE_SMART:
					case DISABLE_SMART:
					case RETURN_SMART_STATUS:
						return TRUE;
					default:;
				}
				break;
			default:;
			}
		}
		break;
		default:;
	}

	return FALSE;
}

static
NTSTATUS
GetCurrentPowerMode(
	IN PLURELATION_NODE		Lurn,
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	OUT PATA_POWERMODE		AtaPowerMode
){
	NTSTATUS status;
	LANSCSI_PDUDESC		pduDesc;
	UCHAR				powerMode;
	UCHAR				pduResponse;

	UNREFERENCED_PARAMETER(Lurn);

	//
	// Init PDU descriptor.
	//

	LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &pduDesc, IDE_COMMAND, WIN_CHECKPOWERMODE1, 0, 0, 0, NULL, NULL);
	pduDesc.Flags &= ~(PDUDESC_FLAG_LBA | PDUDESC_FLAG_LBA48 | PDUDESC_FLAG_DMA | PDUDESC_FLAG_UDMA);

	// Send smart command PDU
	status = LspRequest(IdeDisk->LanScsiSession, &pduDesc, &pduResponse);
	if(NT_SUCCESS(status) && pduResponse == LANSCSI_RESPONSE_SUCCESS) {
		powerMode = (UCHAR)pduDesc.BlockCount;
		if(powerMode == 0x00) {
			*AtaPowerMode = ATA_STANDBY_MODE;
		} else if(powerMode == 0x80) {
			*AtaPowerMode = ATA_IDLE_MODE;
		} else {
			*AtaPowerMode = ATA_ACTIVE_OR_IDLE_MODE;
		}

	} else {
		KDPrintM(1, ("LspRequest() failed. STATUS=%08lx\n", status));
		return STATUS_IO_DEVICE_ERROR;
	}

	return STATUS_SUCCESS;
}

#if __ENABLE_CHECK_POWER_MODE__

static
NTSTATUS
LurnIdeDiskCheckPowerMode(
		IN PLURELATION_NODE		Lurn,
		IN PLURNEXT_IDE_DEVICE	IdeDisk,
		IN PCCB					Ccb
){
	NTSTATUS	status;
	BOOLEAN		mediaAccess;
	LARGE_INTEGER	currentMediaAccess;
	LARGE_INTEGER	diffTime;
	ULONG			timeInc;
	ATA_POWERMODE	powerMode;

	//
	// If the device does not support power management, return.
	//

	if(IdeDisk->LuHwData.HwVersion <= LANSCSIIDE_VERSION_1_0) {
		return STATUS_SUCCESS;
	}
	if(!(IdeDisk->LuHwData.LudataFlags & LUDATA_FLAG_POWERMGMT_ENABLED)) {
		return STATUS_SUCCESS;
	}

	//
	// Check to see if this is a media access command.
	//

	mediaAccess = IsMediaAccessCommand(Ccb);
	if(mediaAccess) {

		//
		// Update the last media access time now.
		// This prevent re-check power-mode again after checking power-mode fails.
		//

		timeInc = KeQueryTimeIncrement();
		KeQueryTickCount(&currentMediaAccess);
		currentMediaAccess.QuadPart *= timeInc; // make it 100-nano unit time.

		//
		// Get difference
		//

		diffTime.QuadPart = currentMediaAccess.QuadPart - IdeDisk->LastMediaAccessTime.QuadPart;

		//
		// update the last media access time.
		//

		IdeDisk->LastMediaAccessTime.QuadPart = currentMediaAccess.QuadPart;

		 if( diffTime.QuadPart >= POWERMODE_CHECK_TIMEOUT) {

			 KDPrintM(DBG_LURN_ERROR, ("Checking the power mode\n"));

			//
			// Check power mode
			//

			status = GetCurrentPowerMode(Lurn, IdeDisk, &powerMode);
			if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_ERROR, ("GetCurrentPowerMode() failed. status=%08lx\n", status));
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				return STATUS_IO_DEVICE_ERROR;
			}

			//
			// If this is currently in standby or idle mode,
			// increase timeout.
			//

			if(	powerMode == ATA_IDLE_MODE ||
				powerMode == ATA_STANDBY_MODE) {

				Ccb->CcbStatusFlags |= CCBSTATUS_FLAG_LONG_COMM_TIMEOUT;
				KDPrintM(DBG_LURN_ERROR, ("Device is in standy or idle mode. set long timeout.\n"));
			}
#if DBG
			else {
				KDPrintM(DBG_LURN_ERROR, ("Device is active.\n"));
			}
#endif
		}
	}

	return STATUS_SUCCESS;
}

#else

static
NTSTATUS
LurnIdeDiskCheckPowerMode(
		IN PLURELATION_NODE		Lurn,
		IN PLURNEXT_IDE_DEVICE	IdeDisk,
		IN PCCB					Ccb
){
	UNREFERENCED_PARAMETER(Lurn);
	UNREFERENCED_PARAMETER(IdeDisk);
	UNREFERENCED_PARAMETER(Ccb);

	return STATUS_SUCCESS;
}

#endif
//////////////////////////////////////////////////////////////////////////
//
// SCSIOP_TEST_UNIT_READY
//

LurnIdeDiskTestUnitReady(
	IN PLURELATION_NODE			Lurn,
	IN		PLURNEXT_IDE_DEVICE	IdeDisk,
	IN OUT	PCCB				Ccb
){
	NTSTATUS		status;
	ATA_POWERMODE	ataPowerMode;

	if(IdeDisk->LuHwData.HwVersion <= LANSCSIIDE_VERSION_1_0) {
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		return STATUS_SUCCESS;
	}

	//
	//
	// Refer to SCS/ATA translation (SAT) draft revision 09
	// why I use power mode command and the sense data for test unit ready.
	//

	status = GetCurrentPowerMode(Lurn, IdeDisk, &ataPowerMode);
	if(!NT_SUCCESS(status)) {
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		LSCCB_SETSENSE(Ccb,
			SCSI_SENSE_NOT_READY,
			0x05 /* LOGICAL UNIT DOES NOT RESPOND TO SELECTION */,
			0);
		// Do not do reconnection for the test unit ready command.
		status = STATUS_SUCCESS;
	}
#if DBG
	else {
		KDPrintM(DBG_LURN_ERROR, ("Current power mode = %x\n", ataPowerMode));
	}
#endif

	Ccb->CcbStatus = CCB_STATUS_SUCCESS;

	return status;
}

//////////////////////////////////////////////////////////////////////////
//
// SCSIOP_MODE_SENSE/SCSIOP_MODE_SELECT
//

NTSTATUS
LurnIdeDiskModeSense(
	IN PLURELATION_NODE		Lurn,
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN	PCCB				Ccb
){
	PCDB	Cdb;
	PMODE_PARAMETER_HEADER	parameterHeader;
	PMODE_PARAMETER_BLOCK	parameterBlock;
	ULONG	requiredLen;
	ULONG	BlockCount;

	KDPrintM(DBG_LURN_TRACE, ("Entered.\n"));

	Cdb = (PCDB)Ccb->Cdb;
	parameterHeader = (PMODE_PARAMETER_HEADER)Ccb->DataBuffer;
	parameterBlock =  (PMODE_PARAMETER_BLOCK)((PUCHAR)parameterHeader + sizeof(MODE_PARAMETER_HEADER));

	//
	// Buffer size should larger than MODE_PARAMETER_HEADER
	//
	requiredLen = sizeof(MODE_PARAMETER_HEADER);
	if(Ccb->DataBufferLength < requiredLen) {
		KDPrintM(DBG_LURN_ERROR, ("Buffer too small. %d.\n", Ccb->DataBufferLength));
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;	//SRB_STATUS_SUCCESS;
		return STATUS_SUCCESS;
	}

	//
	// Zero out the buffer
	//

	RtlZeroMemory(
		Ccb->DataBuffer,
		Ccb->DataBufferLength
		);

	//
	// We only report current values.
	//

	if(Cdb->MODE_SENSE.Pc != (MODE_SENSE_CURRENT_VALUES>>6)) {
		KDPrintM(DBG_LURN_ERROR,
			("unsupported page control:%x\n", (ULONG)Cdb->MODE_SENSE.Pc));
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		return STATUS_SUCCESS;
	} else {
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
	}

	//
	// Mode parameter header.
	//
	parameterHeader->ModeDataLength =
		sizeof(MODE_PARAMETER_HEADER) -
		sizeof(parameterHeader->ModeDataLength); // exclude ModeDataLength itself
	parameterHeader->MediumType = 00;	// Default medium type.

	// Fill device specific parameter
	if(!(Lurn->AccessRight & GENERIC_WRITE)) {
		KDPrintM(DBG_LURN_TRACE,
			("MODE_DSP_WRITE_PROTECT\n"));
		parameterHeader->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;

		if(LsCcbIsFlagOn(Ccb, CCB_FLAG_W2K_READONLY_PATCH) ||
			LsCcbIsFlagOn(Ccb, CCB_FLAG_ALLOW_WRITE_IN_RO_ACCESS))
			parameterHeader->DeviceSpecificParameter &= ~MODE_DSP_WRITE_PROTECT;
	} else
		parameterHeader->DeviceSpecificParameter &= ~MODE_DSP_WRITE_PROTECT;

	//
	// Mode parameter block
	//
	requiredLen += sizeof(MODE_PARAMETER_BLOCK);
	if(Ccb->DataBufferLength < requiredLen) {
		Ccb->CcbStatus = CCB_STATUS_DATA_OVERRUN;
		KDPrintM(DBG_LURN_ERROR, ("Could not fill out parameter block. %d.\n", Ccb->DataBufferLength));
		return STATUS_SUCCESS;
	}

	// Set the length of mode parameter block descriptor to the parameter header.
	parameterHeader->BlockDescriptorLength = sizeof(MODE_PARAMETER_BLOCK);
	parameterHeader->ModeDataLength += sizeof(MODE_PARAMETER_BLOCK);

	BlockCount = (ULONG)(Lurn->EndBlockAddr - Lurn->StartBlockAddr + 1);
	parameterBlock->DensityCode = 0;	// It is Reserved for direct access devices.
	parameterBlock->BlockLength[0] = (BYTE)(IdeDisk->LuHwData.BlockBytes>>16);
	parameterBlock->BlockLength[1] = (BYTE)(IdeDisk->LuHwData.BlockBytes>>8);
	parameterBlock->BlockLength[2] = (BYTE)(IdeDisk->LuHwData.BlockBytes);

	//
	// Caching mode page
	//

	if(	Cdb->MODE_SENSE.PageCode == MODE_SENSE_RETURN_ALL ||
		Cdb->MODE_SENSE.PageCode == MODE_PAGE_CACHING) {	// all pages
		PMODE_CACHING_PAGE	cachingPage;
		NTSTATUS	status;
		UCHAR	pduResponse;
		LANSCSI_PDUDESC	pduDesc;
		struct hd_driveid idinfo;

		KDPrintM(DBG_LURN_TRACE, ("Caching page\n"));

		requiredLen += sizeof(MODE_CACHING_PAGE);
		if(Ccb->DataBufferLength < requiredLen) {
			Ccb->CcbStatus = CCB_STATUS_DATA_OVERRUN;
			KDPrintM(DBG_LURN_ERROR, ("Could not fill out caching page. %d.\n", Ccb->DataBufferLength));
			return STATUS_SUCCESS;
		}

		parameterHeader->ModeDataLength += sizeof(MODE_CACHING_PAGE);
		cachingPage = (PMODE_CACHING_PAGE)((PUCHAR)parameterBlock + sizeof(MODE_PARAMETER_BLOCK));

		//
		// Check write cache status
		// Other hosts or disk power reset may change the write cache setting.
		//

		LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &pduDesc, IDE_COMMAND, WIN_IDENTIFY, 0, 1, sizeof(idinfo), &idinfo, NULL);
		status = LspRequest(IdeDisk->LanScsiSession, &pduDesc, &pduResponse);
		if(!NT_SUCCESS(status)) {
			Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
			KDPrintM(DBG_LURN_ERROR, ("Identify Failed... STATUS=%08lx PduResponse=%x\n", status, pduResponse));
			return status;
		}
		if(pduResponse != LANSCSI_RESPONSE_SUCCESS) {
			Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
			KDPrintM(DBG_LURN_ERROR, ("Identify Failed... STATUS=%08lx PduResponse=%x\n", status, pduResponse));
			return STATUS_SUCCESS;
		}

		//
		// Update LU hardware flags.
		//

		IdeDisk->LuHwData.LudataFlags &=~(LUDATA_FLAG_WCACHE_SUPPORT | LUDATA_FLAG_WCACHE_ENABLED);
		if(idinfo.command_set_1 & 0x20) {
			IdeDisk->LuHwData.LudataFlags |= LUDATA_FLAG_WCACHE_SUPPORT;
			KDPrintM(DBG_LURN_TRACE, ("Write cache supported\n"));
			if(idinfo.cfs_enable_1 & 0x20) {
				IdeDisk->LuHwData.LudataFlags |= LUDATA_FLAG_WCACHE_ENABLED;
				KDPrintM(DBG_LURN_TRACE, ("Write cache enabled\n"));
			}
		}

		//
		// Fill out cache page
		//

		cachingPage->PageCode = MODE_PAGE_CACHING;
		cachingPage->PageLength = sizeof(MODE_CACHING_PAGE) -
			(FIELD_OFFSET(MODE_CACHING_PAGE, PageLength) + sizeof(cachingPage->PageLength));
		cachingPage->WriteCacheEnable = (IdeDisk->LuHwData.LudataFlags & LUDATA_FLAG_WCACHE_ENABLED) != 0;
		cachingPage->ReadDisableCache = 0;
		KDPrintM(DBG_LURN_INFO, ("reported write cache=%d\n", (ULONG)cachingPage->WriteCacheEnable));
	}

	//
	// Set residual data length
	//

	Ccb->ResidualDataLength = Ccb->DataBufferLength - requiredLen;

	return STATUS_SUCCESS;
}


NTSTATUS
LurnIdeDiskModeSelect(
	IN PLURELATION_NODE		Lurn,
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN	PCCB				Ccb
){
	NTSTATUS	status;
	PCDB		Cdb;
	PMODE_PARAMETER_HEADER	parameterHeader;
	PMODE_PARAMETER_BLOCK	parameterBlock;
	ULONG				requiredLen;
	UCHAR				parameterLength;
	PUCHAR				modePages;
	LANSCSI_PDUDESC		pduDesc;
	UCHAR				pduResponse;

	UNREFERENCED_PARAMETER(Lurn);

	KDPrintM(DBG_LURN_TRACE, ("Entered.\n"));

	Cdb = (PCDB)Ccb->Cdb;
	parameterHeader = (PMODE_PARAMETER_HEADER)Ccb->DataBuffer;
	parameterBlock =  (PMODE_PARAMETER_BLOCK)((PUCHAR)parameterHeader + sizeof(MODE_PARAMETER_HEADER));
	parameterLength = Cdb->MODE_SELECT.ParameterListLength;

	//
	// Buffer size should larger than MODE_PARAMETER_HEADER
	//

	requiredLen = sizeof(MODE_PARAMETER_HEADER);
	if(Ccb->DataBufferLength < requiredLen ||
		parameterLength < requiredLen) {
		KDPrintM(DBG_LURN_ERROR, ("Buffer too small. %d.\n", Ccb->DataBufferLength));
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		return STATUS_SUCCESS;
	}

	requiredLen += sizeof(MODE_PARAMETER_BLOCK);
	if(Ccb->DataBufferLength < requiredLen ||
		parameterLength < requiredLen) {
			KDPrintM(DBG_LURN_ERROR, ("Buffer too small. %d.\n", Ccb->DataBufferLength));
			Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
			return STATUS_SUCCESS;
	}

	//
	// We only handle mode pages and volatile settings.
	//

	if(Cdb->MODE_SELECT.PFBit == 0) {
		KDPrintM(DBG_LURN_ERROR,
			("unsupported page format:%x\n", (ULONG)Cdb->MODE_SELECT.PFBit));
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		return STATUS_SUCCESS;
	} else if(Cdb->MODE_SELECT.SPBit != 0)	{
		KDPrintM(DBG_LURN_ERROR,
			("unsupported save page to non-volitile memory:%x.\n", (ULONG)Cdb->MODE_SELECT.SPBit));
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		return STATUS_SUCCESS;
	} else {
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
	}

	//
	// Get the mode pages
	//

	modePages = (PUCHAR)parameterBlock + sizeof(MODE_PARAMETER_BLOCK);

	//
	// Caching mode page
	//

	if(	(*modePages & 0x3f) == MODE_PAGE_CACHING) {	// all pages
		PMODE_CACHING_PAGE	cachingPage;

		KDPrintM(DBG_LURN_ERROR, ("Caching page\n"));

		requiredLen += sizeof(MODE_CACHING_PAGE);
		if(Ccb->DataBufferLength < requiredLen ||
			parameterLength < requiredLen) {
				KDPrintM(DBG_LURN_ERROR, ("Buffer too small. %d.\n", Ccb->DataBufferLength));
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				return STATUS_SUCCESS;
		}

		cachingPage = (PMODE_CACHING_PAGE)modePages;

		if(cachingPage->WriteCacheEnable) {
			if(IdeDisk->LuHwData.HwVersion > LANSCSIIDE_VERSION_1_0) {
				LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &pduDesc, IDE_COMMAND, WIN_SETFEATURES, 0, 0, 0, NULL, NULL);
				pduDesc.Feature = SETFEATURES_EN_WCACHE;
				status = LspRequest(IdeDisk->LanScsiSession, &pduDesc, &pduResponse);
				if(!NT_SUCCESS(status)) {
					KDPrintM(DBG_LURN_ERROR, ("ENABLE WCACHE: Set Feature Failed. Transport error.\n"));
					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
					return status;
				}
				if(pduResponse != LANSCSI_RESPONSE_SUCCESS) {
					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;

					KDPrintM(DBG_LURN_ERROR, ("ENABLE WCACHE: Set Feature Failed. Remote device error.\n"));
					return STATUS_SUCCESS;
				}

				//
				// Update write cache flags
				//
				KDPrintM(DBG_LURN_INFO, ("Write cache enabled\n"));
				IdeDisk->LuHwData.LudataFlags |= LUDATA_FLAG_WCACHE_ENABLED;
			} else {

				//
				// NDAS chip 1.0 does not support write cache setting
				// even if the set feature command succeeds.
				//

				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;

				KDPrintM(DBG_LURN_ERROR, ("NDAS chip 1.0 does not support write cache setting.\n"));
				return STATUS_SUCCESS;
			}
		}
#if __ENABLE_WRITECACHE_CONTROL__
		else {
			if(IdeDisk->LuHwData.HwVersion > LANSCSIIDE_VERSION_1_0) {
				LURNIDE_INITIALIZE_PDUDESC(IdeDisk, &pduDesc, IDE_COMMAND, WIN_SETFEATURES, 0, 0, 0, NULL, NULL);
				pduDesc.Feature = SETFEATURES_DIS_WCACHE;
				status = LspRequest(IdeDisk->LanScsiSession, &pduDesc, &pduResponse);
				if(!NT_SUCCESS(status)) {
					KDPrintM(DBG_LURN_ERROR, ("DISABLE WCACHE: Set Feature Failed. Transport error.\n"));
					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
					return status;
				}
				if(pduResponse != LANSCSI_RESPONSE_SUCCESS) {
					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;

					KDPrintM(DBG_LURN_ERROR, ("DISABLE WCACHE: Set Feature Failed. Remote device error.\n"));
					return STATUS_SUCCESS;
				}

				//
				// Update write cache flags
				//
				KDPrintM(DBG_LURN_INFO, ("Write cache disabled\n"));
				IdeDisk->LuHwData.LudataFlags &= ~LUDATA_FLAG_WCACHE_ENABLED;
			} else {

				//
				// NDAS chip 1.0 does not support write cache setting
				// even if the set feature command succeeds.
				//

				Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;

				KDPrintM(DBG_LURN_INFO, ("NDAS chip 1.0 does not support write cache setting.\n"));
				return STATUS_SUCCESS;
			}
		}
#else
		KDPrintM(DBG_LURN_ERROR, ("Not support write cache disabling\n"));
		Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
		return STATUS_NOT_SUPPORTED;
#endif
	} else {
		KDPrintM(DBG_LURN_ERROR,
			("unsupported pagecode:%x, *modePages = %x\n", (ULONG)Cdb->MODE_SENSE.PageCode, *modePages));
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;	//SRB_STATUS_SUCCESS;
		status = STATUS_SUCCESS;
	}

	return STATUS_SUCCESS;
}



//////////////////////////////////////////////////////////////////////////
//
//	Execute CCB.
//

static
NTSTATUS
LurnIdeDiskExecute(
	IN PLURELATION_NODE		Lurn,
	IN PLURNEXT_IDE_DEVICE	IdeDisk,
	IN	PCCB				Ccb
)
{
	NTSTATUS			status;
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
	status = STATUS_SUCCESS;

	ASSERT(IdeDisk);


	//
	// Check disk power mode.
	//

	status = LurnIdeDiskCheckPowerMode(Lurn, IdeDisk, Ccb);
	if(!NT_SUCCESS(status)) {
		KDPrintM(DBG_LURN_ERROR, ("LurnIdeDiskCheckPowerMode() failed. continue to execute. STATUS=%08lx\n", status));
		// Continue even if power mode check failed.
		status = STATUS_SUCCESS;
	}


	//
	// process extended command used by RAID LURNs.
	//

	ext_cmd = Ccb->pExtendedCommand;
	while(NULL != ext_cmd)
	{
		// linked list
		switch(ext_cmd->Operation)
		{
		case CCB_EXT_READ:
		case CCB_EXT_WRITE:
//		case CCB_EXT_READ_OPERATE_WRITE:
			{
				PCMD_BYTE_OP ext_cmd_op = (PCMD_BYTE_OP)ext_cmd;
				UINT64 logicalBlockAddress;
//				BYTE one_sector[BLOCK_SIZE];

				logicalBlockAddress = (ext_cmd_op->CountBack) ? 
					(IdeDisk->LuHwData.SectorCount) - ext_cmd_op->logicalBlockAddress : ext_cmd_op->logicalBlockAddress;
				switch(ext_cmd_op->Operation)
				{
				case CCB_EXT_READ:
					{
						switch(ext_cmd_op->ByteOperation)
						{
						case EXT_BLOCK_OPERATION:
							KDPrintM(DBG_LURN_ERROR,	("CCB_EXT_READ: ext_cmd_op->pByteData = %p Addr %I64x %x\n", ext_cmd_op->pByteData, logicalBlockAddress, ext_cmd_op->LengthBlock));
							status = LurnIdeDiskRead(
										Ccb,
										IdeDisk,
										ext_cmd_op->pByteData,
										logicalBlockAddress,
										ext_cmd_op->LengthBlock,
										IdeDisk->ReadSplitSize);
							break;
#if 0
						case EXT_BYTE_OPERATION_COPY:
							status = LurnIdeDiskRead(
										Ccb,
										IdeDisk,
										one_sector,
										logicalBlockAddress,
										1,
										IdeDisk->MaxDataRecvLength);
							RtlCopyMemory(ext_cmd_op->pByteData, one_sector + ext_cmd_op->Offset, ext_cmd_op->LengthByte);
							// write back
							break;
#endif
						default:
							ASSERT(FALSE);
							break;
						}

						KDPrintM(DBG_LURN_NOISE,	("CCB_EXT_READ\n"));

						if(!NT_SUCCESS(status))
						{
							return status;
						}
					}

					break;
				case CCB_EXT_WRITE:
					{
						if(!(Lurn->AccessRight & GENERIC_WRITE)) {
							if(!LsCcbIsFlagOn(Ccb, CCB_FLAG_ALLOW_WRITE_IN_RO_ACCESS)) {
								ASSERT(FALSE);
								Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
								return STATUS_UNSUCCESSFUL;
							}
						}

						KDPrintM(DBG_LURN_NOISE,	("CCB_EXT_WRITE\n"));

						//				RtlZeroMemory(one_sector, sizeof(one_sector));

						switch(ext_cmd_op->ByteOperation)
						{
						case EXT_BLOCK_OPERATION:
							status = LurnIdeDiskWrite(
											Ccb,
											Lurn,
											IdeDisk,
											ext_cmd_op->pByteData,
											logicalBlockAddress,
											ext_cmd_op->LengthBlock,
											IdeDisk->WriteSplitSize,
											TRUE);	// Force flush after writing.CCB_EXT command assumes synchronous write,
							break;
#if 0
						case EXT_BYTE_OPERATION_COPY:
							RtlZeroMemory(one_sector, IdeDisk->LuData.BlockBytes);
							RtlCopyMemory(one_sector + ext_cmd_op->Offset, ext_cmd_op->pByteData, ext_cmd_op->LengthByte);
							// write back
							status = LurnIdeDiskWrite(
												Ccb,
												Lurn,
												IdeDisk,
												one_sector,
												logicalBlockAddress,
												1,
												IdeDisk->MaxDataSendLength,
												FALSE);
							break;
						case EXT_BYTE_OPERATION_AND:
						case EXT_BYTE_OPERATION_OR:
#endif
						default:
							ASSERT(FALSE);
							// NA JUNG YE
							break;
						}

						if(!NT_SUCCESS(status))
						{
							return status;
						}
					}

					break;
#if 0
				case CCB_EXT_READ_OPERATE_WRITE:
					{
						KDPrintM(DBG_LURN_ERROR,	("CCB_EXT_READ_OPERATE_WRITE\n"));


						// AING : It costs lots of time for hard disk to read - write serially.
						// Do not use this operation often.

						// read sector

						status = LurnIdeDiskRead(
										Ccb,
										IdeDisk,
										one_sector,
										logicalBlockAddress,
										1,
										IdeDisk->MaxDataRecvLength);
						KDPrintM(DBG_LURN_ERROR,	("CCB_EXT_READ_OPERATE_WRITE read status %x\n", status));

						if(!NT_SUCCESS(status))
						{
							return status;
						}

						if(!(Lurn->AccessRight & GENERIC_WRITE)) {
							if(!LsCcbIsFlagOn(Ccb, CCB_FLAG_ALLOW_WRITE_IN_RO_ACCESS)) {
								ASSERT(FALSE);
								Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
								return STATUS_UNSUCCESSFUL;
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
						status = LurnIdeDiskWrite(
										Ccb,
										Lurn,
										IdeDisk,
										one_sector,
										logicalBlockAddress,
										1,
										IdeDisk->MaxDataSendLength,
										FALSE);

						KDPrintM(DBG_LURN_ERROR,	("CCB_EXT_READ_OPERATE_WRITE write status %x\n", status));

						if(!NT_SUCCESS(status))
						{
							return status;
						}
					}

					break;
#endif
				}
			}
			break;
		}

		ext_cmd = (PCMD_COMMON)ext_cmd->pNextCmd;
	}

	if (Ccb->pExtendedCommand) {

		return status;
	}

	//
	// Execute the CCB depending SCSI operation code.
	//

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
			LsCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
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
			IdeDisk->LuHwData.Model,
			16
			);

		//
		// Hardware version
		//

		switch(IdeDisk->LuHwData.HwVersion) {
		case LANSCSIIDE_VERSION_1_0:
			_snprintf(inquiryData.ProductRevisionLevel, 4, "1.0");
			break;
		case LANSCSIIDE_VERSION_1_1:
			_snprintf(inquiryData.ProductRevisionLevel, 4, "1.1");
			break;
		case LANSCSIIDE_VERSION_2_0:
			switch(IdeDisk->LuHwData.HwRevision) {
			case LANSCSIIDE_VER20_REV_1G_ORIGINAL:
				_snprintf(inquiryData.ProductRevisionLevel, 4, "2.0");
				break;
			case LANSCSIIDE_VER20_REV_100M:
				_snprintf(inquiryData.ProductRevisionLevel, 4, "2.24");
				break;
			case LANSCSIIDE_VER20_REV_1G:
				_snprintf(inquiryData.ProductRevisionLevel, 4, "2.16");
				break;
			default:
				_snprintf(inquiryData.ProductRevisionLevel, 4, "2.99");
			}
			break;
		default:
			_snprintf(inquiryData.ProductRevisionLevel, 4, "0.0");
		}


		RtlMoveMemory (
					Ccb->DataBuffer,
					&inquiryData,
					Ccb->DataBufferLength > sizeof (INQUIRYDATA) ?
					sizeof (INQUIRYDATA) :
					Ccb->DataBufferLength
				);

		LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
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

		blockSize = IdeDisk->LuHwData.BlockBytes;
		REVERSE_BYTES(&readCapacityData->BytesPerBlock, &blockSize);

		Ccb->CcbStatus = CCB_STATUS_SUCCESS;

		KDPrintM(DBG_LURN_INFO, ("SCSIOP_READ_CAPACITY: %I64u : %u\n", logicalBlockAddress, blockSize));

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

		blockSize = IdeDisk->LuHwData.BlockBytes;
		REVERSE_BYTES(&readCapacityDataEx->BytesPerBlock, &blockSize);

		Ccb->CcbStatus = CCB_STATUS_SUCCESS;

		KDPrintM(DBG_LURN_INFO, ("SCSIOP_READ_CAPACITY16: %I64u : %u\n", logicalBlockAddress, blockSize));

		return STATUS_SUCCESS;

	}

	case SCSIOP_RESERVE_UNIT:
		KDPrintM(DBG_LURN_INFO, ("RESERVE(6): %02x %02x %02x %02x %02x %02x\n",
												Ccb->Cdb[0], Ccb->Cdb[1],
												Ccb->Cdb[2], Ccb->Cdb[3],
												Ccb->Cdb[4], Ccb->Cdb[5]));
		status = LurnIdeDiskReserveRelease(Lurn,IdeDisk, Ccb);
		return STATUS_SUCCESS;

	case 0x56:					// RESERVE_UNIT(10)

		KDPrintM(DBG_LURN_INFO, ("RESERVE(10): %02x %02x %02x %02x %02x %02x\n",
												Ccb->Cdb[0], Ccb->Cdb[1],
												Ccb->Cdb[2], Ccb->Cdb[3],
												Ccb->Cdb[4], Ccb->Cdb[5],
												Ccb->Cdb[6], Ccb->Cdb[7],
												Ccb->Cdb[8], Ccb->Cdb[9]));

		LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_ILLEGAL_COMMAND, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		status = STATUS_SUCCESS;
		return STATUS_SUCCESS;

	case SCSIOP_RELEASE_UNIT:
		KDPrintM(DBG_LURN_INFO, ("RELEASE(6): %02x %02x %02x %02x %02x %02x\n",
												Ccb->Cdb[0], Ccb->Cdb[1],
												Ccb->Cdb[2], Ccb->Cdb[3],
												Ccb->Cdb[4], Ccb->Cdb[5]));
		status = LurnIdeDiskReserveRelease(Lurn,IdeDisk, Ccb);
		break;

	case 0x57:					// RELEASE_UNIT(10)
		KDPrintM(DBG_LURN_INFO, ("RELEASE(10): %02x %02x %02x %02x %02x %02x\n",
												Ccb->Cdb[0], Ccb->Cdb[1],
												Ccb->Cdb[2], Ccb->Cdb[3],
												Ccb->Cdb[4], Ccb->Cdb[5],
												Ccb->Cdb[6], Ccb->Cdb[7],
												Ccb->Cdb[8], Ccb->Cdb[9]));

		LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_ILLEGAL_COMMAND, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		status = STATUS_SUCCESS;
		return STATUS_SUCCESS;

	case SCSIOP_TEST_UNIT_READY:

		status = LurnIdeDiskTestUnitReady(Lurn, IdeDisk, Ccb);
		break;

	case SCSIOP_START_STOP_UNIT:
	{
		PCDB		cdb = (PCDB)(Ccb->Cdb);
		UCHAR		response;

		if(cdb->START_STOP.Start == START_UNIT_CODE) {

			//
			// Should not start with the disk dirty.
			//
			if(IdeDisk->LuHwData.HwVersion > LANSCSIIDE_VERSION_1_0) {
				// This can be happen if RAID recovery is in progress
				//ASSERT(IdeDisk->DiskWriteCacheDirty == FALSE);
			} else {
				// For 1.0 DiskWriteCacheDirty cannot be cleared.
			}
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
					("START_STOP_UNIT: GetDiskInfo() failed."
					" We succeeded to raise the disconnect event."
					" Response:%d.\n", response));
			}
#endif

			KDPrintM(DBG_LURN_ERROR,
				("START_STOP_UNIT: Start Unit %d %d.\n", Ccb->LurId[1], cdb->START_STOP.LogicalUnitNumber));
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;

		} else if(cdb->START_STOP.Start == STOP_UNIT_CODE) {

			//
			// Should not stop with the disk dirty.
			//

			if(IdeDisk->LuHwData.HwVersion > LANSCSIIDE_VERSION_1_0) {
				// This can be happen if RAID recovery is in progress				
				// ASSERT(IdeDisk->DiskWriteCacheDirty == FALSE);
			} else {
				// For 1.0 DiskWriteCacheDirty cannot be cleared.
			}

			//
			//	Set stop flag.
			//

			IdeDisk->LURNHostStop = TRUE;


			KDPrintM(DBG_LURN_ERROR,
				("START_STOP_UNIT: Stop Unit %d %d.\n", Ccb->LurId[1], cdb->START_STOP.LogicalUnitNumber));
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;
		} else {
			KDPrintM(DBG_LURN_ERROR,
				("START_STOP_UNIT: Invaild operation!!! %d %d.\n", Ccb->LurId[1], cdb->START_STOP.LogicalUnitNumber));
			Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;	//SRB_STATUS_INVALID_REQUEST;
		}

		//
		// CCB_STATUS_SUCCESS_TIMER will make
		// MiniportCompletionRoutine use timer for its completion.
		//
		LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		KDPrintM(DBG_LURN_ERROR, ("START_STOP_UNIT CcbStatus:%d!!\n", Ccb->CcbStatus));

		return STATUS_SUCCESS;
	}

	case SCSIOP_MODE_SENSE:
	{
		status = LurnIdeDiskModeSense(Lurn, IdeDisk, Ccb);
		break;
	}

	case SCSIOP_MODE_SELECT:
	{
		status = LurnIdeDiskModeSelect(Lurn, IdeDisk, Ccb);
		break;
	}

	case 0x3E:		// READ_LONG
	case SCSIOP_READ:
	case SCSIOP_READ16:
	{
		UINT64	logicalBlockAddress;
		ULONG	transferBlocks;
		UINT64	endSector;

		//
		// Extract block address, block length, and FUA option.
		//

		if( Ccb->Cdb[0] == SCSIOP_READ ||
			Ccb->Cdb[0] == 0x3E) {
#if DBG
			PCDB	cdb;

			cdb = (PCDB)Ccb->Cdb;
			if(cdb->CDB10.ForceUnitAccess) {

				//
				// Read with force unit access.
				//
				// NOTE: There is no ATA command for read FUA even in spec.
				//       Windows does not seem to send read FUA commands, though.
				//
				KDPrintM(DBG_LURN_INFO, ("READ:CDB10: ForceUnitAccess detected.\n"));
			}
#endif
			logicalBlockAddress = CDB10_LOGICAL_BLOCK_BYTE((PCDB)Ccb->Cdb);
			transferBlocks = CDB10_TRANSFER_BLOCKS((PCDB)Ccb->Cdb);
		} else {
#if DBG
			PCDBEXT cdb16;

			cdb16 = (PCDBEXT)Ccb->Cdb;
			if(cdb16->CDB16.ForceUnitAccess) {
				KDPrintM(DBG_LURN_INFO, ("READ:CDB16: ForceUnitAccess detected.\n"));
			}
#endif
			logicalBlockAddress = CDB16_LOGICAL_BLOCK_BYTE((PCDB)Ccb->Cdb);
			transferBlocks = CDB16_TRANSFER_BLOCKS((PCDB)Ccb->Cdb);
		}

		//
		// Verify data buffer size.
		//

		if(Ccb->DataBufferLength < (ULONG)(transferBlocks*IdeDisk->LuHwData.BlockBytes)) {
			KDPrintM(DBG_LURN_ERROR,
				("READ: CCB Ccb->DataBufferLength = %d, Trans*BlockSize = %d\n",
						Ccb->DataBufferLength, (ULONG)(transferBlocks*IdeDisk->LuHwData.BlockBytes)));

			Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
			return STATUS_SUCCESS;
		}
#if DBG
		if(LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE)) {
			KDPrintM(DBG_LURN_ERROR,
						("SCSIOP_READ[%d,%d,%d]  Address:%lu DataBufferLength = %lu, Sectors = %lu\n",
						(int)Ccb->LurId[0], (int)Ccb->LurId[1], (int)Ccb->LurId[2],
						logicalBlockAddress, Ccb->DataBufferLength, (ULONG)(transferBlocks)));
		}
#endif

		//
		// Succeed with no block transfer.
		//

		if(transferBlocks == 0) {
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;
			return STATUS_SUCCESS;
		}


		//
		//	If an IO is within barrier area,
		//	fail it.
		//

		endSector = logicalBlockAddress + transferBlocks - 1;
		if(endSector >=  Lurn->UnitBlocks) {
			if(endSector <  Lurn->UnitBlocks + 512) {
			KDPrintM(DBG_LURN_ERROR,
					("READ: Block Overflows 0x%x, luExtension->SectorCount = 0x%x\n",
					endSector,  Lurn->UnitBlocks));

			Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;	//SRB_STATUS_INVALID_REQUEST;
			return STATUS_SUCCESS;
			}
		}

		if (LsCcbIsFlagOn(Ccb, CCB_FLAG_BACKWARD_ADDRESS)) {

			// Caution !!! more sanity check is needed

			NDASSCSI_ASSERT( IdeDisk->LuHwData.SectorCount >= logicalBlockAddress );

			logicalBlockAddress = IdeDisk->LuHwData.SectorCount - logicalBlockAddress;
		}

		//////////////////////////////////////////////////
		//
		// Send Read Request.
		//

		status = LurnIdeDiskRead(
							Ccb,
							IdeDisk,
							Ccb->DataBuffer,
							logicalBlockAddress,
							transferBlocks,
							IdeDisk->ReadSplitSize);
		if(!NT_SUCCESS(status) || Ccb->CcbStatus != CCB_STATUS_SUCCESS)
		{
			return status;
		}
		else
		{

			Ccb->CcbStatus = CCB_STATUS_SUCCESS;

			return STATUS_SUCCESS;
		}

	}

	case SCSIOP_WRITE:
	case SCSIOP_WRITE16:
	{
		UINT64	logicalBlockAddress;
		ULONG	transferBlocks;
		UINT64	endSector;
		BOOLEAN	forceUnitAccess;


		//
		// Extract block address, block length, and FUA option.
		//
		forceUnitAccess = FALSE;
		if(Ccb->Cdb[0] == SCSIOP_WRITE) {
			PCDB	cdb;

			cdb = (PCDB)Ccb->Cdb;

			logicalBlockAddress = CDB10_LOGICAL_BLOCK_BYTE((PCDB)Ccb->Cdb);
			transferBlocks = CDB10_TRANSFER_BLOCKS((PCDB)Ccb->Cdb);

			//
			// Ignore force unit access for the performance.
			//

			if(cdb->CDB10.ForceUnitAccess) {
				//
				// Windows calls it Write-through.
				// This option usually is translated by disk class driver (xferpkt.c)
				// from SL_WRITE_THROUGH. File system drivers initiate the write
				// through IOs to be safer. Or, user program requests write-through.
				//
				KDPrintM(DBG_LURN_TRACE, ("WRITE:CDB10: ForceUnitAccess detected.\n"));
				forceUnitAccess = TRUE;
			}

		} else {
			PCDBEXT cdb16;

			cdb16 = (PCDBEXT)Ccb->Cdb;

			logicalBlockAddress = CDB16_LOGICAL_BLOCK_BYTE((PCDB)Ccb->Cdb);
			transferBlocks = CDB16_TRANSFER_BLOCKS((PCDB)Ccb->Cdb);

			if(cdb16->CDB16.ForceUnitAccess) {
				//
				// Windows calls it Write-through.
				//
				KDPrintM(DBG_LURN_TRACE, ("WRITE:CDB16: ForceUnitAccess detected.\n"));
				forceUnitAccess = TRUE;
			}
		}

#if DBG
		if(LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE)) {
			KDPrintM(DBG_LURN_ERROR,
						("SCSIOP_WRITE[%d,%d,%d]  Address:%lu DataBufferLength = %lu, Sectors = %lu\n",
						(int)Ccb->LurId[0], (int)Ccb->LurId[1], (int)Ccb->LurId[2],
						logicalBlockAddress, Ccb->DataBufferLength, (ULONG)(transferBlocks)));
		}
#endif

		//
		// Verify data buffer size.
		//

		if(Ccb->DataBufferLength < (ULONG)(transferBlocks * IdeDisk->LuHwData.BlockBytes)) {
			KDPrintM(DBG_LURN_ERROR,
				("WRITE: CCB Ccb->DataBufferLength = %d, Trans*BlockSize = %d\n",
						Ccb->DataBufferLength, (ULONG)(transferBlocks*IdeDisk->LuHwData.BlockBytes)));

			Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
			return STATUS_SUCCESS;
		}

		//
		// Succeed with no block transfer.
		//

		if(transferBlocks == 0) {
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;
			return STATUS_SUCCESS;
		}

		//
		//	If an IO is within barrier area,
		//	fail it.
		//

		endSector = logicalBlockAddress + transferBlocks - 1;
		if(endSector >=  Lurn->UnitBlocks) {
			if(endSector <  Lurn->UnitBlocks + 512) {
				KDPrintM(DBG_LURN_ERROR,
					("WRITE: Block Overflows 0x%x, luExtension->SectorCount = 0x%x\n",
					endSector,  Lurn->UnitBlocks));

				Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;	//SRB_STATUS_INVALID_REQUEST;
				return STATUS_SUCCESS;
			}
		}

		//
		//	Access check
		//

		if(!(Lurn->AccessRight & GENERIC_WRITE)){	// Read-only LURN
			if(!LsCcbIsFlagOn(Ccb, CCB_FLAG_ALLOW_WRITE_IN_RO_ACCESS)) {
				KDPrintM(DBG_LURN_ERROR, ("WRITE: WRITE command"
					" received without Write accessright.\n"));
				Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
				return STATUS_UNSUCCESSFUL;
			}
		}

		if (LsCcbIsFlagOn(Ccb, CCB_FLAG_BACKWARD_ADDRESS)) {

			// Caution !!! more sanity check is needed

			NDASSCSI_ASSERT( IdeDisk->LuHwData.SectorCount >= logicalBlockAddress );

			logicalBlockAddress = IdeDisk->LuHwData.SectorCount - logicalBlockAddress;
		}

#if __NDAS_SCSI_BOUNDARY_CHECK__

	if (logicalBlockAddress < IdeDisk->LuHwData.SectorCount && (logicalBlockAddress + transferBlocks) > Lurn->UnitBlocks ) {

		if (!LsCcbIsFlagOn(Ccb, CCB_FLAG_BACKWARD_ADDRESS)) {

			NDASSCSI_ASSERT( FALSE );

			Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;	//SRB_STATUS_INVALID_REQUEST;
			return STATUS_SUCCESS;
		}
	}

#endif


		//////////////////////////////////////////////////
		//
		// Send Write Request.
		//

		status = LurnIdeDiskWrite(
						Ccb,
						Lurn,
						IdeDisk,
						(PVOID)((PCHAR)Ccb->DataBuffer),
						logicalBlockAddress,
						transferBlocks,
						IdeDisk->WriteSplitSize,
						forceUnitAccess);

		if(!NT_SUCCESS(status) || Ccb->CcbStatus != CCB_STATUS_SUCCESS)
		{
			break;
		} else {
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		}
		break;
	}

	case SCSIOP_VERIFY:
	case SCSIOP_VERIFY16:
	{
		UINT64	logicalBlockAddress;
		ULONG	transferBlocks;

		if(Ccb->Cdb[0] == SCSIOP_VERIFY) {

			logicalBlockAddress = CDB10_LOGICAL_BLOCK_BYTE((PCDB)Ccb->Cdb);
			transferBlocks = CDB10_TRANSFER_BLOCKS((PCDB)Ccb->Cdb);

		} else {

			logicalBlockAddress = CDB16_LOGICAL_BLOCK_BYTE((PCDB)Ccb->Cdb);
			transferBlocks = CDB16_TRANSFER_BLOCKS((PCDB)Ccb->Cdb);

		}
		KDPrintM(DBG_LURN_INFO,
			("SCSIOP_VERIFY[%d,%d,%d]  Address:%I64u DataBufferLength = %lu, Sectors = %lu\n",
			(int)Ccb->LurId[0], (int)Ccb->LurId[1], (int)Ccb->LurId[2],
			logicalBlockAddress, Ccb->DataBufferLength, transferBlocks));

		status = LurnIdeDiskVerify(Lurn, IdeDisk, Ccb, logicalBlockAddress, transferBlocks);
		break;
	}

	case SCSIOP_SYNCHRONIZE_CACHE:
	case SCSIOP_SYNCHRONIZE_CACHE16:

		KDPrintM(DBG_LURN_TRACE, ("SYNCHRONIZE_CACHE\n"));

		{
			LANSCSI_PDUDESC		pduDescTemp;
			BYTE				response;

			if(IdeDisk->LuHwData.HwVersion <= LANSCSIIDE_VERSION_1_0) {
				KDPrintM(DBG_LURN_ERROR, ("SYNCHRONIZE_CACHE: Not supported. Return success.\n"));
				Ccb->CcbStatus = CCB_STATUS_SUCCESS;
				status = STATUS_SUCCESS;
				break;
			}

			LURNIDE_INITIALIZE_PDUDESC(
				IdeDisk, 
				&pduDescTemp, 
				IDE_COMMAND, 
				WIN_FLUSH_CACHE, 
				0,
				0, 
				0,
				NULL,
				NULL);
			status = LspRequest(
				IdeDisk->LanScsiSession,
				&pduDescTemp,
				&response
				);

			if(!NT_SUCCESS(status) || response != LANSCSI_RESPONSE_SUCCESS) {
				//
				// Sync failed.
				// Some data may not be written. In RAID1~5, this requires resyncing process. 
				// Just record error and let RAID code check this.
				// to do: pass additional fault info to flush
				LurnRecordFault(IdeDisk->Lurn, LURN_ERR_FLUSH, 0, 0);
			}
			
			//
			// Reset disk dirty
			//

			if(NT_SUCCESS(status) && (response == LANSCSI_RESPONSE_SUCCESS)) {
				IdeDisk->DiskWriteCacheDirty = FALSE;

			}

			//
			// Upper devices send SCSIOP_SYNCHRONIZE_CACHE before hibernation.
			// But, flush operation may not succeed because
			// NICs always power down just before SCSIOP_SYNCHRONIZE_CACHE.
			// If that synchronize cache command is detected,
			// Just complete it with success.
			//

			if(Ccb->Flags & CCB_FLAG_MUST_SUCCEED) {
				KDPrintM(DBG_LURN_ERROR, ("reutun status overrided.\n"));
				KDPrintM(DBG_LURN_ERROR,
					("SYNCHRONIZE_CACHE : LspRequest(): status - %x, response - %x\n", status, response));

				Ccb->CcbStatus = CCB_STATUS_SUCCESS;
				status = STATUS_SUCCESS;
			} else {
				if(!NT_SUCCESS(status)) {
					KDPrintM(DBG_LURN_ERROR,
						("SYNCHRONIZE_CACHE : LspRequest failed : status - %x, response - %x\n", status, response));
					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
					break;
				}
				if(response != LANSCSI_RESPONSE_SUCCESS) {
					KDPrintM(DBG_LURN_ERROR,
						("SYNCHRONIZE_CACHE : LspRequest failed : status - %x, response - %x\n", status, response));
					Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
					status = STATUS_SUCCESS;
					break;
				}

				Ccb->CcbStatus = CCB_STATUS_SUCCESS;
			}
			KDPrintM(DBG_LURN_TRACE, ("SYNCHRONIZE_CACHE OUT\n"));
		}
		break;

	case SCSIOP_SEEK:
		LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		status = STATUS_SUCCESS;
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
		LsCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_ILLEGAL_COMMAND, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		status = STATUS_SUCCESS;

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
		status = STATUS_SUCCESS;

		KDPrintM(DBG_LURN_ERROR, ("%s: CcbStatus:%d!!\n", CdbOperationString(Ccb->Cdb[0]), Ccb->CcbStatus));

		break;
	}

	return status;
}


#endif

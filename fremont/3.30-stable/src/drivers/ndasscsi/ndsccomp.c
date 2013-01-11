#include "port.h"


#if !__SCSIPORT__

#ifdef __MODULE__
#undef __MODULE__
#endif // __MODULE__

#define __MODULE__ "NDASSCSI"

#endif

#if !__SCSIPORT__

//////////////////////////////////////////////////////////////////////////
//
//	search scsiport FDO
//

typedef struct _ADAPTER_EXTENSION {

	PDEVICE_OBJECT DeviceObject;

} ADAPTER_EXTENSION, *PADAPTER_EXTENSION;

typedef struct _HW_DEVICE_EXTENSION2 {

    PADAPTER_EXTENSION FdoExtension;
    UCHAR HwDeviceExtension[0];

} HW_DEVICE_EXTENSION2, *PHW_DEVICE_EXTENSION2;

#define GET_FDO_EXTENSION(HwExt) ((CONTAINING_RECORD(HwExt, HW_DEVICE_EXTENSION2, HwDeviceExtension))->FdoExtension)

#endif

PDEVICE_OBJECT
FindScsiportFdo(
		IN OUT	PMINIPORT_DEVICE_EXTENSION	HwDeviceExtension
){
	// DeviceObject which is one of argument is always NULL.
	PADAPTER_EXTENSION		deviceExtension = GET_FDO_EXTENSION(HwDeviceExtension);

	ASSERT(deviceExtension->DeviceObject);

	return deviceExtension->DeviceObject;
}


//////////////////////////////////////////////////////////////////////////
//
//	Event log
//

VOID
NdasMiniLogError(
	IN PMINIPORT_DEVICE_EXTENSION HwDeviceExtension,
	IN PSCSI_REQUEST_BLOCK Srb OPTIONAL,
	IN UCHAR PathId,
	IN UCHAR TargetId,
	IN UCHAR Lun,
	IN ULONG ErrorCode,
	IN ULONG UniqueId
){
	PDEVICE_OBJECT DeviceObject = HwDeviceExtension->ScsiportFdoObject;
	LSU_ERROR_LOG_ENTRY errorLogEntry;

	UNREFERENCED_PARAMETER(Srb);

	if(HwDeviceExtension == NULL) {
		KDPrint(2, ("HwDeviceExtension NULL!! \n"));
		return;
	}
	if(DeviceObject == NULL) {
		KDPrint(2, ("DeviceObject NULL!! \n"));
		return;
	}

	//
    // Save the error log data in the log entry.
    //

	errorLogEntry.ErrorCode = ErrorCode;
	errorLogEntry.MajorFunctionCode = IRP_MJ_SCSI;
	errorLogEntry.IoctlCode = 0;
    errorLogEntry.UniqueId = UniqueId;
	errorLogEntry.SequenceNumber = 0;
	errorLogEntry.ErrorLogRetryCount = 0;
	errorLogEntry.Parameter2 = HwDeviceExtension->SlotNumber;
	errorLogEntry.DumpDataEntry = 4;
	errorLogEntry.DumpData[0] = TargetId;
	errorLogEntry.DumpData[1] = Lun;
	errorLogEntry.DumpData[2] = PathId;
	errorLogEntry.DumpData[3] = ErrorCode;

	LsuWriteLogErrorEntry(DeviceObject, &errorLogEntry);

	return;
}


//////////////////////////////////////////////////////////////////////////
//
//	I/o control to NDASBUS
//

//
//	General ioctl to NDASBUS
//
NTSTATUS
IoctlToLanscsiBus(
	IN ULONG		IoControlCode,
    IN PVOID		InputBuffer  OPTIONAL,
    IN ULONG		InputBufferLength,
    OUT PVOID		OutputBuffer  OPTIONAL,
    IN ULONG		OutputBufferLength,
	OUT PULONG		BufferNeeded
)
{
	NTSTATUS			ntStatus;
	PWSTR				symbolicLinkList;
    UNICODE_STRING		objectName;
	PFILE_OBJECT		fileObject;
	PDEVICE_OBJECT		deviceObject;
	PIRP				irp;
    KEVENT				event;
    IO_STATUS_BLOCK		ioStatus;


	KDPrint(3,("IoControlCode = %x\n", IoControlCode));

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	ntStatus = IoGetDeviceInterfaces(
		&GUID_NDAS_BUS_ENUMERATOR_INTERFACE_CLASS,
		NULL,
		0,
		&symbolicLinkList
		);
	
	if(!NT_SUCCESS(ntStatus)) {
		KDPrint(2,("IoGetDeviceInterfaces ntStatus = 0x%x\n", ntStatus));
		return ntStatus;
	}
	
	ASSERT(symbolicLinkList != NULL);

	KDPrint(2,("symbolicLinkList = %ws\n", symbolicLinkList));

    RtlInitUnicodeString(&objectName, symbolicLinkList);
	ntStatus = IoGetDeviceObjectPointer(
					&objectName,
					FILE_ALL_ACCESS,
					&fileObject,
					&deviceObject
					);

	if(!NT_SUCCESS(ntStatus)) {
		KDPrint(2,("ntStatus = 0x%x\n", ntStatus));
		ExFreePool(symbolicLinkList);
		return ntStatus;
	}
	
    KeInitializeEvent(&event, NotificationEvent, FALSE);

	irp = IoBuildDeviceIoControlRequest(
			IoControlCode,
			deviceObject,
			InputBuffer,
			InputBufferLength,
			OutputBuffer,
			OutputBufferLength,
			FALSE,
			&event,
			&ioStatus
			);
	
    if (irp == NULL) {
		KDPrint(2,("irp NULL\n"));
		ExFreePool(symbolicLinkList);
		ObDereferenceObject(fileObject); 
		return ntStatus;
	}
	KDPrint(3,("Before Done...ioStatus.Status = 0x%08x Information = %d\n", ioStatus.Status, ioStatus.Information));

	ntStatus = IoCallDriver(deviceObject, irp);
    if (ntStatus == STATUS_PENDING)
    {
		KDPrint(2,("IoCallDriver STATUS_PENDING\n"));
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
        ntStatus = ioStatus.Status;
    } else if(NT_SUCCESS(ntStatus)) {
		ntStatus = ioStatus.Status;
    }

	if(BufferNeeded)
		*BufferNeeded = (ULONG)ioStatus.Information;

	ExFreePool(symbolicLinkList);
	ObDereferenceObject(fileObject);
	
	KDPrint(2,("Done...ioStatus.Status = 0x%08x Information = %d\n", ioStatus.Status, ioStatus.Information));
	
	return ntStatus;
}

VOID
IoctlToLanscsiBus_Worker(
		IN PDEVICE_OBJECT		DeviceObject,
		IN PNDSC_WORKITEM	WorkitemCtx
	) {

	UNREFERENCED_PARAMETER(DeviceObject);

	IoctlToLanscsiBus(
				PtrToUlong(WorkitemCtx->Ccb),
				WorkitemCtx->Arg1,
				PtrToUlong(WorkitemCtx->Arg2),
				NULL,
				0,
				NULL
			);


	if(WorkitemCtx->Arg2 && WorkitemCtx->Arg1)
		ExFreePoolWithTag(WorkitemCtx->Arg1, NDSC_PTAG_IOCTL);
}

NTSTATUS
IoctlToLanscsiBusByWorker(
	IN PDEVICE_OBJECT	WorkerDeviceObject,
	IN ULONG			IoControlCode,
    IN PVOID			InputBuffer  OPTIONAL,
    IN ULONG			InputBufferLength
) {
	NDSC_WORKITEM_INIT	WorkitemCtx;
	NTSTATUS			status;

	NDSC_INIT_WORKITEM(	&WorkitemCtx,
							IoctlToLanscsiBus_Worker,
							(PCCB)UlongToPtr(IoControlCode),
							InputBuffer,
							UlongToPtr(InputBufferLength),
							NULL);
	status = MiniQueueWorkItem(&NdasMiniGlobalData, WorkerDeviceObject, &WorkitemCtx);
	if(!NT_SUCCESS(status)) {
		if(InputBufferLength && InputBuffer)
			ExFreePoolWithTag(InputBuffer, NDSC_PTAG_IOCTL);
	}

	return status;
}

//
//	Update Adapter status without Lur.
//
NTSTATUS
UpdateStatusInLSBus(
		ULONG	SlotNo,
		ULONG	AdapterStatus
) {
	NDASBUS_SETPDOINFO	BusSet;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	//
	//	Update Status in LanscsiBus
	//
	BusSet.Size = sizeof(BusSet);
	BusSet.SlotNo = SlotNo;
	BusSet.AdapterStatus = AdapterStatus;
	BusSet.SupportedFeatures = BusSet.EnabledFeatures = 0;
	return IoctlToLanscsiBus(
			IOCTL_NDASBUS_SETPDOINFO,
			&BusSet,
			sizeof(NDASBUS_SETPDOINFO),
			NULL,
			0,
			NULL);
}

//
// query scsiport PDO
//
ULONG
GetScsiAdapterPdoEnumInfo(
	IN PMINIPORT_DEVICE_EXTENSION	HwDeviceExtension,
	IN ULONG						SystemIoBusNumber,
	OUT PLONG						AddDevInfoLength,
	OUT PVOID						*AddDevInfo,
	OUT PULONG						AddDevInfoFlags
) {
	NTSTATUS						status;
	NDASBUS_QUERY_INFORMATION		BusQuery;
	PNDASBUS_INFORMATION			BusInfo;
	LONG							BufferNeeded;

	KDPrint(2,("SystemIoBusNumber:%d\n", SystemIoBusNumber));
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	BusQuery.Size = sizeof(NDASBUS_QUERY_INFORMATION);
	BusQuery.InfoClass = INFORMATION_PDOENUM;
	BusQuery.SlotNo = SystemIoBusNumber;

	//
	//	Get a buffer length needed.
	//
	status = IoctlToLanscsiBus(
						IOCTL_NDASBUS_QUERY_INFORMATION,
						&BusQuery,
						sizeof(NDASBUS_QUERY_INFORMATION),
						NULL,
						0,
						&BufferNeeded
					);
	if(status != STATUS_BUFFER_TOO_SMALL || BufferNeeded <= 0) {
		KDPrint(2,("IoctlToLanscsiBus() Failed.\n"));
		return SP_RETURN_NOT_FOUND;
	}

	//
	//
	//
	BusInfo= (PNDASBUS_INFORMATION)ExAllocatePoolWithTag(NonPagedPool, BufferNeeded, NDSC_PTAG_IOCTL);
	if(BusInfo == NULL)
		return SP_RETURN_ERROR;
	status = IoctlToLanscsiBus(
						IOCTL_NDASBUS_QUERY_INFORMATION,
						&BusQuery,
						sizeof(NDASBUS_QUERY_INFORMATION),
						BusInfo,
						BufferNeeded,
						&BufferNeeded
					);
	if(!NT_SUCCESS(status)) {
		ExFreePoolWithTag(BusInfo, NDSC_PTAG_IOCTL);
		return SP_RETURN_NOT_FOUND;
	}

	HwDeviceExtension->AdapterMaxDataTransferLength = BusInfo->PdoEnumInfo.MaxRequestLength;
	if(HwDeviceExtension->AdapterMaxDataTransferLength == 0) {
		HwDeviceExtension->AdapterMaxDataTransferLength = NDAS_MAX_TRANSFER_LENGTH;
	}

	HwDeviceExtension->EnumFlags			= BusInfo->PdoEnumInfo.Flags;
	*AddDevInfoFlags						= BusInfo->PdoEnumInfo.Flags;

	KDPrint(2,("MaxRequestLength:%d\n",
						BusInfo->PdoEnumInfo.MaxRequestLength));

	if(BusInfo->PdoEnumInfo.Flags & PDOENUM_FLAG_LURDESC) {
		ULONG				LurDescLen;
		PLURELATION_DESC	lurDesc;
		PLURELATION_DESC	lurDescOrig;

		lurDescOrig = (PLURELATION_DESC)BusInfo->PdoEnumInfo.AddDevInfo;
		LurDescLen = lurDescOrig->Length;
		//
		//	Verify sanity
		//
		if(lurDescOrig->Type != LUR_DESC_STRUCT_TYPE) {
			ExFreePoolWithTag(BusInfo,NDSC_PTAG_IOCTL);
			KDPrint(2,("LurDescOrig has invalid type: %04x\n", lurDescOrig->Type));
			return SP_RETURN_NOT_FOUND;
		}
		if(lurDescOrig->CntEcrKeyLength > NDAS_CONTENTENCRYPT_KEY_LENGTH) {
			ExFreePoolWithTag(BusInfo,NDSC_PTAG_IOCTL);
			KDPrint(2,("LurDescOrig has invalid key length: %d\n", lurDescOrig->CntEcrKeyLength));
			return SP_RETURN_NOT_FOUND;
		}

		//
		//	Allocate pool for the LUREALTION descriptor
		//	copy LUREALTION descriptor
		//
		lurDesc = (PLURELATION_DESC)ExAllocatePoolWithTag(NonPagedPool, LurDescLen, NDSC_PTAG_ENUMINFO);
		if(lurDesc == NULL) {
			ExFreePoolWithTag(BusInfo,NDSC_PTAG_IOCTL);
			return SP_RETURN_NOT_FOUND;
		}

		RtlCopyMemory(lurDesc, &BusInfo->PdoEnumInfo.AddDevInfo, LurDescLen);
		*AddDevInfo = lurDesc;
		*AddDevInfoLength = LurDescLen;
	}
	else {
		ASSERT(FALSE);
		return SP_RETURN_NOT_FOUND;
	}

	ExFreePoolWithTag(BusInfo,NDSC_PTAG_IOCTL);
	return SP_RETURN_FOUND;
}


VOID
UpdatePdoInfoInLSBus(
		PMINIPORT_DEVICE_EXTENSION	HwDeviceExtension,
		UINT32						AdapterStatus
	) {
	PNDASBUS_SETPDOINFO		busSet;
	NDAS_FEATURES			supportedFeatures, enabledFeatures;

	ASSERT(HwDeviceExtension);

	//
	//	Query to the LUR
	//
	if(HwDeviceExtension->LURs[0]) {
		KDPrint(4,("going to default LuExtention 0.\n"));
		supportedFeatures = HwDeviceExtension->LURs[0]->SupportedNdasFeatures;
		enabledFeatures = HwDeviceExtension->LURs[0]->EnabledNdasFeatures;
	} else {
		KDPrint(2,("No LUR available..\n"));
		supportedFeatures = enabledFeatures = 0;
		return;
	}

	KDPrint(2,("Set AdapterStatus=%x Supp=%x Enab=%x\n",
					AdapterStatus,
					supportedFeatures,
					enabledFeatures));

	//
	//	Send to LSBus
	//
	busSet = (PNDASBUS_SETPDOINFO)ExAllocatePoolWithTag(NonPagedPool, sizeof(NDASBUS_SETPDOINFO), NDSC_PTAG_IOCTL);
	if(busSet == NULL) {
		return;
	}

	busSet->Size			= sizeof(NDASBUS_SETPDOINFO);
	busSet->SlotNo			= HwDeviceExtension->SlotNumber;
	busSet->AdapterStatus	= AdapterStatus;
	busSet->SupportedFeatures	= supportedFeatures;
	busSet->EnabledFeatures	= enabledFeatures;

	if(KeGetCurrentIrql() == PASSIVE_LEVEL) {
		IoctlToLanscsiBus(
						IOCTL_NDASBUS_SETPDOINFO,
						busSet,
						sizeof(NDASBUS_SETPDOINFO),
						NULL,
						0,
						NULL);
		ExFreePoolWithTag(busSet, NDSC_PTAG_IOCTL);

	} else {

		//
		//	IoctlToLanscsiBus_Worker() will free memory of BusSet.
		//
		IoctlToLanscsiBusByWorker(
						HwDeviceExtension->ScsiportFdoObject,
						IOCTL_NDASBUS_SETPDOINFO,
						busSet,
						sizeof(NDASBUS_SETPDOINFO)
					);
	}
}




//////////////////////////////////////////////////////////////////////////
//
//	NDASSCSI adapter IO completion
//


//
//	Completion routine for IRPs carrying SRB.
//
// NOTE: This routine executes in the context of arbitrary system thread.
//

NTSTATUS
CompletionIrpCompletionRoutine (
	IN PDEVICE_OBJECT		DeviceObject,
	IN PIRP					Irp,
	PSCSI_REQUEST_BLOCK		CompletionSrb
	)
{
	DebugTrace( NDASSCSI_DBG_MINIPORT_TRACE, ("Irp = %p\n", Irp) );

	UNREFERENCED_PARAMETER( DeviceObject );

	IoFreeIrp( Irp );
	ExFreePoolWithTag( CompletionSrb, NDSC_PTAG_SRB );

	return STATUS_MORE_PROCESSING_REQUIRED;
}

//
// Translate CCB status to SRB status
//

VOID
CcbStatusToSrbStatus (
	PCCB Ccb, 
	PSCSI_REQUEST_BLOCK Srb
	) 
{
	Srb->DataTransferLength -= Ccb->ResidualDataLength;

	switch (Ccb->CcbStatus) {

		case CCB_STATUS_SUCCESS:

			Srb->SrbStatus = SRB_STATUS_SUCCESS;
			Srb->ScsiStatus = SCSISTAT_GOOD;

			break;

		case CCB_STATUS_NOT_EXIST:

			Srb->SrbStatus = SRB_STATUS_NO_DEVICE;
			Srb->ScsiStatus = SCSISTAT_GOOD;

			break;

		case CCB_STATUS_INVALID_COMMAND:

			Srb->SrbStatus = SRB_STATUS_INVALID_REQUEST;
			Srb->ScsiStatus = SCSISTAT_GOOD;

			break;

		case CCB_STATUS_LOST_LOCK:
		case CCB_STATUS_COMMAND_FAILED:

			Srb->SrbStatus = SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
			Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;

			break;

		case CCB_STATUS_BAD_SECTOR:

			Srb->SrbStatus = SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
			Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;

			break;

		case CCB_STATUS_COMMMAND_DONE_SENSE2:
		
			Srb->SrbStatus =  SRB_STATUS_BUSY  | SRB_STATUS_AUTOSENSE_VALID;
			Srb->DataTransferLength = 0;
			Srb->ScsiStatus = SCSISTAT_GOOD;

			break;

		case CCB_STATUS_COMMMAND_DONE_SENSE:

			Srb->SrbStatus =  SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
			Srb->DataTransferLength = 0;
			Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;

			break;

		case CCB_STATUS_RESET:

			Srb->SrbStatus = SRB_STATUS_BUS_RESET;
			Srb->ScsiStatus = SCSISTAT_GOOD;

			break;

		case CCB_STATUS_DATA_OVERRUN:

			Srb->SrbStatus = SRB_STATUS_DATA_OVERRUN;
			Srb->ScsiStatus = SCSISTAT_GOOD;

			break;

		case CCB_STATUS_COMMUNICATION_ERROR: {

			PSENSE_DATA	senseData;

			Srb->SrbStatus = SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
			Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
			Srb->DataTransferLength = 0;

			senseData = Srb->SenseInfoBuffer;

			senseData->ErrorCode = 0x70;
			senseData->Valid = 1;
			//senseData->SegmentNumber = 0;
			senseData->SenseKey = SCSI_SENSE_HARDWARE_ERROR;	//SCSI_SENSE_MISCOMPARE;
			//senseData->IncorrectLength = 0;
			//senseData->EndOfMedia = 0;
			//senseData->FileMark = 0;

			senseData->AdditionalSenseLength = 0xb;
			senseData->AdditionalSenseCode = SCSI_ADSENSE_NO_SENSE;
			senseData->AdditionalSenseCodeQualifier = 0;

			break;
		}

		case CCB_STATUS_BUSY:

			Srb->SrbStatus = SRB_STATUS_BUSY;
			Srb->ScsiStatus = SCSISTAT_GOOD;
			KDPrint(2,("CCB_STATUS_BUSY\n"));

			break;

			//
			//	Stop one LUR
			//
		case CCB_STATUS_STOP: {

			Srb->SrbStatus = SRB_STATUS_BUS_RESET;
			Srb->ScsiStatus = SCSISTAT_GOOD;
			Srb->DataTransferLength = 0;
			KDPrint(2,("CCB_STATUS_STOP. Stopping!\n"));

			break;
		}

		case CCB_STATUS_NO_ACCESS: {

			// Error in log-in...
			Srb->SrbStatus = SRB_STATUS_ERROR;
			Srb->ScsiStatus = SCSISTAT_GOOD;
			Srb->DataTransferLength = 0;
			KDPrint(1,("CCB_STATUS_NO_ACCESS.\n"));

			break;
		}

		default: {

			NDAS_ASSERT( FALSE );

			// Error in Connection...
			// CCB_STATUS_UNKNOWN_STATUS, CCB_STATUS_RESET, and so on.
			
			Srb->SrbStatus = SRB_STATUS_ERROR;
			Srb->ScsiStatus = SCSISTAT_GOOD;
			Srb->DataTransferLength = 0;
		}
	}
}

//
// Copy query output to SRB
//

static
NTSTATUS
NdscCopyQueryOutputToSrb(
	PMINIPORT_DEVICE_EXTENSION	HwDeviceExtension,
	ULONG						CcbBufferLength,
	PUCHAR						CcbBuffer,
	ULONG						SrbIoctlBufferLength,
	PUCHAR						SrbIoctlBuffer
){
	NTSTATUS		status;
	PLUR_QUERY		lurQuery;

	lurQuery = (PLUR_QUERY)CcbBuffer;
	if(CcbBufferLength < FIELD_OFFSET(LUR_QUERY, QueryDataLength)) {
		return STATUS_INVALID_PARAMETER;
	}

	status = STATUS_SUCCESS;
	switch(lurQuery->InfoClass) {
		case LurPrimaryLurnInformation: { // NdscPrimaryUnitDiskInformation
			PLURN_PRIMARYINFORMATION	lurPrimaryInfo = (PLURN_PRIMARYINFORMATION)LUR_QUERY_INFORMATION(lurQuery);
			PNDSCIOCTL_PRIMUNITDISKINFO	primUnitDisk = (PNDSCIOCTL_PRIMUNITDISKINFO)SrbIoctlBuffer;

			if(CcbBufferLength < sizeof(LURN_PRIMARYINFORMATION)) {
				return STATUS_INVALID_PARAMETER;
			}
			if(SrbIoctlBufferLength < sizeof(NDSCIOCTL_PRIMUNITDISKINFO)) {
				return STATUS_INVALID_PARAMETER;
			}

			//
			//	Set length.
			//
			primUnitDisk->Length					= sizeof(NDSCIOCTL_PRIMUNITDISKINFO);
			primUnitDisk->UnitDisk.Length			= sizeof(NDSC_UNITDISK);
			//
			//	Adapter information
			//
			primUnitDisk->EnabledTime.QuadPart		=	HwDeviceExtension->EnabledTime.QuadPart;

			//
			// LUR information ( Scsi LU information )
			//
			primUnitDisk->Lur.Length		= sizeof(NDSC_LUR);
			primUnitDisk->Lur.TargetId		= HwDeviceExtension->LURs[0]->LurId[1];
			primUnitDisk->Lur.Lun			= HwDeviceExtension->LURs[0]->LurId[2];
			primUnitDisk->Lur.LurnCnt		= HwDeviceExtension->LURs[0]->NodeCount;
			primUnitDisk->Lur.DeviceMode	= HwDeviceExtension->LURs[0]->DeviceMode;
			primUnitDisk->Lur.SupportedFeatures	= HwDeviceExtension->LURs[0]->SupportedNdasFeatures;
			primUnitDisk->Lur.EnabledFeatures	= HwDeviceExtension->LURs[0]->EnabledNdasFeatures;

			//
			//	Unit device
			//

			primUnitDisk->UnitDisk.UnitDiskId		= lurPrimaryInfo->PrimaryLurn.UnitDiskId;
			primUnitDisk->UnitDisk.Connections		= lurPrimaryInfo->PrimaryLurn.Connections;
			primUnitDisk->UnitDisk.GrantedAccess	= lurPrimaryInfo->PrimaryLurn.AccessRight;
			RtlCopyMemory(
				primUnitDisk->UnitDisk.UserID,
				&lurPrimaryInfo->PrimaryLurn.UserID,
				sizeof(primUnitDisk->UnitDisk.UserID)
				);
			RtlCopyMemory(
				primUnitDisk->UnitDisk.Password,
				&lurPrimaryInfo->PrimaryLurn.Password,
				sizeof(primUnitDisk->UnitDisk.Password)
				);

#if 0
			RtlCopyMemory(	&primUnitDisk->UnitDisk.NetDiskAddress,
				&lurPrimaryInfo->PrimaryLurn.NetDiskAddress,
				sizeof(TA_LSTRANS_ADDRESS)
				);

			RtlCopyMemory(	&primUnitDisk->UnitDisk.BindingAddress,
				&lurPrimaryInfo->PrimaryLurn.BindingAddress,
				sizeof(TA_LSTRANS_ADDRESS)
				);

#else

			primUnitDisk->UnitDisk.NetDiskAddress = lurPrimaryInfo->PrimaryLurn.NdasNetDiskAddress;
			primUnitDisk->UnitDisk.BindingAddress = lurPrimaryInfo->PrimaryLurn.NdasBindingAddress;

#endif

			primUnitDisk->UnitDisk.UnitBlocks		= (UINT32)lurPrimaryInfo->PrimaryLurn.UnitBlocks;
			primUnitDisk->UnitDisk.SlotNo			= HwDeviceExtension->SlotNumber;
			RtlCopyMemory(primUnitDisk->NDSC_ID, lurPrimaryInfo->PrimaryLurn.PrimaryId, LURN_PRIMARY_ID_LENGTH);

			KDPrint(2,("NDSC_ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
				primUnitDisk->NDSC_ID[0], primUnitDisk->NDSC_ID[1], primUnitDisk->NDSC_ID[2], primUnitDisk->NDSC_ID[3], 
				primUnitDisk->NDSC_ID[4], primUnitDisk->NDSC_ID[5], primUnitDisk->NDSC_ID[6], primUnitDisk->NDSC_ID[7], 
				primUnitDisk->NDSC_ID[8], primUnitDisk->NDSC_ID[9], primUnitDisk->NDSC_ID[10], primUnitDisk->NDSC_ID[11], 
				primUnitDisk->NDSC_ID[12], primUnitDisk->NDSC_ID[13], primUnitDisk->NDSC_ID[14], primUnitDisk->NDSC_ID[15])
				);
		break;
		}
		case LurEnumerateLurn: { // NdscLurInformation
			PLURN_ENUM_INFORMATION	lurnEnumInfo = (PLURN_ENUM_INFORMATION)LUR_QUERY_INFORMATION(lurQuery);
			PNDSCIOCTL_LURINFO		info = (PNDSCIOCTL_LURINFO)SrbIoctlBuffer;
			NDAS_DEV_ACCESSMODE		deviceMode;
			NDAS_FEATURES			supportedNdasFeatures, enabledNdasFeatures;
			PNDSC_LURN_FULL			unitDisk;
			ULONG					idx_lurn;
			PLURN_INFORMATION		lurnInformation;
			ULONG					returnLength;
			UINT32				nodeCount;

			if(CcbBufferLength < sizeof(LURN_PRIMARYINFORMATION)) {
				return STATUS_INVALID_PARAMETER;
			}
			if(SrbIoctlBufferLength < FIELD_OFFSET(NDSCIOCTL_LURINFO, Reserved1)) {
				return STATUS_INVALID_PARAMETER;
			}

			if (HwDeviceExtension->LURs[0]) {
				ASSERT(HwDeviceExtension->LURs[0]->NodeCount >= 1);
				deviceMode = HwDeviceExtension->LURs[0]->DeviceMode;
				supportedNdasFeatures = HwDeviceExtension->LURs[0]->SupportedNdasFeatures;
				enabledNdasFeatures = HwDeviceExtension->LURs[0]->EnabledNdasFeatures;
				nodeCount = HwDeviceExtension->LURs[0]->NodeCount;
			} else {
				deviceMode = 0;
				supportedNdasFeatures = 0;
				enabledNdasFeatures = 0;
				nodeCount = 0;
			}

			//
			//	Adapter information
			//

			info->Length			=	FIELD_OFFSET(NDSCIOCTL_LURINFO, Lurns) +
										sizeof(NDSC_LURN_FULL) *
										nodeCount;

			// return length check
			returnLength = FIELD_OFFSET(NDSCIOCTL_LURINFO, Lurns);
			if(SrbIoctlBufferLength < returnLength) {
				return STATUS_BUFFER_OVERFLOW;
			}

			info->EnabledTime.QuadPart					=	HwDeviceExtension->EnabledTime.QuadPart;
			info->Lur.Length		= sizeof(NDSC_LUR);
			info->Lur.TargetId		= HwDeviceExtension->LURs[0]->LurId[1];
			info->Lur.Lun			= HwDeviceExtension->LURs[0]->LurId[2];
			info->Lur.LurnCnt		= HwDeviceExtension->LURs[0]->NodeCount;
			info->LurnCnt		= info->Lur.LurnCnt;
			info->Lur.DeviceMode = deviceMode;
			info->Lur.SupportedFeatures = supportedNdasFeatures;
			info->Lur.EnabledFeatures = enabledNdasFeatures;

			//
			//	Set return values for each LURN.
			//
			for(idx_lurn = 0; idx_lurn < info->LurnCnt; idx_lurn++) {

				//
				//	Add one LURN to return bytes and check the user buffer size.
				//
				returnLength += sizeof(NDSC_LURN_FULL);
				if(SrbIoctlBufferLength < returnLength) {
					status = STATUS_BUFFER_OVERFLOW;
					// Do not exit. Must return full length.
					continue;
				}

				unitDisk = info->Lurns + idx_lurn;
				lurnInformation = lurnEnumInfo->Lurns + idx_lurn;

				unitDisk->Length = sizeof(NDSC_LURN_FULL);
				unitDisk->UnitDiskId = lurnInformation->UnitDiskId;
				unitDisk->Connections = lurnInformation->Connections;
				unitDisk->AccessRight = lurnInformation->AccessRight;
				unitDisk->UnitBlocks = lurnInformation->UnitBlocks;
				unitDisk->StatusFlags = lurnInformation->StatusFlags;
				unitDisk->LurnId = lurnInformation->LurnId;
				unitDisk->LurnType = lurnInformation->LurnType;
				unitDisk->StatusFlags = lurnInformation->StatusFlags;

				RtlCopyMemory(
					unitDisk->UserID,
					&lurnInformation->UserID,
					sizeof(unitDisk->UserID)
					);

				RtlCopyMemory(
					unitDisk->Password,
					&lurnInformation->Password,
					sizeof(unitDisk->Password)
					);

#if 0
				RtlCopyMemory(	&unitDisk->NetDiskAddress,
					&lurnInformation->NetDiskAddress,
					sizeof(TA_LSTRANS_ADDRESS)
					);
				RtlCopyMemory(	&unitDisk->BindingAddress,
					&lurnInformation->BindingAddress,
					sizeof(TA_LSTRANS_ADDRESS)
					);
#else

			unitDisk->NetDiskAddress = lurnInformation->NdasNetDiskAddress;
			unitDisk->BindingAddress = lurnInformation->NdasBindingAddress;

#endif

			}
			break;
		 }

		default:
			status = STATUS_INVALID_PARAMETER;
		break;
	}

	return status;
}
//
//	MACRO for the convenience
//

#define SCSI_PORT_LOG_ERROR_MODULE_COMPLETION(ERRORCODE, EVT_ID)	\
	NdasMiniLogError(												\
							HwDeviceExtension,						\
							srb,									\
							srb->PathId,							\
							srb->TargetId,							\
							srb->Lun,								\
				(ERRORCODE),										\
				EVTLOG_UNIQUEID(EVTLOG_MODULE_COMPLETION,			\
								EVT_ID,								\
								0))

//
//	NDASSCSI adapter device object's completion routine
//

ULONG MiniportIoCount = 0;

NTSTATUS
NdscAdapterCompletion (
	IN PCCB							Ccb,
	IN PMINIPORT_DEVICE_EXTENSION	HwDeviceExtension
	)
{
	KIRQL					oldIrql;
	PSCSI_REQUEST_BLOCK		srb;
	UINT32					AdapterStatus, AdapterStatusBefore;
	UINT32					NeedToUpdatePdoInfoInLSBus;
	BOOLEAN					postCompleteIrp;
	BOOLEAN					criticalSrb;


	srb = Ccb->Srb;

	NDAS_ASSERT(srb);
	NDAS_ASSERT( srb->SrbFlags & SRB_FLAGS_IS_ACTIVE );
 
	// Update Adapter status flag

	NeedToUpdatePdoInfoInLSBus = FALSE;

	KeAcquireSpinLock( &HwDeviceExtension->LanscsiAdapterSpinLock, &oldIrql );

	// Save the current flag

	AdapterStatusBefore = HwDeviceExtension->AdapterStatus;

	//	Check reconnecting process.
	
	if (LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_RECONNECTING)) {

		ADAPTER_SETSTATUSFLAG(HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_RECONNECT_PENDING);

	} else {
		
		ADAPTER_RESETSTATUSFLAG(HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_RECONNECT_PENDING);
	}

	if (!LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_RAID_FLAG_VALID)) {

		NDAS_ASSERT( Ccb->NdasrStatusFlag8 == 0 );
	
	} else {

		NDAS_ASSERT( Ccb->NdasrStatusFlag8 == CCBSTATUS_FLAG_RAID_DEGRADED	 >> 8	||
					 Ccb->NdasrStatusFlag8 == CCBSTATUS_FLAG_RAID_RECOVERING >> 8	||
					 Ccb->NdasrStatusFlag8 == CCBSTATUS_FLAG_RAID_FAILURE    >> 8	||
					 Ccb->NdasrStatusFlag8 == CCBSTATUS_FLAG_RAID_NORMAL     >> 8 );
	}

	// Update adapter status only when CCBSTATUS_FLAG_RAID_FLAG_VALID is on.
	// In other case, Ccb has no chance to get flag information from RAID.

	if (LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_RAID_FLAG_VALID)) {

		//	Check to see if the associate member is in error.

		if (LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_RAID_DEGRADED)) {

			if (!ADAPTER_ISSTATUSFLAG(HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_MEMBER_FAULT)) {

				KDPrint(2, ("NDASSCSI_ADAPTER_STATUSFLAG_MEMBER_FAULT is Set\n") );
			}

			ADAPTER_SETSTATUSFLAG( HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_MEMBER_FAULT );
		
		} else {
		
			ADAPTER_RESETSTATUSFLAG( HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_MEMBER_FAULT );
		}

		//	Check recovering process.

		if (LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_RAID_RECOVERING)) {

			if (!ADAPTER_ISSTATUSFLAG(HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_RECOVERING)) {

				KDPrint(2, ("NDASSCSI_ADAPTER_STATUSFLAG_RECOVERING is Set\n") );
			}

			ADAPTER_SETSTATUSFLAG( HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_RECOVERING );
		
		} else {

			ADAPTER_RESETSTATUSFLAG( HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_RECOVERING );
		}

		// Check RAID failure

		if (LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_RAID_FAILURE)) {

			if (!ADAPTER_ISSTATUSFLAG(HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_RAID_FAILURE)) {

				KDPrint(2, ("NDASSCSI_ADAPTER_STATUSFLAG_RAID_FAILURE is Set\n") );
			}

			ADAPTER_SETSTATUSFLAG( HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_RAID_FAILURE );
	
		} else {

			ADAPTER_RESETSTATUSFLAG( HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_RAID_FAILURE );
		}

		// Set RAID normal status

		if (LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_RAID_NORMAL)) {
		
			if (!ADAPTER_ISSTATUSFLAG(HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_RAID_NORMAL)) {

				KDPrint(2, ("NDASSCSI_ADAPTER_STATUSFLAG_RAID_NORMAL is Set\n") );
			}

			ADAPTER_SETSTATUSFLAG( HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_RAID_NORMAL );

		} else {

			ADAPTER_RESETSTATUSFLAG( HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_RAID_NORMAL );
		}
	}

	// power-recycle occurred.

	if (LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_POWERRECYLE_OCCUR)) {

		ADAPTER_SETSTATUSFLAG(HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_POWERRECYCLED);

	} else {

		ADAPTER_RESETSTATUSFLAG(HwDeviceExtension, NDASSCSI_ADAPTER_STATUSFLAG_POWERRECYCLED);
	}

	AdapterStatus = HwDeviceExtension->AdapterStatus;
	RELEASE_SPIN_LOCK(&HwDeviceExtension->LanscsiAdapterSpinLock, oldIrql);

	if (AdapterStatus != AdapterStatusBefore) {

		if (!(AdapterStatusBefore & NDASSCSI_ADAPTER_STATUSFLAG_MEMBER_FAULT) &&
			 (AdapterStatus & NDASSCSI_ADAPTER_STATUSFLAG_MEMBER_FAULT)) {

			// NDASSCSI_ADAPTER_STATUSFLAG_MEMBER_FAULT on

			SCSI_PORT_LOG_ERROR_MODULE_COMPLETION(NDASSCSI_IO_MEMBER_FAULT, EVTLOG_MEMBER_IN_ERROR);
			KDPrint(2,("Ccb:%p CcbStatus %d. Set member fault.\n", Ccb, Ccb->CcbStatus));
		}

		if ((AdapterStatusBefore & NDASSCSI_ADAPTER_STATUSFLAG_MEMBER_FAULT) &&
			!(AdapterStatus & NDASSCSI_ADAPTER_STATUSFLAG_MEMBER_FAULT)) {

			// NDASSCSI_ADAPTER_STATUSFLAG_MEMBER_FAULT off
			
			SCSI_PORT_LOG_ERROR_MODULE_COMPLETION(NDASSCSI_IO_MEMBER_FAULT_RECOVERED, EVTLOG_MEMBER_RECOVERED);
			KDPrint(2,("Ccb:%p CcbStatus %d. Reset member fault.\n", Ccb, Ccb->CcbStatus));
		}

		if (!(AdapterStatusBefore & NDASSCSI_ADAPTER_STATUSFLAG_RECONNECT_PENDING) &&
			(AdapterStatus & NDASSCSI_ADAPTER_STATUSFLAG_RECONNECT_PENDING)) {

			// NDASSCSI_ADAPTER_STATUSFLAG_RECONNECT_PENDING on
			
			SCSI_PORT_LOG_ERROR_MODULE_COMPLETION(NDASSCSI_IO_RECONNECT_START, EVTLOG_START_RECONNECTION);
			KDPrint(2,("Ccb:%p CcbStatus %d. Start reconnecting\n", Ccb, Ccb->CcbStatus));
		}

		if ((AdapterStatusBefore & NDASSCSI_ADAPTER_STATUSFLAG_RECONNECT_PENDING) &&
			!(AdapterStatus & NDASSCSI_ADAPTER_STATUSFLAG_RECONNECT_PENDING)) {

			// NDASSCSI_ADAPTER_STATUSFLAG_RECONNECT_PENDING off
			
			SCSI_PORT_LOG_ERROR_MODULE_COMPLETION(NDASSCSI_IO_RECONNECTED, EVTLOG_END_RECONNECTION);
			KDPrint(2,("Ccb:%p CcbStatus %d. Finish reconnecting\n", Ccb, Ccb->CcbStatus));
		}

		if (!(AdapterStatusBefore & NDASSCSI_ADAPTER_STATUSFLAG_RECOVERING) &&
			(AdapterStatus & NDASSCSI_ADAPTER_STATUSFLAG_RECOVERING)) {

			// NDASSCSI_ADAPTER_STATUSFLAG_RECOVERING on
			
			SCSI_PORT_LOG_ERROR_MODULE_COMPLETION(NDASSCSI_IO_RECOVERY_START, EVTLOG_START_RECOVERING);
			KDPrint(2,("Ccb:%p CcbStatus %d. Started recovering\n", Ccb, Ccb->CcbStatus));
		}

		if ((AdapterStatusBefore & NDASSCSI_ADAPTER_STATUSFLAG_RECOVERING) &&
			!(AdapterStatus & NDASSCSI_ADAPTER_STATUSFLAG_RECOVERING) &&
			!(AdapterStatus & (NDASSCSI_ADAPTER_STATUSFLAG_RAID_FAILURE|NDASSCSI_ADAPTER_STATUSFLAG_MEMBER_FAULT)) &&
			(AdapterStatus & NDASSCSI_ADAPTER_STATUSFLAG_RAID_NORMAL)) {

			// NDASSCSI_ADAPTER_STATUSFLAG_RECOVERING off
			
			SCSI_PORT_LOG_ERROR_MODULE_COMPLETION(NDASSCSI_IO_RECOVERED, EVTLOG_END_RECOVERING);
			KDPrint(2,("Ccb:%p CcbStatus %d. Ended recovering\n", Ccb, Ccb->CcbStatus));
		}

		if ((AdapterStatus & NDASSCSI_ADAPTER_STATUSFLAG_RAID_FAILURE)	 &&
			!(AdapterStatusBefore & NDASSCSI_ADAPTER_STATUSFLAG_RAID_FAILURE)) {

			SCSI_PORT_LOG_ERROR_MODULE_COMPLETION(NDASSCSI_IO_RAID_FAILURE, EVTLOG_RAID_FAILURE);
			KDPrint(2,("Ccb:%p CcbStatus %d. RAID failure\n", Ccb, Ccb->CcbStatus));
		}
		
		if (!(AdapterStatusBefore & NDASSCSI_ADAPTER_STATUSFLAG_POWERRECYCLED) &&
			(AdapterStatus & NDASSCSI_ADAPTER_STATUSFLAG_POWERRECYCLED)) {

			// NDASSCSI_ADAPTER_STATUSFLAG_POWERRECYCLED on
			
			SCSI_PORT_LOG_ERROR_MODULE_COMPLETION(NDASSCSI_IO_DISK_POWERRECYCLE, EVTLOG_DISK_POWERRECYCLED);
			KDPrint(2,("Ccb:%p CcbStatus %d. Started recovering\n", Ccb, Ccb->CcbStatus));
		}

		NeedToUpdatePdoInfoInLSBus = TRUE;
	}

	//	If CCB_OPCODE_UPDATE is successful, update adapter status in LanscsiBus

	if (Ccb->OperationCode == CCB_OPCODE_UPDATE) {

		if(Ccb->CcbStatus == CCB_STATUS_SUCCESS) {
		
			SCSI_PORT_LOG_ERROR_MODULE_COMPLETION(NDASSCSI_IO_UPGRADE_SUCC, EVTLOG_SUCCEED_UPGRADE);

		} else {

			SCSI_PORT_LOG_ERROR_MODULE_COMPLETION(NDASSCSI_IO_UPGRADE_FAIL, EVTLOG_FAIL_UPGRADE);
		}

		NeedToUpdatePdoInfoInLSBus = TRUE;
	}

	// Copy IO control results to the SRB buffer.
	// If device lock CCB is successful, copy the result to the SRB.
	
	if (Ccb->OperationCode == CCB_OPCODE_DEVLOCK) {

		if (Ccb->CcbStatus == CCB_STATUS_SUCCESS) {
		
			PSRB_IO_CONTROL			srbIoctlHeader;
			PUCHAR					lockIoctlBuffer;
			PNDSCIOCTL_DEVICELOCK	ioCtlAcReDeviceLock;
			PLURN_DEVLOCK_CONTROL	lurnAcReDeviceLock;

			// Get the Ioctl buffer.

			srbIoctlHeader = (PSRB_IO_CONTROL)srb->DataBuffer;
			srbIoctlHeader->ReturnCode = SRB_STATUS_SUCCESS;
			lockIoctlBuffer = (PUCHAR)(srbIoctlHeader + 1);
			ioCtlAcReDeviceLock = (PNDSCIOCTL_DEVICELOCK)lockIoctlBuffer;
			lurnAcReDeviceLock = (PLURN_DEVLOCK_CONTROL)Ccb->DataBuffer;

			// Copy the result

			RtlCopyMemory( ioCtlAcReDeviceLock->LockData,
						   lurnAcReDeviceLock->LockData,
						   NDSCLOCK_LOCKDATA_LENGTH );
		}

	} else if (Ccb->OperationCode == CCB_OPCODE_QUERY) {

		if (Ccb->CcbStatus == CCB_STATUS_SUCCESS) {
		
			PSRB_IO_CONTROL		srbIoctlHeader;
			NTSTATUS			copyStatus;

			srbIoctlHeader = (PSRB_IO_CONTROL)srb->DataBuffer;

			copyStatus = NdscCopyQueryOutputToSrb( HwDeviceExtension,
												   Ccb->DataBufferLength,
												   Ccb->DataBuffer,
												   srbIoctlHeader->Length,
												   (PUCHAR)(srbIoctlHeader + 1) );

			if (copyStatus == STATUS_BUFFER_OVERFLOW) {
			
				srbIoctlHeader->ReturnCode = SRB_STATUS_DATA_OVERRUN;

			} else if (NT_SUCCESS(copyStatus)) {
			
				srbIoctlHeader->ReturnCode = SRB_STATUS_SUCCESS;
			
			} else {
			
				srbIoctlHeader->ReturnCode = SRB_STATUS_ERROR;
			}
		}
	}

	KDPrint( 4, ("CcbStatus %d\n", Ccb->CcbStatus) );

	if (srb->Cdb[0] == SCSIOP_READ		||
		srb->Cdb[0] == SCSIOP_WRITE		||
		srb->Cdb[0] == SCSIOP_READ16	||
		srb->Cdb[0] == 0x3E				||
		srb->Cdb[0] == SCSIOP_WRITE16) {

		if (Ccb->CcbStatus != CCB_STATUS_SUCCESS/* || MiniportIoCount++ % 10000 == 0*/) {

			UINT64	logicalBlockAddress;
			ULONG	transferBlocks;

			LsCcbGetAddressAndLength( (PCDB)srb->Cdb, &logicalBlockAddress, &transferBlocks );

			DebugTrace( NDASSCSI_DBG_MINIPORT_ERROR, ("CcbStatus %X\n", Ccb->CcbStatus) );

			DebugTrace( NDASSCSI_DBG_MINIPORT_ERROR,
						("%p %s(%X,%d):%u,%I64x,%u Tx:%d S:%d To:%d F:%x\n",
						srb,
						CdbOperationString(srb->Cdb[0]),
						(int)srb->Cdb[0],
						(UCHAR)srb->CdbLength,
						((PCDB)srb->Cdb)->CDB10.ForceUnitAccess,
						logicalBlockAddress,
						transferBlocks,
						srb->DataTransferLength,
						srb->SenseInfoBufferLength,
						srb->TimeOutValue,
						srb->SrbFlags) );
		}
	}

	//	Translate CcbStatus to SrbStatus

	CcbStatusToSrbStatus( Ccb, srb );

	// Perform stop process when we get stop status.

	if (Ccb->CcbStatus == CCB_STATUS_STOP) {
	
		// Stop in the timer routine.

		DebugTrace( NDASSCSI_DBG_MINIPORT_INFO, ("Stop status. Stop in the timer routine.\n") );

	} else {

		// Update PDO information on the NDAS bus.

		if (NeedToUpdatePdoInfoInLSBus) {

			DebugTrace( NDASSCSI_DBG_MINIPORT_INFO,
						("<<<<<<<<<<<<<<<< %08lx -> %08lx ADAPTER STATUS CHANGED >>>>>>>>>>>>>>>>\n", 
						 AdapterStatusBefore, AdapterStatus) );

			UpdatePdoInfoInLSBus( HwDeviceExtension, HwDeviceExtension->AdapterStatus );
		}
	}

	// We should not use completion IRP method with disable-disconnect flag.
	// A SRB with DISABLE_DISCONNECT flag causes the SCSI port queue locked.

	criticalSrb = BooleanFlagOn( srb->SrbFlags,
								 SRB_FLAGS_DISABLE_DISCONNECT | SRB_FLAGS_BYPASS_FROZEN_QUEUE | SRB_FLAGS_BYPASS_LOCKED_QUEUE );

	if (criticalSrb) {

		DebugTrace( NDASSCSI_DBG_MINIPORT_INFO, ("Critical Srb = %p\n", srb) );
	}

	if (LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_BUSCHANGE)										|| 
		LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE)									||
		criticalSrb																				||
		Ccb->AbortCcb																			||
		(Ccb->CcbStatus != CCB_STATUS_SUCCESS && Ccb->CcbStatus != CCB_STATUS_DATA_OVERRUN))
	{	
		postCompleteIrp = FALSE;

	} else {

		postCompleteIrp = TRUE;
	}

	KeAcquireSpinLock( &HwDeviceExtension->CcbListSpinLock, &oldIrql );

	RemoveEntryList( &Ccb->ListEntryForMiniPort );
	InitializeListHead( &Ccb->ListEntryForMiniPort );

	if (Ccb->AbortCcb != NULL								||
		LsCcbIsStatusFlagOn(Ccb, CCBSTATUS_FLAG_BUSCHANGE)	|| 
		Ccb->CcbStatus == CCB_STATUS_STOP)
	{	
		LsCcbSetStatusFlag( Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE );	

		InsertTailList( &HwDeviceExtension->CcbCompletionList, 
						&Ccb->ListEntryForMiniPort );

		postCompleteIrp = FALSE;

		if (Ccb->AbortCcb != NULL) {

			PSCSI_REQUEST_BLOCK	abortSrb = Ccb->AbortCcb->Srb;

			NDAS_ASSERT( FALSE );
			NDAS_ASSERT( Ccb->AbortCcb->Srb );

			RemoveEntryList( &Ccb->AbortCcb->ListEntryForMiniPort );
			InitializeListHead( &Ccb->AbortCcb->ListEntryForMiniPort );

			LsCcbSetStatusFlag( Ccb->AbortCcb, CCBSTATUS_FLAG_TIMER_COMPLETE );	

			InsertTailList( &HwDeviceExtension->CcbCompletionList, 
							&Ccb->ListEntryForMiniPort );
		}

	} else {

#if 0
		LsCcbPostCompleteCcb(Ccb);
		LsuDecrementTdiClientInProgress();
		ScsiPortNotification( RequestComplete, HwDeviceExtension, srb );
#else
		InsertTailList( &HwDeviceExtension->CcbCompletionList, 
						&Ccb->ListEntryForMiniPort );
#endif
	}

	KeReleaseSpinLock( &HwDeviceExtension->CcbListSpinLock, oldIrql );

	if (postCompleteIrp) {

		do {

			PDEVICE_OBJECT		pDeviceObject = HwDeviceExtension->ScsiportFdoObject;
			PIRP				pCompletionIrp = NULL;
			PIO_STACK_LOCATION	pIoStack;
			NTSTATUS			ntStatus;
			PSCSI_REQUEST_BLOCK	completionSrb;

			ExAcquireSpinLock( &HwDeviceExtension->LanscsiAdapterSpinLock, &oldIrql );

			if (HwDeviceExtension->CompletionIrpPosted) {
				
				ExReleaseSpinLock( &HwDeviceExtension->LanscsiAdapterSpinLock, oldIrql );				
				break;
			}

			HwDeviceExtension->CompletionIrpPosted = TRUE;
			ExReleaseSpinLock( &HwDeviceExtension->LanscsiAdapterSpinLock, oldIrql );

			completionSrb = ExAllocatePoolWithTag( NonPagedPool, sizeof(SCSI_REQUEST_BLOCK), NDSC_PTAG_SRB );

			if (completionSrb == NULL) {

				HwDeviceExtension->CompletionIrpPosted = FALSE;
				NDAS_ASSERT( FALSE );
				break;
			}

			RtlZeroMemory( completionSrb, sizeof(SCSI_REQUEST_BLOCK) );

			// Build New IRP.

			pCompletionIrp = IoAllocateIrp((CCHAR)(pDeviceObject->StackSize + 1), FALSE);
			
			if (pCompletionIrp == NULL) {
			
				NDAS_ASSERT( FALSE );

				HwDeviceExtension->CompletionIrpPosted = FALSE;
				ExFreePoolWithTag( completionSrb, NDSC_PTAG_SRB );
				break;
			}

			pCompletionIrp->MdlAddress = NULL;

			// Set IRP stack location.

			pIoStack = IoGetNextIrpStackLocation(pCompletionIrp);
			pIoStack->DeviceObject = pDeviceObject;
			pIoStack->MajorFunction = IRP_MJ_SCSI;
			pIoStack->Parameters.DeviceIoControl.InputBufferLength = 0;
			pIoStack->Parameters.DeviceIoControl.OutputBufferLength = 0; 
			pIoStack->Parameters.Scsi.Srb = completionSrb;

			// Set SRB.

			completionSrb->Length = sizeof(SCSI_REQUEST_BLOCK);
			completionSrb->Function = SRB_FUNCTION_EXECUTE_SCSI;

			completionSrb->PathId = HwDeviceExtension->LURs[0]->LurId[0];
			completionSrb->TargetId = HwDeviceExtension->LURs[0]->LurId[1];
			completionSrb->Lun = HwDeviceExtension->LURs[0]->LurId[2];

			completionSrb->QueueAction = SRB_SIMPLE_TAG_REQUEST;

			completionSrb->SrbFlags = SRB_FLAGS_BYPASS_FROZEN_QUEUE | SRB_FLAGS_NO_QUEUE_FREEZE;
			completionSrb->OriginalRequest = pCompletionIrp;
			completionSrb->CdbLength = MAXIMUM_CDB_SIZE;
			completionSrb->Cdb[0] = SCSIOP_COMPLETE;
			completionSrb->TimeOutValue = 20;
			completionSrb->SrbStatus = SRB_STATUS_SUCCESS;
			completionSrb->DataBuffer = HwDeviceExtension;

			DebugTrace( NDASSCSI_DBG_MINIPORT_TRACE, 
						("pCompletionIrp = %p, completionSrb=%p\n", pCompletionIrp, completionSrb) );

			IoSetCompletionRoutine(	pCompletionIrp,
									CompletionIrpCompletionRoutine,
									completionSrb,
									TRUE, TRUE, TRUE );

			// Caution !!! It's not synchronized

			ntStatus = IoCallDriver( pDeviceObject, pCompletionIrp );

			if (ntStatus != STATUS_SUCCESS && ntStatus != STATUS_PENDING) {
			
				NDAS_ASSERT( FALSE );
			}
			
		} while (0);
	}

	ExAcquireSpinLock( &HwDeviceExtension->LanscsiAdapterSpinLock, &oldIrql );

	if (!IsListEmpty(&HwDeviceExtension->CcbList) || !IsListEmpty(&HwDeviceExtension->CcbCompletionList)) {

		ScsiPortNotification( RequestTimerCall, HwDeviceExtension, MiniTimer, NDSC_TIMER_VALUE );
	}

	ScsiPortNotification( NextRequest, HwDeviceExtension, NULL );

	if (HwDeviceExtension->LURs[0]) {

		ScsiPortNotification( NextLuRequest,
							  HwDeviceExtension, 
							  HwDeviceExtension->LURs[0]->LurId[0], 
							  HwDeviceExtension->LURs[0]->LurId[1], 
							  HwDeviceExtension->LURs[0]->LurId[2] );
	}

	ExReleaseSpinLock( &HwDeviceExtension->LanscsiAdapterSpinLock, oldIrql );				

	return STATUS_MORE_PROCESSING_REQUIRED;
}


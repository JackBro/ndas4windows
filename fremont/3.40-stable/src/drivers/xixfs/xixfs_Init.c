#include "xixfs_types.h"
#include "xixfs_drv.h"
#include "xixfs_global.h"
#include "xixfs_debug.h"
#include "xixfs_internal.h"
#include "xixcore/callback.h"

/*
	XI file system global data 
	- manage memory resource
	- manage VDO
	- manage filesystem status
*/

XIFS_DATA				XiGlobalData;
FAST_IO_DISPATCH		XifsFastIoDispatcher;
NPAGED_LOOKASIDE_LIST	XifsFcbLookasideList;
NPAGED_LOOKASIDE_LIST	XifsCcbLookasideList;
NPAGED_LOOKASIDE_LIST	XifsIrpContextLookasideList;
NPAGED_LOOKASIDE_LIST	XifsCloseFcbCtxList;
NPAGED_LOOKASIDE_LIST	XifsAddressLookasideList;
NPAGED_LOOKASIDE_LIST	XifsLcbLookasideList;
NPAGED_LOOKASIDE_LIST	XifsCoreBuffHeadList;





BOOLEAN XifsdTestRaisedStatus = TRUE;
BOOLEAN	XiDataDisable = FALSE;







NTSTATUS 
DriverEntry(
	PDRIVER_OBJECT		DriverObject,	
	PUNICODE_STRING		RegistryPath
);

	
#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#endif


#define XIFSD_UUID	L"XIFSD_UUID"
#define PARAM_NAME	L"XifsdUniqueID"

NTSTATUS
xixfs_OpenOrCreateParameter(
	IN HANDLE		RegHandle,
	OUT PHANDLE		RegParamHandle,
	OUT PULONG		Disposition
)
{
	NTSTATUS			RC;
	HANDLE				ParamHandle;
	UNICODE_STRING		ParamKeyName;
	OBJECT_ATTRIBUTES	KeyObjectAttributes;
	ULONG				RegDisposition;

	UNICODE_STRING			ValueName;
	UCHAR					Information[80];
	PKEY_VALUE_FULL_INFORMATION	KeyInfomation;
	ULONG					InformationLength;


	ParamKeyName.Buffer = PARAM_NAME;
	ParamKeyName.Length = sizeof(PARAM_NAME) - sizeof(WCHAR);
	ParamKeyName.MaximumLength = sizeof(PARAM_NAME);


	ValueName.Buffer = XIFSD_UUID;
	ValueName.Length = sizeof(XIFSD_UUID) - sizeof(WCHAR);
	ValueName.MaximumLength = sizeof(XIFSD_UUID);

	KeyInfomation = (PKEY_VALUE_FULL_INFORMATION)Information;
	RtlZeroMemory(KeyInfomation, sizeof(Information));


    InitializeObjectAttributes(
        &KeyObjectAttributes,
        &ParamKeyName,         // name
        OBJ_CASE_INSENSITIVE,       // attributes
        RegHandle,            // root
        NULL                        // security descriptor
        );

		RC = ZwCreateKey(
				&RegHandle,
				KEY_WRITE,
				&KeyObjectAttributes,
				0,
				NULL,
				0,
				&RegDisposition);



	if(!NT_SUCCESS(RC)){
		DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
			("XIXFS can't open Registry Parameter Key (%wZ)!!\n", &ParamKeyName));
		return RC;
	}


	DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
			("XIXFS open Registry Parameter Key (%wZ)!!\n", &ParamKeyName));


	if(RegDisposition == REG_CREATED_NEW_KEY){
		PUCHAR	Data = NULL;
		DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
				("XIXFS Create Registry Parameter Key (%wZ)!!\n", &ParamKeyName));
		

		Xixfs_GenerateUuid((void *)&XiGlobalData.HostId);

		Data = (PUCHAR)XiGlobalData.HostId;

		DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
			("[Host UUid] <%02x:%02x:%02x:%02x-%02x:%02x:%02x:%02x-%02x:%02x:%02x:%02x-%02x:%02x:%02x:%02x\n",
			Data[0], Data[1], Data[2], Data[3], Data[4], Data[5], Data[6], Data[7],
			Data[8], Data[9], Data[10], Data[11], Data[12], Data[13], Data[14], Data[15]));



		RC = ZwSetValueKey(
				RegHandle,
				&ValueName,
				0,
				REG_BINARY,
				(PVOID)XiGlobalData.HostId,
				16
				);
		
		if(!NT_SUCCESS(RC)){
				DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
					("XIXFS Fail Write Registry Parameter Key  Value (%wZ) Status(0x%x)!!\n", &ValueName, RC));
				return STATUS_UNSUCCESSFUL;
		}


	}else{
		PUCHAR	Data = NULL;
		DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
				("XIXFS Read Registry Parameter Key (%wZ)!!\n", &ParamKeyName));
		
		
		RC = ZwQueryValueKey(
					RegHandle,
					&ValueName,
					KeyValueFullInformation,
					(PVOID)KeyInfomation,
					sizeof(Information),
					&InformationLength
					);

		if(!NT_SUCCESS(RC)){
				DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
					("XIFS Fail Read Registry Parameter Key  Value (%wZ) Status(0x%x)!!\n", &ValueName, RC));
				return STATUS_UNSUCCESSFUL;
		}
	
		DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
					("TitleIndex(%ld) Type(%ld) DataOffset(%ld) DataLength(%ld)\n", 
					KeyInfomation->TitleIndex, KeyInfomation->Type, KeyInfomation->DataOffset, KeyInfomation->NameLength));



		Data = ((PUCHAR)KeyInfomation + KeyInfomation->DataOffset);




		DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
			("[Host UUid] <%02x:%02x:%02x:%02x-%02x:%02x:%02x:%02x-%02x:%02x:%02x:%02x-%02x:%02x:%02x:%02x\n",
			Data[0], Data[1], Data[2], Data[3], Data[4], Data[5], Data[6], Data[7],
			Data[8], Data[9], Data[10], Data[11], Data[12], Data[13], Data[14], Data[15]));


		RtlCopyMemory(XiGlobalData.HostId, Data, 16);
					
	}

	*RegParamHandle = RegHandle;
	*Disposition = RegDisposition;
	return STATUS_SUCCESS;
}




VOID
xixfs_CloseParameter(
	IN HANDLE ParameterHandle
)
{
	ZwClose(ParameterHandle);
}

VOID
xixfs_UnloadXixfs(
	IN  PDRIVER_OBJECT DriverObject
)
{
	LARGE_INTEGER	TimeOut;
	//UNICODE_STRING		DriverDeviceLinkName;

	//DbgPrint("call XixFsdUnloadXixFs\n");

	DebugTrace( DEBUG_LEVEL_INFO, DEBUG_TARGET_INIT, 
				("ENTER xixfs_UnloadXixfs DriverObject(%p) DriverObject->DeviceObject(%p)\n", 
					DriverObject, 
					DriverObject->DeviceObject));


	if(DriverObject!= XiGlobalData.XifsDriverObject){
		DebugTrace( DEBUG_LEVEL_ERROR, DEBUG_TARGET_INIT, 
			("ENTER xixfs_UnloadXixfs is not same DriverObject(%p): saved DriverObject(%p) \n", 
					DriverObject, 
					XiGlobalData.XifsDriverObject));
		//DbgPrint("error XixFsdUnloadXixFs 1\n");
		return;
	}


	if(XiGlobalData.IsXifsComInit == 1){

		// Stop network event call back handler
		KeSetEvent(&(XiGlobalData.XifsNetEventCtx.ShutdownEvent), 0, FALSE);


		// Stop global event server.
		KeSetEvent(&(XiGlobalData.XifsComCtx.ServShutdownEvent), 0, FALSE);

		TimeOut.QuadPart = - DEFAULT_XIFS_UMOUNTWAIT;
		KeWaitForSingleObject(&(XiGlobalData.XifsComCtx.ServStopEvent), Executive, KernelMode, FALSE, &TimeOut);		

		// Stop global event cli
		KeSetEvent(&(XiGlobalData.XifsComCtx.CliShutdownEvent), 0, FALSE);

		TimeOut.QuadPart = - DEFAULT_XIFS_UMOUNTWAIT;
		KeWaitForSingleObject(&(XiGlobalData.XifsComCtx.CliStopEvent), Executive, KernelMode, FALSE, &TimeOut);	
		
	}

	if(xixcore_global.xixcore_case_table){
			xixcore_FreeUpcaseTable(xixcore_global.xixcore_case_table);
	}

	if(XiGlobalData.IsXifsComInit == 1){
		xixcore_CleanUpAllAuxLotLockInfo();
	}
	
	// Clean global data structure.
	ExDeleteNPagedLookasideList(&(XifsFcbLookasideList));
	ExDeleteNPagedLookasideList(&(XifsCcbLookasideList));
	ExDeleteNPagedLookasideList(&(XifsIrpContextLookasideList));
	ExDeleteNPagedLookasideList(&(XifsCloseFcbCtxList));
	ExDeleteNPagedLookasideList(&(XifsAddressLookasideList));
	ExDeleteNPagedLookasideList(&(XifsLcbLookasideList));
	ExDeleteNPagedLookasideList(&(XifsCoreBuffHeadList));
	ExDeleteResourceLite(&(XiGlobalData.DataResource));
		
	

	

	



	// Close Device object for xixfs file system control.
	//RtlInitUnicodeString( &DriverDeviceLinkName, XIXFS_CONTROL_LINK_NAME );
	//IoDeleteSymbolicLink(&DriverDeviceLinkName);
	//IoDeleteDevice(XiGlobalData.XifsControlDeviceObject);

	DebugTrace( DEBUG_LEVEL_INFO, DEBUG_TARGET_INIT, 
				("ENTER xixfs_UnloadXixfs DriverObject(%p)\n", 
					DriverObject));

	ObDereferenceObject(DriverObject);
	//DbgPrint("end XixFsdUnloadXixFs \n");
}


NTSTATUS 
DriverEntry(
	PDRIVER_OBJECT		DriverObject,	
	PUNICODE_STRING		RegistryPath
)	
{
	/*
		function description
		1. Initialize Xifs global variables
		2. Initailize Xifs dispatcher
		3. Initialize fsctl fast dispatcher
		4. Initialize cache callback
		5. Create and register Xi CDO (control device object)
	*/
	NTSTATUS			RC = STATUS_SUCCESS;
	PFAST_IO_DISPATCH	PtrFastIoDispatch = NULL;
	UNICODE_STRING		DriverDeviceName;
	UNICODE_STRING		DriverDeviceLinkName;
	PDEVICE_OBJECT		XifsControlDeviceObject = NULL;
	PIO_REMOVE_LOCK		pLockRemoveLock = NULL;

	/* Registry */
	OBJECT_ATTRIBUTES	RegObjectAttributes;
	HANDLE				RegPathHandle;
	ULONG				RegDisposition;
	HANDLE				RegParamHandle;

		
	
	DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
		("XIDISK_LOT_MAP_INFO (%ld)  XIDISK_LOCK(%ld) XIDISK_LOT_INFO(%ld) XIDISK_ADDR_MAP(%ld).\n", 
		sizeof(XIDISK_LOT_MAP_INFO), 
		sizeof(XIDISK_LOCK),
		sizeof(XIDISK_LOT_INFO),
		sizeof(XIDISK_ADDR_MAP)));


	DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
		("XIDISK_DIR_INFO (%ld)  XIDISK_CHILD_INFORMATION(%ld) XIDISK_FILE_INFO(%ld) XIDISK_COMMON_LOT_HEADER(%ld).\n", 
		sizeof(XIDISK_DIR_INFO), 
		sizeof(XIDISK_CHILD_INFORMATION),
		sizeof(XIDISK_FILE_INFO),
		sizeof(XIDISK_COMMON_LOT_HEADER)));

	DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
		("XIDISK_DIR_HEADER_LOT (%ld)  XIDISK_DIR_HEADER(%ld) XIDISK_FILE_HEADER_LOT(%ld) XIDISK_FILE_HEADER(%ld).\n", 
		sizeof(XIDISK_DIR_HEADER_LOT), 
		sizeof(XIDISK_DIR_HEADER),
		sizeof(XIDISK_FILE_HEADER_LOT),
		sizeof(XIDISK_FILE_HEADER)));


	DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
		("XIDISK_DATA_LOT(%ld)  XIDISK_VOLUME_LOT(%ld) XIDISK_MAP_LOT(%ld) XIDISK_HOST_REG_LOT(%ld).\n", 
		sizeof(XIDISK_DATA_LOT), 
		sizeof(XIDISK_VOLUME_LOT),
		sizeof(XIDISK_MAP_LOT),
		sizeof(XIDISK_HOST_REG_LOT)));
	


	
	ASSERT(	sizeof(XIDISK_LOT_MAP_INFO) ==  XIDISK_LOT_MAP_INFO_SIZE);
	ASSERT(	sizeof(XIDISK_LOCK) == XIDISK_LOCK_SIZE);
	ASSERT(	sizeof(XIDISK_LOT_INFO) == XIDISK_LOT_INFO_SIZE);
	ASSERT(	sizeof(XIDISK_ADDR_MAP) == XIDISK_ADDR_MAP_SIZE);

	ASSERT( sizeof(XIDISK_DIR_INFO) ==  XIDISK_DIR_INFO_SIZE);
	ASSERT( sizeof(XIDISK_CHILD_INFORMATION) == XIDISK_CHILD_RECORD_SIZE);
	ASSERT( sizeof(XIDISK_FILE_INFO) == XIDISK_FILE_INFO_SIZE);
	ASSERT( sizeof(XIDISK_COMMON_LOT_HEADER) == XIDISK_COMMON_LOT_HEADER_SIZE);

	ASSERT( sizeof(XIDISK_DIR_HEADER_LOT) ==  XIDISK_DIR_HEADER_LOT_SIZE);
	ASSERT( sizeof(XIDISK_DIR_HEADER) == XIDISK_DIR_HEADER_SIZE);
	ASSERT( sizeof(XIDISK_FILE_HEADER_LOT) == XIDISK_FILE_HEADER_LOT_SIZE);
	ASSERT( sizeof(XIDISK_FILE_HEADER) == XIDISK_FILE_HEADER_SIZE);


	ASSERT(	sizeof(XIDISK_DATA_LOT) ==  XIDISK_DATA_LOT_SIZE);
	ASSERT(	sizeof(XIDISK_VOLUME_LOT) == XIDISK_VOLUME_LOT_SIZE);
	ASSERT( sizeof(XIDISK_MAP_LOT) == XIDISK_MAP_LOT_SIZE);
	ASSERT( sizeof(XIDISK_HOST_REG_LOT) == XIDISK_HOST_REG_LOT_SIZE);

	DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_INIT, ("XIFS START UP!!!!\n"));
	/*
		Create Xifs CDO(control device object)

	*/
	
	RtlInitUnicodeString(&DriverDeviceName, XIXFS_CONTROL_DEVICE_NAME);
	RC = IoCreateDevice(
			DriverObject,		
			sizeof(IO_REMOVE_LOCK),
			&DriverDeviceName,
			FILE_DEVICE_DISK_FILE_SYSTEM,
			0,
			FALSE,
			&(XifsControlDeviceObject) 
			);

	if(!NT_SUCCESS(RC))
	{
		DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_INIT, ("Make CDO (%p)\n"));
		return RC;
	}

	RtlInitUnicodeString( &DriverDeviceLinkName, XIXFS_CONTROL_LINK_NAME );
	RC = IoCreateSymbolicLink( &DriverDeviceLinkName, &DriverDeviceName );

    if (!NT_SUCCESS(RC)) {
		
        IoDeleteDevice( XifsControlDeviceObject );
	    return RC;
    }

	RtlZeroMemory( (PUCHAR)XifsControlDeviceObject+sizeof(DEVICE_OBJECT), 
				   sizeof(IO_REMOVE_LOCK) );


	pLockRemoveLock = (PIO_REMOVE_LOCK)((PUCHAR)XifsControlDeviceObject+sizeof(DEVICE_OBJECT));
	
	
	IoInitializeRemoveLock(pLockRemoveLock,
								TAG_REMOVE_LOCK,
								0,
								0
								);



	DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_INIT, ("Make CDO (%p)\n"));

	DebugTrace( DEBUG_LEVEL_INFO, DEBUG_TARGET_INIT, 
				("DriverObject(%p) DriverObject->DeviceObject(%p)\n", 
					DriverObject, 
					DriverObject->DeviceObject));



	DebugTrace( DEBUG_LEVEL_INFO, DEBUG_TARGET_INIT, 
				("XifsControlDeviceObject(%p) DriverObject(%p) NextDevice(%p) AttachedDevice(%p)\n", 
				XifsControlDeviceObject,
				XifsControlDeviceObject->DriverObject,
				XifsControlDeviceObject->NextDevice,
				XifsControlDeviceObject->AttachedDevice));
	

	try
	{
	
	//USHORT IrpContextMaxDepth;
	//USHORT InMemoryStructureMaxDepth;
	/*
		Initialize Xifs global variables
	*/

		DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_INIT, ("Initialize Global data\n"));
		RtlZeroMemory(&XiGlobalData, sizeof(XIFS_DATA));

		//Set Node type
		XiGlobalData.NodeTypeCode = XIFS_NODE_FS_GLOBAL;
		XiGlobalData.NodeTypeCode = sizeof(XIFS_DATA);

		ExInitializeResourceLite(&(XiGlobalData.DataResource));
	

		XIXCORE_SET_FLAGS(XiGlobalData.XifsFlags, XIFS_DATA_FLAG_RESOURCE_INITIALIZED);
		DebugTrace(DEBUG_LEVEL_INFO, (DEBUG_TARGET_INIT|DEBUG_TARGET_GDATA), 
								("GData Flag (0x%x)\n",XiGlobalData.XifsFlags ));
		// keep a ptr to the driver object sent to us by the I/O Mgr
		XiGlobalData.XifsDriverObject= DriverObject;

		// initialize the mounted logical volume list head
		
		InitializeListHead(&(XiGlobalData.XifsVDOList));

		InterlockedExchange(&(XiGlobalData.QueuedWork),0);
		KeInitializeEvent(&(XiGlobalData.QueuedWorkcleareEvent), NotificationEvent, FALSE);
		// Initialize Auxilary lot lock list
		//ExInitializeFastMutex(&(XiGlobalData.XifsAuxLotLockListMutex));
		//InitializeListHead(&(XiGlobalData.XifsAuxLotLockList));
		

		
		ExInitializeNPagedLookasideList(
			&(XifsFcbLookasideList),
			NULL,
			NULL,
			0,//POOL_RAISE_IF_ALLOCATION_FAILURE,
			sizeof(XIXFS_FCB),
			TAG_FCB,
			0);

		
		ExInitializeNPagedLookasideList(
			&(XifsCcbLookasideList),
			NULL,
			NULL,
			0,//POOL_RAISE_IF_ALLOCATION_FAILURE,
			sizeof(XIXFS_CCB),
			TAG_CCB,
			0);


		ExInitializeNPagedLookasideList(
			&(XifsIrpContextLookasideList),
			NULL,
			NULL,
			0,//POOL_RAISE_IF_ALLOCATION_FAILURE,
			sizeof(XIXFS_IRPCONTEXT),
			TAG_IPCONTEXT,
			0);	


		ExInitializeNPagedLookasideList(
			&(XifsCloseFcbCtxList),
			NULL,
			NULL,
			0,//POOL_RAISE_IF_ALLOCATION_FAILURE,
			sizeof(XIFS_CLOSE_FCB_CTX),
			TAG_CLOSEFCBCTX,
			0);	


		ExInitializeNPagedLookasideList(
			&(XifsAddressLookasideList),
			NULL,
			NULL,
			0,//POOL_RAISE_IF_ALLOCATION_FAILURE,
			sizeof(XIXCORE_IO_LOT_INFO),
			TAG_ADDRESS,
			0);
		
		ExInitializeNPagedLookasideList(
			&(XifsLcbLookasideList),
			NULL,
			NULL,
			0,//POOL_RAISE_IF_ALLOCATION_FAILURE,
			sizeof(XIXFS_LCB),
			TAG_LCB,
			0);


		ExInitializeNPagedLookasideList(
			&(XifsCoreBuffHeadList),
			NULL,
			NULL,
			0,//POOL_RAISE_IF_ALLOCATION_FAILURE,
			sizeof(XIXCORE_BUFFER),
			TAG_COREBUFF,
			0);



		XIXCORE_SET_FLAGS(XiGlobalData.XifsFlags, XIFS_DATA_FLAG_MEMORY_INITIALIZED);
		DebugTrace(DEBUG_LEVEL_CRITICAL, (DEBUG_TARGET_INIT|DEBUG_TARGET_GDATA), 
								("GData Flag (0x%x)\n",XiGlobalData.XifsFlags ));
		/*
			Initailize Xifs dispatcher
		*/
		DriverObject->MajorFunction[IRP_MJ_CREATE]				= 
		DriverObject->MajorFunction[IRP_MJ_CLOSE]					= 
		DriverObject->MajorFunction[IRP_MJ_READ]					= 
		DriverObject->MajorFunction[IRP_MJ_WRITE]					= 
		DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION]	= 
		DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION]		= 
		DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS]			= 
		DriverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = 
		DriverObject->MajorFunction[IRP_MJ_SET_VOLUME_INFORMATION] = 
		DriverObject->MajorFunction[IRP_MJ_DIRECTORY_CONTROL]	= 
		DriverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] 	= 
		DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]		=
		DriverObject->MajorFunction[IRP_MJ_SHUTDOWN]				= 
		DriverObject->MajorFunction[IRP_MJ_LOCK_CONTROL]			= 
		DriverObject->MajorFunction[IRP_MJ_CLEANUP]				= 
		DriverObject->MajorFunction[IRP_MJ_QUERY_SECURITY]		=
		DriverObject->MajorFunction[IRP_MJ_SET_SECURITY]			= 
		DriverObject->MajorFunction[IRP_MJ_QUERY_EA]				= 
		DriverObject->MajorFunction[IRP_MJ_SET_EA]				=
		DriverObject->MajorFunction[IRP_MJ_PNP]					=  (PDRIVER_DISPATCH)xixfs_Dispatch;
		
		

		// DriverObject->DriverUnload
		//	Remove ctrol device object
		//
		DriverObject->DriverUnload = xixfs_UnloadXixfs;

		/*
			Initailize Xifs fast Io dispatcher
		*/

		// Now, it is time to initialize the fast-io stuff ...
		PtrFastIoDispatch = DriverObject->FastIoDispatch = &(XifsFastIoDispatcher);

		// initialize the global fast-io structure

		PtrFastIoDispatch->SizeOfFastIoDispatch		= sizeof(FAST_IO_DISPATCH);
		PtrFastIoDispatch->FastIoCheckIfPossible	= xixfs_FastIoCheckIfPossible;
		PtrFastIoDispatch->FastIoRead				= xixfs_FastIoRead;
		PtrFastIoDispatch->FastIoWrite				= xixfs_FastIoWrite;
		PtrFastIoDispatch->FastIoQueryBasicInfo		= xixfs_FastIoQueryBasicInfo;
		PtrFastIoDispatch->FastIoQueryStandardInfo	= xixfs_FastIoQueryStdInfo;
		PtrFastIoDispatch->FastIoQueryNetworkOpenInfo = xixfs_FastIoQueryNetInfo;
		PtrFastIoDispatch->FastIoLock				= xixfs_FastIoLock;
		PtrFastIoDispatch->FastIoUnlockSingle		= xixfs_FastIoUnlockSingle;
		PtrFastIoDispatch->FastIoUnlockAll			= xixfs_FastIoUnlockAll;
		PtrFastIoDispatch->FastIoUnlockAllByKey		= xixfs_FastIoUnlockAllByKey;
		PtrFastIoDispatch->AcquireFileForNtCreateSection = xixfs_FastIoAcqCreateSec;
		PtrFastIoDispatch->ReleaseFileForNtCreateSection = xixfs_FastIoRelCreateSec;

		// the remaining are only valid under NT Version 4.0 and later
		PtrFastIoDispatch->FastIoQueryNetworkOpenInfo	= xixfs_FastIoQueryNetInfo;
		PtrFastIoDispatch->AcquireForModWrite			= xixfs_FastIoAcqModWrite;
		PtrFastIoDispatch->ReleaseForModWrite			= xixfs_FastIoRelModWrite;
		PtrFastIoDispatch->AcquireForCcFlush			= xixfs_FastIoAcqCcFlush;
		PtrFastIoDispatch->ReleaseForCcFlush			= xixfs_FastIoRelCcFlush;

		// MDL functionality
		PtrFastIoDispatch->MdlRead						= xixfs_FastIoMdlRead;
		PtrFastIoDispatch->MdlReadComplete				= xixfs_FastIoMdlReadComplete;
		PtrFastIoDispatch->PrepareMdlWrite				= xixfs_FastIoPrepareMdlWrite;
		PtrFastIoDispatch->MdlWriteComplete				= xixfs_FastIoMdlWriteComplete;

		/*
			Initailize Xifs cache mgr callback
		*/
		XiGlobalData.XifsCacheMgrCallBacks.AcquireForLazyWrite	= xixfs_AcqLazyWrite;
		XiGlobalData.XifsCacheMgrCallBacks.ReleaseFromLazyWrite = xixfs_RelLazyWrite;
		XiGlobalData.XifsCacheMgrCallBacks.AcquireForReadAhead	= xixfs_AcqReadAhead;
		XiGlobalData.XifsCacheMgrCallBacks.ReleaseFromReadAhead = xixfs_RelReadAhead;		


		XiGlobalData.XifsCacheMgrVolumeCallBacks.AcquireForLazyWrite	= xixfs_NopAcqLazyWrite;
		XiGlobalData.XifsCacheMgrVolumeCallBacks.ReleaseFromLazyWrite	= xixfs_NopRelLazyWrite;
		XiGlobalData.XifsCacheMgrVolumeCallBacks.AcquireForReadAhead	= xixfs_NopAcqReadAhead;
		XiGlobalData.XifsCacheMgrVolumeCallBacks.ReleaseFromReadAhead	= xixfs_NopRelReadAhead;		


		

		DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, ("!!!! ResgisterPath %wZ\n", RegistryPath));

		InitializeObjectAttributes(
				&RegObjectAttributes,
				RegistryPath,               // name
				OBJ_CASE_INSENSITIVE,       // attributes
				NULL,                       // root
				NULL                        // security descriptor
				);

		RC = ZwCreateKey(
				&RegPathHandle,
				KEY_WRITE,
				&RegObjectAttributes,
				0,
				NULL,
				0,
				&RegDisposition
			);

		if(!NT_SUCCESS(RC)){
			DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, ("XIFS can't open Registry Key!!\n"));
			//try_return(RC);
		}

		ASSERT(NT_SUCCESS(RC));

		DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
			("%s XIFSD key: %lx\n",
            ((RegDisposition == REG_CREATED_NEW_KEY) ? "created" : "opened"), RegPathHandle));


		RC = xixfs_OpenOrCreateParameter(RegPathHandle,  &RegParamHandle, &RegDisposition);

		if(!NT_SUCCESS(RC)){
			DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, ("XIFS can't open Registry Key!!\n"));
			//try_return(RC);			
		}

		DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL, 
			("%s XIFSD key: %lx\n",
            ((RegDisposition == REG_CREATED_NEW_KEY) ? "created" : "opened"), RegParamHandle));


		ASSERT(NT_SUCCESS(RC));


		xixfs_CloseParameter(RegParamHandle);

		ZwClose(RegPathHandle);


		/*
			Register file system
		*/
		DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_INIT, ("XIFS Register FILE SYSTEM!!!!\n"));

		XiGlobalData.XifsControlDeviceObject = XifsControlDeviceObject;

		
		IoRegisterFileSystem(XifsControlDeviceObject);
		ObReferenceObject(DriverObject);
		XiGlobalData.IsRegistered = 1;
		XiGlobalData.IsXifsComInit = 0;
			
	
	}
	except(FsRtlIsNtstatusExpected(GetExceptionCode())?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH)
	{

		if (XifsControlDeviceObject) 
		{
			DebugTrace(DEBUG_LEVEL_EXCEPTIONS, DEBUG_TARGET_INIT, 
				("Free XifsdControlDeviceObject!!!!\n"));
			IoDeleteSymbolicLink(&DriverDeviceLinkName);
			IoDeleteDevice(XifsControlDeviceObject);
	        XiGlobalData.XifsControlDeviceObject = NULL;
		}
		
		if (XiGlobalData.XifsFlags & XIFS_DATA_FLAG_RESOURCE_INITIALIZED) 
		{
			DebugTrace(DEBUG_LEVEL_EXCEPTIONS, DEBUG_TARGET_INIT, 
				("Free UnInitialize XiGlobalData.DataResource\n"));
			ExDeleteResourceLite(&(XiGlobalData.DataResource));
			XIXCORE_CLEAR_FLAGS(XiGlobalData.XifsFlags, XIFS_DATA_FLAG_RESOURCE_INITIALIZED);
		}

		if (XiGlobalData.XifsFlags &  XIFS_DATA_FLAG_MEMORY_INITIALIZED) 
		{
			DebugTrace(DEBUG_LEVEL_EXCEPTIONS, DEBUG_TARGET_INIT, 
				("Free UnInitialize Global Memory lookaside list\n"));

			ExDeleteNPagedLookasideList(&(XifsFcbLookasideList));
			ExDeleteNPagedLookasideList(&(XifsCcbLookasideList));
			ExDeleteNPagedLookasideList(&(XifsIrpContextLookasideList));
			ExDeleteNPagedLookasideList(&(XifsCloseFcbCtxList));
			ExDeleteNPagedLookasideList(&(XifsAddressLookasideList));
			ExDeleteNPagedLookasideList(&(XifsLcbLookasideList));
			ExDeleteNPagedLookasideList(&(XifsCoreBuffHeadList));
			XIXCORE_CLEAR_FLAGS(XiGlobalData.XifsFlags, XIFS_DATA_FLAG_MEMORY_INITIALIZED);
		}


		RC = GetExceptionCode();
			
	}

	return RC;
}



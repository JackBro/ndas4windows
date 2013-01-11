#include <ntddk.h>
#include "LSKLib.h"
#include "KDebug.h"
#include "LSProto.h"
#include "LSLurn.h"
#include "LSLurnAssoc.h"
#include <scrc32.h>

#ifdef __MODULE__
#undef __MODULE__
#endif // __MODULE__
#define __MODULE__ "LSLurnAssoc"

#define SAFE_FREE_POOL(MEM_POOL_PTR) \
	if(MEM_POOL_PTR) { \
		ExFreePool(MEM_POOL_PTR); \
		MEM_POOL_PTR = NULL; \
	}

#define SAFE_FREE_POOL_WITH_TAG(MEM_POOL_PTR, POOL_TAG) \
	if(MEM_POOL_PTR) { \
		ExFreePoolWithTag(MEM_POOL_PTR, POOL_TAG); \
		MEM_POOL_PTR = NULL; \
	}


//////////////////////////////////////////////////////////////////////////
//
//	Associate LURN interfaces
//

//
//	aggregation interface
//
NTSTATUS
LurnAggrInitialize(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
	);

NTSTATUS
LurnAggrRequest(
		PLURELATION_NODE	Lurn,
		PCCB				Ccb
	);

LURN_INTERFACE LurnAggrInterface = { 
					LSSTRUC_TYPE_LURN_INTERFACE,
					sizeof(LURN_INTERFACE),
					LURN_AGGREGATION,
					0,
					{
						LurnAggrInitialize,
						LurnDestroyDefault,
						LurnAggrRequest
					}
		 };


//
//	RAID 0 (Spanning) interface
//
NTSTATUS
LurnRAID0Initialize(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
	) ;

NTSTATUS
LurnRAID0Destroy(
		PLURELATION_NODE Lurn
	) ;

NTSTATUS
LurnRAID0Request(
		PLURELATION_NODE	Lurn,
		PCCB				Ccb
	);

LURN_INTERFACE LurnRAID0Interface = { 
					LSSTRUC_TYPE_LURN_INTERFACE,
					sizeof(LURN_INTERFACE),
					LURN_RAID0,
					0,
					{
						LurnRAID0Initialize,
						LurnRAID0Destroy,
						LurnRAID0Request
					}
		 };


//
//	RAID1(mirroring V2) online-recovery interface
//
NTSTATUS
LurnRAID1RInitialize(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
	) ;

NTSTATUS
LurnRAID1RRequest(
		PLURELATION_NODE	Lurn,
		PCCB				Ccb
	);

NTSTATUS
LurnRAID1RDestroy(
		PLURELATION_NODE Lurn
	) ;

LURN_INTERFACE LurnRAID1RInterface = { 
					LSSTRUC_TYPE_LURN_INTERFACE,
					sizeof(LURN_INTERFACE),
					LURN_RAID1R,
					0,
					{
						LurnRAID1RInitialize,
						LurnRAID1RDestroy,
						LurnRAID1RRequest
					}
		 };

#if 0
//
//	RAID4 online-recovery interface
//
NTSTATUS
LurnRAID4RInitialize(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
	) ;

NTSTATUS
LurnRAID4RRequest(
		PLURELATION_NODE	Lurn,
		PCCB				Ccb
	);

NTSTATUS
LurnRAID4RDestroy(
		PLURELATION_NODE Lurn
	) ;

LURN_INTERFACE LurnRAID4RInterface = { 
					LSSTRUC_TYPE_LURN_INTERFACE,
					sizeof(LURN_INTERFACE),
					LURN_RAID4R,
					0,
					{
						LurnRAID4RInitialize,
						LurnRAID4RDestroy,
						LurnRAID4RRequest
					}
		 };
#endif

//////////////////////////////////////////////////////////////////////////
//
//	common to LURN array
//	common to associate LURN
//

NTSTATUS
LurnAssocSendCcbToChildrenArray(
		IN PLURELATION_NODE			*pLurnChildren,
		IN LONG						ChildrenCnt,
		IN PCCB						Ccb,
		IN CCB_COMPLETION_ROUTINE	CcbCompletion,
		IN PCUSTOM_DATA_BUFFER		pcdbDataBuffer,
		IN PVOID					*apExtendedCmd, // NULL if no cmd
		IN BOOLEAN					AssociateCascade
)
{
	LONG		idx_child;
	NTSTATUS	status;
	PCCB		NextCcb[LUR_MAX_LURNS_PER_LUR];
	PCMD_COMMON	pCmdTemp;

	ASSERT(!LSCcbIsFlagOn(Ccb, CCB_FLAG_DONOT_PASSDOWN));

	//
	//	Allocate new CCBs for the children
	//
	for(idx_child = 0; idx_child < ChildrenCnt; idx_child++)
	{
		status = LSCcbAllocate(&NextCcb[idx_child]);

		if(!NT_SUCCESS(status))
		{
			LONG	idx;

			KDPrintM(DBG_LURN_ERROR, ("LSCcbAllocate failed.\n"));
			for(idx = 0; idx < idx_child; idx++) {
				LSCcbFree(NextCcb[idx]);
			}

			LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
			LSCcbCompleteCcb(Ccb);
			return status;
		}

		status = LSCcbInitializeByCcb(Ccb, pLurnChildren[idx_child], NextCcb[idx_child]);
		if(!NT_SUCCESS(status))
		{
			LONG	idx;

			KDPrintM(DBG_LURN_ERROR, ("LSCcbAllocate failed.\n"));
			
			for(idx = 0; idx <= idx_child; idx++) {
				LSCcbFree(NextCcb[idx]);
			}

			LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
			LSCcbCompleteCcb(Ccb);
			return status;
		}
	
		NextCcb[idx_child]->AssociateID = (USHORT)idx_child;
		LSCcbSetFlag(NextCcb[idx_child], CCB_FLAG_ASSOCIATE|CCB_FLAG_ALLOCATED);
		LSCcbSetFlag(NextCcb[idx_child], Ccb->Flags&CCB_FLAG_SYNCHRONOUS);
		
#if 0
		if (pLurnChildren[idx_child]->LurnParent &&
			LURN_REDUNDENT_TYPE(pLurnChildren[idx_child]->LurnParent->LurnType)) {
			//
			// In redundent RAID type, any single disconnect can mean data inconsistency.
			//	Let monitor thread handles disconnection.
			//
			LSCcbSetFlag(NextCcb[idx_child], CCB_FLAG_RETRY_NOT_ALLOWED);
		}
#endif

		LSCcbSetCompletionRoutine(NextCcb[idx_child], CcbCompletion, Ccb);

		// attach the data buffers to each CCBs(optional)
		if(pcdbDataBuffer)
		{
			ASSERT(ChildrenCnt == pcdbDataBuffer->DataBufferCount);

			NextCcb[idx_child]->DataBuffer = pcdbDataBuffer->DataBuffer[idx_child];
			NextCcb[idx_child]->DataBufferLength = (ULONG)pcdbDataBuffer->DataBufferLength[idx_child];
		}

		// add extended cmd
		if(apExtendedCmd)
		{
			// iterate to last command in Ccb
			for(pCmdTemp = NextCcb[idx_child]->pExtendedCommand; NULL != pCmdTemp && NULL != pCmdTemp->pNextCmd; pCmdTemp = pCmdTemp->pNextCmd)
				;
			// attach
			if(NULL == pCmdTemp) // nothing in list
				NextCcb[idx_child]->pExtendedCommand = apExtendedCmd[idx_child];
			else
				pCmdTemp->pNextCmd = apExtendedCmd[idx_child];
		}
	}

	if(AssociateCascade)
	{
		Ccb->CascadeEventArray = ExAllocatePoolWithTag(
			NonPagedPool, sizeof(KEVENT) * ChildrenCnt, EVENT_ARRAY_TAG);
		if(!Ccb->CascadeEventArray)
		{
			ASSERT(FALSE);
			for(idx_child = 0; idx_child < ChildrenCnt; idx_child++)
			{
				LSCcbFree(NextCcb[idx_child]);
			}
			
			status = STATUS_INSUFFICIENT_RESOURCES;
			LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
			LSCcbCompleteCcb(Ccb);
			return status;
		}

		for(idx_child = 0; idx_child < ChildrenCnt; idx_child++)
		{
			KDPrintM(DBG_LURN_INFO, ("Init event(%p)\n", &Ccb->CascadeEventArray[idx_child]));
			KeInitializeEvent(
				&Ccb->CascadeEventArray[idx_child],
				SynchronizationEvent, 
				FALSE);

			NextCcb[idx_child]->CascadeEvent = &Ccb->CascadeEventArray[idx_child];
			if (idx_child<ChildrenCnt-1) {
				NextCcb[idx_child]->CascadeNextCcb = NextCcb[idx_child+1];
			} else {
				NextCcb[idx_child]->CascadeNextCcb = NULL;
			}
			KDPrintM(DBG_LURN_INFO, ("Cascade #%d (%p).\n", idx_child, NextCcb[idx_child]->CascadeEvent));
		}
		
		Ccb->CascadeEventArrarySize = ChildrenCnt;
		
		// ignition code
//		Ccb->CascadeEventToWork = 0;
		KeSetEvent(&Ccb->CascadeEventArray[0], IO_NO_INCREMENT, FALSE);
	}

	//
	//	Send CCBs to the child.
	//
	Ccb->AssociateCount = ChildrenCnt;
	Ccb->ChildReqCount = ChildrenCnt; 
	for(idx_child = 0; idx_child < ChildrenCnt; idx_child++) {
		status = LurnRequest(pLurnChildren[idx_child], NextCcb[idx_child]);
		if(!NT_SUCCESS(status)) {
			LONG	idx;

			KDPrintM(DBG_LURN_ERROR, ("LurnRequest to Child#%d failed.\n", idx_child));
			for(idx = idx_child; idx < ChildrenCnt; idx++) {
					LSCcbSetStatus(NextCcb[idx], CCB_STATUS_COMMAND_FAILED);
					LSCcbSetNextStackLocation(NextCcb[idx]);
					LSCcbCompleteCcb(NextCcb[idx]);
			}
			break;
		}
	}

	return STATUS_SUCCESS;
}

NTSTATUS
LurnAssocSendCcbToAllChildren(
		IN PLURELATION_NODE			Lurn,
		IN PCCB						Ccb,
		IN CCB_COMPLETION_ROUTINE	CcbCompletion,
		IN PCUSTOM_DATA_BUFFER		pcdbDataBuffer,
		IN PVOID					*apExtendedCmd,
		IN BOOLEAN					AssociateCascade
){
	return LurnAssocSendCcbToChildrenArray((PLURELATION_NODE *)Lurn->LurnChildren, Lurn->LurnChildrenCnt, Ccb, CcbCompletion, pcdbDataBuffer, apExtendedCmd, AssociateCascade);
}

NTSTATUS
LurnAssocQuery(
	IN PLURELATION_NODE			Lurn,
	IN CCB_COMPLETION_ROUTINE	CcbCompletion,
	IN OUT PCCB					Ccb
)
{
	NTSTATUS			status;
	PLUR_QUERY			query;

	if(CCB_OPCODE_QUERY != Ccb->OperationCode)
		return STATUS_INVALID_PARAMETER;

	KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_QUERY\n"));

	//
	//	Check to see if the CCB is coming for only this LURN.
	//
	if(LSCcbIsFlagOn(Ccb, CCB_FLAG_DONOT_PASSDOWN)) {
		LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
		LSCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;
	}

	query = (PLUR_QUERY)Ccb->DataBuffer;

	switch(query->InfoClass)
	{
	case LurEnumerateLurn:
		{
			PLURN_ENUM_INFORMATION	ReturnInfo;
			PLURN_INFORMATION		LurnInfo;

			ReturnInfo = (PLURN_ENUM_INFORMATION)LUR_QUERY_INFORMATION(query);
			LurnInfo = &ReturnInfo->Lurns[Lurn->LurnId];
			LurnInfo->Length		= sizeof(LURN_INFORMATION);
			LurnInfo->LurnId		= Lurn->LurnId;
			LurnInfo->LurnType		= Lurn->LurnType;
			LurnInfo->UnitBlocks	= Lurn->UnitBlocks;
			LurnInfo->BlockBytes	= Lurn->BlockBytes;
			LurnInfo->AccessRight	= Lurn->AccessRight;
			LurnInfo->StatusFlags	= 0;

			status = LurnAssocSendCcbToAllChildren(Lurn, Ccb, CcbCompletion, NULL, NULL, FALSE);

		}
		break;

	case LurRefreshLurn:
		{
			// only the leaf nodes will process this query
			status = LurnAssocSendCcbToAllChildren(Lurn, Ccb, CcbCompletion, NULL, NULL, FALSE);
		}
		break;

	case LurPrimaryLurnInformation:
	default:
		if(Lurn->LurnChildrenCnt > 0) {
			status = LurnRequest(Lurn->LurnChildren[0], Ccb);
			if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_TRACE, ("LurnRequest to Child#0 failed.\n"));
				LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
				LSCcbCompleteCcb(Ccb);
			}
		} else {
			status = STATUS_ILLEGAL_FUNCTION;
			LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
			LSCcbCompleteCcb(Ccb);
		}
	}
	return status;
}

NTSTATUS
LurnAssocRefreshCcbStatusFlag(
	IN PLURELATION_NODE			pLurn,
	PULONG						CcbStatusFlags
)
{
	NTSTATUS					ntStatus;

	CCB							Ccb;

	PLUR_QUERY					LurQuery;
	BYTE						LurBuffer[SIZE_OF_LURQUERY(0, sizeof(LURN_REFRESH))];
	PLURN_REFRESH				LurnRefresh;

	//
	//	initialize query CCB
	//
	LSCCB_INITIALIZE(&Ccb);
	Ccb.OperationCode = CCB_OPCODE_QUERY;
	LSCcbSetFlag(&Ccb, CCB_FLAG_SYNCHRONOUS);

	RtlZeroMemory(LurBuffer, sizeof(LurBuffer));
	LurQuery = (PLUR_QUERY)LurBuffer;
	LurQuery->InfoClass = LurRefreshLurn;
	LurQuery->QueryDataLength = 0;

	LurnRefresh = (PLURN_REFRESH)LUR_QUERY_INFORMATION(LurQuery);
	LurnRefresh->Length = sizeof(LURN_REFRESH);

	Ccb.DataBuffer = LurQuery;
	Ccb.DataBufferLength = LurQuery->Length;

	ntStatus = LurnRequest(pLurn, &Ccb);

	if(!NT_SUCCESS(ntStatus)) {
		KDPrint(1,("LurnRequest() failed.\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	*CcbStatusFlags |= LurnRefresh->CcbStatusFlags;

	return STATUS_SUCCESS;
}

#define EXEC_SYNC_POOL_TAG 'YSXE'
//static 
NTSTATUS
LurnExecuteSyncMulti(
				IN ULONG					NrLurns,
				IN PLURELATION_NODE			Lurns[],
				IN UCHAR					CDBOperationCode,
				IN PCHAR					DataBuffer[],
				IN UINT64					BlockAddress,		// Child block addr
				IN UINT16					BlockTransfer,		// Child block count
				IN PCMD_COMMON				ExtendedCommand)
{
	NTSTATUS				status;
	PCCB					Ccb;
	UINT32					DataBufferLength = BlockTransfer * Lurns[0]->BlockBytes; // taken from the first LURN.
	ULONG					i, Waits;
	PKEVENT					CompletionEvents = NULL, *CompletionWaitEvents = NULL;
	PKWAIT_BLOCK			WaitBlockArray = NULL;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	ASSERT(NrLurns < LUR_MAX_LURNS_PER_LUR);

	if ((SCSIOP_WRITE == CDBOperationCode || SCSIOP_WRITE16 == CDBOperationCode)&& 
		!(GENERIC_WRITE & Lurns[0]->AccessRight))
	{
		KDPrintM(DBG_LURN_INFO, ("SKIP(R/O)\n"));
		return STATUS_SUCCESS;
	}

	KDPrintM(DBG_LURN_NOISE, ("NrLurns : %d, Lurn : %08x, DataBuffer : %08x, ExtendedCommand : %08x\n",
		NrLurns, Lurns, DataBuffer, ExtendedCommand));

	Ccb = (PCCB)ExAllocatePoolWithTag(NonPagedPool, sizeof(CCB) * NrLurns,
		EXEC_SYNC_POOL_TAG);
	if(!Ccb)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto out;
	}

	CompletionEvents = (PKEVENT)ExAllocatePoolWithTag(NonPagedPool, sizeof(KEVENT) * NrLurns,
		EXEC_SYNC_POOL_TAG);
	if(!CompletionEvents)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto out;
	}

	CompletionWaitEvents = (PKEVENT *)ExAllocatePoolWithTag(NonPagedPool, sizeof(KEVENT *) * NrLurns,
		EXEC_SYNC_POOL_TAG);
	if(!CompletionWaitEvents)
	{
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto out;
	}
	
	if(NrLurns > THREAD_WAIT_OBJECTS)
	{
		WaitBlockArray = (PKWAIT_BLOCK)ExAllocatePoolWithTag(NonPagedPool, sizeof(KWAIT_BLOCK) * NrLurns,
			EXEC_SYNC_POOL_TAG);
		if(!WaitBlockArray)
		{
			status = STATUS_INSUFFICIENT_RESOURCES;
			goto out;
		}
	}
	else
	{
		WaitBlockArray = NULL;
	}


	Waits = 0;

	for(i = 0; i < NrLurns; i++)
	{
		LSCCB_INITIALIZE(&Ccb[i]);

		Ccb[i].OperationCode = CCB_OPCODE_EXECUTE;
		Ccb[i].DataBuffer = (DataBuffer) ? DataBuffer[i] : NULL;
		Ccb[i].DataBufferLength = DataBufferLength;		

		((PCDB)(Ccb[i].Cdb))->CDB10.OperationCode = CDBOperationCode; // OperationCode is in same place for CDB10 and CDB16
		LSCcbSetLogicalAddress((PCDB)(Ccb[i].Cdb), BlockAddress);
		LSCcbSetTransferLength((PCDB)(Ccb[i].Cdb), BlockTransfer);

		Ccb[i].CompletionEvent = &CompletionEvents[i];
		Ccb[i].pExtendedCommand = (ExtendedCommand) ? &ExtendedCommand[i] : NULL;

		// Set ccb flags
		if((CDBOperationCode == SCSIOP_WRITE ||
			CDBOperationCode == SCSIOP_WRITE16 ||
			(Ccb[i].pExtendedCommand && Ccb[i].pExtendedCommand->Operation == CCB_EXT_WRITE)) &&
			Lurns[i]->Lur->EnabledNdasFeatures & NDASFEATURE_SIMULTANEOUS_WRITE){
			LSCcbSetFlag(&Ccb[i], CCB_FLAG_ACQUIRE_BUFLOCK);
		}

		if(Lurns[i]->Lur->LurFlags & LURFLAG_WRITE_CHECK_REQUIRED) {
			LSCcbSetFlag(&Ccb[i], CCB_FLAG_WRITE_CHECK);
		} else {
			LSCcbResetFlag(&Ccb[i], CCB_FLAG_WRITE_CHECK);
		}

		if (Lurns[i]->LurnParent &&
			LURN_REDUNDENT_TYPE(Lurns[i]->LurnParent->LurnType)) {
			LSCcbSetFlag(&Ccb[i], CCB_FLAG_RETRY_NOT_ALLOWED);
		}

		KeInitializeEvent(Ccb[i].CompletionEvent, SynchronizationEvent, FALSE);

		status = LurnRequest(Lurns[i], &Ccb[i]);
		KDPrintM(DBG_LURN_NOISE, ("LurnRequest result : %08x\n", status));

		if(!NT_SUCCESS(status))
		{
			KDPrintM(DBG_LURN_ERROR, ("LurnRequest Failed Status : %08lx", status));

			ASSERT(FALSE);
			goto out;
		}

		if(STATUS_PENDING == status)
		{
			CompletionWaitEvents[Waits] = &CompletionEvents[i];
			Waits++;
		}
		else if(!LURN_IS_RUNNING(Lurns[i]->LurnStatus))
		{
			// can not proceed this job
			status = STATUS_UNSUCCESSFUL;
			goto out;
		}
		else
		{
			// CCB_OPCODE_EXECUTE always pending
			ASSERT(STATUS_PENDING == status);
		}
	}

	if(Waits)
	{
		status = KeWaitForMultipleObjects(
			Waits,
			CompletionWaitEvents,
			WaitAll,
			Executive,
			KernelMode,
			FALSE,
			NULL,
			WaitBlockArray
			);

		KDPrintM(DBG_LURN_NOISE, ("KeWaitForMultipleObjects result : %08x, Waits : %d\n",
			status, Waits));

		if(STATUS_SUCCESS != status)
		{
			KDPrintM(DBG_LURN_TRACE, ("KeWaitForMultipleObjects result : %08x, Waits : %d\n",
				status, Waits));
			// Pass if NT_SUCCESS. On win2k, it can be other value than STATUS_SUCCESS(such as STATUS_WAIT1)
			if(!NT_SUCCESS(status))
			{
				KDPrintM(DBG_LURN_ERROR, ("KeWaitForMultipleObjects result : %08x, Waits : %d\n",
					status, Waits));
				status = STATUS_UNSUCCESSFUL;
				goto out;
			}
		}
	}

	for(i = 0; i < NrLurns; i++)
	{
		if(CCB_STATUS_SUCCESS != Ccb[i].CcbStatus)
		{
			KDPrintM(DBG_LURN_ERROR, ("Failed CcbStatus : %08lx, CcbStatusFlags : %08lx\n",
				Ccb[i].CcbStatus, Ccb[i].CcbStatusFlags));
			status = STATUS_UNSUCCESSFUL;
			goto out;
		}
	}

	status = STATUS_SUCCESS;
out:
	SAFE_FREE_POOL_WITH_TAG(CompletionEvents, EXEC_SYNC_POOL_TAG);
	SAFE_FREE_POOL_WITH_TAG(CompletionWaitEvents, EXEC_SYNC_POOL_TAG);
	SAFE_FREE_POOL_WITH_TAG(WaitBlockArray, EXEC_SYNC_POOL_TAG);
	SAFE_FREE_POOL_WITH_TAG(Ccb, EXEC_SYNC_POOL_TAG);

	if(STATUS_SUCCESS != status)
	{
		KDPrintM(DBG_LURN_ERROR, ("Failed : Status %08lx\n", status));
	}

	return status;
}

/* static */
NTSTATUS
LurnExecuteSyncRead(
	IN PLURELATION_NODE	Lurn,
	OUT PUCHAR			DataBuffer,
	IN INT64			LogicalBlockAddress,	// child block address
	IN UINT32			TransferBlocks			// child block count
){
	PCMD_BYTE_OP ExtendedCommand = NULL;

	NTSTATUS status;
	CMD_BYTE_OP ext_cmd;

	RtlZeroMemory(&ext_cmd, sizeof(CMD_BYTE_OP));
	ext_cmd.Operation = CCB_EXT_READ;
	ext_cmd.CountBack = 
		(LogicalBlockAddress < 0) ? TRUE : FALSE;
	ext_cmd.logicalBlockAddress = 
		(UINT64)((LogicalBlockAddress < 0) ? -1 * LogicalBlockAddress : LogicalBlockAddress);
	ext_cmd.ByteOperation = EXT_BLOCK_OPERATION;
	ext_cmd.pByteData = DataBuffer;
	ext_cmd.LengthBlock = (UINT16)TransferBlocks;
	ext_cmd.pLurnCreated = Lurn;

	status = LurnExecuteSyncMulti(
		1, 
		&Lurn,
		SCSIOP_READ, // whatever to execute
		&DataBuffer, // not used
		0,
		0,
		(PCMD_COMMON)&ext_cmd);

	return status;
}

/* static */
NTSTATUS
LurnExecuteSyncWrite(
	IN PLURELATION_NODE	Lurn,
	IN OUT PUCHAR		DataBuffer,
	IN INT64			LogicalBlockAddress,	// child block address
	IN UINT32			TransferBlocks			// child block count
){
	PCMD_BYTE_OP ExtendedCommand = NULL;

	NTSTATUS status;
	CMD_BYTE_OP ext_cmd;

	RtlZeroMemory(&ext_cmd, sizeof(CMD_BYTE_OP));
	ext_cmd.Operation = CCB_EXT_WRITE;
	ext_cmd.CountBack = 
		(LogicalBlockAddress < 0) ? TRUE : FALSE;
	ext_cmd.logicalBlockAddress = 
		(UINT64)((LogicalBlockAddress < 0) ? -1 * LogicalBlockAddress : LogicalBlockAddress);
	ext_cmd.ByteOperation = EXT_BLOCK_OPERATION;
	ext_cmd.pByteData = DataBuffer;
	ext_cmd.LengthBlock = (UINT16)TransferBlocks;
	ext_cmd.pLurnCreated = Lurn;

	status = LurnExecuteSyncMulti(
		1, 
		&Lurn,
		SCSIOP_READ, // whatever to execute
		&DataBuffer, // not used
		0,
		0,
		(PCMD_COMMON)&ext_cmd);

	return status;
}

#if 0 // Hobin fix ver. 
NTSTATUS
LurnRMDRead(
	IN PLURELATION_NODE		Lurn)
{
	NTSTATUS status;
	ULONG i;
	PRAID_INFO pRaidInfo;
	NDAS_RAID_META_DATA rmd_tmp, *rmd;
	UINT32 uiUSNMax;

	KDPrintM(DBG_LURN_INFO, ("IN\n"));

	pRaidInfo = Lurn->LurnRAIDInfo;
	ASSERT(pRaidInfo);

	rmd = &pRaidInfo->rmd;

	uiUSNMax = 0;

	for(i = 0; i < Lurn->LurnChildrenCnt; i++)
	{
		if(LURN_STATUS_RUNNING != Lurn->LurnChildren[i]->LurnStatus)
			continue;

		status = LurnExecuteSyncRead(Lurn->LurnChildren[i], (PUCHAR)&rmd_tmp,
			NDAS_BLOCK_LOCATION_RMD, 1);

		if(!NT_SUCCESS(status))
			continue;

		if(
			NDAS_RAID_META_DATA_SIGNATURE != rmd_tmp.Signature || 
			!IS_RMD_CRC_VALID(crc32_calc, rmd_tmp))
		{
			// invalid rmd
			continue;
		}
		else
		{
			if(uiUSNMax < rmd_tmp.uiUSN)
			{
				uiUSNMax = rmd_tmp.uiUSN;
				// newer one
				RtlCopyMemory(rmd, &rmd_tmp, sizeof(NDAS_RAID_META_DATA));
			}
		}
	}

	if(0 == uiUSNMax)
	{
		// not found, init rmd here
		RtlZeroMemory(rmd, sizeof(NDAS_RAID_META_DATA));
		rmd->Signature = NDAS_RAID_META_DATA_SIGNATURE;
		rmd->uiUSN = 1;
		for(i = 0; i < Lurn->LurnChildrenCnt; i ++)
		{
			rmd->UnitMetaData[i].iUnitDeviceIdx = (unsigned _int16)i;
		}
		SET_RMD_CRC(crc32_calc, *rmd);
	}

	status = STATUS_SUCCESS;
	
	KDPrintM(DBG_LURN_INFO, ("OUT\n"));
	return status;
}

static
NTSTATUS
LurnRMDWrite(
			IN PLURELATION_NODE		Lurn)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	ULONG i, fail_count;
	PRAID_INFO pRaidInfo;
	NDAS_RAID_META_DATA rmd_tmp, *rmd;
	UINT32 uiUSNMax;

	if(!(GENERIC_WRITE & Lurn->AccessRight))
	{
		KDPrintM(DBG_LURN_INFO, ("SKIP(R/O)\n"));
		return STATUS_SUCCESS;
	}

	KDPrintM(DBG_LURN_INFO, ("IN\n"));
	pRaidInfo = Lurn->LurnRAIDInfo;
	ASSERT(pRaidInfo);

	rmd = &pRaidInfo->rmd;

	// read NDAS_BLOCK_LOCATION_RMD_T to get highest USN
	uiUSNMax = rmd->uiUSN;

	for(fail_count = 0, i = 0; i < Lurn->LurnChildrenCnt; i++)
	{
		if(LURN_STATUS_RUNNING != Lurn->LurnChildren[i]->LurnStatus)
			continue;

		status = LurnExecuteSyncRead(Lurn->LurnChildren[i], (PUCHAR)&rmd_tmp,
			NDAS_BLOCK_LOCATION_RMD, 1);

		if(!NT_SUCCESS(status))
		{
			fail_count++;
			KDPrintM(DBG_LURN_ERROR, ("read failed on Lurn->LurnChildren[%d] = %p\n", i, Lurn->LurnChildren[i]));
			continue;
		}

		if(
			NDAS_RAID_META_DATA_SIGNATURE != rmd_tmp.Signature || 
			!IS_RMD_CRC_VALID(crc32_calc, rmd_tmp))
		{
			// invalid rmd
			fail_count++;
			KDPrintM(DBG_LURN_ERROR, ("bad RMD on Lurn->LurnChildren[%d] = %p\n", i, Lurn->LurnChildren[i]));
			continue;
		}
		else
		{
			if(uiUSNMax < rmd_tmp.uiUSN)
			{
				uiUSNMax = rmd_tmp.uiUSN;
			}
		}
	}

	if(i == fail_count)
	{
		goto fail;
	}

	// here we flush the HDD
	for(i = 0; i < Lurn->LurnChildrenCnt; i++)
	{
		if(LURN_STATUS_RUNNING != Lurn->LurnChildren[i]->LurnStatus)
			continue;

		status = LurnExecuteSync(
			1,
			&Lurn->LurnChildren[i],
			SCSIOP_SYNCHRONIZE_CACHE,
			NULL,
			0,
			0,
			NULL);

		if(!NT_SUCCESS(status))
		{
			KDPrintM(DBG_LURN_ERROR, ("Flush failed on = Lurn->LurnChildren[%d](%p)\n", i, Lurn->LurnChildren[i]));
			return STATUS_UNSUCCESSFUL;
		}
	}
	
	// increase USN to highest
	rmd->uiUSN = uiUSNMax +1;
	SET_RMD_CRC(crc32_calc, *rmd);

	// write rmd to NDAS_BLOCK_LOCATION_RMD_T
	for(i = 0; i < Lurn->LurnChildrenCnt; i++)
	{
		if(LURN_STATUS_RUNNING != Lurn->LurnChildren[i]->LurnStatus)
			continue;

		status = LurnExecuteSyncWrite(Lurn->LurnChildren[i], (PUCHAR)rmd,
			NDAS_BLOCK_LOCATION_RMD_T, 1);

		if(!NT_SUCCESS(status))
		{
			fail_count++;
			KDPrintM(DBG_LURN_ERROR, ("write _T failed on Lurn->LurnChildren[%d] = %p\n", i, Lurn->LurnChildren[i]));
			continue;
		}
	}

	if(i == fail_count)
	{
		goto fail;
	}

	// write rmd to NDAS_BLOCK_LOCATION_RMD
	for(i = 0; i < Lurn->LurnChildrenCnt; i++)
	{
		if(LURN_STATUS_RUNNING != Lurn->LurnChildren[i]->LurnStatus)
			continue;

		status = LurnExecuteSyncWrite(Lurn->LurnChildren[i], (PUCHAR)rmd,
			NDAS_BLOCK_LOCATION_RMD, 1);

		if(!NT_SUCCESS(status))
		{
			fail_count++;
			KDPrintM(DBG_LURN_ERROR, ("write failed on Lurn->LurnChildren[%d] = %p\n", i, Lurn->LurnChildren[i]));
			continue;
		}
	}

	if(i == fail_count)
	{
		goto fail;
	}

	// here we flush the HDD
	for(i = 0; i < Lurn->LurnChildrenCnt; i++)
	{
		if(LURN_STATUS_RUNNING != Lurn->LurnChildren[i]->LurnStatus)
			continue;

		status = LurnExecuteSync(
			1,
			&Lurn->LurnChildren[i],
			SCSIOP_SYNCHRONIZE_CACHE,
			NULL,
			0,
			0,
			NULL);

		if(!NT_SUCCESS(status))
		{
			KDPrintM(DBG_LURN_ERROR, ("Flush failed on = Lurn->LurnChildren[%d](%p)\n", i, Lurn->LurnChildren[i]));
			return STATUS_UNSUCCESSFUL;
		}
	}

	status = STATUS_SUCCESS;
fail:
	KDPrintM(DBG_LURN_INFO, ("OUT\n"));
	return status;
}

/*
LurnReadyToRecover
returns STATUS_SUCCESS even if raid status is RAID_STATUS_FAIL
check raid status after function return
*/
static
NTSTATUS
LurnRefreshRaidStatus(
	PLURELATION_NODE Lurn,
	UINT32 *new_raid_status
	)
{
	NTSTATUS status = STATUS_UNSUCCESSFUL;
	PRAID_INFO pRaidInfo;
	PNDAS_RAID_META_DATA rmd;
	UINT32 i;
	UINT32 iChildDefected;
	BOOLEAN rmd_invalid = FALSE;

	KDPrintM(DBG_LURN_INFO, ("IN\n"));

	ASSERT(Lurn);
	
	pRaidInfo = Lurn->LurnRAIDInfo;
	ASSERT(pRaidInfo);

	rmd = &pRaidInfo->rmd;

	if (RAID_STATUS_RECOVERING == pRaidInfo->RaidStatus ||
		RAID_STATUS_TERMINATING == pRaidInfo->RaidStatus ||
		RAID_STATUS_FAIL == pRaidInfo->RaidStatus)
	{
		// nothing to do here
		// do not change raid status
		KDPrintM(DBG_LURN_INFO, ("RaidStatus : %d. nothing to do in this function\n", pRaidInfo->RaidStatus));
		status = STATUS_SUCCESS;
		goto out;
	}

	// mark current fault children
	for(i = 0; i < pRaidInfo->nDiskCount; i++)
	{
		if(LURN_IS_RUNNING(pRaidInfo->MapLurnChildren[i]->LurnStatus))
			continue;

		if(NDAS_UNIT_META_BIND_STATUS_FAULT & rmd->UnitMetaData[i].UnitDeviceStatus)
		{
			KDPrintM(DBG_LURN_INFO, ("Child not running, Mark as fault at %d\n", i));
			continue;
		}

		// new fault
		rmd->UnitMetaData[i].UnitDeviceStatus |= NDAS_UNIT_META_BIND_STATUS_FAULT;
		rmd_invalid = TRUE;
	}

	// find fault device(s)
	iChildDefected = 0xFFFFFFFF;
	for(i = 0; i < pRaidInfo->nDiskCount; i++)
	{
		if(NDAS_UNIT_META_BIND_STATUS_FAULT & rmd->UnitMetaData[i].UnitDeviceStatus)
		{
			KDPrintM(DBG_LURN_INFO, ("Device %d(%d in DIB) is set as fault\n", i, rmd->UnitMetaData[i].iUnitDeviceIdx));
			if(0xFFFFFFFF != iChildDefected)
			{
				KDPrintM(DBG_LURN_ERROR, ("2 or more faults. we cannot proceed.\n"));
				KDPrintM(DBG_LURN_ERROR, ("**** **** RAID_STATUS_FAIL **** ****\n"));
				*new_raid_status = RAID_STATUS_FAIL;
				status = STATUS_UNSUCCESSFUL;
				goto out;
			}

			iChildDefected = i;
		}
	}

	if(0xFFFFFFFF == iChildDefected)
	{
		KDPrintM(DBG_LURN_INFO, ("fault device not found. I'm healthy\n"));
		*new_raid_status = RAID_STATUS_NORMAL;
		status = STATUS_SUCCESS;
		goto out;
	}

	// found fault device(one and only)
	pRaidInfo->iChildDefected = iChildDefected;	
	
	// if read only | secondary. Just keep emergency
	if(!(GENERIC_WRITE & Lurn->AccessRight))
	{
		*new_raid_status = RAID_STATUS_EMERGENCY;
		status = STATUS_SUCCESS;
		goto out;
	}

	// is the fault device alive now?
	if(LURN_IS_RUNNING(pRaidInfo->MapLurnChildren[iChildDefected]->LurnStatus))
	{
		KDPrintM(DBG_LURN_INFO, ("fault marked device(%d) alive, use the fault device(do not use spare device)\n", iChildDefected));
		if(RAID_STATUS_INITIAILIZING == pRaidInfo->RaidStatus)
		{
			// fault device found
			KDPrintM(DBG_LURN_INFO, ("init time, set all bitmap\n"));

			// fill bitmaps first & write to recover info disk
			RtlSetAllBits(pRaidInfo->Bitmap);
		}
		else
		{
			// bitmap already has bitmap information. just go
		}

		// now bitmap ready. start to recover
		*new_raid_status = RAID_STATUS_RECOVERING;

		rmd->UnitMetaData[iChildDefected].UnitDeviceStatus |= NDAS_UNIT_META_BIND_STATUS_FAULT;
		rmd_invalid = TRUE;

		// ok done.
		status = STATUS_SUCCESS;
		goto out;
	}

	KDPrintM(DBG_LURN_INFO, ("fault device not alive, we seek spare device to recover\n"));
	// find alive spare device
	for(i = pRaidInfo->nDiskCount; i < pRaidInfo->nDiskCount + pRaidInfo->nSpareDisk; i++)
	{
		if(LURN_STATUS_RUNNING == pRaidInfo->MapLurnChildren[i]->LurnStatus)
		{
			// alive spare device found. Hot spare time.
			PLURELATION_NODE node_tmp;
			NDAS_UNIT_META_DATA umd_tmp;

			// fill bitmaps 
			RtlSetAllBits(pRaidInfo->Bitmap);

			KDPrintM(DBG_LURN_INFO, ("Spare device alive. rmd->UnitMetaData[%d].iUnitDeviceIdx = %d\n", 
				i, rmd->UnitMetaData[i].iUnitDeviceIdx));

			// swap unit device information in rmd
			rmd->UnitMetaData[iChildDefected].UnitDeviceStatus = NDAS_UNIT_META_BIND_STATUS_SPARE;
			RtlCopyMemory(&umd_tmp, &rmd->UnitMetaData[iChildDefected], sizeof(NDAS_UNIT_META_DATA));
			RtlCopyMemory(&rmd->UnitMetaData[iChildDefected], &rmd->UnitMetaData[i], sizeof(NDAS_UNIT_META_DATA));
			RtlCopyMemory(&rmd->UnitMetaData[i], &umd_tmp, sizeof(NDAS_UNIT_META_DATA));
			// set spare flag to indicate this is spare recovery
			// when recovery complete, clear spare flag as well as fault flag.
			rmd->UnitMetaData[iChildDefected].UnitDeviceStatus = NDAS_UNIT_META_BIND_STATUS_FAULT | NDAS_UNIT_META_BIND_STATUS_SPARE;
			rmd_invalid = TRUE;


			// swap child in map
			KDPrintM(DBG_LURN_ERROR, ("MAPPING Swap pRaidInfo->MapLurnChildren[%d] <-> pRaidInfo->MapLurnChildren[%d]\n",
				iChildDefected, i));
			node_tmp = pRaidInfo->MapLurnChildren[iChildDefected];
			pRaidInfo->MapLurnChildren[iChildDefected] = pRaidInfo->MapLurnChildren[i];
			pRaidInfo->MapLurnChildren[i] = node_tmp;

			*new_raid_status = RAID_STATUS_RECOVERING;
			status = STATUS_SUCCESS;
			goto out;
		}
	}

	KDPrintM(DBG_LURN_INFO, ("we can not start to recover. keep emergency mode\n"));
	ASSERT(
		RAID_STATUS_INITIAILIZING == pRaidInfo->RaidStatus ||
		RAID_STATUS_EMERGENCY == pRaidInfo->RaidStatus
		);
	*new_raid_status = RAID_STATUS_EMERGENCY;

	status = STATUS_SUCCESS;
out:
	KDPrintM(DBG_LURN_INFO, ("status = %x, rmd_invalid = %d, *new_raid_status = %d\n", status, rmd_invalid, *new_raid_status));
	if(NT_SUCCESS(status) && rmd_invalid)
	{
		status = LurnRMDWrite(Lurn);
	}

	KDPrintM(DBG_LURN_INFO, ("*new_raid_status = %d\n", *new_raid_status));

	KDPrintM(DBG_LURN_INFO, ("OUT\n"));
	return status;
}


/*
LurnRAIDInitialize MUST be called only by LurnRAIDThreadProcRecover
*/
static
NTSTATUS
LurnRAIDInitialize(
	IN OUT PLURELATION_NODE		Lurn
	)
{
	NTSTATUS status;

	PRAID_INFO pRaidInfo;
	PRTL_BITMAP Bitmap;
	UINT32 SectorsPerBit;
	ULONG i;
	KIRQL oldIrql;
	PNDAS_RAID_META_DATA rmd;
	UINT32 iChildDefected = 0xFFFFFFFF;
	UINT32 new_raid_status;

	KDPrintM(DBG_LURN_INFO, ("IN\n"));

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL) ;

	ASSERT(Lurn);
	ASSERT(LURN_RAID1R == Lurn->LurnType || LURN_RAID4R == Lurn->LurnType);

	pRaidInfo = Lurn->LurnRAIDInfo;
	ASSERT(pRaidInfo);

	Bitmap = pRaidInfo->Bitmap;
	ASSERT(Bitmap);

	RtlClearAllBits(pRaidInfo->Bitmap);

	SectorsPerBit = pRaidInfo->SectorsPerBit;
	ASSERT(SectorsPerBit > 0);

	// assert that read/write operation is blocked
	ASSERT(RAID_STATUS_INITIAILIZING == pRaidInfo->RaidStatus);

	// We supports missing device launching. Do not ensure all children being running status
	if(0)
	{
		ULONG LurnStatus;

		for(i = 0; i < Lurn->LurnChildrenCnt; i++)
		{
			ACQUIRE_SPIN_LOCK(&Lurn->LurnChildren[i]->LurnSpinLock, &oldIrql);
			LurnStatus = Lurn->LurnChildren[i]->LurnStatus;
			RELEASE_SPIN_LOCK(&Lurn->LurnChildren[i]->LurnSpinLock, oldIrql);

			if(LURN_IS_RUNNING(LurnStatus))
				continue;

			// error
			KDPrintM(DBG_LURN_ERROR, ("LURN_STATUS Failed : %d\n", LurnStatus));
			//				ASSERT(FALSE);
			KDPrintM(DBG_LURN_ERROR, ("**** **** RAID_STATUS_FAIL **** ****\n"));
			pRaidInfo->RaidStatus = RAID_STATUS_FAIL;
			return STATUS_UNSUCCESSFUL;
		}
	}

	KDPrintM(DBG_LURN_INFO, ("all children are running, now read & test RMD\n"));
	status = LurnRMDRead(Lurn);
	rmd = &pRaidInfo->rmd;

	if(!NT_SUCCESS(status))
	{
		KDPrintM(DBG_LURN_ERROR, ("**** **** RAID_STATUS_FAIL **** ****\n"));
		pRaidInfo->RaidStatus = RAID_STATUS_FAIL;
		return STATUS_UNSUCCESSFUL;
	}

	// map MapChild
	for(i = 0; i < Lurn->LurnChildrenCnt; i++)
	{
		KDPrintM(DBG_LURN_ERROR, ("MAPPING pRaidInfo->MapLurnChildren[%d] : Lurn->LurnChildren[%d]\n",
			i, rmd->UnitMetaData[i].iUnitDeviceIdx));
		pRaidInfo->MapLurnChildren[i] = Lurn->LurnChildren[rmd->UnitMetaData[i].iUnitDeviceIdx];
	}

	// if unmount flag is not set and there is fault flag, full recovery is required.
	if(!(NDAS_RAID_META_DATA_STATE_UNMOUNTED & rmd->state))
	{
		KDPrintM(DBG_LURN_ERROR, (">=>=>=>=>= NO UNMOUNT FLAG\n"));
		// Seek fault device
		for(i = 0; i < pRaidInfo->nDiskCount; i++)
		{
			if(NDAS_UNIT_META_BIND_STATUS_FAULT & rmd->UnitMetaData[i].UnitDeviceStatus)
			{
				// fault device found
				KDPrintM(DBG_LURN_ERROR, (">=>=>=>=>= %d is really fault => DID NOT UNMOUNT ON RECOVERY => FULL RECOVER\n", i));

				// fill bitmaps first & write to recover info disk
				RtlSetAllBits(pRaidInfo->Bitmap);

				break;
			}
		}
		
		if(i == pRaidInfo->nDiskCount)
		{
			KDPrintM(DBG_LURN_ERROR, (">=>=>=>=>= fault device not found => DID NOT UNMOUNT ON NORMAL => NO RECOVER\n"));
		}
	}

	rmd->state = NDAS_RAID_META_DATA_STATE_MOUNTED;
	status = LurnRMDWrite(Lurn);
	if(!NT_SUCCESS(status))
	{
		KDPrintM(DBG_LURN_ERROR, ("**** **** RAID_STATUS_FAIL **** ****\n"));
		pRaidInfo->RaidStatus = RAID_STATUS_FAIL;
		return STATUS_UNSUCCESSFUL;
	}


	status = LurnRefreshRaidStatus(Lurn, &new_raid_status);
	ACQUIRE_SPIN_LOCK(&pRaidInfo->LockInfo, &oldIrql);
	ASSERT(RAID_STATUS_INITIAILIZING != new_raid_status);
	pRaidInfo->RaidStatus = new_raid_status;
	RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);

	KDPrintM(DBG_LURN_INFO, ("OUT\n"));

	return status;
}

//////////////////////////////////////////////////////////////////////////
//
// LurnRAIDThreadProcRecover
//
// LurnRAIDThreadProcRecover is thread function which used
// to recover the broken RAID(1, 4)
//
// Created in LurnRAID?Initialize
// Starts to recover when Recover Thread Event is called
//
// 1. Revive the dead IdeLurn
// 2. Initialize variables
// 3. Merge LWS to Bitmap
// 4. Lock - Read - Write - Unlock each sectors for bitmap
// 5. Clear bitmap when each sectors in bitmap is cleared
// 6. Recover LWS
// 7. If recovery is done | status is back to emergency, Wait for Recover Thread Event
// 8. Terminate Thread in LurnRAID?Destroy

//#define CHECK_WRITTEN_DATA 1

static
void
LurnRAIDThreadProcRecover(
	IN	PVOID	Context
	)
{
	NTSTATUS				status;
	ULONG					i, j;
	PLURELATION_NODE		Lurn;
	PLURELATION_NODE		LurnDefected, LurnsHealthy[NDAS_MAX_RAID_CHILD -1];
	PRAID_INFO				pRaidInfo;
	UINT32					OldRaidStatus;
	PNDAS_RAID_META_DATA	rmd;
	UINT32					nDiskCount;

	UINT32					SectorsPerBit;

	KIRQL					oldIrql;
	LARGE_INTEGER			TimeOut;

	// recover variables
	PRTL_BITMAP				Bitmap;
	UINT32					buf_size_each_child, sectors_each_child; // in bytes
	PUCHAR					buf_for_recovery = NULL;
#ifdef CHECK_WRITTEN_DATA
	PUCHAR					buf_for_test = NULL;
	size_t					tested_size;
#endif
	PUCHAR					bufs_read_from_healthy[NDAS_MAX_RAID_CHILD -1];
	PUCHAR					buf_write_to_fault = NULL;
	ULONG					bit_to_recover, bit_recovered;
	UINT64					sector_to_recover_begin;
	USHORT					sectors_to_recover;
	BOOLEAN					success_on_recover;
	PULONG					parity_src_ptr, parity_tar_ptr;


	KDPrintM(DBG_LURN_INFO, ("Entered\n"));

	// initialize variables, buffers
	ASSERT(Context);

	Lurn = (PLURELATION_NODE)Context;
	KDPrintM(DBG_LURN_INFO, ("=================== BEFORE RAID LOOP ========================\n"));
	KDPrintM(DBG_LURN_INFO, ("==== Lurn : %p\n", Lurn));
	KDPrintM(DBG_LURN_INFO, ("==== Lurn->LurnType : %d\n", Lurn->LurnType));
	ASSERT(LURN_RAID1R == Lurn->LurnType || LURN_RAID4R == Lurn->LurnType);

	pRaidInfo = Lurn->LurnRAIDInfo;
	KDPrintM(DBG_LURN_INFO, ("==== pRaidInfo : %p\n", pRaidInfo));
	KDPrintM(DBG_LURN_INFO, ("==== pRaidInfo->RaidStatus : %d\n", pRaidInfo->RaidStatus));
	KDPrintM(DBG_LURN_INFO, ("==== pRaidInfo->nDiskCount : %d\n", pRaidInfo->nDiskCount));
	KDPrintM(DBG_LURN_INFO, ("==== pRaidInfo->nSpareDisk : %d\n", pRaidInfo->nSpareDisk));
	ASSERT(pRaidInfo);
	
	rmd = &pRaidInfo->rmd;

	Bitmap = pRaidInfo->Bitmap;
	KDPrintM(DBG_LURN_INFO, ("==== Bitmap->SizeOfBitMap : %d\n", Bitmap->SizeOfBitMap));
	ASSERT(Bitmap);

	SectorsPerBit = pRaidInfo->SectorsPerBit;
	ASSERT(SectorsPerBit > 0);

	nDiskCount = Lurn->LurnChildrenCnt - pRaidInfo->nSpareDisk;
	ASSERT(nDiskCount == pRaidInfo->nDiskCount);
	KDPrintM(DBG_LURN_INFO, ("==== nDiskCount : %d\n", nDiskCount));

	// allocate buffers for recovery
	sectors_each_child = SectorsPerBit; // pRaidInfo->MaxBlocksPerRequest / (nDiskCount -1);
	KDPrintM(DBG_LURN_INFO, ("==== sectors_each_child : %d\n", sectors_each_child));
	buf_size_each_child = SECTOR_SIZE * sectors_each_child;
	KDPrintM(DBG_LURN_INFO, ("==== buf_size_each_child : %d\n", buf_size_each_child));
	if(LURN_RAID4R == Lurn->LurnType)
	{
		buf_for_recovery = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool, 
			buf_size_each_child * nDiskCount, RAID_RECOVER_POOL_TAG);
		ASSERT(buf_for_recovery);
		for(i = 0; i < nDiskCount -1; i++)
		{
			bufs_read_from_healthy[i] = buf_for_recovery + i * buf_size_each_child;
		}
		
		// use last parts for write buffer
		buf_write_to_fault = buf_for_recovery + i * buf_size_each_child;
	}
	else if(LURN_RAID1R == Lurn->LurnType)
	{
		ASSERT(2 == nDiskCount);
		buf_for_recovery = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool, 
			buf_size_each_child, RAID_RECOVER_POOL_TAG);
		ASSERT(buf_for_recovery);
#if CHECK_WRITTEN_DATA
		buf_for_test = (PUCHAR)ExAllocatePoolWithTag(NonPagedPool, 
			buf_size_each_child, RAID_RECOVER_POOL_TAG);
		ASSERT(buf_for_test);
#endif
		// RAID 1 don't need parity. Just write with read
		buf_write_to_fault = bufs_read_from_healthy[0] = buf_for_recovery;		
	}
	KDPrintM(DBG_LURN_INFO, ("==== buf_for_recovery : %p\n", buf_for_recovery));
	KDPrintM(DBG_LURN_INFO, ("==== buf_write_to_fault : %p\n", buf_write_to_fault));
	KDPrintM(DBG_LURN_INFO, ("=============================================================\n"));

	if(!buf_for_recovery)
	{
		KDPrintM(DBG_LURN_ERROR, ("Failed to allocate buf_for_recovery\n"));
		KDPrintM(DBG_LURN_ERROR, ("**** **** RAID_STATUS_FAIL **** ****\n"));
		pRaidInfo->RaidStatus = RAID_STATUS_FAIL;
		goto fail;
	}

	KDPrintM(DBG_LURN_INFO, ("Variables, Buffers initialized. Read & testing bitmaps\n"));

	// assert that read/write operation is blocked
	ASSERT(RAID_STATUS_INITIAILIZING == pRaidInfo->RaidStatus);

	// OK. we are here. The RAID LOOP
	// The RAID is running anyway. normal/emergency/recovery
	// Here is the flow map
	// RAID LOOP :
	//   If Status recovery, go Recover LOOP
	//   Else, Wait for event(or just wait) for 10 sec(or whatever)
	//   Check status change(lock).
	//     Status normal : back to RAID LOOP
	//     Status emergency : try to revive devices & LurnRefreshRAIDStatus. back to RAID LOOP
	//     Status terminate, fail : goto fail, terminate
	//     Status recovery, init : no kidding! impossible
	// Recover LOOP :
	//   Wait for event (instant, or just skip)
	//   Check status change.
	//     Status recover : ok, proceed
	//     Status terminate, fail : terminate thread
	//     Status else : no kidding! impossible
	// Recover bit LOOP :
	//   Read-(Parity)-Write. Clear bit. Clear bitmap sector(for each clear full sector)
	//   If fully recovered, Status normal, goto RAID LOOP
	//   If fail, goto Fail.
	//   Back to Recover LOOP
	// fail :
	//   Wait for terminate event
	// terminate :
	//   ok. Let's die
	KDPrintM(DBG_LURN_INFO, ("Jumps into RAID LOOP\n"));

	while(TRUE) // RAID LOOP
	{ 
		if(RAID_STATUS_TERMINATING != pRaidInfo->RaidStatus && !LURN_IS_RUNNING(Lurn->LurnStatus))
			break;

		if(RAID_STATUS_RECOVERING != pRaidInfo->RaidStatus)
		{
			ACQUIRE_SPIN_LOCK(&pRaidInfo->LockInfo, &oldIrql);
			OldRaidStatus = pRaidInfo->RaidStatus;
			switch(pRaidInfo->RaidStatus)
			{
			case RAID_STATUS_INITIAILIZING:
				// we support write share
/*
				if(!(GENERIC_WRITE & Lurn->AccessRight))
				{
					// initialize primary only
					break;
				}
*/
				RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
				status = LurnRAIDInitialize(Lurn);

				ACQUIRE_SPIN_LOCK(&pRaidInfo->LockInfo, &oldIrql);

				if(!NT_SUCCESS(status))
				{
					RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
					goto fail;
				}

				ASSERT(RAID_STATUS_INITIAILIZING != pRaidInfo->RaidStatus);

				if(!RAID_IS_RUNNING(pRaidInfo))
				{
					RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
					goto fail;
				}

				break;
			case RAID_STATUS_RECOVERING: // impossible pass
				ASSERT(FALSE);
				break;
			case RAID_STATUS_NORMAL:
				// nothing to do
				break;
			case RAID_STATUS_EMERGENCY_READY: // set at completion
				KDPrintM(DBG_LURN_INFO, ("case RAID_STATUS_EMERGENCY_READY\n"));
				if(GENERIC_WRITE & Lurn->AccessRight)
				{
					PWRITE_LOG write_log;
					// set write log to bitmap
					for(i = 0 ; i < NDAS_RAID_WRITE_LOG_SIZE; i++)
					{
						write_log = &pRaidInfo->WriteLogs[i];
						if(0 == write_log->transferBlocks)
							continue;

						KDPrintM(DBG_LURN_INFO, ("Write log 0x%016I64x(%d) to bitmap\n", 
							write_log->logicalBlockAddress, write_log->transferBlocks));
						RtlSetBits(pRaidInfo->Bitmap, 
							(ULONG)(write_log->logicalBlockAddress / SectorsPerBit),
							write_log->transferBlocks / SectorsPerBit +1);
					}

					RtlZeroMemory(pRaidInfo->WriteLogs, sizeof(pRaidInfo->WriteLogs));

					RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
					status = LurnRMDWrite(Lurn);
					ASSERT(NT_SUCCESS(status));
					ACQUIRE_SPIN_LOCK(&pRaidInfo->LockInfo, &oldIrql);
				}
				pRaidInfo->RaidStatus = RAID_STATUS_EMERGENCY;
			case RAID_STATUS_EMERGENCY: // fall through
				KDPrintM(DBG_LURN_INFO, ("case RAID_STATUS_EMERGENCY\n"));
				if(!(GENERIC_WRITE & Lurn->AccessRight))
				{
					// keep emergency for read only, secondary
					// AING_TO_DO : we will add RMD check code later
					KDPrintM(DBG_LURN_INFO, ("keep emergency for read only, secondary\n"));
					break;
				}

				{
					BOOLEAN try_revive = FALSE;
					UINT32 new_raid_status;
					BOOLEAN all_children_in_raid_alive;
					RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
					// try to revive all the dead children
					KDPrintM(DBG_LURN_TRACE, ("try to revive all the dead children\n"));
					all_children_in_raid_alive = TRUE;
					for(i = 0; i < Lurn->LurnChildrenCnt; i++)
					{
						if(LURN_IS_RUNNING(pRaidInfo->MapLurnChildren[i]->LurnStatus))
						{
							continue;
						}

						if(i < pRaidInfo->nDiskCount)
						{
							all_children_in_raid_alive = FALSE;
						}

						KDPrintM(DBG_LURN_TRACE, ("Reviving pRaidInfo->MapLurnChildren[%d] = %p\n", i, pRaidInfo->MapLurnChildren[i]));
						if(pRaidInfo->MapLurnChildren[i]->LurnDesc)
						{
							pRaidInfo->MapLurnChildren[i]->LurnDesc->AccessRight |= GENERIC_WRITE;
						}
						if(!(pRaidInfo->MapLurnChildren[i]->AccessRight & GENERIC_WRITE))
						{
							KDPrintM(DBG_LURN_INFO, ("pRaidInfo->MapLurnChildren[%d]->AccessRight : R/O -> R/W\n", i));
							pRaidInfo->MapLurnChildren[i]->AccessRight |= GENERIC_WRITE;
						}
						status = LurnInitialize(pRaidInfo->MapLurnChildren[i], pRaidInfo->MapLurnChildren[i]->Lur, pRaidInfo->MapLurnChildren[i]->LurnDesc);
						if(NT_SUCCESS(status))
						{
							KDPrintM(DBG_LURN_INFO, ("!!! SUCCESS to revive pRaidInfo->MapLurnChildren[%d] : %p!!!\n", i, pRaidInfo->MapLurnChildren[i]));
							// children doesn't need description anymore as it is initialized
							SAFE_FREE_POOL(pRaidInfo->MapLurnChildren[i]->LurnDesc);

							// check DIB, RMD
							// ignore if DIB is invalid
							// full recovery if RMD full recovery is set
							try_revive = TRUE;
						}
					}

					if(all_children_in_raid_alive)
					{
						KDPrintM(DBG_LURN_INFO, ("all_children_in_raid_alive!\n"));
						try_revive = TRUE;
					}

					KDPrintM(DBG_LURN_TRACE, ("try_revive = %d\n", try_revive));

					// find any alive spare
					KDPrintM(DBG_LURN_TRACE, ("Finding any alive spare %d ~ %d -1\n", nDiskCount, Lurn->LurnChildrenCnt));
					for(i = nDiskCount; i < Lurn->LurnChildrenCnt; i++)
					{
						KDPrintM(DBG_LURN_TRACE, ("pRaidInfo->MapLurnChildren[%d]->LurnStatus == %d\n", i, pRaidInfo->MapLurnChildren[i]->LurnStatus));
						if(LURN_IS_RUNNING(pRaidInfo->MapLurnChildren[i]->LurnStatus))
						{
							KDPrintM(DBG_LURN_INFO, ("!!! Spare alive pRaidInfo->MapLurnChildren[%d] : %p!!!\n", i, pRaidInfo->MapLurnChildren[i]));
							try_revive = TRUE;
						}
/*
						// LurnInitialize already completed0
						else
						{
							status = LurnInitialize(pRaidInfo->MapLurnChildren[i], pRaidInfo->MapLurnChildren[i]->Lur, pRaidInfo->MapLurnChildren[i]->LurnDesc);
							if(NT_SUCCESS(status))
							{
								KDPrintM(DBG_LURN_INFO, ("!!! SUCCESS to revive pRaidInfo->MapLurnChildren[%d] : %p!!!\n", i, pRaidInfo->MapLurnChildren[i]));
								// children doesn't need description anymore as it is initialized
								SAFE_FREE_POOL(pRaidInfo->MapLurnChildren[i]->LurnDesc);

								// check DIB
								try_revive = TRUE;
							}
						}
*/
					}
					KDPrintM(DBG_LURN_TRACE, ("try_revive = %d\n", try_revive));

					// ok, something changed
					if(try_revive)
					{
						KDPrintM(DBG_LURN_INFO, ("try_revive = %d. something changed\n", try_revive));

						status = LurnRefreshRaidStatus(Lurn, &new_raid_status);
						ASSERT(NT_SUCCESS(status));
						ACQUIRE_SPIN_LOCK(&pRaidInfo->LockInfo, &oldIrql);
						ASSERT(RAID_STATUS_EMERGENCY == pRaidInfo->RaidStatus);
						if(RAID_STATUS_EMERGENCY == pRaidInfo->RaidStatus)
						{
							pRaidInfo->RaidStatus = new_raid_status;
						}
					}
					else
					{
						ACQUIRE_SPIN_LOCK(&pRaidInfo->LockInfo, &oldIrql);
					}
				}

				break;
			case RAID_STATUS_TERMINATING: // set at destroy
				ASSERT(LURN_STATUS_STOP_PENDING == Lurn->LurnStatus);
				KDPrintM(DBG_LURN_INFO, ("case RAID_STATUS_TERMINATING\n"));
				RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
				goto terminate;
				break;
			case RAID_STATUS_FAIL:
				// usually impossible
				ASSERT(FALSE);
				RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
				goto fail;
				break;
			}
			RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);

			if(RAID_STATUS_RECOVERING != pRaidInfo->RaidStatus)
			{
				// wait if the status is not changed
				if(OldRaidStatus == pRaidInfo->RaidStatus)
				{
					// Wait for 5 sec
					KDPrintM(DBG_LURN_NOISE, ("KeWaitForSingleObject ...\n"));

					TimeOut.QuadPart = - NANO100_PER_SEC * 5;
					status = KeWaitForSingleObject(
						&pRaidInfo->RecoverThreadEvent,
						Executive,
						KernelMode,
						FALSE,
						&TimeOut);
				}

				// back to RAID LOOP
				continue;
			}
			else
			{
				// fall through

			}
		}

		// prepare to recover
		for(i = 0, j = 0; i < nDiskCount; i++)
		{
			if(NDAS_UNIT_META_BIND_STATUS_FAULT & rmd->UnitMetaData[i].UnitDeviceStatus)
			{
				LurnDefected = pRaidInfo->MapLurnChildren[i];
				continue;
			}

			LurnsHealthy[j] = pRaidInfo->MapLurnChildren[i];
			j++;
		}
		ASSERT(i == j +1);
		
		bit_to_recover = RtlFindSetBits(Bitmap, 1, 0); // find first bit.
		bit_recovered = 0xFFFFFFFF; // not any recovered yet
		KDPrintM(DBG_LURN_INFO, ("STARTS RECOVERING -> %08lx\n",	bit_to_recover));

		success_on_recover = TRUE;
		while(0xFFFFFFFF != bit_to_recover) // Recover LOOP per each bitmap bits
		{
			ACQUIRE_SPIN_LOCK(&pRaidInfo->LockInfo, &oldIrql);
			switch(pRaidInfo->RaidStatus)
			{
			case RAID_STATUS_INITIAILIZING: // R/O -> R/W
				break;
			case RAID_STATUS_NORMAL: // impossible pass
			case RAID_STATUS_EMERGENCY: // set at completion
			case RAID_STATUS_EMERGENCY_READY:
				ASSERT(FALSE);
				break;
			case RAID_STATUS_RECOVERING: // ok, proceed
				break;
			case RAID_STATUS_TERMINATING: // set at destroy
				ASSERT(LURN_STATUS_STOP_PENDING == Lurn->LurnStatus);
				KDPrintM(DBG_LURN_INFO, ("case RAID_STATUS_TERMINATING\n"));
				RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
				ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL) ;				
				goto terminate;
				break;
			case RAID_STATUS_FAIL:
				// usually impossible
				ASSERT(FALSE);
				RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
				goto fail;
				break;
			}
			if(RAID_STATUS_INITIAILIZING == pRaidInfo->RaidStatus)
			{
				RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
				break;
			}
			RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);

			// recover sectors for one bit
			// recover range : bit_to_recover * SectorsPerBit ~ min(bit_to_recover * SectorsPerBit, Lurn->UnitBlocks)
			// prepare to recover one bit
			// we recover sector_to_recover_begin ~ sector_to_recover_begin + SectorsPerBit -1

			sector_to_recover_begin = bit_to_recover * SectorsPerBit;
			if(sector_to_recover_begin + SectorsPerBit <= Lurn->UnitBlocks)
			{
				// recover range fits in disk. recover full range
				sectors_to_recover = (USHORT)SectorsPerBit;
			}
			else if(sector_to_recover_begin < Lurn->UnitBlocks)
			{
				// recover range should fit to disk
				sectors_to_recover = (USHORT)(Lurn->UnitBlocks - sector_to_recover_begin);
				KDPrintM(DBG_LURN_ERROR, 
					("** final edge of recovery range ** This should occur only once Blocks(0x%08lx), Begin(0x%08lx), Range(%d)\n",
					Lurn->UnitBlocks, sector_to_recover_begin, sectors_to_recover));
			}
			else
			{
				sectors_to_recover = 0;
			}

			success_on_recover = TRUE;
			if(sectors_to_recover > 0)
			{
				// we should recover

				// READ sectors from the healthy LURN
				status = LurnExecuteSync(
					nDiskCount -1,
					LurnsHealthy,
					SCSIOP_READ,
					bufs_read_from_healthy,
					sector_to_recover_begin,
					sectors_to_recover,
					NULL);

				if(!NT_SUCCESS(status))
				{
					KDPrintM(DBG_LURN_ERROR, ("LurnExecuteSync(read) failed at %08lx(%d)\n",	sector_to_recover_begin, sectors_to_recover));
					success_on_recover = FALSE;
					break;
				}
#if 0
				KDPrintM(DBG_LURN_ERROR, ("sectors_to_recover(%08d) 92 * %d + 376 : %02X %02X %02X %02X %02X %02X %02X %02X \n",
				sectors_to_recover,
				Lurn->ChildBlockBytes,
				(ULONG)*(bufs_read_from_healthy[0] + (91 * Lurn->ChildBlockBytes + 376 +0)),
				(ULONG)*(bufs_read_from_healthy[0] + (91 * Lurn->ChildBlockBytes + 376 +1)),
				(ULONG)*(bufs_read_from_healthy[0] + (91 * Lurn->ChildBlockBytes + 376 +2)),
				(ULONG)*(bufs_read_from_healthy[0] + (91 * Lurn->ChildBlockBytes + 376 +3)),
				(ULONG)*(bufs_read_from_healthy[0] + (91 * Lurn->ChildBlockBytes + 376 +4)),
				(ULONG)*(bufs_read_from_healthy[0] + (91 * Lurn->ChildBlockBytes + 376 +5)),
				(ULONG)*(bufs_read_from_healthy[0] + (91 * Lurn->ChildBlockBytes + 376 +6)),
				(ULONG)*(bufs_read_from_healthy[0] + (91 * Lurn->ChildBlockBytes + 376 +7))
				));
#endif				
//				ASSERT(bit_to_recover == 0);

				// create BufferRecover
				if(LURN_RAID4R == Lurn->LurnType)
				{
					// parity work
					RtlCopyMemory(buf_write_to_fault, bufs_read_from_healthy[0], buf_size_each_child);
					for(i = 1; i < nDiskCount -1; i++)
					{
						parity_tar_ptr = (PULONG)buf_write_to_fault;
						parity_src_ptr = (PULONG)bufs_read_from_healthy[i];

						// always do parity for all range (ok even if sectors_to_recover < SectorsPerBit
						j = (buf_size_each_child) / sizeof(ULONG);
						while(j--)
						{
							*parity_tar_ptr ^= *parity_src_ptr;
							parity_tar_ptr++;
							parity_src_ptr++;
						}
					}
				}
				else if(LURN_RAID1R == Lurn->LurnType)
				{
					ASSERT(buf_write_to_fault == bufs_read_from_healthy[0]);
				}

				// WRITE sectors to the defected LURN
				status = LurnExecuteSync(
					1,
					&LurnDefected,
					SCSIOP_WRITE,
					&buf_write_to_fault,
					sector_to_recover_begin,
					sectors_to_recover,
					NULL);

				if(!NT_SUCCESS(status))
				{
					KDPrintM(DBG_LURN_ERROR, ("LurnExecuteSync(write) failed at %08lx(%d)\n",	sector_to_recover_begin, sectors_to_recover));
					success_on_recover = FALSE;
					break;
				}
#if CHECK_WRITTEN_DATA1	// Check data is written correctly.
				// READ sectors from the healthy LURN
				status = LurnExecuteSync(
					nDiskCount -1,
					&LurnDefected,
					SCSIOP_READ,
					&buf_for_test,
					sector_to_recover_begin,
					sectors_to_recover,
					NULL);

				if(!NT_SUCCESS(status))
				{
					KDPrintM(DBG_LURN_ERROR, ("LurnExecuteSync(read) failed at %08lx(%d)\n",	sector_to_recover_begin, sectors_to_recover));
					success_on_recover = FALSE;
					break;
				}				

				tested_size = RtlCompareMemory(buf_for_test, buf_write_to_fault, sectors_to_recover * Lurn->ChildBlockBytes);
				ASSERT(tested_size == sectors_to_recover * Lurn->ChildBlockBytes);
#endif
			}
			else
			{
				// out of range, just clear bitmap

			}

			if(!success_on_recover)
			{
				KDPrintM(DBG_LURN_ERROR, ("FALSE == success_on_recover\n"));
				break;
			}

			// clear the bit & find next set bit
			ACQUIRE_SPIN_LOCK(&pRaidInfo->LockInfo, &oldIrql);
			bit_recovered = bit_to_recover;
			RtlClearBits(Bitmap, bit_recovered, 1);
			bit_to_recover = RtlFindSetBits(Bitmap, 1, bit_recovered); // find next bit.
			pRaidInfo->BitmapIdxToRecover = bit_to_recover;
			RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
			if(sector_to_recover_begin < Lurn->UnitBlocks)
			{
				// removing noisy trace
				if(bit_to_recover & 0xff && bit_recovered +1 == bit_to_recover)
				{
					KDPrintM(DBG_LURN_TRACE, ("-> %08lx\n", bit_to_recover));
				}
				else
				{
					KDPrintM(DBG_LURN_INFO, ("-> %08lx\n", bit_to_recover));
				}
			}
			else
			{
				KDPrintM(DBG_LURN_NOISE, ("-> %08lx\n", bit_to_recover));
			}

			if(0xFFFFFFFF == bit_to_recover)
			{
				// recovery complete
				success_on_recover = TRUE;
				KDPrintM(DBG_LURN_ERROR, ("0xFFFFFFFF == bit_to_recover\n"));
				break;
			}

		} // Recover LOOP

		KDPrintM(DBG_LURN_INFO, ("Out of recover loop\n"));

		ACQUIRE_SPIN_LOCK(&pRaidInfo->LockInfo, &oldIrql);
		switch(pRaidInfo->RaidStatus)
		{
		case RAID_STATUS_INITIAILIZING: // R/O -> R/W
			break;
		case RAID_STATUS_NORMAL:
		case RAID_STATUS_EMERGENCY:
		case RAID_STATUS_EMERGENCY_READY:
			// impossible pass
			ASSERT(FALSE);
			break;
		case RAID_STATUS_RECOVERING:
			if(!success_on_recover)
			{
				pRaidInfo->RaidStatus = RAID_STATUS_EMERGENCY;
				break;
			}

			// recover complete
			{
				// recover complete or nothing to recover
				// clear RMD flags
				for(i = 0; i < nDiskCount; i++)
				{
					rmd->UnitMetaData[i].UnitDeviceStatus = 0;
				}

				KDPrintM(DBG_LURN_INFO, ("FINISHING RECOVERY : WRITE RMD\n"));
				RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
				status = LurnRMDWrite(Lurn);
				ACQUIRE_SPIN_LOCK(&pRaidInfo->LockInfo, &oldIrql);
				if(!NT_SUCCESS(status))
				{
					KDPrintM(DBG_LURN_ERROR, ("LurnRMDWrite Failed\n"));
					break;
				}

				KDPrintM(DBG_LURN_INFO, ("!!! RECOVERY COMPLETE !!!\n"));
				pRaidInfo->RaidStatus = RAID_STATUS_NORMAL;
			}
			break;
		case RAID_STATUS_TERMINATING: // set at destroy
			RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
			goto terminate;
			break;
		case RAID_STATUS_FAIL:
			// usually impossible
			ASSERT(FALSE);
			RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
			goto fail;
			break;
		}
		RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);

			// find next bit to recover
	} // RAID LOOP
fail:
	// wait for terminate thread event
	KDPrintM(DBG_LURN_INFO, ("KeWaitForSingleObject ...\n"));

	status = KeWaitForSingleObject(
		&pRaidInfo->RecoverThreadEvent,
		Executive,
		KernelMode,
		FALSE,
		NULL);

	ASSERT(RAID_STATUS_TERMINATING ==  pRaidInfo->RaidStatus);

terminate:
	// terminate thread

	pRaidInfo->rmd.state = NDAS_RAID_META_DATA_STATE_UNMOUNTED;
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL) ;				
	status = LurnRMDWrite(Lurn);
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL) ;				
	ASSERT(NT_SUCCESS(status));

	SAFE_FREE_POOL_WITH_TAG(buf_for_recovery, RAID_RECOVER_POOL_TAG);
#if CHECK_WRITTEN_DATA
	SAFE_FREE_POOL_WITH_TAG(buf_for_test, RAID_RECOVER_POOL_TAG);
#endif
	KDPrintM(DBG_LURN_INFO, ("Terminated\n"));
	PsTerminateSystemThread(STATUS_SUCCESS);
	return;
}
#endif // hobin fix ver

//
// Set additional flag to pass to upper layer.
//
VOID 
LSAssocSetRedundentRaidStatusFlag(
	PLURELATION_NODE Lurn,
	PCCB Ccb
) {
	ULONG Flags = 0;
	UINT32 DraidStatus;
	KIRQL	oldIrql;
	if (Lurn->LurnRAIDInfo) {
		ACQUIRE_SPIN_LOCK(&Lurn->LurnRAIDInfo->SpinLock, &oldIrql);
		if (Lurn->LurnRAIDInfo->pDraidClient) {
			DraidStatus = Lurn->LurnRAIDInfo->pDraidClient->DRaidStatus;
			if (DRIX_RAID_STATUS_REBUILDING == DraidStatus)
			{
				Flags |= CCBSTATUS_FLAG_RECOVERING;
			}
			else if (DRIX_RAID_STATUS_DEGRADED == DraidStatus)
			{
				Flags |= CCBSTATUS_FLAG_LURN_DEGRADED;
			}
		}
		RELEASE_SPIN_LOCK(&Lurn->LurnRAIDInfo->SpinLock, oldIrql);
		if (Flags) {
			ACQUIRE_SPIN_LOCK(&Ccb->CcbSpinLock, &oldIrql);
			LSCcbSetStatusFlag(Ccb, Flags);
			RELEASE_SPIN_LOCK(&Ccb->CcbSpinLock, oldIrql);
		}	
	}	
}

//
// If all mode sense reports were successul, summary them and complete ccb..
//
NTSTATUS
LurnAssocModeSenseCompletion(
		IN PCCB	Ccb,
		IN PCCB	OriginalCcb
) {
	KIRQL	oldIrql;
	LONG	RemainingAssocCount;

	ACQUIRE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, &oldIrql);

	//
	// Ignore stopped node and fail at error.
	//
	switch(Ccb->CcbStatus) {
	case CCB_STATUS_SUCCESS:	// prority 0
		break;
	case CCB_STATUS_BUSY:		// prority 1
		if(OriginalCcb->CcbStatus == CCB_STATUS_SUCCESS) {
			OriginalCcb->CcbStatus = CCB_STATUS_BUSY;
		}
		break;
	case CCB_STATUS_STOP:
		// Ignore stopped node.
		break;
	default:	// prority 2
		if(OriginalCcb->CcbStatus != CCB_STATUS_STOP) {
			OriginalCcb->CcbStatus = Ccb->CcbStatus;
		}
		break;
	}
	LSCcbSetStatusFlag(	OriginalCcb,
					Ccb->CcbStatusFlags & CCBSTATUS_FLAG_ASSIGNMASK
			);

	if (CCB_STATUS_SUCCESS == Ccb->CcbStatus) {
		//
		// Update original CCB's MODE_SENSE data.
		//
		PMODE_CACHING_PAGE	RootCachingPage;
		PMODE_CACHING_PAGE	ChildCachingPage;
		UINT32	CachingPageOffset;

		CachingPageOffset = sizeof(MODE_PARAMETER_HEADER) + sizeof(MODE_PARAMETER_BLOCK);
		
		RootCachingPage = (PMODE_CACHING_PAGE)(((PUCHAR)OriginalCcb->DataBuffer) + CachingPageOffset);
		ChildCachingPage = (PMODE_CACHING_PAGE)(((PUCHAR)Ccb->DataBuffer) + CachingPageOffset);
		if (ChildCachingPage->WriteCacheEnable == 0) 
			RootCachingPage->WriteCacheEnable = 0;
		if (ChildCachingPage->ReadDisableCache == 1)
			RootCachingPage->ReadDisableCache = 1;	
	}
	ExFreePoolWithTag(Ccb->DataBuffer, CCB_CUSTOM_BUFFER_POOL_TAG);

	RELEASE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, oldIrql);

	//
	//	Complete the original CCB if this is last Ccb
	//

	RemainingAssocCount = InterlockedDecrement(&OriginalCcb->AssociateCount);
	ASSERT(RemainingAssocCount >= 0);
	if(RemainingAssocCount != 0) {
		return STATUS_SUCCESS;
	}

	LSAssocSetRedundentRaidStatusFlag(OriginalCcb->CcbCurrentStackLocation->Lurn, OriginalCcb);
	LSCcbCompleteCcb(OriginalCcb);

	return STATUS_SUCCESS;
}


//
// Get minimum specification among all children.
//
NTSTATUS
LurnAssocModeSense(
	IN PLURELATION_NODE Lurn,
	IN PCCB Ccb
) {
	CUSTOM_DATA_BUFFER customBuffer;
	ULONG i, j;
	NTSTATUS status;
	PCDB	Cdb;
	PMODE_PARAMETER_HEADER	parameterHeader = (PMODE_PARAMETER_HEADER)Ccb->DataBuffer;
	PMODE_PARAMETER_BLOCK	parameterBlock =  (PMODE_PARAMETER_BLOCK)((PUCHAR)Ccb->DataBuffer + sizeof(MODE_PARAMETER_HEADER));
	ULONG	requiredLen;
	ULONG	BlockCount;

	//
	// Check Ccb sanity check and set default sense data.
	//		
	//
	// Buffer size should larger than MODE_PARAMETER_HEADER
	//
	requiredLen = sizeof(MODE_PARAMETER_HEADER);
	if(Ccb->DataBufferLength < requiredLen) {
		KDPrintM(DBG_LURN_ERROR, ("Buffer too small. %d.\n", Ccb->DataBufferLength));
		
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;	//SRB_STATUS_SUCCESS;
		LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
		LSCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;
	}
	
	RtlZeroMemory(
		Ccb->DataBuffer,
		Ccb->DataBufferLength
		);
	Cdb = (PCDB)Ccb->Cdb;

	//
	// We only report current values.
	//

	if(Cdb->MODE_SENSE.Pc != (MODE_SENSE_CURRENT_VALUES>>6)) {
		KDPrintM(DBG_LURN_ERROR,
			("unsupported page control:%x\n", (ULONG)Cdb->MODE_SENSE.Pc));
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
		LSCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;
	} else {
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
	}

	//
	// Mode parameter header.
	//
	parameterHeader->ModeDataLength = 
		sizeof(MODE_PARAMETER_HEADER) - sizeof(parameterHeader->ModeDataLength);
	parameterHeader->MediumType = 00;	// Default medium type.

	// Fill device specific parameter
	if(!(Lurn->AccessRight & GENERIC_WRITE)) {
		KDPrintM(DBG_LURN_INFO,
		("SCSIOP_MODE_SENSE: MODE_DSP_WRITE_PROTECT\n"));
		parameterHeader->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;

		if(LSCcbIsFlagOn(Ccb, CCB_FLAG_W2K_READONLY_PATCH) ||
			LSCcbIsFlagOn(Ccb, CCB_FLAG_ALLOW_WRITE_IN_RO_ACCESS))
			parameterHeader->DeviceSpecificParameter &= ~MODE_DSP_WRITE_PROTECT;
	} else {
		parameterHeader->DeviceSpecificParameter &= ~MODE_DSP_WRITE_PROTECT;
	}

	//
	// Mode parameter block
	//
	requiredLen += sizeof(MODE_PARAMETER_BLOCK);
	if(Ccb->DataBufferLength < requiredLen) {
		Ccb->CcbStatus = CCB_STATUS_DATA_OVERRUN;
		KDPrintM(DBG_LURN_ERROR, ("Could not fill out parameter block. %d.\n", Ccb->DataBufferLength));
		LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
		LSCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;
	}
	
	// Set the length of mode parameter block descriptor to the parameter header.
	parameterHeader->BlockDescriptorLength = sizeof(MODE_PARAMETER_BLOCK);
	parameterHeader->ModeDataLength += sizeof(MODE_PARAMETER_BLOCK);

	//
	// Make Block.
	//
	BlockCount = (ULONG)(Lurn->EndBlockAddr - Lurn->StartBlockAddr + 1);
	parameterBlock->DensityCode = 0;	// It is Reserved for direct access devices.
	parameterBlock->NumberOfBlocks[0] = (BYTE)(BlockCount>>16);
	parameterBlock->NumberOfBlocks[1] = (BYTE)(BlockCount>>8);
	parameterBlock->NumberOfBlocks[2] = (BYTE)(BlockCount);

	if(Cdb->MODE_SENSE.PageCode == MODE_SENSE_RETURN_ALL ||
		Cdb->MODE_SENSE.PageCode == MODE_PAGE_CACHING) {	// all pages
		PMODE_CACHING_PAGE	cachingPage;

		requiredLen += sizeof(MODE_CACHING_PAGE);
		if(Ccb->DataBufferLength < requiredLen) {
			Ccb->CcbStatus = CCB_STATUS_DATA_OVERRUN;
			KDPrintM(DBG_LURN_ERROR, ("Could not fill out caching page. %d.\n", Ccb->DataBufferLength));
			LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
			LSCcbCompleteCcb(Ccb);
			return STATUS_SUCCESS;
		}

		parameterHeader->ModeDataLength += sizeof(MODE_CACHING_PAGE);
		cachingPage = (PMODE_CACHING_PAGE)((PUCHAR)parameterBlock + sizeof(MODE_PARAMETER_BLOCK));

		cachingPage->PageCode = MODE_PAGE_CACHING;
		cachingPage->PageLength = sizeof(MODE_CACHING_PAGE) -
			(FIELD_OFFSET(MODE_CACHING_PAGE, PageLength) + sizeof(cachingPage->PageLength));
		// Set default value.
		cachingPage->WriteCacheEnable = 1;
		cachingPage->ReadDisableCache = 0;
	}	else {
		KDPrintM(DBG_LURN_TRACE,
					("SCSIOP_MODE_SENSE: unsupported pagecode:%x\n", (ULONG)Cdb->MODE_SENSE.PageCode));
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
		LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;	//SRB_STATUS_SUCCESS;
		LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
		LSCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;
	}
	
	Ccb->ResidualDataLength = Ccb->DataBufferLength - requiredLen;

	// Prepare buffer for each child's 
	customBuffer.DataBufferCount = 0;
	for(i = 0; i < Lurn->LurnChildrenCnt; i++) {
		customBuffer.DataBuffer[i] = ExAllocatePoolWithTag(
						NonPagedPool,
						Ccb->DataBufferLength,
						CCB_CUSTOM_BUFFER_POOL_TAG
					);
		if(!customBuffer.DataBuffer[i])
		{
			// free allocated buffers
			for(j = 0; j < i; j++)
			{
				ExFreePoolWithTag(
					customBuffer.DataBuffer[j], CCB_CUSTOM_BUFFER_POOL_TAG
					);
				customBuffer.DataBuffer[i] = NULL;
			}

			Ccb->CcbStatus = CCB_STATUS_SUCCESS;
			status = STATUS_INSUFFICIENT_RESOURCES;
			KDPrintM(DBG_LURN_ERROR, ("ExAllocatePoolWithTag Failed[%d]\n", i));
			LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
			LSCcbCompleteCcb(Ccb);
			return STATUS_INSUFFICIENT_RESOURCES;
		}
		customBuffer.DataBufferLength[i] = Ccb->DataBufferLength;
		customBuffer.DataBufferCount++;
	}
	//
	// Send request to all childrens.
	//
	status = LurnAssocSendCcbToAllChildren(
											Lurn,
											Ccb,
											LurnAssocModeSenseCompletion,
											&customBuffer,
											NULL,
											FALSE
							);

	if(!NT_SUCCESS(status))
	{
		for(i = 0; i < Lurn->LurnChildrenCnt; i++)
		{
			ExFreePoolWithTag(
				customBuffer.DataBuffer[i], CCB_CUSTOM_BUFFER_POOL_TAG);
		}
		LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
		LSCcbCompleteCcb(Ccb);		
		return status;
	}
	return STATUS_SUCCESS;
}

NTSTATUS
LurnAssocModeSelectCompletion(
		IN PCCB	Ccb,
		IN PCCB	OriginalCcb
) {
	KIRQL	oldIrql;
	LONG	RemainingAssocCount;

	ACQUIRE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, &oldIrql);

	//
	// Ignore stopped node and fail at error.
	//
	switch(Ccb->CcbStatus) {
	case CCB_STATUS_SUCCESS:	// prority 0
		break;
	case CCB_STATUS_BUSY:		// prority 1
		if(OriginalCcb->CcbStatus == CCB_STATUS_SUCCESS) {
			OriginalCcb->CcbStatus = CCB_STATUS_BUSY;
		}
		break;
	case CCB_STATUS_STOP:
		break;
	default:	// prority 2
		if(OriginalCcb->CcbStatus != CCB_STATUS_STOP) {
			OriginalCcb->CcbStatus = Ccb->CcbStatus;
		}
		break;
	}
	LSCcbSetStatusFlag(	OriginalCcb,
					Ccb->CcbStatusFlags & CCBSTATUS_FLAG_ASSIGNMASK
			);

	ExFreePoolWithTag(Ccb->DataBuffer, CCB_CUSTOM_BUFFER_POOL_TAG);

	RELEASE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, oldIrql);

	//
	//	Complete the original CCB if this is last Ccb
	//

	RemainingAssocCount = InterlockedDecrement(&OriginalCcb->AssociateCount);
	ASSERT(RemainingAssocCount >= 0);
	if(RemainingAssocCount != 0) {
		return STATUS_SUCCESS;
	}
	LSAssocSetRedundentRaidStatusFlag(OriginalCcb->CcbCurrentStackLocation->Lurn, OriginalCcb);
	LSCcbCompleteCcb(OriginalCcb);

	return STATUS_SUCCESS;
}
	
NTSTATUS 
LurnAssocModeSelect(
	IN PLURELATION_NODE Lurn,
	IN PCCB Ccb
) {
	CUSTOM_DATA_BUFFER customBuffer;
	ULONG i, j;
	NTSTATUS status;
	PCDB	Cdb;
	PMODE_PARAMETER_HEADER	parameterHeader;
	PMODE_PARAMETER_BLOCK	parameterBlock;
	ULONG	requiredLen;
	UCHAR	parameterLength;
	PUCHAR	modePages;
	
	// Check buffer is enough
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
		LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
		LSCcbCompleteCcb(Ccb);		
		return STATUS_SUCCESS;
	}

	requiredLen += sizeof(MODE_PARAMETER_BLOCK);
	if(Ccb->DataBufferLength < requiredLen ||parameterLength < requiredLen) {
		KDPrintM(DBG_LURN_ERROR, ("Buffer too small. %d.\n", Ccb->DataBufferLength));
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
		LSCcbCompleteCcb(Ccb);		
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
		LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
		LSCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;
	} else if(Cdb->MODE_SELECT.SPBit != 0)	{
		KDPrintM(DBG_LURN_ERROR,
			("unsupported save page to non-volitile memory:%x.\n", (ULONG)Cdb->MODE_SELECT.SPBit));
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
		LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
		LSCcbCompleteCcb(Ccb);
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
		KDPrintM(DBG_LURN_ERROR, ("Caching page\n"));

		requiredLen += sizeof(MODE_CACHING_PAGE);
		if(Ccb->DataBufferLength < requiredLen ||parameterLength < requiredLen) {
				KDPrintM(DBG_LURN_ERROR, ("Buffer too small. %d.\n", Ccb->DataBufferLength));
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
				LSCcbCompleteCcb(Ccb);
				return STATUS_SUCCESS;
		}
		//
		// Send to all children.
		//
		// Prepare buffer for each child's 
		customBuffer.DataBufferCount = 0;
		for(i = 0; i < Lurn->LurnChildrenCnt; i++) {
			customBuffer.DataBuffer[i] = ExAllocatePoolWithTag(
							NonPagedPool,
							Ccb->DataBufferLength,
							CCB_CUSTOM_BUFFER_POOL_TAG
						);
			if(!customBuffer.DataBuffer[i])
			{
				// free allocated buffers
				for(j = 0; j < i; j++)
				{
					ExFreePoolWithTag(
						customBuffer.DataBuffer[j], CCB_CUSTOM_BUFFER_POOL_TAG
						);
					customBuffer.DataBuffer[i] = NULL;
				}

				Ccb->CcbStatus = CCB_STATUS_SUCCESS;
				status = STATUS_INSUFFICIENT_RESOURCES;
				KDPrintM(DBG_LURN_ERROR, ("ExAllocatePoolWithTag Failed[%d]\n", i));
				LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
				LSCcbCompleteCcb(Ccb);
				return STATUS_INSUFFICIENT_RESOURCES;
			}
			RtlCopyMemory(customBuffer.DataBuffer[i], Ccb->DataBuffer, Ccb->DataBufferLength);
			customBuffer.DataBufferLength[i] = Ccb->DataBufferLength;
			customBuffer.DataBufferCount++;
		}
		//
		// Send request to all childrens.
		//
		status = LurnAssocSendCcbToAllChildren(
												Lurn,
												Ccb,
												LurnAssocModeSelectCompletion,
												&customBuffer,
												NULL,
												FALSE
								);

		if(!NT_SUCCESS(status))
		{
			for(i = 0; i < Lurn->LurnChildrenCnt; i++)
			{
				ExFreePoolWithTag(
					customBuffer.DataBuffer[i], CCB_CUSTOM_BUFFER_POOL_TAG);
			}
			LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
			LSCcbCompleteCcb(Ccb);		
			return status;
		}	
	} else {
		KDPrintM(DBG_LURN_ERROR,
			("unsupported pagecode:%x\n", (ULONG)Cdb->MODE_SENSE.PageCode));
		LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
		Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;	//SRB_STATUS_SUCCESS;
		status = STATUS_SUCCESS;
		LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
		LSCcbCompleteCcb(Ccb);
		return STATUS_SUCCESS;
	}
	return STATUS_SUCCESS;
}


NTSTATUS
LurnRAIDUpdateCcbCompletion(
	IN PCCB Ccb,
	IN PCCB OriginalCcb)
{
	KIRQL	oldIrql;
	LONG	ass;
	LONG	i;
	BOOLEAN FailAll = FALSE;
		
	PLURELATION_NODE	Lurn;
	Lurn = OriginalCcb->CcbCurrentStackLocation->Lurn;
	ASSERT(Lurn);

	ACQUIRE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, &oldIrql);

	ASSERT(
		OriginalCcb->CascadeEventArray && 
		Ccb->CascadeEvent && 
		Ccb->CascadeEvent == &OriginalCcb->CascadeEventArray[Ccb->AssociateID]);

	ASSERT(OriginalCcb->OperationCode == CCB_OPCODE_UPDATE);

	KDPrintM(DBG_LURN_INFO, ("Ccb update status %x\n", Ccb->CcbStatus));

	switch(Ccb->CcbStatus) {
	case CCB_STATUS_NO_ACCESS:
		KDPrintM(DBG_LURN_INFO, ("Failed to get access right. Fail next ccb too\n"));
		OriginalCcb->CcbStatus = CCB_STATUS_NO_ACCESS;
		FailAll = TRUE; // Make another cascading CCB to fail.
		break;
	case CCB_STATUS_SUCCESS:	// prority 0
		break;
	case CCB_STATUS_STOP:		// prority 2

		if(OriginalCcb->CcbStatus != CCB_STATUS_BUSY) {
			if(LURN_RAID1R == Lurn->LurnType || LURN_RAID4R == Lurn->LurnType)
			{
				// do not stop for fault-tolerant RAID
			}
			else
			{
				OriginalCcb->CcbStatus = CCB_STATUS_STOP;
			}
		}
		break;

	case CCB_STATUS_BUSY:		// prority 3
		//
		//	We allow CCB_STATUS_BUSY when SRB exists.
		//
		ASSERT(OriginalCcb->Srb);
		OriginalCcb->CcbStatus = CCB_STATUS_BUSY;
		break;
	default:					// prority 1
		if(OriginalCcb->CcbStatus == CCB_STATUS_SUCCESS) {
			OriginalCcb->CcbStatus = Ccb->CcbStatus;
		}
		break;
	}

	if (Ccb->CascadeNextCcb) {
		KDPrintM(DBG_LURN_INFO, ("Set event for next ccb(%p)\n"));
		if (FailAll) {
			Ccb->FailCascadedCcbCode = Ccb->CcbStatus;
		}
		KeSetEvent(Ccb->CascadeNextCcb->CascadeEvent, IO_NO_INCREMENT, FALSE);
	}
#if 0
	// if this CCB has not to woke up by event, do not wake other CCB
	if(OriginalCcb->CascadeEventToWork == Ccb->AssociateID)
	{
		// trigger next event
		for(i = OriginalCcb->CascadeEventToWork +1; i < OriginalCcb->CascadeEventArrarySize; i++)
		{
			if(!KeReadStateEvent(&OriginalCcb->CascadeEventArray[i]))
			{
				//
				// inginite next CCB
				//
				if (FailAll) {
					
					
				}
				KDPrintM(DBG_LURN_INFO, ("KeSetEvent next event (%p)\n",
					&OriginalCcb->CascadeEventArray[i]));
				OriginalCcb->CascadeEventToWork = i;
				KeSetEvent(&OriginalCcb->CascadeEventArray[i], IO_NO_INCREMENT, FALSE);
				break;
			}
		}
	}
	else
	{
		KDPrintM(DBG_LURN_INFO, ("Do not KeSetEvent (Ccb[%d] is not to work)\n",
			Ccb->AssociateID));
	}
#endif

	// set own cascade event (even if already set) as a completion mark
	KeSetEvent(Ccb->CascadeEvent, IO_NO_INCREMENT, FALSE);

	LSCcbSetStatusFlag(	OriginalCcb,
		Ccb->CcbStatusFlags & CCBSTATUS_FLAG_ASSIGNMASK
		);
	RELEASE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, oldIrql);

	//
	//	Complete the original CCB
	//
	KDPrintM(DBG_LURN_INFO,("OriginalCcb:%p. OrignalCcb->StatusFlags:%08lx\n",
		OriginalCcb, OriginalCcb->CcbStatusFlags));
	ass = InterlockedDecrement(&OriginalCcb->AssociateCount);
	ASSERT(ass >= 0);
	if(ass != 0) {
		KDPrintM(DBG_LURN_INFO,("Ass:%d Ccb:%p Ccb->CcbStatus:%x Ccb->StatusFlags:%08lx Ccb->AssociateID#%d\n",
			ass, Ccb, Ccb->CcbStatus, Ccb->CcbStatusFlags, Ccb->AssociateID));
		return STATUS_SUCCESS;
	}
	KDPrintM(DBG_LURN_INFO,("OriginalCcb:%p Completed. Ccb->AssociateID#%d\n",
		OriginalCcb, Ccb->AssociateID));

	KDPrintM(DBG_LURN_INFO,("OriginalCcb->OperationCode = %08x, OriginalCcb->CcbStatus = %08x, LurnUpdate->UpdateClass = %08x\n",
		OriginalCcb->OperationCode, OriginalCcb->CcbStatus, ((PLURN_UPDATE)OriginalCcb->DataBuffer)->UpdateClass));

	ExFreePoolWithTag(OriginalCcb->CascadeEventArray, EVENT_ARRAY_TAG);
	OriginalCcb->CascadeEventArray = NULL;

	//	post-process
	// set event to work as primary
	switch(Lurn->LurnType)
	{
	case LURN_RAID1R:
	case LURN_RAID4R:
	{
		//
		// Success if two CCBs are both successful or one success, another not exist.
		//
		if (OriginalCcb->CcbStatus == CCB_STATUS_SUCCESS ||
			OriginalCcb->CcbStatus == CCB_STATUS_NOT_EXIST)
		{
			PLURN_UPDATE	LurnUpdate;
			LurnUpdate = (PLURN_UPDATE)OriginalCcb->DataBuffer;

			if(LurnUpdate->UpdateClass == LURN_UPDATECLASS_WRITEACCESS_USERID)
			{
				KDPrintM(DBG_LURN_ERROR,("********** Lurn->LurnType(%d) : R/O ->R/W : Start to initialize **********\n",
					Lurn->LurnType));

				if(Lurn->LurnParent == 0 || Lurn->LurnParent == Lurn)
				{
					Lurn->Lur->EnabledNdasFeatures &= ~NDASFEATURE_SECONDARY;
					Lurn->AccessRight |= GENERIC_WRITE;
					KDPrintM(DBG_LURN_INFO,("Updated enabled feature: %08lx\n",Lurn->Lur->EnabledNdasFeatures));
					KDPrintM(DBG_LURN_INFO,("Starting DRAID arbiter from updated permission\n"));

					//
					// Update access write of stopped child too
					//
					for(i=0;i<(LONG)Lurn->LurnChildrenCnt;i++) {
						if (!(Lurn->LurnChildren[i]->AccessRight & GENERIC_WRITE)) {
							KDPrintM(DBG_LURN_INFO,("Node %d is not updated to have write access\n", i));

							if (Lurn->LurnChildren[i]->LurnStatus == LURN_STATUS_RUNNING) {
								KDPrintM(DBG_LURN_INFO,("Node %d is running status.It should be stop or failure status\n", i));
								ASSERT(FALSE);
								// This should not happen. If it happens go to revert path.
								goto update_failed;
							}
							Lurn->LurnChildren[i]->AccessRight |= GENERIC_WRITE;
						}
					}
					
					//
					// Cannot call DraidArbiterStart directly because we are on the completion routine.
					// Let DRAID client call DraidArbiterStart
					//
					// Local client is polling local arbiter if this host is primary.
				}
			} else {
				// Other case does not happen.
				ASSERT(FALSE);
			}

			OriginalCcb->CcbStatus = CCB_STATUS_SUCCESS;
		}else {
update_failed:
			//
			// If failed to update whole available node, revert access right.
			//
			// to do...
			//
			KDPrintM(DBG_LURN_INFO,("Failed to update access right of all nodes. To do: demote rights if already updated.\n"));
		}
	}
	break;
	case LURN_AGGREGATION:
	case LURN_RAID0:
	{
		// Success only when all Ccb success
		if (OriginalCcb->CcbStatus == CCB_STATUS_SUCCESS)
		{
			PLURN_UPDATE	LurnUpdate;
			LurnUpdate = (PLURN_UPDATE)OriginalCcb->DataBuffer;

			if(LurnUpdate->UpdateClass == LURN_UPDATECLASS_WRITEACCESS_USERID)
			{
				KDPrintM(DBG_LURN_ERROR,("********** Lurn->LurnType(%d) : R/O ->R/W : Start to initialize **********\n",
					Lurn->LurnType));

				if(Lurn->LurnParent == 0 || Lurn->LurnParent == Lurn)
				{
					Lurn->Lur->EnabledNdasFeatures &= ~NDASFEATURE_SECONDARY;
					Lurn->AccessRight |= GENERIC_WRITE;
					KDPrintM(DBG_LURN_INFO,("Updated enabled feature: %08lx\n",Lurn->Lur->EnabledNdasFeatures));
				}
			}
		}
	}
	break;
	default:
	ASSERT(FALSE);
	}

#if DBG
	if(OriginalCcb->OperationCode == CCB_OPCODE_UPDATE && OriginalCcb->CcbStatus == CCB_STATUS_BUSY) {
		KDPrintM(DBG_LURN_INFO,("CCB_OPCODE_UPDATE: return CCB_STATUS_BUSY\n"));
	}
#endif

	LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
	LSCcbCompleteCcb(OriginalCcb);

	return STATUS_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
//
//	Aggregation Lurn
//

//
// Get the children's block bytes.
//

ULONG
LurnAsscGetChildBlockBytes(
	PLURELATION_NODE ParentLurn
){
	ULONG	idx_child;
	ULONG	childBlockBytes;


	childBlockBytes = ParentLurn->LurnChildren[0]->BlockBytes;
	if(childBlockBytes == 0) {
		KDPrintM(DBG_LURN_ERROR,("First child does not have the same block bytes\n"));
		return 0;
	}

	//
	// Verify the block bytes of the other children.
	//

	for(idx_child = 1; idx_child < ParentLurn->LurnChildrenCnt; idx_child ++) {

		if(childBlockBytes != ParentLurn->LurnChildren[idx_child]->BlockBytes) {
			KDPrintM(DBG_LURN_ERROR,("Children do not have the same block bytes\n"));
			return 0;
		}
	}

	return childBlockBytes;
}

NTSTATUS
LurnAggrInitialize(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
){
	UNREFERENCED_PARAMETER(LurnDesc);

	//
	// Determine block bytes
	//

	Lurn->ChildBlockBytes = LurnAsscGetChildBlockBytes(Lurn);
	if(Lurn->ChildBlockBytes == 0)
		return STATUS_DEVICE_NOT_READY;

	Lurn->BlockBytes = Lurn->ChildBlockBytes;

	return STATUS_SUCCESS;
}


NTSTATUS
LurnAggrCcbCompletion(
		IN PCCB	Ccb,
		IN PCCB	OriginalCcb
	) {
	KIRQL	oldIrql;
	LONG	ass;

	//
	//	Higher number of priority will overwrite CcbStatus.
	//
	//	
	//	Aggregation priority
	//
	//	CCB_STATUS_SUCCESS	: 0
	//	CCB_STATUS_BUSY		: 1
	//	Other error code	: 2
	//	CCB_STATUS_STOP		: 3
	ACQUIRE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, &oldIrql);

	switch(Ccb->CcbStatus) {
	case CCB_STATUS_SUCCESS:	// prority 0
		break;

	case CCB_STATUS_BUSY:		// prority 1
		if(OriginalCcb->CcbStatus == CCB_STATUS_SUCCESS) {
			OriginalCcb->CcbStatus = CCB_STATUS_BUSY;
		}
		break;
	case CCB_STATUS_STOP:		// prority 3
		OriginalCcb->CcbStatus = CCB_STATUS_STOP;
		break;
	default:					// prority 2
		if(OriginalCcb->CcbStatus != CCB_STATUS_STOP) {
			OriginalCcb->CcbStatus = Ccb->CcbStatus;
		}
		break;
	}
	LSCcbSetStatusFlag(	OriginalCcb,
					Ccb->CcbStatusFlags & CCBSTATUS_FLAG_ASSIGNMASK
			);

	RELEASE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, oldIrql);

	//
	//	Complete the original CCB
	//

	ass = InterlockedDecrement(&OriginalCcb->AssociateCount);
	ASSERT(ass >= 0);
	if(ass != 0) {
		return STATUS_SUCCESS;
	}

	//
	//	post-process for CCB_OPCODE_UPDATE
	//
	if(OriginalCcb->OperationCode == CCB_OPCODE_UPDATE &&
		OriginalCcb->CcbStatus == CCB_STATUS_SUCCESS
		) {
			PLURN_UPDATE	LurnUpdate;
			LurnUpdate = (PLURN_UPDATE)OriginalCcb->DataBuffer;

			if(LurnUpdate->UpdateClass == LURN_UPDATECLASS_WRITEACCESS_USERID) {
				PLURELATION_NODE	Lurn;
				//
				//	If this is root LURN, update LUR access right.
				//
				Lurn = OriginalCcb->CcbCurrentStackLocation->Lurn;
				ASSERT(Lurn);

				if(Lurn->LurnParent == 0 || Lurn->LurnParent == Lurn) {
					Lurn->Lur->EnabledNdasFeatures &= ~NDASFEATURE_SECONDARY;
					KDPrintM(DBG_LURN_INFO,("Updated enabled feature: %08lx\n",Lurn->Lur->EnabledNdasFeatures));
				}
			}
	}

	LSCcbCompleteCcb(OriginalCcb);

	return STATUS_SUCCESS;
}

static
NTSTATUS
LurnAggrExecute(
		PLURELATION_NODE		Lurn,
		IN	PCCB				Ccb
	) {
	ULONG				idx_child;
	NTSTATUS			status;
	PLURELATION_NODE	ChildLurn = NULL;

	switch(Ccb->Cdb[0]) {
	case SCSIOP_WRITE16:
	case SCSIOP_WRITE:
	case 0x3E:		// READ_LONG
	case SCSIOP_READ16:
	case SCSIOP_READ:
	case SCSIOP_VERIFY16:
	case SCSIOP_VERIFY:  {
		UINT64				startBlockAddress, endBlockAddress;
		ULONG				transferBlocks;
		ASSERT(Ccb->CdbLength <= MAXIMUM_CDB_SIZE);

		LSCcbGetAddressAndLength((PCDB)&Ccb->Cdb[0], &startBlockAddress, &transferBlocks);

		endBlockAddress = startBlockAddress + transferBlocks - 1;

		if(transferBlocks == 0) {
			LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
			LSCcbCompleteCcb(Ccb);
			return STATUS_SUCCESS;
		}
		if(endBlockAddress > Lurn->EndBlockAddr) {
			KDPrintM(DBG_LURN_ERROR,("SCSIOP_WRITE/READ/VERIFY: Ccb's ending sector:%ld, Lurn's ending sector:%ld\n", endBlockAddress, Lurn->EndBlockAddr));
			LSCcbSetStatus(Ccb, CCB_STATUS_INVALID_COMMAND);
			LSCcbCompleteCcb(Ccb);
			break;
		}

		//
		//	Find the target LURN.
		//
		for(idx_child = 0; idx_child < Lurn->LurnChildrenCnt; idx_child ++) {
			ChildLurn = Lurn->LurnChildren[idx_child];

			if( startBlockAddress >= ChildLurn->StartBlockAddr &&
				startBlockAddress <= ChildLurn->EndBlockAddr) {
				break;
			}
		}
		if(ChildLurn == NULL || idx_child >= Lurn->LurnChildrenCnt) {
			KDPrintM(DBG_LURN_ERROR,("SCSIOP_WRITE/READ/VERIFY: Could not found child LURN. Ccb's ending sector:%ld\n", startBlockAddress));
			LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
			LSCcbCompleteCcb(Ccb);
			status = STATUS_SUCCESS;
			break;
		}

		//
		//	determine if need to split the CCB.
		//
		if(endBlockAddress <= ChildLurn->EndBlockAddr) {
			PCCB		NextCcb;
			PCDB		pCdb;
			//
			//	One CCB
			//	Allocate one CCB for the children
			//
			KDPrintM(DBG_LURN_TRACE,("SCSIOP_WRITE/READ/VERIFY: found LURN#%d\n", idx_child));

			status = LSCcbAllocate(&NextCcb);
			if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_ERROR, ("SCSIOP_WRITE/READ/VERIFY: LSCcbAllocate failed.\n"));
				LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
				LSCcbCompleteCcb(Ccb);
				break;
			}
			Ccb->AssociateCount = 1;
			LSCcbInitializeByCcb(Ccb, Lurn->LurnChildren[idx_child], NextCcb);
			LSCcbSetFlag(NextCcb, CCB_FLAG_ASSOCIATE|CCB_FLAG_ALLOCATED);
			LSCcbSetFlag(NextCcb, Ccb->Flags&CCB_FLAG_SYNCHRONOUS);
			NextCcb->AssociateID = (USHORT)idx_child;
			LSCcbSetCompletionRoutine(NextCcb, LurnAggrCcbCompletion, Ccb);

			// start address
			startBlockAddress -= ChildLurn->StartBlockAddr;
			pCdb = (PCDB)&NextCcb->Cdb[0];

			LSCcbSetLogicalAddress(pCdb, (UINT32)startBlockAddress);

			status = LurnRequest(ChildLurn, NextCcb);
			if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_ERROR, ("SCSIOP_WRITE/READ/VERIFY: LurnRequest to Child#%d failed.\n", idx_child));
				LSCcbFree(NextCcb);
				LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
				LSCcbCompleteCcb(Ccb);
				break;
			}

		} else {
			PCCB		NextCcb[2];
			UINT64		firstStartBlockAddress;
			ULONG		firstTransferBlocks;
			ULONG		secondTransferBlocks;
			PCDB		pCdb;
			LONG		idx_ccb;
			UINT64		BlockAddress_0;

			KDPrintM(DBG_LURN_ERROR,("SCSIOP_WRITE/READ/VERIFY: found LURN#%d, #%d\n", idx_child, idx_child+1));
			if(idx_child+1 >= Lurn->LurnChildrenCnt) {
				KDPrintM(DBG_LURN_ERROR,("SCSIOP_WRITE/READ/VERIFY: TWO CCB: no LURN#%d\n", idx_child+1));
				LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
				LSCcbCompleteCcb(Ccb);
				break;
			}
			//
			//	Two CCB
			//	Allocate Two CCBs for the children
			//
			for(idx_ccb = 0; idx_ccb < 2; idx_ccb++) {
				status = LSCcbAllocate(&NextCcb[idx_ccb]);
				if(!NT_SUCCESS(status)) {
					LONG	idx;

					KDPrintM(DBG_LURN_ERROR, ("SCSIOP_WRITE/READ/VERIFY: LSCcbAllocate failed.\n"));
					for(idx = 0; idx < idx_ccb; idx++) {
						LSCcbFree(NextCcb[idx]);
					}

					LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
					LSCcbCompleteCcb(Ccb);
					return STATUS_SUCCESS;
				}
				LSCcbInitializeByCcb(Ccb, Lurn->LurnChildren[idx_ccb], NextCcb[idx_ccb]);
				LSCcbSetFlag(NextCcb[idx_ccb], CCB_FLAG_ASSOCIATE|CCB_FLAG_ALLOCATED);
				LSCcbSetFlag(NextCcb[idx_ccb], Ccb->Flags&CCB_FLAG_SYNCHRONOUS);
				NextCcb[idx_ccb]->AssociateID = (USHORT)(idx_child + idx_ccb);
				LSCcbSetCompletionRoutine(NextCcb[idx_ccb], LurnAggrCcbCompletion, Ccb);
			}

			//
			//	set associate counter
			//
			Ccb->AssociateCount = 2;
			//
			//	first LURN
			//
			ChildLurn = Lurn->LurnChildren[idx_child];
			pCdb = (PCDB)&NextCcb[0]->Cdb[0];

			// start address
			firstStartBlockAddress = startBlockAddress - ChildLurn->StartBlockAddr;

			LSCcbSetLogicalAddress(pCdb, firstStartBlockAddress);

			// transfer length
			firstTransferBlocks = (USHORT)(ChildLurn->EndBlockAddr - startBlockAddress + 1);

			LSCcbSetTransferLength(pCdb, firstTransferBlocks);

			NextCcb[0]->DataBufferLength = firstTransferBlocks * Lurn->ChildBlockBytes;
			NextCcb[0]->AssociateID = 0;

			status = LurnRequest(ChildLurn, NextCcb[0]);
			if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_ERROR, ("SCSIOP_WRITE/READ/VERIFY: LurnRequest to the first child#%d failed.\n", idx_child));

				LSCcbFree(NextCcb[0]);
				LSCcbFree(NextCcb[1]);

				LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
				LSCcbCompleteCcb(Ccb);
				break;
			}

			//
			//	second LURN
			//
			ChildLurn = Lurn->LurnChildren[idx_child + 1];
			pCdb = (PCDB)&NextCcb[1]->Cdb[0];

			// start address
			BlockAddress_0 = 0;

			LSCcbSetLogicalAddress(pCdb, BlockAddress_0);
			
			// transfer length
			secondTransferBlocks = transferBlocks - firstTransferBlocks;
			ASSERT(secondTransferBlocks > 0);

			LSCcbSetTransferLength(pCdb, secondTransferBlocks);

			NextCcb[1]->DataBufferLength = secondTransferBlocks * Lurn->ChildBlockBytes;
			NextCcb[1]->DataBuffer = ((PUCHAR)Ccb->DataBuffer) + (firstTransferBlocks * Lurn->ChildBlockBytes);	// offset 18
			NextCcb[1]->AssociateID = 1;

			status = LurnRequest(ChildLurn, NextCcb[1]);
			if(!NT_SUCCESS(status)) {
				KDPrintM(DBG_LURN_ERROR, ("SCSIOP_WRITE/READ/VERIFY: LurnRequest to the child#%d failed.\n", idx_child));
				LSCcbSetStatus(NextCcb[1], CCB_STATUS_INVALID_COMMAND);
				LSCcbSetNextStackLocation(NextCcb[1]);
				LSCcbCompleteCcb(NextCcb[1]);
				status = STATUS_SUCCESS;
				break;
			}

		}
		break;
	}

	case SCSIOP_INQUIRY: {
		INQUIRYDATA			inquiryData;
		UCHAR				Model[16] = AGGR_MODEL_NAME;


		KDPrintM(DBG_LURN_INFO,("SCSIOP_INQUIRY Ccb->Lun = 0x%x\n", Ccb->LurId[2]));

		RtlZeroMemory(Ccb->DataBuffer, Ccb->DataBufferLength);
		RtlZeroMemory(&inquiryData, sizeof(INQUIRYDATA));

		//
		//	We don't support EVPD(enable vital product data).
		//
		if(Ccb->Cdb[1]  & CDB_INQUIRY_EVPD) {

			KDPrintM(DBG_LURN_ERROR,("SCSIOP_INQUIRY: got EVPD. Not supported.\n"));

			LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
			LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
			LSCcbCompleteCcb(Ccb);
			break;
		}

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
			Model,
			16
			);

		RtlCopyMemory(
					inquiryData.ProductRevisionLevel,
					PRODUCT_REVISION_LEVEL,
					(strlen(PRODUCT_REVISION_LEVEL)+1) < 4 ?  
							(strlen(PRODUCT_REVISION_LEVEL)+1) : 4
					);

		RtlMoveMemory (
					Ccb->DataBuffer,
					&inquiryData,
					Ccb->DataBufferLength > sizeof (INQUIRYDATA) ? 
					sizeof (INQUIRYDATA) : 
					Ccb->DataBufferLength
					);

		LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
		LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
		LSCcbCompleteCcb(Ccb);
		break;
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
			readCapacityData->LogicalBlockAddress = 0xffffffff;
		}

		blockSize = Lurn->BlockBytes;
		REVERSE_BYTES(&readCapacityData->BytesPerBlock, &blockSize);

		LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
		LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
		LSCcbCompleteCcb(Ccb);
		break;
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

		blockSize = Lurn->BlockBytes;
		REVERSE_BYTES(&readCapacityDataEx->BytesPerBlock, &blockSize);

		LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
		LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
		LSCcbCompleteCcb(Ccb);
		break;
	}
	case SCSIOP_MODE_SENSE:
	{
		LurnAssocModeSense(Lurn, Ccb);
		break;
	}
	case SCSIOP_MODE_SELECT:
	{
		LurnAssocModeSelect(Lurn, Ccb);
		break;
	}
	default: {
		//
		//	send to all child LURNs.
		//
		status = LurnAssocSendCcbToAllChildren(
												Lurn,
												Ccb,
												LurnAggrCcbCompletion,
												NULL,
												NULL,
												FALSE
								);
		break;
		}
	}

	return STATUS_SUCCESS;
}



NTSTATUS
LurnAggrRequest(
		PLURELATION_NODE	Lurn,
		PCCB				Ccb
	) {
	NTSTATUS				status;

	//
	//	dispatch a request
	//
	switch(Ccb->OperationCode) {
	case CCB_OPCODE_EXECUTE:
		KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_EXECUTE!\n"));
		LurnAggrExecute(Lurn, Ccb);
		break;
	case CCB_OPCODE_FLUSH:
		KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_FLUSH!\n"));
		// Nothing to do for aggregated disks. This flush is sent to controller side cache.		
		LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
		LSCcbCompleteCcb(Ccb);
		break;
	case CCB_OPCODE_SHUTDOWN:
		KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_SHUTDOWN!\n"));
		LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
		LSCcbCompleteCcb(Ccb);
		break;

	//
	//	Send to all LURNs
	//
	case CCB_OPCODE_ABORT_COMMAND:
	case CCB_OPCODE_RESETBUS:
	case CCB_OPCODE_STOP:
	case CCB_OPCODE_RESTART:
	case CCB_OPCODE_NOOP:
		{

			//
			//	Check to see if the CCB is coming for only this LURN.
			//
			if(LSCcbIsFlagOn(Ccb, CCB_FLAG_DONOT_PASSDOWN)) {
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				LSCcbCompleteCcb(Ccb);
				break;
			}
			status = LurnAssocSendCcbToAllChildren(Lurn, Ccb, LurnAggrCcbCompletion, NULL, NULL, FALSE);
			break;
		}

	case CCB_OPCODE_UPDATE:
		{

			//
			//	Check to see if the CCB is coming for only this LURN.
			//
			if(LSCcbIsFlagOn(Ccb, CCB_FLAG_DONOT_PASSDOWN)) {
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				LSCcbCompleteCcb(Ccb);
				break;
			}
			status = LurnAssocSendCcbToAllChildren(Lurn, Ccb, LurnRAIDUpdateCcbCompletion, NULL, NULL, TRUE);
			break;
		}
	
	case CCB_OPCODE_QUERY:
		status = LurnAssocQuery(Lurn, LurnAggrCcbCompletion, Ccb);
		break;

	default:
		KDPrintM(DBG_LURN_TRACE, ("INVALID COMMAND\n"));
		LSCcbSetStatus(Ccb, CCB_STATUS_INVALID_COMMAND);
		LSCcbCompleteCcb(Ccb);
		break;
	}

	return STATUS_SUCCESS;
}


#if 0
NTSTATUS
LurnFaultTolerantCcbCompletion(
		IN PCCB	Ccb,
		IN PCCB	OriginalCcb
	) {
	KIRQL	oldIrql;
	LONG	ass;

	//
	//	Higher number of priority will overwrite CcbStatus.
	//
	//	
	//	Mirroring priority
	//
	//	Other error codes when one child is in error							: 0
	//	CCB_STATUS_STOP when one child is in error								: 1
	//	CCB_STATUS_SUCCESS														: 2
	//	CCB_STATUS_BUSY															: 3
	//	Other error codes when both children are in error						: 4
	//	CCB_STATUS_STOP/CCB_STATUS_NOT_EXIST when both children are in error	: 5
	//
	ACQUIRE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, &oldIrql);
	switch(Ccb->CcbStatus) {

	case CCB_STATUS_SUCCESS:	// priority 2
		break;

	case CCB_STATUS_BUSY:		// priority 3
		if(OriginalCcb->CcbStatus == CCB_STATUS_SUCCESS) {
			ASSERT(OriginalCcb->Srb);
			OriginalCcb->CcbStatus = CCB_STATUS_BUSY;
		}
		break;

	case CCB_STATUS_STOP:		// priority 1/5

		if(!LSCcbIsStatusFlagOn(OriginalCcb, CCBSTATUS_FLAG_LURN_STOP)) {
			LSCcbSetStatusFlag(OriginalCcb, CCBSTATUS_FLAG_LURN_STOP);	// priority 1
			LSCcbSetStatusFlag(OriginalCcb, CCBSTATUS_FLAG_LURN_DEGRADED);
		} else {
			//
			//	Two children stopped!
			//
			OriginalCcb->CcbStatus = CCB_STATUS_STOP;	// priority 5
		}
		break;
	case CCB_STATUS_NOT_EXIST:
		if(LSCcbIsStatusFlagOn(OriginalCcb, CCBSTATUS_FLAG_LURN_DEGRADED)) {
			//
			//	Two children stopped!
			//
			OriginalCcb->CcbStatus = CCB_STATUS_STOP;	// priority 5
			break;
		}
	default:					// priority 0/4
		KDPrintM(DBG_LURN_ERROR, ("ERROR: Ccb->CcbStatus= %x, AssociateID = %d\n",
								(int)Ccb->CcbStatus, (unsigned int)Ccb->AssociateID));

		if(!LSCcbIsStatusFlagOn(OriginalCcb, CCBSTATUS_FLAG_LURN_IN_ERROR)) {
			LSCcbSetStatusFlag(OriginalCcb, CCBSTATUS_FLAG_LURN_IN_ERROR);	// priority 0
		} else {
			//
			//	Two children have an error!
			//
			OriginalCcb->CcbStatus = Ccb->CcbStatus;	// 	// priority 4
		}
		break;
	}

	LSCcbSetStatusFlag(	OriginalCcb,
						Ccb->CcbStatusFlags & CCBSTATUS_FLAG_ASSIGNMASK
		);
	RELEASE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, oldIrql);


	//
	//	Complete the original CCB
	//
	ass = InterlockedDecrement(&OriginalCcb->AssociateCount);
	ASSERT(ass >= 0);
	if(ass != 0) {
		return STATUS_SUCCESS;
	}
	LSCcbCompleteCcb(OriginalCcb);

	return STATUS_SUCCESS;
}
#endif
//////////////////////////////////////////////////////////////////////////
//
//	RAID0 Lurn
//

NTSTATUS
LurnRAID0Initialize(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
	) 
{
	PRAID_INFO pRaidInfo = NULL;
	ULONG ulDataBufferSize, ulDataBufferSizePerDisk;
	NTSTATUS ntStatus;

	UNREFERENCED_PARAMETER(LurnDesc);

	//
	// Determine block bytes.
	//

	Lurn->ChildBlockBytes = LurnAsscGetChildBlockBytes(Lurn);
	Lurn->BlockBytes = Lurn->ChildBlockBytes * Lurn->LurnChildrenCnt;

//	Raid Information
	Lurn->LurnRAIDInfo = ExAllocatePoolWithTag(NonPagedPool, sizeof(RAID_INFO), 
		RAID_INFO_POOL_TAG);

	if(NULL == Lurn->LurnRAIDInfo)
	{
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		goto out;
	}

	pRaidInfo = Lurn->LurnRAIDInfo;

	RtlZeroMemory(pRaidInfo, sizeof(RAID_INFO));

	KeInitializeSpinLock(&pRaidInfo->SpinLock);

//	LurnSetRaidInfoStatus(pRaidInfo, RAID_STATUS_NORMAL);

//	Data buffer shuffled
	pRaidInfo->MaxDataSendLength = LurnDesc->MaxDataSendLength;
	pRaidInfo->MaxDataRecvLength = LurnDesc->MaxDataRecvLength;

	ASSERT(0 == (pRaidInfo->MaxDataSendLength/Lurn->ChildBlockBytes % Lurn->LurnChildrenCnt));
	ASSERT(0 == (pRaidInfo->MaxDataRecvLength/Lurn->ChildBlockBytes % Lurn->LurnChildrenCnt));

	// service sets max blocks per request as full devices size of 1 I/O
	// ex) 64KB per unit device and 2 devices -> MBR = 128KB;
	if(pRaidInfo->MaxDataSendLength > pRaidInfo->MaxDataRecvLength) {
		ulDataBufferSizePerDisk = (pRaidInfo->MaxDataSendLength / Lurn->LurnChildrenCnt);
	} else {
		ulDataBufferSizePerDisk = (pRaidInfo->MaxDataRecvLength / Lurn->LurnChildrenCnt);
	}

	ulDataBufferSize = ulDataBufferSizePerDisk * Lurn->LurnChildrenCnt;

	ASSERT(sizeof(pRaidInfo->DataBufferLookaside) >= sizeof(NPAGED_LOOKASIDE_LIST));
	ExInitializeNPagedLookasideList(
		&pRaidInfo->DataBufferLookaside,
		NULL, // PALLOCATE_FUNCTION  Allocate  OPTIONAL
		NULL, // PFREE_FUNCTION  Free  OPTIONAL
		0, // Flags Reserved. Must be zero
		ulDataBufferSizePerDisk,
		RAID_DATA_BUFFER_POOL_TAG,
		0 // Depth Reserved. Must be zero
		);

	ntStatus = STATUS_SUCCESS;
out:
	if(!NT_SUCCESS(ntStatus))
	{
		if(Lurn->LurnRAIDInfo)
		{
			SAFE_FREE_POOL_WITH_TAG(Lurn->LurnRAIDInfo, RAID_INFO_POOL_TAG);
		}
	}


	return ntStatus;
}

NTSTATUS
LurnRAID0Destroy(
		PLURELATION_NODE Lurn
	) 
{
	PRAID_INFO pRaidInfo;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL) ;
	ASSERT(Lurn->LurnRAIDInfo);

	pRaidInfo = Lurn->LurnRAIDInfo;

	ExDeleteNPagedLookasideList(
		&pRaidInfo->DataBufferLookaside);

	ASSERT(pRaidInfo);
	SAFE_FREE_POOL_WITH_TAG(pRaidInfo, RAID_INFO_POOL_TAG);

	return STATUS_SUCCESS ;
}

NTSTATUS
LurnRAID0CcbCompletion(
		IN PCCB	Ccb,
		IN PCCB	OriginalCcb
	) {
	KIRQL	oldIrql;
	LONG	ass;
	NTSTATUS status;
	PRAID_INFO pRaidInfo;
	PLURELATION_NODE pLurnOriginal;

	status = STATUS_SUCCESS;

	ACQUIRE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, &oldIrql);

	pLurnOriginal = OriginalCcb->CcbCurrentStackLocation->Lurn;
	pRaidInfo = pLurnOriginal->LurnRAIDInfo;

	switch(Ccb->CcbStatus) {

	case CCB_STATUS_SUCCESS:
		if (CCB_OPCODE_EXECUTE == OriginalCcb->OperationCode)
		{
			switch(OriginalCcb->Cdb[0])
			{
			case 0x3E:		// READ_LONG
			case SCSIOP_READ:
			case SCSIOP_READ16:				
				{
					ULONG i, j, BlocksPerDisk;

					KDPrintM(DBG_LURN_NOISE,("SCSIOP_READ\n"));
					ASSERT(Ccb->DataBuffer);

					BlocksPerDisk = Ccb->DataBufferLength / pLurnOriginal->ChildBlockBytes;
					i = Ccb->AssociateID;
					for(j = 0; j < BlocksPerDisk; j++)
					{
						RtlCopyMemory( // Copy back
							(PCHAR)OriginalCcb->DataBuffer + (i + j * (pLurnOriginal->LurnChildrenCnt)) * pLurnOriginal->ChildBlockBytes,
							(PCHAR)Ccb->DataBuffer + (j * pLurnOriginal->ChildBlockBytes),
							pLurnOriginal->ChildBlockBytes);
					}
				}
				break;
			}
		}
		break;

	case CCB_STATUS_BUSY:
		if(OriginalCcb->CcbStatus == CCB_STATUS_SUCCESS) {
//			ASSERT(OriginalCcb->Srb);
			OriginalCcb->CcbStatus = CCB_STATUS_BUSY;
		}
		break;

	case CCB_STATUS_STOP:
		OriginalCcb->CcbStatus = CCB_STATUS_STOP;
		break;
	case CCB_STATUS_NOT_EXIST:
		if(LSCcbIsStatusFlagOn(OriginalCcb, CCBSTATUS_FLAG_LURN_DEGRADED)) {
			OriginalCcb->CcbStatus = CCB_STATUS_STOP;
			break;
		}
	default:
		KDPrintM(DBG_LURN_ERROR, ("ERROR: Ccb->CcbStatus= %x, AssociateID = %d\n",
								(int)Ccb->CcbStatus, (unsigned int)Ccb->AssociateID));

		OriginalCcb->CcbStatus = Ccb->CcbStatus;
		break;
	}

	if(CCB_OPCODE_EXECUTE == OriginalCcb->OperationCode)
	{
		switch(OriginalCcb->Cdb[0])
		{
		case SCSIOP_WRITE:
		case SCSIOP_WRITE16:			
		case 0x3E:		// READ_LONG
		case SCSIOP_READ:
		case SCSIOP_READ16:
			KDPrintM(DBG_LURN_NOISE,("Release data buffer look aside (%p)\n", Ccb->DataBuffer));
			ASSERT(Ccb->DataBuffer);
			ExFreeToNPagedLookasideList(
				&pRaidInfo->DataBufferLookaside,
				Ccb->DataBuffer);

			Ccb->DataBuffer = NULL;
		}
	}

	LSCcbSetStatusFlag(	OriginalCcb,
						Ccb->CcbStatusFlags & CCBSTATUS_FLAG_ASSIGNMASK
		);
	RELEASE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, oldIrql);


	//
	//	Complete the original CCB
	//

	ass = InterlockedDecrement(&OriginalCcb->AssociateCount);
	ASSERT(ass >= 0);
	if(ass != 0) {
		return status;
	}

	//
	//	post-process for CCB_OPCODE_UPDATE
	//
	if(OriginalCcb->OperationCode == CCB_OPCODE_UPDATE &&
		OriginalCcb->CcbStatus == CCB_STATUS_SUCCESS
		) {
			PLURN_UPDATE	LurnUpdate;
			LurnUpdate = (PLURN_UPDATE)OriginalCcb->DataBuffer;

			if(LurnUpdate->UpdateClass == LURN_UPDATECLASS_WRITEACCESS_USERID) {
				PLURELATION_NODE	Lurn;
				//
				//	If this is root LURN, update LUR access right.
				//
				Lurn = OriginalCcb->CcbCurrentStackLocation->Lurn;
				ASSERT(Lurn);

				if(Lurn->LurnParent == 0 || Lurn->LurnParent == Lurn) {
					Lurn->Lur->EnabledNdasFeatures &= ~NDASFEATURE_SECONDARY;
					Lurn->AccessRight |= GENERIC_WRITE;
					KDPrintM(DBG_LURN_INFO,("Updated enabled feature: %08lx\n",Lurn->Lur->EnabledNdasFeatures));
				}
			}
	}

	LSCcbCompleteCcb(OriginalCcb);

	return STATUS_SUCCESS;
}

static
NTSTATUS
LurnRAID0Execute(
		PLURELATION_NODE		Lurn,
		IN	PCCB				Ccb
	)
{
	NTSTATUS			status = STATUS_SUCCESS;
	PRAID_INFO			pRaidInfo;

	pRaidInfo = Lurn->LurnRAIDInfo;

	switch(Ccb->Cdb[0]) {
	case SCSIOP_WRITE:
	case SCSIOP_WRITE16:		
		{
			UINT64 logicalBlockAddress;
			UINT32 transferBlocks;

			int DataBufferLengthPerDisk;
			ULONG BlocksPerDisk;
			register ULONG i, j;
			CUSTOM_DATA_BUFFER cdb;

			LSCcbGetAddressAndLength((PCDB)&Ccb->Cdb[0], &logicalBlockAddress, &transferBlocks);

			ASSERT(transferBlocks <= pRaidInfo->MaxDataSendLength/Lurn->BlockBytes);
	
			KDPrintM(DBG_LURN_NOISE,("W Ccb->DataBufferLength %d\n", Ccb->DataBufferLength));
			
			KDPrintM(DBG_LURN_NOISE,("SCSIOP_WRITE\n"));

			DataBufferLengthPerDisk = Ccb->DataBufferLength / Lurn->LurnChildrenCnt;
			BlocksPerDisk = DataBufferLengthPerDisk / Lurn->ChildBlockBytes;

			// create new data buffer and encrypt here.
			// new data buffer will be deleted at completion routine
			cdb.DataBufferCount = 0;
			for(i = 0; i < Lurn->LurnChildrenCnt; i++)
			{
				cdb.DataBuffer[i] = ExAllocateFromNPagedLookasideList(&pRaidInfo->DataBufferLookaside);
				ASSERT(cdb.DataBuffer[i]);
				if(!cdb.DataBuffer[i])
				{
					// free allocated buffers
					for(j = 0; j < i; j++)
					{
						ExFreeToNPagedLookasideList(
							&pRaidInfo->DataBufferLookaside,
							cdb.DataBuffer[i]);
						cdb.DataBuffer[i] = NULL;
					}

					Ccb->CcbStatus = CCB_STATUS_SUCCESS;
					status = STATUS_INSUFFICIENT_RESOURCES;

					KDPrintM(DBG_LURN_ERROR, ("ExAllocateFromNPagedLookasideList Failed[%d]\n", i));
					LSCcbCompleteCcb(Ccb);
					break;
				}
				
				cdb.DataBufferLength[i] = DataBufferLengthPerDisk;
				cdb.DataBufferCount++;
			}
			
			for(i = 0; i < Lurn->LurnChildrenCnt; i++)
			{
				for(j = 0; j < BlocksPerDisk; j++)
				{
					RtlCopyMemory(
						(PCHAR)cdb.DataBuffer[i] + j * Lurn->ChildBlockBytes,
						(PCHAR)Ccb->DataBuffer + (i + j * (Lurn->LurnChildrenCnt)) * Lurn->ChildBlockBytes,
						Lurn->ChildBlockBytes);
				}

			}

			//
			//	send to all child LURNs.
			//
			status = LurnAssocSendCcbToAllChildren(
													Lurn,
													Ccb,
													LurnRAID0CcbCompletion,
													&cdb,
													NULL,
													FALSE
									);

			if(!NT_SUCCESS(status))
			{
				for(i = 0; i < Lurn->LurnChildrenCnt; i++)
				{
					ExFreeToNPagedLookasideList(
						&pRaidInfo->DataBufferLookaside,
						cdb.DataBuffer[i]);
				}
			}
		}
		break;
	case SCSIOP_VERIFY:
	case SCSIOP_VERIFY16:
		status = LurnAssocSendCcbToAllChildren(
			Lurn,
			Ccb,
			LurnRAID0CcbCompletion,
			NULL,
			NULL,
			FALSE
			);
		break;
	case 0x3E:		// READ_LONG
	case SCSIOP_READ:
	case SCSIOP_READ16:
		{
			int DataBufferLengthPerDisk;
			ULONG i, j;
			CUSTOM_DATA_BUFFER cdb;

			DataBufferLengthPerDisk = Ccb->DataBufferLength / (Lurn->LurnChildrenCnt);
			KDPrintM(DBG_LURN_NOISE,("R Ccb->DataBufferLength %d\n", Ccb->DataBufferLength));

			cdb.DataBufferCount = 0;
			for(i = 0; i < Lurn->LurnChildrenCnt; i++)
			{
				cdb.DataBuffer[i] = ExAllocateFromNPagedLookasideList(&pRaidInfo->DataBufferLookaside);
				ASSERT(cdb.DataBuffer[i]);
				if(!cdb.DataBuffer[i])
				{
					// free allocated buffers
					for(j = 0; j < i; j++)
					{
						ExFreeToNPagedLookasideList(
							&pRaidInfo->DataBufferLookaside,
							cdb.DataBuffer[i]);
						cdb.DataBuffer[i] = NULL;
					}

					Ccb->CcbStatus = CCB_STATUS_SUCCESS;
					status = STATUS_INSUFFICIENT_RESOURCES;

					KDPrintM(DBG_LURN_ERROR, ("ExAllocateFromNPagedLookasideList Failed[%d]\n", i));
					LSCcbCompleteCcb(Ccb);
					break;
				}

				cdb.DataBufferLength[i] = DataBufferLengthPerDisk;
				cdb.DataBufferCount++;
			}

			cdb.DataBufferCount = i;
			
			status = LurnAssocSendCcbToAllChildren(
				Lurn,
				Ccb,
				LurnRAID0CcbCompletion,
				&cdb,
				NULL,
				FALSE
				);

			if(!NT_SUCCESS(status))
			{
				for(i = 0; i < Lurn->LurnChildrenCnt; i++)
				{
					ExFreeToNPagedLookasideList(
						&pRaidInfo->DataBufferLookaside,
						cdb.DataBuffer[i]);
				}
			}
		}
		break;
	case SCSIOP_INQUIRY: {
		INQUIRYDATA			inquiryData;
		UCHAR				Model[16] = RAID0_MODEL_NAME;


		KDPrintM(DBG_LURN_INFO,("SCSIOP_INQUIRY Ccb->Lun = 0x%x\n", Ccb->LurId[2]));
		//
		//	We don't support EVPD(enable vital product data).
		//
		if(Ccb->Cdb[1]  & CDB_INQUIRY_EVPD) {

			KDPrintM(DBG_LURN_ERROR,("SCSIOP_INQUIRY: got EVPD. Not supported.\n"));

			LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
			LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
			LSCcbCompleteCcb(Ccb);
			break;
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
			Model,
			16
			);

		RtlCopyMemory(
					inquiryData.ProductRevisionLevel,
					PRODUCT_REVISION_LEVEL,
					(strlen(PRODUCT_REVISION_LEVEL)+1) < 4 ?  
							(strlen(PRODUCT_REVISION_LEVEL)+1) : 4
					);

		RtlMoveMemory (
					Ccb->DataBuffer,
					&inquiryData,
					Ccb->DataBufferLength > sizeof (INQUIRYDATA) ? 
					sizeof (INQUIRYDATA) : 
					Ccb->DataBufferLength
					);

		LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
		LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;
		
		status = STATUS_SUCCESS;
		LSCcbCompleteCcb(Ccb);
		break;
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
			readCapacityData->LogicalBlockAddress = 0xffffffff;
		}

		blockSize = Lurn->BlockBytes;
		REVERSE_BYTES(&readCapacityData->BytesPerBlock, &blockSize);

		LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
		LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
		LSCcbCompleteCcb(Ccb);
		break;
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

		blockSize = Lurn->BlockBytes;
		REVERSE_BYTES(&readCapacityDataEx->BytesPerBlock, &blockSize);

		LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
		LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
		LSCcbCompleteCcb(Ccb);
		break;
	}
	
	case SCSIOP_MODE_SENSE:
	{
		LurnAssocModeSense(Lurn, Ccb);
		break;
	}
#if 0
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
				KDPrintM(DBG_LURN_INFO, 
				("SCSIOP_MODE_SENSE: MODE_DSP_WRITE_PROTECT\n"));
				parameterHeader->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;
	
				if(LSCcbIsFlagOn(Ccb, CCB_FLAG_W2K_READONLY_PATCH) ||
					LSCcbIsFlagOn(Ccb, CCB_FLAG_ALLOW_WRITE_IN_RO_ACCESS))
					parameterHeader->DeviceSpecificParameter &= ~MODE_DSP_WRITE_PROTECT;
			} else
				parameterHeader->DeviceSpecificParameter &= ~MODE_DSP_WRITE_PROTECT;
	
			parameterHeader->BlockDescriptorLength = sizeof(MODE_PARAMETER_BLOCK);

			//
			// Make Block.
			//
			BlockCount = (ULONG)(Lurn->EndBlockAddr - Lurn->StartBlockAddr + 1);
			parameterBlock->DensityCode = 0;	// It is Reserved for direct access devices.
			parameterBlock->BlockLength[0] = (BYTE)(Lurn->BlockBytes>>16);
			parameterBlock->BlockLength[1] = (BYTE)(Lurn->BlockBytes>>8);
			parameterBlock->BlockLength[2] = (BYTE)(Lurn->BlockBytes);

			LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;
			LSCcbCompleteCcb(Ccb);
		} else {
			KDPrintM(DBG_LURN_TRACE,
						("SCSIOP_MODE_SENSE: unsupported pagecode:%x\n", (ULONG)Cdb->MODE_SENSE.PageCode));
			LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
			LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
			Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;	//SRB_STATUS_SUCCESS;
			LSCcbCompleteCcb(Ccb);
		}
		break; 
#endif		
	case SCSIOP_MODE_SELECT:
		LurnAssocModeSelect(Lurn, Ccb);
		break;
	default:
		//
		//	send to all child LURNs.
		//	Set WriteVerify completion routine to CCB
		//		it guarantees CCB_STATUS_SUCCESS when least one LURN is working fine.
		//

		status = LurnAssocSendCcbToAllChildren(
												Lurn,
												Ccb,
												LurnRAID0CcbCompletion,
												NULL,
												NULL,
												FALSE
								);
		break;

	}

	return status;
}

NTSTATUS
LurnRAID0Request(
		PLURELATION_NODE	Lurn,
		PCCB				Ccb
	) {
	NTSTATUS				status;

	//
	//	dispatch a request
	//
	KDPrintM(DBG_LURN_TRACE, ("LurnRAID0Request!\n"));

	switch(Ccb->OperationCode) {
	case CCB_OPCODE_EXECUTE:

		KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_EXECUTE!\n"));
		LurnRAID0Execute(Lurn, Ccb);
		break;
	case CCB_OPCODE_FLUSH:
		KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_FLUSH!\n"));
		// Nothing to do for aggregated disks. This flush is sent to controller side cache.		
		LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
		LSCcbCompleteCcb(Ccb);
		break;
	case CCB_OPCODE_SHUTDOWN:
		KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_SHUTDOWN!\n"));
		LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
		LSCcbCompleteCcb(Ccb);
		break;


	//
	//	Send to all LURNs
	//
	case CCB_OPCODE_ABORT_COMMAND:
	case CCB_OPCODE_RESETBUS:
	case CCB_OPCODE_RESTART:
	case CCB_OPCODE_STOP:
	case CCB_OPCODE_NOOP:
		{
			//
			//	Check to see if the CCB is coming for only this LURN.
			//
			if(LSCcbIsFlagOn(Ccb, CCB_FLAG_DONOT_PASSDOWN)) {
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				LSCcbCompleteCcb(Ccb);
				break;
			}
			status = LurnAssocSendCcbToAllChildren(Lurn, Ccb, LurnRAID0CcbCompletion, NULL, NULL, FALSE);
			break;
		}

	case CCB_OPCODE_UPDATE:
		{
			//
			//	Check to see if the CCB is coming for only this LURN.
			//
			if(LSCcbIsFlagOn(Ccb, CCB_FLAG_DONOT_PASSDOWN)) {
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				LSCcbCompleteCcb(Ccb);
				break;
			}
			status = LurnAssocSendCcbToAllChildren(Lurn, Ccb, LurnRAIDUpdateCcbCompletion, NULL, NULL, TRUE);
			break;
		}

	case CCB_OPCODE_QUERY:
		status = LurnAssocQuery(Lurn, LurnAggrCcbCompletion, Ccb);
		break;

	default:
		KDPrintM(DBG_LURN_TRACE, ("INVALID COMMAND\n"));
		LSCcbSetStatus(Ccb, CCB_STATUS_INVALID_COMMAND);
		LSCcbCompleteCcb(Ccb);
		break;
	}

	return STATUS_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
//
//	RAID1 Lurn
//

NTSTATUS
LurnRAID1RInitialize(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
	) 
{
	PRAID_INFO pRaidInfo = NULL;
	NTSTATUS ntStatus;

	ASSERT(KeGetCurrentIrql() ==  PASSIVE_LEVEL);

	KDPrintM(DBG_LURN_INFO, ("LurnDesc->LurnType = %d\n", LurnDesc->LurnType));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->LurnId = %d\n", LurnDesc->LurnId));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->StartBlockAddr = 0x%I64x\n", LurnDesc->StartBlockAddr));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->EndBlockAddr = 0x%I64x\n", LurnDesc->EndBlockAddr));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->UnitBlocks = 0x%I64x\n", LurnDesc->UnitBlocks));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->MaxDataSendLength = %d bytes\n", LurnDesc->MaxDataSendLength));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->MaxDataRecvLength = %d bytes\n", LurnDesc->MaxDataRecvLength));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->LurnOptions = %d\n", LurnDesc->LurnOptions));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->LurnParent = %d\n", LurnDesc->LurnParent));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->LurnChildrenCnt = %d\n", LurnDesc->LurnChildrenCnt));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->LurnChildren = 0x%p\n", LurnDesc->LurnChildren));

	//
	// Determine block bytes.
	//

	Lurn->ChildBlockBytes = LurnAsscGetChildBlockBytes(Lurn);
	Lurn->BlockBytes = Lurn->ChildBlockBytes;

	//	Raid Information
	Lurn->LurnRAIDInfo = ExAllocatePoolWithTag(NonPagedPool, sizeof(RAID_INFO),
		RAID_INFO_POOL_TAG);

	if(NULL == Lurn->LurnRAIDInfo)
	{
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		goto out;
	}

	pRaidInfo = Lurn->LurnRAIDInfo;

	RtlZeroMemory(pRaidInfo, sizeof(RAID_INFO));

	KeInitializeSpinLock(&pRaidInfo->SpinLock);

	// set spare disk count
	pRaidInfo->nDiskCount = LurnDesc->LurnChildrenCnt - LurnDesc->LurnInfoRAID.nSpareDisk;
	pRaidInfo->nSpareDisk = LurnDesc->LurnInfoRAID.nSpareDisk;
	pRaidInfo->SectorsPerBit = LurnDesc->LurnInfoRAID.SectorsPerBit;
	pRaidInfo->RaidSetId = LurnDesc->LurnInfoRAID.RaidSetId;
	pRaidInfo->ConfigSetId = LurnDesc->LurnInfoRAID.ConfigSetId;
	
	if(!pRaidInfo->SectorsPerBit)
	{
		KDPrintM(DBG_LURN_ERROR, ("SectorsPerBit is zero!\n"));

		ntStatus = STATUS_INVALID_PARAMETER;
		goto out;
	}

	pRaidInfo->MaxDataSendLength = LurnDesc->MaxDataSendLength;
	pRaidInfo->MaxDataRecvLength = LurnDesc->MaxDataRecvLength;

	// Always create draid client.
	ntStatus = DraidClientStart(Lurn); 
	if (!NT_SUCCESS(ntStatus)) {
		goto out;
	}

	ntStatus = STATUS_SUCCESS;
out:
	if(!NT_SUCCESS(ntStatus))
	{
		if(Lurn->LurnRAIDInfo)
		{
			SAFE_FREE_POOL_WITH_TAG(Lurn->LurnRAIDInfo, RAID_INFO_POOL_TAG);
		}
	}
	

	return ntStatus;
}

NTSTATUS
LurnRAID1RDestroy(
		PLURELATION_NODE Lurn
	) 
{
	PRAID_INFO pRaidInfo;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL) ;
	ASSERT(Lurn->LurnRAIDInfo);

	pRaidInfo = Lurn->LurnRAIDInfo;
	ASSERT(pRaidInfo);

	SAFE_FREE_POOL_WITH_TAG(pRaidInfo, RAID_INFO_POOL_TAG);

	return STATUS_SUCCESS ;
}

//
// to do: restruct this function!
//
NTSTATUS
LurnRAID1RCcbCompletion(
		IN PCCB	Ccb,
		IN PCCB	OriginalCcb
	)
{
	KIRQL	oldIrql;
	LONG	AssocCount;
	NTSTATUS status;
	PRAID_INFO pRaidInfo;
	PLURELATION_NODE pLurnOriginal;
	PLURELATION_NODE pLurnCurrent;
	PDRAID_CLIENT_INFO pClient;
	UINT32 DraidStatus;

	status = STATUS_SUCCESS;

	ACQUIRE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, &oldIrql);

	pLurnOriginal = OriginalCcb->CcbCurrentStackLocation->Lurn;
	pLurnCurrent = Ccb->CcbCurrentStackLocation->Lurn;
	ASSERT(pLurnOriginal);
	pRaidInfo = pLurnOriginal->LurnRAIDInfo;
	ACQUIRE_DPC_SPIN_LOCK(&pRaidInfo->SpinLock);
	pClient = pRaidInfo->pDraidClient;
	if (pClient == NULL) {
		// Client is alreay terminated.
		DraidStatus = DRIX_RAID_STATUS_TERMINATED;
	} else {
		DraidStatus = pClient->DRaidStatus;
		DraidClientUpdateNodeFlags(pClient, pLurnCurrent, 0, 0);
	}
	RELEASE_DPC_SPIN_LOCK(&pRaidInfo->SpinLock);


	// 
	// Find proper Ccbstatus based on OriginalCcb->CcbStatus(Empty or may contain first completed child Ccb's staus) and this Ccb.
	//
#if 0
	if (Ccb->CcbStatus != CCB_STATUS_SUCCESS) {
		KDPrintM(DBG_LURN_INFO, ("LurnRAID1RCcbCompletion: CcbStatus = %x\n", Ccb->CcbStatus));
	}
#endif	
	switch(Ccb->Cdb[0]) {
	case 0x3E:		// READ_LONG
	case SCSIOP_READ:
	case SCSIOP_READ16:	
		//
		// Read request is sent to only one target.
		// If success, pass through to original ccb
		// If error and 
		// 	another target is running state, return busy to make upper layer to retry to another host.
		// 	another target is down, pass this Ccb status to original ccb
		//
		if (Ccb->CcbStatus == CCB_STATUS_SUCCESS) {
			OriginalCcb->CcbStatus = Ccb->CcbStatus;
		break;
		} else {
			if (pClient && pClient->DRaidStatus == DRIX_RAID_STATUS_NORMAL) {
				// Maybe another node is in running state. Hope that node can handle request and return busy
				ASSERT(OriginalCcb->Srb || OriginalCcb->OperationCode == CCB_OPCODE_RESETBUS);
				KDPrintM(DBG_LURN_INFO, ("Read on RAID1 failed. Enter emergency and return busy for retrial on redundent target\n"));

			OriginalCcb->CcbStatus = CCB_STATUS_BUSY;
			} else {
				// Error in degraded mode. Pass error 
				KDPrintM(DBG_LURN_INFO, ("Read on RAID1 failed and redundent target is in defective. Returning error %x\n", Ccb->CcbStatus));
				// 
				// No other target is alive. Pass error including sense data
				//
				OriginalCcb->CcbStatus = Ccb->CcbStatus;

				if(OriginalCcb->SenseBuffer != NULL) {
					RtlCopyMemory(OriginalCcb->SenseBuffer, Ccb->SenseBuffer, Ccb->SenseDataLength);
				}
			}
		}
		break;
	case SCSIOP_WRITE:
	case SCSIOP_WRITE16:
	case SCSIOP_VERIFY:
	case SCSIOP_VERIFY16:		 // fall through
	default:
			//
		// CCB Status priority(Higher level of error override another device's error) : In high to low order.
		//	Etc error(Invalid command, out of bound, unknown error)
		//	CCB_STATUS_COMMAND_FAILED(Errors that upper layer need to know about error - bad sector, etc)
		//	CCB_STATUS_BUSY: (lower this status to below success priority to improve responsiveness??)
		//	CCB_STATUS_SUCCES: 
		//	CCB_STATUS_STOP/CCB_STATUS_NOT_EXIST/CCB_STATUS_COMMUNICATION_ERROR: Enter emergency if in normal status.
		//														Return as success if another target is success, or 
			//
		switch(Ccb->CcbStatus) {
		case CCB_STATUS_SUCCESS:
			if (OriginalCcb->CcbStatus == CCB_STATUS_STOP ||
				OriginalCcb->CcbStatus == CCB_STATUS_NOT_EXIST ||
				OriginalCcb->CcbStatus == CCB_STATUS_COMMUNICATION_ERROR) {

				// Make upper layer retry.
				OriginalCcb->CcbStatus = CCB_STATUS_BUSY;

				ACQUIRE_DPC_SPIN_LOCK(&pRaidInfo->SpinLock);
				if (pRaidInfo->pDraidClient) {
					pClient = pRaidInfo->pDraidClient;
					ACQUIRE_DPC_SPIN_LOCK(&pClient->SpinLock);
					if (pClient->DRaidStatus == DRIX_RAID_STATUS_DEGRADED) {
						if (!(pClient->NodeFlagsRemote[pLurnCurrent->LurnChildIdx] & 
							(DRIX_NODE_FLAG_STOP|DRIX_NODE_FLAG_DEFECTIVE | DRIX_NODE_FLAG_OUT_OF_SYNC))) {
							// Succeeded node is none-fault node. It is okay.
							OriginalCcb->CcbStatus = CCB_STATUS_SUCCESS;
						} else {
							// None-fault node has failed, this cause RAID fail. Need to stop
							KDPrintM(DBG_LURN_ERROR, ("Non-defective target %d failed. RAID failure\n", pLurnCurrent->LurnChildIdx));
							OriginalCcb->CcbStatus = CCB_STATUS_STOP;
				}
					} else {
						// No target should fail in other cases. If so, RAID status will be changed. Wait for it returning busy.
						KDPrintM(DBG_LURN_ERROR, ("Ccb for target %d failed not in degraded mode. Returning busy.\n", pLurnCurrent->LurnChildIdx));
						OriginalCcb->CcbStatus = CCB_STATUS_BUSY;						
					}
					RELEASE_DPC_SPIN_LOCK(&pClient->SpinLock);
				} else {
					KDPrintM(DBG_LURN_ERROR, ("RAID client already terminated\n"));
					// RAID has terminated. Pass status
					OriginalCcb->CcbStatus = CCB_STATUS_STOP;
				}
				RELEASE_DPC_SPIN_LOCK(&pRaidInfo->SpinLock);

			} else if (OriginalCcb->CcbStatus == CCB_STATUS_SUCCESS) {

				// Both target returned success or another target has not completed yet
				// Set success
				OriginalCcb->CcbStatus = CCB_STATUS_SUCCESS;
			} else {
				//
				// All other error including CCB_STATUS_BUSY & CCB_STATUS_COMMAND_FAILED
				// OriginalCcb is already filled with error info, so just keep them.
				// 
			}
						break;
		case CCB_STATUS_STOP:
		case CCB_STATUS_NOT_EXIST:
		case CCB_STATUS_COMMUNICATION_ERROR: 

			if (OriginalCcb->ChildReqCount == 1) {
				// Request sent to only one host. Pass error to upper layer.
				OriginalCcb->CcbStatus = Ccb->CcbStatus;
				if(OriginalCcb->SenseBuffer != NULL) {
					RtlCopyMemory(OriginalCcb->SenseBuffer, Ccb->SenseBuffer, Ccb->SenseDataLength);
					}
			} else if (OriginalCcb->AssociateCount == 2) {
				//
				// Another target host didn't return yet. Fill Ccb with current status
				// 
				OriginalCcb->CcbStatus = Ccb->CcbStatus;
				if(OriginalCcb->SenseBuffer != NULL) {
					RtlCopyMemory(OriginalCcb->SenseBuffer, Ccb->SenseBuffer, Ccb->SenseDataLength);
				}

				ACQUIRE_DPC_SPIN_LOCK(&pRaidInfo->SpinLock);
				if (pRaidInfo->pDraidClient) {
					pClient = pRaidInfo->pDraidClient;
					ACQUIRE_DPC_SPIN_LOCK(&pClient->SpinLock);
					if (pClient->DRaidStatus == DRIX_RAID_STATUS_DEGRADED) {
						if (pClient->NodeFlagsRemote[pLurnCurrent->LurnChildIdx] & 
							(DRIX_NODE_FLAG_STOP|DRIX_NODE_FLAG_DEFECTIVE | DRIX_NODE_FLAG_OUT_OF_SYNC)) {
							// Fault target has failed. It is okay. Pass code.

						} else {
							// Normal target has failed, this cause RAID fail. Need to stop
							KDPrintM(DBG_LURN_ERROR, ("Non-defective target %d failed. RAID failure\n", pLurnCurrent->LurnChildIdx));
							OriginalCcb->CcbStatus = CCB_STATUS_STOP;
					}
					} else {
						// One of target is stopped not in degraded mode.
						KDPrintM(DBG_LURN_ERROR, ("Ccb for target %d failed not in degraded mode.\n", pLurnCurrent->LurnChildIdx));
						OriginalCcb->CcbStatus = CCB_STATUS_STOP;						
					}
					RELEASE_DPC_SPIN_LOCK(&pClient->SpinLock);						
				} else {
					// RAID has terminated. Pass status
				}
				RELEASE_DPC_SPIN_LOCK(&pRaidInfo->SpinLock);

			} else if (OriginalCcb->AssociateCount ==1) {
				//
				// Another target has completed already.  Check its status
				//
				switch (OriginalCcb->CcbStatus) {
				case CCB_STATUS_SUCCESS:
					ACQUIRE_DPC_SPIN_LOCK(&pRaidInfo->SpinLock);
					if (pRaidInfo->pDraidClient) {
						pClient = pRaidInfo->pDraidClient;
						ACQUIRE_DPC_SPIN_LOCK(&pClient->SpinLock);
						//
						// One target has succeeded, another target has failed. 
						// If RAID is degraded status and failed target is already recognized as fault unit, it is okay
						//	But if not return busy.
						if (pClient->DRaidStatus == DRIX_RAID_STATUS_DEGRADED) {
							if (pClient->NodeFlagsRemote[pLurnCurrent->LurnChildIdx] & 
								(DRIX_NODE_FLAG_STOP|DRIX_NODE_FLAG_DEFECTIVE | DRIX_NODE_FLAG_OUT_OF_SYNC)) {
								// Failed target is already marked as failure. This is expected situation.
								OriginalCcb->CcbStatus = CCB_STATUS_SUCCESS;
							} else {
								// Failed target is not marked as failed. 
								KDPrintM(DBG_LURN_ERROR, ("Non-defective target %d failed. Target flag = %x, RAID failure\n", 
									pLurnCurrent->LurnChildIdx));
								OriginalCcb->CcbStatus = CCB_STATUS_BUSY;
							}
						} else {
							// No target should fail in other cases. If so, RAID status will be changed. Wait for it returning busy.
							KDPrintM(DBG_LURN_ERROR, ("One of the target ccb failed not in degraded mode. Returning busy.\n"));
							OriginalCcb->CcbStatus = CCB_STATUS_BUSY;
						}
						RELEASE_DPC_SPIN_LOCK(&pClient->SpinLock);						
					} else {
						// client stopped
					OriginalCcb->CcbStatus = CCB_STATUS_STOP;
					RELEASE_DPC_SPIN_LOCK(&pClient->SpinLock);
					break;
				}
					RELEASE_DPC_SPIN_LOCK(&pRaidInfo->SpinLock);
					break;

				case CCB_STATUS_STOP:
				case CCB_STATUS_NOT_EXIST:
				case CCB_STATUS_COMMUNICATION_ERROR: 
					KDPrintM(DBG_LURN_ERROR, ("Both target returned error\n"));
					// both target completed with error
					// pass error.
					break;
				case CCB_STATUS_BUSY:
				case CCB_STATUS_COMMAND_FAILED:					
				default:
					// CCB_STATUS_BUSY,CCB_STATUS_COMMAND_FAILED has higher priority.
					// Go with OriginalCcb's status.
							break;
						}
					}

			break;
		case CCB_STATUS_BUSY:
			if (OriginalCcb->CcbStatus != CCB_STATUS_SUCCESS &&
				OriginalCcb->CcbStatus != CCB_STATUS_STOP &&
				OriginalCcb->CcbStatus != CCB_STATUS_NOT_EXIST &&
				OriginalCcb->CcbStatus != CCB_STATUS_COMMUNICATION_ERROR) {
				// FAIL and unknown error. Preserve previous error
			} else {
				// Overwrite low priority errors
				OriginalCcb->CcbStatus = Ccb->CcbStatus;
				if(OriginalCcb->SenseBuffer != NULL) {
					RtlCopyMemory(OriginalCcb->SenseBuffer, Ccb->SenseBuffer, Ccb->SenseDataLength);
				}
			}
			break;	
		case CCB_STATUS_COMMAND_FAILED:
			if (OriginalCcb->CcbStatus != CCB_STATUS_BUSY &&
				OriginalCcb->CcbStatus != CCB_STATUS_SUCCESS &&
				OriginalCcb->CcbStatus != CCB_STATUS_STOP &&
				OriginalCcb->CcbStatus != CCB_STATUS_NOT_EXIST &&
				OriginalCcb->CcbStatus != CCB_STATUS_COMMUNICATION_ERROR) {
				// Unknown error. Preserve previous error
		} else {
				// Overwrite low priority errors
				OriginalCcb->CcbStatus = Ccb->CcbStatus;
				if(OriginalCcb->SenseBuffer != NULL) {
					RtlCopyMemory(OriginalCcb->SenseBuffer, Ccb->SenseBuffer, Ccb->SenseDataLength);
				}
		}
		break;

		default:
			//
			// Pass error to upper level regardless result of another child.
			//
			// To do: STATUS and error code from the target completed late overwrite previous status. Need to combine two error.
			//
			if (OriginalCcb->CcbStatus == CCB_STATUS_BUSY) {
				OriginalCcb->CcbStatus = Ccb->CcbStatus;
				if(OriginalCcb->SenseBuffer != NULL) {
					RtlCopyMemory(OriginalCcb->SenseBuffer, Ccb->SenseBuffer, Ccb->SenseDataLength);
				}
			}
			break;
		}
		break;
	}

	if(DRIX_RAID_STATUS_FAILED ==  DraidStatus ||
		DRIX_RAID_STATUS_TERMINATED == DraidStatus)
	{
		OriginalCcb->CcbStatus = CCB_STATUS_STOP;
		KDPrintM(DBG_LURN_INFO,("RAID status is %d. Ccb status stop\n", DraidStatus));
	}

	LSCcbSetStatusFlag(	OriginalCcb,
		Ccb->CcbStatusFlags & CCBSTATUS_FLAG_ASSIGNMASK
		);

	RELEASE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, oldIrql);

	//
	//	Complete the original CCB
	//
	AssocCount = InterlockedDecrement(&OriginalCcb->AssociateCount);
	ASSERT(AssocCount >= 0);
	if(AssocCount != 0) {
		return status;
	}

	if (OriginalCcb->CcbStatus != CCB_STATUS_SUCCESS) {
		KDPrintM(DBG_LURN_INFO,("Completing Ccb with status %x\n", OriginalCcb->CcbStatus));
	}

	if (OriginalCcb->Cdb[0] == SCSIOP_WRITE ||OriginalCcb->Cdb[0] == SCSIOP_WRITE16) {
		DraidReleaseBlockIoPermissionToClient(pClient, OriginalCcb);
	}

	LSAssocSetRedundentRaidStatusFlag(pLurnOriginal, OriginalCcb);
	LSCcbCompleteCcb(OriginalCcb);

	return STATUS_SUCCESS;
}

static
NTSTATUS
LurnRAID1RExecute(
		PLURELATION_NODE		Lurn,
		IN	PCCB				Ccb
	) 
{
	NTSTATUS			status;

	PRAID_INFO pRaidInfo;
	KIRQL				oldIrql;
	PDRAID_CLIENT_INFO pClient;
	UINT32 DraidStatus;
	pRaidInfo = Lurn->LurnRAIDInfo;

	//
	// Forward disk IO related request to Client thread
	//
	ACQUIRE_SPIN_LOCK(&pRaidInfo->SpinLock, &oldIrql);	
	pClient = pRaidInfo->pDraidClient;
	if (pClient) {
		ACQUIRE_DPC_SPIN_LOCK(&pClient->SpinLock);
		if (pClient->DRaidStatus != DRIX_RAID_STATUS_TERMINATED) {
			if (pClient->InTransition)  {
				RELEASE_DPC_SPIN_LOCK(&pClient->SpinLock);
				RELEASE_SPIN_LOCK(&pRaidInfo->SpinLock, oldIrql);
				KDPrintM(DBG_LURN_INFO, ("RAID status is in transition. Returning busy\n"));
				Ccb->CcbStatus = CCB_STATUS_BUSY;
				goto complete_here;
			}
			switch(Ccb->Cdb[0]) {
				case SCSIOP_WRITE:
				case SCSIOP_WRITE16:		
				case 0x3E:		// READ_LONG
				case SCSIOP_READ:
				case SCSIOP_READ16:		
				case SCSIOP_VERIFY:
				case SCSIOP_VERIFY16:		
					// These command will be handled by client thread
					InsertTailList(&pClient->CcbQueue, &Ccb->ListEntry);
					KeSetEvent(&pClient->ClientThreadEvent, IO_NO_INCREMENT, FALSE);
					LSCcbMarkCcbAsPending(Ccb);
					RELEASE_DPC_SPIN_LOCK(&pClient->SpinLock);
					RELEASE_SPIN_LOCK(&pRaidInfo->SpinLock, oldIrql);
					return STATUS_PENDING; // no meaning..
				default:
					break;
			}
		} else {
			// Terminated 
			KDPrintM(DBG_LURN_INFO, ("RAID status is in transition. Returning busy\n"));			
			RELEASE_DPC_SPIN_LOCK(&pClient->SpinLock);
			RELEASE_SPIN_LOCK(&pRaidInfo->SpinLock, oldIrql);
			Ccb->CcbStatus = CCB_STATUS_STOP;
			goto complete_here;
		}
		RELEASE_DPC_SPIN_LOCK(&pClient->SpinLock);
	}
	RELEASE_SPIN_LOCK(&pRaidInfo->SpinLock, oldIrql);
	
	switch(Ccb->Cdb[0]) {
	case SCSIOP_INQUIRY: {
		INQUIRYDATA			inquiryData;
		UCHAR				Model[16] = RAID1R_MODEL_NAME;


		KDPrintM(DBG_LURN_INFO,("SCSIOP_INQUIRY Ccb->Lun = 0x%x\n", Ccb->LurId[2]));
		//
		//	We don't support EVPD(enable vital product data).
		//
		if(Ccb->Cdb[1]  & CDB_INQUIRY_EVPD) {

			KDPrintM(DBG_LURN_ERROR,("SCSIOP_INQUIRY: got EVPD. Not supported.\n"));

			LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
			LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
			goto complete_here;
			break;
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
			Model,
			16
			);

		RtlCopyMemory(
			inquiryData.ProductRevisionLevel,
			PRODUCT_REVISION_LEVEL,
			(strlen(PRODUCT_REVISION_LEVEL)+1) < 4 ?  
			(strlen(PRODUCT_REVISION_LEVEL)+1) : 4
			);

		RtlMoveMemory (
			Ccb->DataBuffer,
			&inquiryData,
			Ccb->DataBufferLength > sizeof (INQUIRYDATA) ? 
			sizeof (INQUIRYDATA) : 
		Ccb->DataBufferLength
			);

		LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
		LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;

		status = STATUS_SUCCESS;
		goto complete_here;
		break;
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
				readCapacityData->LogicalBlockAddress = 0xffffffff;
			}

			blockSize = Lurn->BlockBytes;
			REVERSE_BYTES(&readCapacityData->BytesPerBlock, &blockSize);

			LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
			LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
			LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
			LSCcbCompleteCcb(Ccb);
			break;
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

			blockSize = Lurn->BlockBytes;
			REVERSE_BYTES(&readCapacityDataEx->BytesPerBlock, &blockSize);

			LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
			LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);
			LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
			LSCcbCompleteCcb(Ccb);
			break;
		}
	
	case SCSIOP_MODE_SENSE:
		{
			LurnAssocModeSense(Lurn, Ccb);
			break;
		}
#if 0		
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
					KDPrintM(DBG_LURN_INFO, 
						("SCSIOP_MODE_SENSE: MODE_DSP_WRITE_PROTECT\n"));
					parameterHeader->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;

					if(LSCcbIsFlagOn(Ccb, CCB_FLAG_W2K_READONLY_PATCH) ||
						LSCcbIsFlagOn(Ccb, CCB_FLAG_ALLOW_WRITE_IN_RO_ACCESS))
						parameterHeader->DeviceSpecificParameter &= ~MODE_DSP_WRITE_PROTECT;
					} else
						parameterHeader->DeviceSpecificParameter &= ~MODE_DSP_WRITE_PROTECT;

				parameterHeader->BlockDescriptorLength = sizeof(MODE_PARAMETER_BLOCK);

				//
				// Make Block.
				//
				BlockCount = (ULONG)(Lurn->EndBlockAddr - Lurn->StartBlockAddr + 1);
				parameterBlock->DensityCode = 0;	// It is Reserved for direct access devices.
				parameterBlock->BlockLength[0] = (BYTE)(Lurn->BlockBytes>>16);
				parameterBlock->BlockLength[1] = (BYTE)(Lurn->BlockBytes>>8);
				parameterBlock->BlockLength[2] = (BYTE)(Lurn->BlockBytes);

				LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
				Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;
				goto complete_here;
			} else {
				KDPrintM(DBG_LURN_TRACE,
					("SCSIOP_MODE_SENSE: unsupported pagecode:%x\n", (ULONG)Cdb->MODE_SENSE.PageCode));
				LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
				LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;	//SRB_STATUS_SUCCESS;
				goto complete_here;
			}
			break; 
		}
#endif
	case SCSIOP_MODE_SELECT:
		LurnAssocModeSelect(Lurn, Ccb);
		break;

	default:
		//
		//	send to all child LURNs.
		//	Set WriteVerify completion routine to CCB
		//		it guarantees CCB_STATUS_SUCCESS when least one LURN is working fine.
		//

		{
			status = LurnAssocSendCcbToAllChildren(
				Lurn,
				Ccb,
				LurnRAID1RCcbCompletion,
				NULL,
				NULL,
				FALSE
				);
		}
		break;

	}

	return STATUS_SUCCESS;
complete_here:

	ACQUIRE_SPIN_LOCK(&pRaidInfo->SpinLock, &oldIrql);	
	pClient = pRaidInfo->pDraidClient;
	if (pClient == NULL) {
		DraidStatus = DRIX_RAID_STATUS_TERMINATED;
	} else {
		DraidStatus = pClient->DRaidStatus;
	}
	RELEASE_SPIN_LOCK(&pRaidInfo->SpinLock, oldIrql);

	LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);

	LSCcbCompleteCcb(Ccb);

	return STATUS_SUCCESS;
}

NTSTATUS
LurnRAID1RFlushCompletion(
		IN PCCB	Ccb,	
		IN PVOID Param // Not used.
) {
	UNREFERENCED_PARAMETER(Param);
	LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
	LSAssocSetRedundentRaidStatusFlag(Ccb->CcbCurrentStackLocation->Lurn, Ccb);
	LSCcbCompleteCcb(Ccb);
	return STATUS_SUCCESS;
}

typedef struct _LURN_RAID_SHUT_DOWN_PARAM {
	WORK_QUEUE_ITEM 	WorkItem;
	PLURELATION_NODE	Lurn;
	PCCB				Ccb;
} LURN_RAID_SHUT_DOWN_PARAM, *PLURN_RAID_SHUT_DOWN_PARAM;

VOID
LurnRAID1ShutDownWorker(
	IN PVOID Parameter
) {
	PLURN_RAID_SHUT_DOWN_PARAM Params = (PLURN_RAID_SHUT_DOWN_PARAM) Parameter;
	PRAID_INFO pRaidInfo;
	NTSTATUS status;

	//
	// Is it possible that LURN is destroyed already?
	// 

	KDPrintM(DBG_LURN_INFO, ("Shutdowning DRAID\n"));
	
	pRaidInfo = Params->Lurn->LurnRAIDInfo;

	DraidClientStop(Params->Lurn);
	if (pRaidInfo->pDraidArbiter)
		DraidArbiterStop(Params->Lurn);
	status = LurnAssocSendCcbToAllChildren(Params->Lurn, Params->Ccb, LurnRAID1RCcbCompletion, NULL, NULL, FALSE);
	// LurnRAID1RCcbCompletion will call Ccbcompletion routine.
	ExFreePoolWithTag(Params, DRAID_SHUTDOWN_POOL_TAG);	
}


NTSTATUS
LurnRAID1RRequest(
				 PLURELATION_NODE	Lurn,
				 PCCB				Ccb
				 )
{
	NTSTATUS				status;
	PRAID_INFO pRaidInfo;
	KIRQL	oldIrql;
	pRaidInfo = Lurn->LurnRAIDInfo;

	//
	//	dispatch a request
	//
	KDPrintM(DBG_LURN_TRACE, ("IN\n"));

	switch(Ccb->OperationCode) {
	case CCB_OPCODE_EXECUTE:

		KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_EXECUTE!\n"));
		LurnRAID1RExecute(Lurn, Ccb);
		break;

		//
		//	Send to all LURNs
		//
	case CCB_OPCODE_ABORT_COMMAND:
	case CCB_OPCODE_RESETBUS:
	case CCB_OPCODE_RESTART:
	case CCB_OPCODE_NOOP:
		{
			//
			//	Check to see if the CCB is coming for only this LURN.
			//
			if(LSCcbIsFlagOn(Ccb, CCB_FLAG_DONOT_PASSDOWN)) {
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				//			LSCcbCompleteCcb(Ccb);
				goto complete_here;
			}
			status = LurnAssocSendCcbToAllChildren(Lurn, Ccb, LurnRAID1RCcbCompletion, NULL, NULL, FALSE);
			break;
		}
	case CCB_OPCODE_FLUSH:
		KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_FLUSH\n"));
		//
		// This code may be running at DPC level.
		// Flush operation should not block
		//

		status = DraidClientFlush(Lurn, Ccb, LurnRAID1RFlushCompletion);
		if (status == STATUS_PENDING) {
			LSCcbMarkCcbAsPending(Ccb);
		} else {
			// Assume success
			LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
			goto complete_here;
		}
		break;

	case CCB_OPCODE_SHUTDOWN:
		KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_SHUTDOWN\n"));
		//
		// This code may be running at DPC level.
		// Run stop operation asynchrously.
		//
		// Alloc work item and call LurnRAID1ShutDownWorker
		{
			PLURN_RAID_SHUT_DOWN_PARAM Param;

			ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);
			if(Lurn->LurnStatus == LURN_STATUS_STOP_PENDING || Lurn->LurnStatus == LURN_STATUS_STOP)
			{
				RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);		
				// Already stopping
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				goto complete_here;				
			} else {
				Lurn->LurnStatus = LURN_STATUS_STOP_PENDING;
			}
			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

			Param = ExAllocatePoolWithTag(
				NonPagedPool, sizeof(LURN_RAID_SHUT_DOWN_PARAM), DRAID_SHUTDOWN_POOL_TAG);
			if (Param==NULL) {
				KDPrintM(DBG_LURN_INFO, ("Failed to alloc shutdown worker\n"));
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				goto complete_here;				
			}
			Param->Lurn = Lurn;
			Param->Ccb = Ccb;
			ExInitializeWorkItem(&Param->WorkItem, LurnRAID1ShutDownWorker, Param);
			ExQueueWorkItem(&Param->WorkItem, DelayedWorkQueue);
		}
		break;
	case CCB_OPCODE_STOP:
		{
			KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_STOP\n"));
			ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);
			if(Lurn->LurnStatus == LURN_STATUS_STOP_PENDING || Lurn->LurnStatus == LURN_STATUS_STOP)
			{
				RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);		
				// Already stopping
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				goto complete_here;				
			} else {
				Lurn->LurnStatus = LURN_STATUS_STOP_PENDING;
			}
			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

			DraidClientStop(Lurn);
			if (pRaidInfo->pDraidArbiter)
				DraidArbiterStop(Lurn);

			//
			//	Check to see if the CCB is coming for only this LURN.
			//
			if(LSCcbIsFlagOn(Ccb, CCB_FLAG_DONOT_PASSDOWN)) {
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				goto complete_here;
			}
			status = LurnAssocSendCcbToAllChildren(Lurn, Ccb, LurnRAID1RCcbCompletion, NULL, NULL, FALSE);
			break;
		}
	case CCB_OPCODE_UPDATE:
		{
			//
			//	Check to see if the CCB is coming for only this LURN.
			//
			if(LSCcbIsFlagOn(Ccb, CCB_FLAG_DONOT_PASSDOWN)) {
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				//			LSCcbCompleteCcb(Ccb);
				goto complete_here;
			}
			status = LurnAssocSendCcbToAllChildren(Lurn, Ccb, LurnRAIDUpdateCcbCompletion, NULL, NULL, TRUE);
			break;
		}

	case CCB_OPCODE_QUERY:
		status = LurnAssocQuery(Lurn, LurnAggrCcbCompletion, Ccb);
		break;

	default:
		KDPrintM(DBG_LURN_TRACE, ("INVALID COMMAND\n"));
		LSCcbSetStatus(Ccb, CCB_STATUS_INVALID_COMMAND);
		//		LSCcbCompleteCcb(Ccb);
		goto complete_here;
		break;
	}

	return STATUS_SUCCESS;

complete_here:

	LSAssocSetRedundentRaidStatusFlag(Lurn, Ccb);

	LSCcbCompleteCcb(Ccb);

	return STATUS_SUCCESS;
}


#if 0	// RAID4 is not supported now.
//////////////////////////////////////////////////////////////////////////
//
//	RAID 4R
//

NTSTATUS
LurnRAID4RInitialize(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
	) 
{
	PRAID_INFO pRaidInfo = NULL;
	ULONG ulBitMapSize;
	NTSTATUS ntStatus;

	OBJECT_ATTRIBUTES objectAttributes;

	ASSERT(KeGetCurrentIrql() ==  PASSIVE_LEVEL);

	KDPrintM(DBG_LURN_INFO, ("LurnDesc->LurnType = %d\n", LurnDesc->LurnType));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->LurnId = %d\n", LurnDesc->LurnId));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->StartBlockAddr = 0x%I64x\n", LurnDesc->StartBlockAddr));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->EndBlockAddr = 0x%I64x\n", LurnDesc->EndBlockAddr));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->UnitBlocks = 0x%I64x\n", LurnDesc->UnitBlocks));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->MaxDataSendLength = %d bytes\n", LurnDesc->MaxDataSendLength));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->MaxDataRecvLength = %d bytes\n", LurnDesc->MaxDataRecvLength));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->LurnOptions = %d\n", LurnDesc->LurnOptions));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->LurnParent = %d\n", LurnDesc->LurnParent));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->LurnChildrenCnt = %d\n", LurnDesc->LurnChildrenCnt));
	KDPrintM(DBG_LURN_INFO, ("LurnDesc->LurnChildren = 0x%p\n", LurnDesc->LurnChildren));

//	Raid Information
	Lurn->LurnRAIDInfo = ExAllocatePoolWithTag(NonPagedPool, sizeof(RAID_INFO), 
		RAID_INFO_POOL_TAG);

	if(NULL == Lurn->LurnRAIDInfo)
	{
		ntStatus = STATUS_INSUFFICIENT_RESOURCES;
		goto out;
	}

	pRaidInfo = Lurn->LurnRAIDInfo;

	RtlZeroMemory(pRaidInfo, sizeof(RAID_INFO));

	LurnSetRaidInfoStatus(pRaidInfo, RAID_STATUS_INITIAILIZING);

	// set spare disk count
	pRaidInfo->nDiskCount = LurnDesc->LurnChildrenCnt - LurnDesc->LurnInfoRAID.nSpareDisk;
	pRaidInfo->nSpareDisk = LurnDesc->LurnInfoRAID.nSpareDisk;

	pRaidInfo->SectorsPerBit = LurnDesc->LurnInfoRAID.SectorsPerBit;
	if(!pRaidInfo->SectorsPerBit)
	{
		KDPrintM(DBG_LURN_ERROR, ("SectorsPerBit is zero!\n"));

		ntStatus = STATUS_INVALID_PARAMETER;
		goto out;
	}

	//
	// Determine block bytes.
	//

	Lurn->ChildBlockBytes = LurnAsscGetChildBlockBytes(Lurn);
	Lurn->BlockBytes = Lurn->ChildBlockBytes * (Lurn->LurnChildrenCnt - pRaidInfo->nSpareDisk - 1); // exclude parity

	//
	// Maximum of data send/receive length from the upper LURN.
	//

	pRaidInfo->MaxDataSendLength = LurnDesc->MaxDataSendLength;
	pRaidInfo->MaxDataRecvLength = LurnDesc->MaxDataRecvLength;

	// allocate shuffle data buffer
	{
		ULONG ulDataBufferSizePerDisk;
		ULONG ulDataBufferSize;

		if(pRaidInfo->MaxDataSendLength > pRaidInfo->MaxDataRecvLength)
			ulDataBufferSizePerDisk = pRaidInfo->MaxDataSendLength / (pRaidInfo->nDiskCount -1);
		else
			ulDataBufferSizePerDisk = pRaidInfo->MaxDataRecvLength / (pRaidInfo->nDiskCount -1);

		ulDataBufferSize = pRaidInfo->nDiskCount * ulDataBufferSizePerDisk;

		KDPrintM(DBG_LURN_INFO, 
			("allocating data buffer. ulDataBufferSizePerDisk = %d, ulDataBufferSize = %d\n",
			ulDataBufferSizePerDisk, ulDataBufferSize));

		ASSERT(sizeof(pRaidInfo->DataBufferLookaside) >= sizeof(NPAGED_LOOKASIDE_LIST));
		ExInitializeNPagedLookasideList(
			&pRaidInfo->DataBufferLookaside,
			NULL, // PALLOCATE_FUNCTION  Allocate  OPTIONAL
			NULL, // PFREE_FUNCTION  Free  OPTIONAL
			0, // Flags Reserved. Must be zero
			ulDataBufferSizePerDisk,
			RAID_DATA_BUFFER_POOL_TAG,
			0 // Depth Reserved. Must be zero
			);
	}

	// Create & init BITMAP
	{
	//	Bitmap (1) * (bitmap structure size + bitmap size)
		ulBitMapSize = (ULONG)NDAS_BLOCK_SIZE_BITMAP * Lurn->ChildBlockBytes; // use full bytes 1MB(8Mb) of bitmap
		pRaidInfo->Bitmap = (PRTL_BITMAP)ExAllocatePoolWithTag(NonPagedPool, 
			ulBitMapSize + sizeof(RTL_BITMAP), DRAID_BITMAP_POOL_TAG);

		if(NULL == pRaidInfo->Bitmap)
		{
			ntStatus = STATUS_INSUFFICIENT_RESOURCES;
			goto out;
		}

		RtlInitializeBitMap(
			pRaidInfo->Bitmap,
			(PULONG)(pRaidInfo->Bitmap +1), // start address of bitmap data
			ulBitMapSize * 8);

		RtlClearAllBits(pRaidInfo->Bitmap);
	}

	// Init write log
	pRaidInfo->iWriteLog = 0;
	RtlZeroMemory(pRaidInfo->WriteLogs, sizeof(pRaidInfo->WriteLogs));

	KeInitializeSpinLock(&pRaidInfo->LockInfo);

	// Create recover thread
	if(LUR_IS_PRIMARY(Lurn->Lur)) {
		DraidArbiterStart(Lurn);
	} else {
		KDPrintM(DBG_LURN_INFO, ("Not a primary node. Don't start Draid Arbiter\n"));
	}

	// Always create draid client.
	ntStatus = DraidClientStart(Lurn); 
	if (!NT_SUCCESS(ntStatus)) {
		goto out;
	}

	ntStatus = STATUS_SUCCESS;
out:
	if(!NT_SUCCESS(ntStatus))
	{
		if(Lurn->LurnRAIDInfo)
		{
			if(pRaidInfo) {
				SAFE_FREE_POOL_WITH_TAG(pRaidInfo->Bitmap, DRAID_BITMAP_POOL_TAG);
			}
			SAFE_FREE_POOL_WITH_TAG(Lurn->LurnRAIDInfo, RAID_INFO_POOL_TAG);
		}
	}


	return ntStatus;
}

NTSTATUS
LurnRAID4RDestroy(
		PLURELATION_NODE Lurn
	) 
{
	PRAID_INFO pRaidInfo;

	KDPrintM(DBG_LURN_INFO, ("IN\n"));

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL) ;
	ASSERT(Lurn->LurnRAIDInfo);

	pRaidInfo = Lurn->LurnRAIDInfo;
	ASSERT(pRaidInfo);

	ExDeleteNPagedLookasideList(
		&pRaidInfo->DataBufferLookaside);

	ASSERT(pRaidInfo->Bitmap);
	SAFE_FREE_POOL_WITH_TAG(pRaidInfo->Bitmap, DRAID_BITMAP_POOL_TAG);

	SAFE_FREE_POOL_WITH_TAG(pRaidInfo, RAID_INFO_POOL_TAG);

	KDPrintM(DBG_LURN_INFO, ("OUT\n"));
	return STATUS_SUCCESS ;
}

NTSTATUS
LurnRAID4RCcbCompletion(
		IN PCCB	Ccb,
		IN PCCB	OriginalCcb
	)
{
	KIRQL	oldIrql, oldIrqlRaidInfo;
	LONG	ass;
	NTSTATUS status;
	PRAID_INFO pRaidInfo;
	PLURELATION_NODE pLurnOriginal;

	status = STATUS_SUCCESS;

	//
	//	Higher number of priority will overwrite CcbStatus.
	//
	//	
	//	Mirroring priority
	//
	//	Other error codes when one child is in error							: 0
	//	CCB_STATUS_STOP when one child is in error								: 1
	//	CCB_STATUS_SUCCESS														: 2
	//	CCB_STATUS_BUSY															: 3
	//	Other error codes when both children are in error						: 4
	//	CCB_STATUS_STOP/CCB_STATUS_NOT_EXIST when both children are in error	: 5
	//
	ACQUIRE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, &oldIrql);

	pLurnOriginal = OriginalCcb->CcbCurrentStackLocation->Lurn;
	ASSERT(pLurnOriginal);
	pRaidInfo = pLurnOriginal->LurnRAIDInfo;

	ACQUIRE_SPIN_LOCK(&pRaidInfo->LockInfo, &oldIrqlRaidInfo);

	switch(Ccb->CcbStatus) {

	case CCB_STATUS_SUCCESS:	// priority 2
		if (CCB_OPCODE_EXECUTE == OriginalCcb->OperationCode)
		{
			switch(OriginalCcb->Cdb[0])
			{
			case 0x3E:		// READ_LONG
			case SCSIOP_READ:
			case SCSIOP_READ16:
				{
					ULONG i, j, BlocksPerDisk;
					register int k;
					PULONG pDataBufferToRecover, pDataBufferSrc;

					KDPrintM(DBG_LURN_NOISE,("SCSIOP_READ\n"));
					ASSERT(Ccb->DataBuffer);

					BlocksPerDisk = Ccb->DataBufferLength / pLurnOriginal->ChildBlockBytes;
					i = Ccb->AssociateID;

					// do not copy parity
					if (i < pRaidInfo->nDiskCount -1)
					{
						for(j = 0; j < BlocksPerDisk; j++)
						{
							RtlCopyMemory( // Copy back
								(PCHAR)OriginalCcb->DataBuffer + (i + j * (pRaidInfo->nDiskCount -1)) * pLurnOriginal->ChildBlockBytes,
								(PCHAR)Ccb->DataBuffer + (j * pLurnOriginal->ChildBlockBytes),
								pLurnOriginal->ChildBlockBytes);
						}
					}

					// if non-parity device is stop, do parity work on the blocks
					if ((RAID_STATUS_EMERGENCY == pRaidInfo->RaidStatus ||
						RAID_STATUS_EMERGENCY_READY == pRaidInfo->RaidStatus ||
						RAID_STATUS_RECOVERING == pRaidInfo->RaidStatus) &&
						pRaidInfo->nDiskCount -1 != pRaidInfo->iChildFault)
					{
						// parity work
						pDataBufferSrc = (PULONG)Ccb->DataBuffer;
						for(j = 0; j < BlocksPerDisk; j++)
						{
							pDataBufferToRecover = 
								(PULONG)(
								(PCHAR)OriginalCcb->DataBuffer + 
								(pRaidInfo->iChildDefected + j * (pRaidInfo->nDiskCount -1)) * pLurnOriginal->ChildBlockBytes);

							k = pLurnOriginal->ChildBlockBytes / sizeof(ULONG);
							while(k--)
							{
								*pDataBufferToRecover ^= *pDataBufferSrc;
								pDataBufferToRecover++;
								pDataBufferSrc++;
							}
						}
					}
				}
				break;
			}
		}
		break;

	case CCB_STATUS_BUSY:		// priority 3
		if(OriginalCcb->CcbStatus == CCB_STATUS_SUCCESS) {
			ASSERT(OriginalCcb->Srb || OriginalCcb->OperationCode == CCB_OPCODE_RESETBUS);
			OriginalCcb->CcbStatus = CCB_STATUS_BUSY;
		}
		break;

	case CCB_STATUS_STOP:		// priority 1/5
		if(!LSCcbIsStatusFlagOn(OriginalCcb, CCBSTATUS_FLAG_LURN_STOP))
		{
			PLURELATION_NODE pLurnChildDefected;
			ULONG i;

			LSCcbSetStatusFlag(OriginalCcb, CCBSTATUS_FLAG_LURN_STOP);	// priority 1
			LSCcbSetStatusFlag(OriginalCcb, CCBSTATUS_FLAG_LURN_DEGRADED);

			KDPrintM(DBG_LURN_ERROR, ("CCB_STATUS_STOP, CCBSTATUS_FLAG_LURN_STOP not flagged\n"));
			KDPrintM(DBG_LURN_INFO, ("pRaidInfo->RaidStatus : %d\n", pRaidInfo->RaidStatus));

			//////////////////////////////////////////////
			//
			//	Initialize raid information
			//
			{
				// set backup information
				ASSERT(Ccb->CcbCurrentStackLocation);
				ASSERT(Ccb->CcbCurrentStackLocation->Lurn);
				pLurnChildDefected = Ccb->CcbCurrentStackLocation->Lurn;

				ASSERT(OriginalCcb->CcbCurrentStackLocation);
				ASSERT(OriginalCcb->CcbCurrentStackLocation->Lurn);

				ASSERT(LURN_IDE_DISK == pLurnChildDefected->LurnType);
				ASSERT(LURN_RAID4R == pLurnOriginal->LurnType);

				if(!pLurnChildDefected || !pLurnOriginal)
				{
					ASSERT(FALSE);
					status = STATUS_ILLEGAL_INSTRUCTION;
					break;
				}

				ASSERT(pLurnChildDefected->LurnRAIDInfo);

				if(RAID_STATUS_RECOVERING == pRaidInfo->RaidStatus)
				{
					ASSERT(FALSE);
					OriginalCcb->CcbStatus = CCB_STATUS_STOP;
				}

				// 1 fail + 1 fail = broken
				if(RAID_STATUS_NORMAL != pRaidInfo->RaidStatus)
				{
					KDPrintM(DBG_LURN_ERROR, ("RAID_STATUS_NORMAL != pRaidInfo->RaidStatus(%d)\n", pRaidInfo->RaidStatus));

					if(pLurnChildDefected != pRaidInfo->MapLurnChildren[pRaidInfo->iChildFault])
					{
						ASSERT(FALSE);
						//						status = STATUS_DATATYPE_MISALIGNMENT;
						OriginalCcb->CcbStatus = CCB_STATUS_STOP;
						break;
					}
				}

				// set pRaidInfo->iChildDefected
				for(i = 0; i < pRaidInfo->nDiskCount + pRaidInfo->nSpareDisk; i++)
				{
					if(pLurnChildDefected == pRaidInfo->MapLurnChildren[i])
					{
						pRaidInfo->iChildFault = i;
						break;
					}
				}

				if(i < pRaidInfo->nDiskCount)
				{
					// ok
					KDPrintM(DBG_LURN_TRACE, ("pLurnChildDefected(%p) == pRaidInfo->MapLurnChildren[%d]\n", pLurnChildDefected, i));
				}
				else if(i < pRaidInfo->nDiskCount + pRaidInfo->nSpareDisk)
				{
					// ignore spare, just break
					KDPrintM(DBG_LURN_INFO, ("pLurnChildDefected(%p) == pRaidInfo->MapLurnChildren[%d](spare) opcode : %x, cdb[0] : %x\n", pLurnChildDefected, i, OriginalCcb->OperationCode, (ULONG)OriginalCcb->Cdb[0]));
					break;
				}
				else //if(i == pRaidInfo->nDiskCount)
				{
					// failed to find a defected child
					KDPrintM(DBG_LURN_ERROR, ("pLurnChildDefected(%p) NOT found pRaidInfo->MapLurnChildren[%d] opcode : %x, cdb[0] : %x\n", pLurnChildDefected, i, OriginalCcb->OperationCode, (ULONG)OriginalCcb->Cdb[0]));
					ASSERT(FALSE);
					status = STATUS_DATATYPE_MISALIGNMENT;
					OriginalCcb->CcbStatus = CCB_STATUS_STOP;
					break;
				}

				if (RAID_STATUS_EMERGENCY != pRaidInfo->RaidStatus &&
					RAID_STATUS_EMERGENCY_READY != pRaidInfo->RaidStatus)
				{
					KDPrintM(DBG_LURN_ERROR, ("Set from %d to RAID_STATUS_EMERGENCY_READY\n", pRaidInfo->RaidStatus));

					LurnSetRaidInfoStatus(pRaidInfo, RAID_STATUS_EMERGENCY_READY);
					pRaidInfo->rmd.UnitMetaData[pRaidInfo->iChildFault].UnitDeviceStatus = NDAS_UNIT_META_BIND_STATUS_NOT_SYNCED;

					for(i = 0; i < pRaidInfo->nDiskCount; i++)
					{
						// 1 fail + 1 fail = broken
						if(NDAS_UNIT_META_BIND_STATUS_NOT_SYNCED & pRaidInfo->rmd.UnitMetaData[i].UnitDeviceStatus &&
							i != pRaidInfo->iChildFault)
						{
							ASSERT(FALSE);
							OriginalCcb->CcbStatus = CCB_STATUS_STOP;
							break;
						}
					}

					if(CCB_STATUS_STOP == OriginalCcb->CcbStatus)
						break;

					// access again, as we can't complete reading at this time
					KDPrintM(DBG_LURN_ERROR, ("NORMAL -> EMERGENCY_READY : Read again, OriginalCcb->CcbStatus = CCB_STATUS_BUSY\n"));
					OriginalCcb->CcbStatus = CCB_STATUS_BUSY;
				}

				KDPrintM(DBG_LURN_ERROR, ("CCB_STATUS_STOP, pRaidInfo->iChildFault = %d\n", pRaidInfo->iChildFault));
			}
		} else {
			//
			//	at least two children stopped!
			//
			ASSERT(FALSE);
			OriginalCcb->CcbStatus = CCB_STATUS_STOP;	// priority 5
		}
		break;
	case CCB_STATUS_NOT_EXIST:
		if(LSCcbIsStatusFlagOn(OriginalCcb, CCBSTATUS_FLAG_LURN_DEGRADED)) {
			//
			//	at least two children problem.! (1 stop, 1 not exist)
			//
			ASSERT(FALSE);
			OriginalCcb->CcbStatus = CCB_STATUS_STOP;	// priority 5
			break;
		}
	default:					// priority 0/4
		if(CCB_STATUS_NOT_EXIST != Ccb->CcbStatus)
		{
			KDPrintM(DBG_LURN_ERROR, ("ERROR: Ccb->CcbStatus= %x, AssociateID = %d, OriginalCcb->AssociateCount = %d\n",
				(int)Ccb->CcbStatus, (unsigned int)Ccb->AssociateID, OriginalCcb->AssociateCount));
		}

		if(!LSCcbIsStatusFlagOn(OriginalCcb, CCBSTATUS_FLAG_LURN_IN_ERROR)) {
			LSCcbSetStatusFlag(OriginalCcb, CCBSTATUS_FLAG_LURN_IN_ERROR);	// priority 0
		} else {
			//
			//	at least two children have an error or do not exist! (2 not exist)
			//
			OriginalCcb->CcbStatus = Ccb->CcbStatus;	// 	// priority 4
		}
		break;
	}

	if(CCB_OPCODE_EXECUTE == OriginalCcb->OperationCode)
	{
		switch(OriginalCcb->Cdb[0])
		{
		case SCSIOP_WRITE:
		case SCSIOP_WRITE16:			
		case 0x3E:		// READ_LONG
		case SCSIOP_READ:
		case SCSIOP_READ16:			
			KDPrintM(DBG_LURN_NOISE,("Release data buffer look aside (%p)\n", Ccb->DataBuffer));
			ASSERT(Ccb->DataBuffer);
			ExFreeToNPagedLookasideList(
				&pRaidInfo->DataBufferLookaside,
				Ccb->DataBuffer);

			Ccb->DataBuffer = NULL;
		}
	}

	if(RAID_STATUS_FAIL == pRaidInfo->RaidStatus)
	{
		ASSERT(FALSE);
		OriginalCcb->CcbStatus = CCB_STATUS_STOP;
	}

	LSCcbSetStatusFlag(	OriginalCcb,
		Ccb->CcbStatusFlags & CCBSTATUS_FLAG_ASSIGNMASK
		);

	if(RAID_STATUS_RECOVERING == pRaidInfo->RaidStatus)
		LSCcbSetStatusFlag(OriginalCcb, CCBSTATUS_FLAG_RECOVERING);

	RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrqlRaidInfo);
	RELEASE_SPIN_LOCK(&OriginalCcb->CcbSpinLock, oldIrql);

	//
	//	Complete the original CCB
	//
	ass = InterlockedDecrement(&OriginalCcb->AssociateCount);
	ASSERT(ass >= 0);
	if(ass != 0) {
		return status;
	}

	if(pRaidInfo->RaidStatus != RAID_STATUS_NORMAL)
	{
		KDPrintM(DBG_LURN_NOISE,("All Ccb complete in abnormal status : %d\n", (int)OriginalCcb->Cdb[0]));
	}

	LSAssocSetRedundentRaidStatusFlag(pRaidInfo, OriginalCcb);

	LSCcbCompleteCcb(OriginalCcb);

	return STATUS_SUCCESS;
}

static
NTSTATUS
LurnRAID4RExecute(
		PLURELATION_NODE		Lurn,
		IN	PCCB				Ccb
	) 
{
	NTSTATUS			status = STATUS_SUCCESS;

	PRAID_INFO pRaidInfo;
	KIRQL	oldIrql;

	pRaidInfo = Lurn->LurnRAIDInfo;
	// record a bitmap information to the next disk of the defected disk
	// pExtendedCommands itself will go into LurnAssocSendCcbToChildrenArray
	if(RAID_STATUS_INITIAILIZING != pRaidInfo->RaidStatus)
	{
		ASSERT(pRaidInfo->nDiskCount > 0 && pRaidInfo->nDiskCount <= NDAS_MAX_RAID4R_CHILD);
	}

	if(
		RAID_STATUS_EMERGENCY == pRaidInfo->RaidStatus ||
		RAID_STATUS_RECOVERING == pRaidInfo->RaidStatus 
		)
	{
		ASSERT(pRaidInfo->iChildFault < pRaidInfo->nDiskCount);
	}

	// recovery/data buffer protection code
	switch(Ccb->Cdb[0])
	{
		case SCSIOP_WRITE:
		case SCSIOP_WRITE16:			
		case 0x3E:		// READ_LONG
		case SCSIOP_READ:
		case SCSIOP_READ16:			
		case SCSIOP_VERIFY:
		case SCSIOP_VERIFY16:			
			{
				UINT64 logicalBlockAddress;
				UINT32 transferBlocks;

				LSCcbGetAddressAndLength((PCDB)&Ccb->Cdb[0], &logicalBlockAddress, &transferBlocks);

				// Busy if this location is under recovering

				ACQUIRE_SPIN_LOCK(&pRaidInfo->LockInfo, &oldIrql);
				if(
					RAID_STATUS_INITIAILIZING == pRaidInfo->RaidStatus || // do not IO when reading bitmap
					RAID_STATUS_EMERGENCY_READY == pRaidInfo->RaidStatus || // emergency mode not ready
					(
						RAID_STATUS_RECOVERING == pRaidInfo->RaidStatus && // do not IO where being recovered
						(
							pRaidInfo->BitmapIdxToRecover == logicalBlockAddress / pRaidInfo->SectorsPerBit ||
							pRaidInfo->BitmapIdxToRecover == (logicalBlockAddress + transferBlocks -1) / pRaidInfo->SectorsPerBit
						)
					)
					)
				{
					RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);

					KDPrintM(DBG_LURN_INFO, ("!! RECOVER THREAD PROTECTION : %08lx, cmd : 0x%x, %x, %d\n", pRaidInfo->RaidStatus, (UINT32)Ccb->Cdb[0], logicalBlockAddress, transferBlocks));
					Ccb->CcbStatus = CCB_STATUS_BUSY;
					goto complete_here;
				}

				RELEASE_SPIN_LOCK(&pRaidInfo->LockInfo, oldIrql);
			}
	}

	switch(Ccb->Cdb[0]) {
	case SCSIOP_WRITE:
	case SCSIOP_WRITE16:		
		{
			UINT64 logicalBlockAddress;
			UINT32 transferBlocks;
			register ULONG i, j;
			register int k;

			int DataBufferLengthPerDisk;
			ULONG BlocksPerDisk;
			PULONG pDataBufferParity, pDataBufferSrc;
			CUSTOM_DATA_BUFFER cdb;

			KDPrintM(DBG_LURN_NOISE,("SCSIOP_WRITE\n"));

			LSCcbGetAddressAndLength((PCDB)&Ccb->Cdb[0], &logicalBlockAddress, &transferBlocks);

			ASSERT(transferBlocks <= pRaidInfo->MaxDataSendLength/Lurn->BlockBytes);
			KDPrintM(DBG_LURN_NOISE,("W Ccb->DataBufferLength %d\n", Ccb->DataBufferLength));

			switch(pRaidInfo->RaidStatus)
			{
			case RAID_STATUS_NORMAL:
			case RAID_STATUS_RECOVERING:
				// add write log
				{
					PWRITE_LOG write_log;
					pRaidInfo->iWriteLog++;
					write_log = &pRaidInfo->WriteLogs[pRaidInfo->iWriteLog % NDAS_RAID_WRITE_LOG_SIZE];
					write_log->logicalBlockAddress = logicalBlockAddress;
					write_log->transferBlocks = transferBlocks;
					write_log->timeStamp = pRaidInfo->iWriteLog;
				}
				break;
			case RAID_STATUS_EMERGENCY:
				{
					UINT32 uiBitmapStartInBits, uiBitmapEndInBits;

					KDPrintM(DBG_LURN_TRACE,("RAID_STATUS_EMERGENCY\n"));
					
					// seek first sector in bitmap
					uiBitmapStartInBits = (UINT32)(logicalBlockAddress / 
						pRaidInfo->SectorsPerBit);
					uiBitmapEndInBits = (UINT32)((logicalBlockAddress + transferBlocks -1) /
						pRaidInfo->SectorsPerBit);

					RtlSetBits(pRaidInfo->Bitmap, uiBitmapStartInBits, uiBitmapEndInBits - uiBitmapStartInBits +1);
				}
				break;
			default:
				// invalid status
				ASSERT(FALSE);
				break;
			}

			// create new data buffer and encrypt here.
			// new data buffer will be deleted at completion routine
			DataBufferLengthPerDisk = Ccb->DataBufferLength / (pRaidInfo->nDiskCount -1);
			BlocksPerDisk = DataBufferLengthPerDisk / Lurn->ChildBlockBytes;

			cdb.DataBufferCount = 0;
			for(i = 0; i < pRaidInfo->nDiskCount; i++)
			{
				cdb.DataBuffer[i] = ExAllocateFromNPagedLookasideList(&pRaidInfo->DataBufferLookaside);
				ASSERT(cdb.DataBuffer[i]);
				if(!cdb.DataBuffer[i])
				{
					// free allocated buffers
					for(j = 0; j < i; j++)
					{
						ExFreeToNPagedLookasideList(
							&pRaidInfo->DataBufferLookaside,
							cdb.DataBuffer[i]);
						cdb.DataBuffer[i] = NULL;
					}

					Ccb->CcbStatus = CCB_STATUS_SUCCESS;
					status = STATUS_INSUFFICIENT_RESOURCES;

					KDPrintM(DBG_LURN_ERROR, ("ExAllocateFromNPagedLookasideList Failed[%d]\n", i));
					LSCcbCompleteCcb(Ccb);
					break;
				}

				cdb.DataBufferLength[i] = DataBufferLengthPerDisk;
				cdb.DataBufferCount++;
			}
			
			// split DataBuffer into each DataBuffers of children by block size
			for(i = 0; i < pRaidInfo->nDiskCount -1; i++)
			{
				pDataBufferSrc = (PULONG)cdb.DataBuffer[i];

				for(j = 0; j < BlocksPerDisk; j++)
				{
					RtlCopyMemory(
						(PCHAR)pDataBufferSrc + j * Lurn->ChildBlockBytes,
						(PCHAR)Ccb->DataBuffer + (i + j * (pRaidInfo->nDiskCount -1)) * Lurn->ChildBlockBytes,
						Lurn->ChildBlockBytes);
				}
			}

			// generate parity
			// initialize the parity buffer with the first buffer
			RtlCopyMemory(
				cdb.DataBuffer[pRaidInfo->nDiskCount -1],
				cdb.DataBuffer[0],
				Lurn->ChildBlockBytes * BlocksPerDisk);

			// p' ^= p ^ d; from second buffer
			for(i = 1; i < pRaidInfo->nDiskCount -1; i++)
			{
				pDataBufferSrc = (PULONG)cdb.DataBuffer[i];
				pDataBufferParity = (PULONG)cdb.DataBuffer[pRaidInfo->nDiskCount -1];

				// parity work
				k = (BlocksPerDisk * Lurn->ChildBlockBytes) / sizeof(ULONG);
				while(k--)
				{
					*pDataBufferParity ^= *pDataBufferSrc;
					pDataBufferParity++;
					pDataBufferSrc++;
				}
			}

			status = LurnAssocSendCcbToChildrenArray(
				pRaidInfo->MapLurnChildren,
				pRaidInfo->nDiskCount,
				Ccb,
				LurnRAID4RCcbCompletion,
				&cdb,
				NULL,
				FALSE);

			if(!NT_SUCCESS(status))
			{
				for(i = 0; i < pRaidInfo->nDiskCount; i++)
				{
					ExFreeToNPagedLookasideList(
						&pRaidInfo->DataBufferLookaside,
						cdb.DataBuffer[i]);
				}
			}
		}
		break;
	case SCSIOP_VERIFY:
	case SCSIOP_VERIFY16:		
		{
			KDPrintM(DBG_LURN_NOISE,("SCSIOP_VERIFY\n"));

			status = LurnAssocSendCcbToChildrenArray(
				pRaidInfo->MapLurnChildren,
				pRaidInfo->nDiskCount,
				Ccb,
				LurnRAID4RCcbCompletion,
				NULL,
				NULL,
				FALSE);
		}
		break;

	case 0x3E:		// READ_LONG
	case SCSIOP_READ:
	case SCSIOP_READ16:		
		{
			ULONG DataBufferLengthPerDisk;
			ULONG i, j;
			CUSTOM_DATA_BUFFER cdb;

			KDPrintM(DBG_LURN_NOISE,("SCSIOP_READ\n"));

			DataBufferLengthPerDisk = Ccb->DataBufferLength / (pRaidInfo->nDiskCount -1);
			KDPrintM(DBG_LURN_NOISE,("R Ccb->DataBufferLength %d\n", Ccb->DataBufferLength));
			cdb.DataBufferCount = 0;
			for(i = 0; i < pRaidInfo->nDiskCount; i++)
			{
				cdb.DataBuffer[i] = ExAllocateFromNPagedLookasideList(&pRaidInfo->DataBufferLookaside);
				ASSERT(cdb.DataBuffer[i]);
				if(!cdb.DataBuffer[i])
				{
					// free allocated buffers
					for(j = 0; j < i; j++)
					{
						ExFreeToNPagedLookasideList(
							&pRaidInfo->DataBufferLookaside,
							cdb.DataBuffer[i]);
						cdb.DataBuffer[i] = NULL;
					}

					Ccb->CcbStatus = CCB_STATUS_SUCCESS;
					status = STATUS_INSUFFICIENT_RESOURCES;

					KDPrintM(DBG_LURN_ERROR, ("ExAllocateFromNPagedLookasideList Failed[%d]\n", i));
					LSCcbCompleteCcb(Ccb);
					break;
				}


				cdb.DataBufferLength[i] = DataBufferLengthPerDisk;
				cdb.DataBufferCount++;
			}

			// We should erase the buffer for defected child
			// We will fill this buffer using parity at completion routine
			if ((RAID_STATUS_EMERGENCY == pRaidInfo->RaidStatus ||
				RAID_STATUS_EMERGENCY_READY == pRaidInfo->RaidStatus ||
				RAID_STATUS_RECOVERING == pRaidInfo->RaidStatus))
			{
				RtlZeroMemory(Ccb->DataBuffer, Ccb->DataBufferLength);
			}

			cdb.DataBufferCount = pRaidInfo->nDiskCount;
			status = LurnAssocSendCcbToChildrenArray(
				pRaidInfo->MapLurnChildren,
				pRaidInfo->nDiskCount,
				Ccb,
				LurnRAID4RCcbCompletion,
				&cdb,
				NULL,
				FALSE
				);
			if(!NT_SUCCESS(status))
			{
				for(i = 0; i < pRaidInfo->nDiskCount; i++)
				{
					ExFreeToNPagedLookasideList(
						&pRaidInfo->DataBufferLookaside,
						cdb.DataBuffer[i]);
				}
			}
		}
		break;

	case SCSIOP_INQUIRY: {
		INQUIRYDATA			inquiryData;
		UCHAR				Model[16] = RAID4R_MODEL_NAME;


		KDPrintM(DBG_LURN_INFO,("SCSIOP_INQUIRY Ccb->Lun = 0x%x\n", Ccb->LurId[2]));
		//
		//	We don't support EVPD(enable vital product data).
		//
		if(Ccb->Cdb[1]  & CDB_INQUIRY_EVPD) {

			KDPrintM(DBG_LURN_ERROR,("SCSIOP_INQUIRY: got EVPD. Not supported.\n"));

			LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
			LSCcbSetStatus(Ccb, CCB_STATUS_COMMAND_FAILED);
			goto complete_here;
			break;
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
			Model,
			16
			);

		RtlCopyMemory(
			inquiryData.ProductRevisionLevel,
			PRODUCT_REVISION_LEVEL,
			(strlen(PRODUCT_REVISION_LEVEL)+1) < 4 ?  
			(strlen(PRODUCT_REVISION_LEVEL)+1) : 4
			);

		RtlMoveMemory (
			Ccb->DataBuffer,
			&inquiryData,
			Ccb->DataBufferLength > sizeof (INQUIRYDATA) ? 
			sizeof (INQUIRYDATA) : 
		Ccb->DataBufferLength
			);

		LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
		LSCcbSetStatusFlag(Ccb, CCBSTATUS_FLAG_TIMER_COMPLETE);
		Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;

		status = STATUS_SUCCESS;
		goto complete_here;
		break;
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
				readCapacityData->LogicalBlockAddress = 0xffffffff;
			}

			blockSize = Lurn->BlockBytes;
			REVERSE_BYTES(&readCapacityData->BytesPerBlock, &blockSize);

			LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
			LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
			LSCcbCompleteCcb(Ccb);
			break;
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

			blockSize = Lurn->BlockBytes;
			REVERSE_BYTES(&readCapacityDataEx->BytesPerBlock, &blockSize);

			LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
			LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
			LSCcbCompleteCcb(Ccb);
			break;
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

			blockSize = BLOCK_SIZE * (Lurn->LurnChildrenCnt - pRaidInfo->nSpareDisk - 1);
			REVERSE_BYTES(&readCapacityDataEx->BytesPerBlock, &blockSize);

			LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
			Ccb->CcbStatus = CCB_STATUS_SUCCESS;
			status = STATUS_SUCCESS;

			KDPrintM(DBG_LURN_TRACE, ("SCSIOP_READ_CAPACITY16: %08x : %04x\n", (UINT32)logicalBlockAddress, (UINT32)blockSize));
			goto complete_here;
			break;
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
					KDPrintM(DBG_LURN_INFO, 
						("SCSIOP_MODE_SENSE: MODE_DSP_WRITE_PROTECT\n"));
					parameterHeader->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;

					if(Lurn->Lur->LurFlags & LURFLAG_FAKEWRITE)
						parameterHeader->DeviceSpecificParameter &= ~MODE_DSP_WRITE_PROTECT;
				} else
					parameterHeader->DeviceSpecificParameter &= ~MODE_DSP_WRITE_PROTECT;

				parameterHeader->BlockDescriptorLength = sizeof(MODE_PARAMETER_BLOCK);

				//
				// Make Block.
				//
				BlockCount = (ULONG)(Lurn->EndBlockAddr - Lurn->StartBlockAddr + 1);
				parameterBlock->DensityCode = 0;	// It is Reserved for direct access devices.
				parameterBlock->BlockLength[0] = (BYTE)(Lurn->BlockBytes>>16);
				parameterBlock->BlockLength[1] = (BYTE)(Lurn->BlockBytes>>8);
				parameterBlock->BlockLength[2] = (BYTE)(Lurn->BlockBytes);

				LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
				Ccb->CcbStatus = CCB_STATUS_SUCCESS;	//SRB_STATUS_SUCCESS;
				goto complete_here;
			} else {
				KDPrintM(DBG_LURN_TRACE,
					("SCSIOP_MODE_SENSE: unsupported pagecode:%x\n", (ULONG)Cdb->MODE_SENSE.PageCode));
				LSCCB_SETSENSE(Ccb, SCSI_SENSE_ILLEGAL_REQUEST, SCSI_ADSENSE_INVALID_CDB, 0);
				LurnAssocRefreshCcbStatusFlag(Lurn, &Ccb->CcbStatusFlags);
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;	//SRB_STATUS_SUCCESS;
				goto complete_here;
			}
			break; 
		}

	default:
		//
		//	send to all child LURNs.
		//	Set WriteVerify completion routine to CCB
		//		it guarantees CCB_STATUS_SUCCESS when least one LURN is working fine.
		//

		{
			status = LurnAssocSendCcbToAllChildren(
				Lurn,
				Ccb,
				LurnRAID4RCcbCompletion,
				NULL,
				NULL,
				FALSE
				);
		}
		break;
	}

	return STATUS_SUCCESS;
complete_here:
	LSAssocSetRedundentRaidStatusFlag(pRaidInfo, Ccb);

	LSCcbCompleteCcb(Ccb);

	return status;
}

NTSTATUS
LurnRAID4RRequest(
				 PLURELATION_NODE	Lurn,
				 PCCB				Ccb
				 )
{
	NTSTATUS				status;
	PRAID_INFO pRaidInfo;

	pRaidInfo = Lurn->LurnRAIDInfo;

	//
	//	dispatch a request
	//
	KDPrintM(DBG_LURN_TRACE, ("IN\n"));

	switch(Ccb->OperationCode) {
	case CCB_OPCODE_EXECUTE:

		KDPrintM(DBG_LURN_TRACE, ("CCB_OPCODE_EXECUTE!\n"));
		LurnRAID4RExecute(Lurn, Ccb);
		break;

		//
		//	Send to all LURNs
		//
	case CCB_OPCODE_ABORT_COMMAND:
	case CCB_OPCODE_RESETBUS:
	case CCB_OPCODE_RESTART:
	case CCB_OPCODE_NOOP:
		{
			//
			//	Check to see if the CCB is coming for only this LURN.
			//
			if(LSCcbIsFlagOn(Ccb, CCB_FLAG_DONOT_PASSDOWN)) {
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				goto complete_here;
			}
			status = LurnAssocSendCcbToAllChildren(Lurn, Ccb, LurnRAID4RCcbCompletion, NULL, NULL, FALSE);
			break;
		}
	case CCB_OPCODE_STOP:
		{
			LARGE_INTEGER TimeOut;
			KIRQL oldIrql;

			KDPrintM(DBG_LURN_INFO, ("CCB_OPCODE_STOP\n"));

			ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);
			if(pRaidInfo->ThreadRecoverHandle)
			{
				Lurn->LurnStatus = LURN_STATUS_STOP_PENDING;
				LurnSetRaidInfoStatus(pRaidInfo, RAID_STATUS_TERMINATING);
				KDPrintM(DBG_LURN_INFO, ("KeSetEvent\n"));
				KeSetEvent(&pRaidInfo->RecoverThreadEvent,IO_NO_INCREMENT, FALSE);
			}
			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

			TimeOut.QuadPart = - NANO100_PER_SEC * 120;

			KDPrintM(DBG_LURN_INFO, ("KeWaitForSingleObject IN\n"));
			status = KeWaitForSingleObject(
				pRaidInfo->ThreadRecoverObject,
				Executive,
				KernelMode,
				FALSE,
				&TimeOut
				);

			KDPrintM(DBG_LURN_INFO, ("KeWaitForSingleObject OUT\n"));

			ASSERT(status == STATUS_SUCCESS);

			//
			//	Dereference the thread object.
			//

			ObDereferenceObject(pRaidInfo->ThreadRecoverObject);
			
			ACQUIRE_SPIN_LOCK(&Lurn->LurnSpinLock, &oldIrql);

			pRaidInfo->ThreadRecoverObject = NULL;
			pRaidInfo->ThreadRecoverHandle = NULL;

			RELEASE_SPIN_LOCK(&Lurn->LurnSpinLock, oldIrql);

			//
			//	Check to see if the CCB is coming for only this LURN.
			//
			if(LSCcbIsFlagOn(Ccb, CCB_FLAG_DONOT_PASSDOWN)) {
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				goto complete_here;
			}
			status = LurnAssocSendCcbToAllChildren(Lurn, Ccb, LurnRAID4RCcbCompletion, NULL, NULL, FALSE);
			break;
		}
	case CCB_OPCODE_UPDATE:
		{
			//
			//	Check to see if the CCB is coming for only this LURN.
			//
			if(LSCcbIsFlagOn(Ccb, CCB_FLAG_DONOT_PASSDOWN)) {
				LSCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
				goto complete_here;
			}
			status = LurnAssocSendCcbToAllChildren(Lurn, Ccb, LurnRAIDUpdateCcbCompletion, NULL, NULL, TRUE); // use same function as Mirror
			break;
		}

	case CCB_OPCODE_QUERY:
		status = LurnAssocQuery(Lurn, LurnAggrCcbCompletion, Ccb);
		break;

	default:
		KDPrintM(DBG_LURN_TRACE, ("INVALID COMMAND\n"));
		LSCcbSetStatus(Ccb, CCB_STATUS_INVALID_COMMAND);
		goto complete_here;
		break;
	}

	return STATUS_SUCCESS;

complete_here:
	LSAssocSetRedundentRaidStatusFlag(pRaidInfo, Ccb);

	LSCcbCompleteCcb(Ccb);

	return STATUS_SUCCESS;
}
#endif

//
// Currently we do not support hot-swap and RAID with conflict configuration.
// Return error if any of the member does not have expected value.
// Make user to resolve the problem using bindtool.
//
NTSTATUS
LurnRMDRead(
	IN PLURELATION_NODE		Lurn, 
	OUT PNDAS_RAID_META_DATA rmd,
	OUT PUINT32 UpTodateNode
)
{
	NTSTATUS status;
	ULONG i;
	PRAID_INFO pRaidInfo;
	NDAS_RAID_META_DATA rmd_tmp;
	UINT32 uiUSNMax;
	UINT32 FreshestNode = 0;
	BOOLEAN		UsedInDegraded[MAX_DRAID_MEMBER_DISK] = {0};
	
	// Update NodeFlags if it's RMD is missing or invalid.
	
	KDPrintM(DBG_LURN_INFO, ("IN\n"));

	pRaidInfo = Lurn->LurnRAIDInfo;
	ASSERT(pRaidInfo);

	uiUSNMax = 0;

	for(i = 0; i < Lurn->LurnChildrenCnt; i++)	// i is node flag
	{
		if(LURN_STATUS_RUNNING != Lurn->LurnChildren[i]->LurnStatus) {
			KDPrintM(DBG_LURN_INFO, ("Lurn is not running. Skip reading node %d.\n", i));
			continue;
		}
		status = LurnExecuteSyncRead(Lurn->LurnChildren[i], (PUCHAR)&rmd_tmp,
			NDAS_BLOCK_LOCATION_RMD, 1);

		if(!NT_SUCCESS(status)) {
			KDPrintM(DBG_LURN_INFO, ("Failed to read from node %d\n", i));
			uiUSNMax = 0;
			break;
		}
		if(NDAS_RAID_META_DATA_SIGNATURE != rmd_tmp.Signature || 
			!IS_RMD_CRC_VALID(crc32_calc, rmd_tmp))
		{
			KDPrintM(DBG_LURN_INFO, ("Node %d has invalid RMD. All disk must have RMD\n", i));
			uiUSNMax = 0;
			break;
		} else if (RtlCompareMemory(&Lurn->LurnRAIDInfo->RaidSetId, &rmd_tmp.RaidSetId, sizeof(GUID)) != sizeof(GUID)) {
			KDPrintM(DBG_LURN_INFO, ("Node %d is not member of this RAID set\n", i));
			uiUSNMax = 0;
			break;
		} else if (RtlCompareMemory(&Lurn->LurnRAIDInfo->ConfigSetId, &rmd_tmp.ConfigSetId, sizeof(GUID)) != sizeof(GUID)) {
			KDPrintM(DBG_LURN_INFO, ("Node %d has different configuration set.\n", i));
			//
			// To do: mark this node as defective and continue.
			//
			uiUSNMax = 0;
			break;			
		} else {
			if(uiUSNMax < rmd_tmp.uiUSN)
			{
				uiUSNMax = rmd_tmp.uiUSN;
				KDPrintM(DBG_LURN_INFO, ("Found newer RMD USN %x from node %d\n", uiUSNMax, i));

				// newer one
				RtlCopyMemory(rmd, &rmd_tmp, sizeof(NDAS_RAID_META_DATA));
				FreshestNode = i;
			}
			if (rmd_tmp.state & NDAS_RAID_META_DATA_STATE_USED_IN_DEGRADED) {
				UsedInDegraded[i] = TRUE;
			}
		}
	}

	if(0 == uiUSNMax)
	{
		// This can happen if information that svc given is different from actual RMD.
		KDPrintM(DBG_LURN_INFO, ("Cannot find valid RMD or some LURN does not have valid RMD.\n"));
		RtlZeroMemory(rmd, sizeof(NDAS_RAID_META_DATA));
		status = STATUS_UNSUCCESSFUL;
		ASSERT(FALSE); // You can ignore this. Simply RAID will be unmounted.
	} else {
		status = STATUS_SUCCESS;
	}

	if (Lurn->LurnType == LURN_RAID1R) {
		//
		// Check UsedInDegraded flag is conflicted
		//	(We can assume RAID map is same if ConfigurationSetId matches)
		//		Check active member is all marked as used in degraded mode.
		//
		if (UsedInDegraded[rmd->UnitMetaData[0].iUnitDeviceIdx] == TRUE &&
			UsedInDegraded[rmd->UnitMetaData[1].iUnitDeviceIdx] == TRUE) {
			// Both disk is used in degraded mode. User need to solve this problem.
			// fail ReadRmd
			KDPrintM(DBG_LURN_INFO, ("All active members had been independently mounted in degraded mode. Conflict RAID situation. Cannot continue\n"));
			RtlZeroMemory(rmd, sizeof(NDAS_RAID_META_DATA));
			status = STATUS_UNSUCCESSFUL;
			uiUSNMax = 0;
		}
	}

	if (UpTodateNode)
		*UpTodateNode = FreshestNode;

	KDPrintM(DBG_LURN_INFO, ("OUT\n"));
	return status;
}


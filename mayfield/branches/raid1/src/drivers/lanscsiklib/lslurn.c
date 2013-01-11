#include <ntddk.h>
//#include "public.h"
#include "ver.h"
#include "LSKlib.h"
#include "KDebug.h"
#include "basetsdex.h"
#include "cipher.h"
#include "LSProto.h"
#include "LSTransport.h"
#include "LSCcb.h"
#include "LSLurn.h"
#include "LSLurnIDE.h"
#include "LSLurnAssoc.h"

#ifdef __MODULE__
#undef __MODULE__
#endif // __MODULE__
#define __MODULE__ "LSLurn"

//////////////////////////////////////////////////////////////////////////
//
//	LU Relation
//
typedef struct _LURNDESC_TABLE {
	PLURELATION_NODE_DESC	LurnDesc;
	PLURELATION_NODE		Lurn;
} LURNDESC_ENTRY, *PLURNDESC_ENTRY;


//
//	Send Stop Ccb to the LURN.
//
NTSTATUS
SendStopCcbToLurn(
		PLURELATION_NODE	Lurn
	) {
	PCCB		ccb;
	NTSTATUS	ntStatus;

	KDPrintM(DBG_LURN_TRACE,("entered.\n"));
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	ntStatus = LSCcbAllocate(&ccb);
	if(!NT_SUCCESS(ntStatus)) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	LSCCB_INITIALIZE(ccb, 0);
	ccb->OperationCode				= CCB_OPCODE_STOP;
	ccb->HwDeviceExtension			= NULL;
	LSCcbSetFlag(ccb, CCB_FLAG_SYNCHRONOUS|CCB_FLAG_ALLOCATED|CCB_FLAG_LOWER_LURN);
	LSCcbSetCompletionRoutine(ccb, NULL, NULL);

	//
	//	Send a CCB to the LURN.
	//
	ntStatus = LurnRequest(
			Lurn,
			ccb
		);
	if(!NT_SUCCESS(ntStatus)) {
		LSCcbPostCompleteCcb(ccb);
		return ntStatus;
	}

	return STATUS_SUCCESS;
}


static
void
LurSetDefaultConfiguration(
	PLURELATION	Lur,
	UINT32		DefaultLurFlags
) {
	Lur->LurFlags = DefaultLurFlags;
}


static
void
LurModifyConfiguration(
	PLURELATION			Lur,
	PLURELATION_DESC	LurDesc
) {

	if(LurDesc->LurOptions & LUROPTION_OFF_WRITESHARE_PS) {
		Lur->LurFlags &= ~LURFLAG_WRITESHARE_PS;
	} else if(LurDesc->LurOptions & LUROPTION_OFF_WRITESHARE_PS) {
		Lur->LurFlags |= LURFLAG_WRITESHARE_PS;
	} else if(LurDesc->LurOptions & LUROPTION_OFF_FAKEWRITE) {
		Lur->LurFlags &= ~LURFLAG_FAKEWRITE;
	} else if(LurDesc->LurOptions & LUROPTION_ON_WRITESHARE_PS) {
		Lur->LurFlags |= LURFLAG_FAKEWRITE;
	}
#ifdef __ENABLE_CONTENTENCRYPT_AES_TEST__

	LurDesc->CntEcrMethod = NDAS_CONTENTENCRYPT_METHOD_AES;
	LurDesc->CntEcrKeyLength = 32;
	LurDesc->CntEcrKey[0] = 0x1a;
	LurDesc->CntEcrKey[1] = 0xc3;
	LurDesc->CntEcrKey[2] = 0x4d;
	LurDesc->CntEcrKey[3] = 0x59;
	LurDesc->CntEcrKey[4] = 0x1a;
	LurDesc->CntEcrKey[5] = 0xc1;
	LurDesc->CntEcrKey[6] = 0xad;
	LurDesc->CntEcrKey[7] = 0x5d;
	LurDesc->CntEcrKey[8] = 0xca;
	LurDesc->CntEcrKey[9] = 0xcd;
	LurDesc->CntEcrKey[10] = 0x8d;
	LurDesc->CntEcrKey[11] = 0x50;
	LurDesc->CntEcrKey[12] = 0x12;
	LurDesc->CntEcrKey[13] = 0xc1;
	LurDesc->CntEcrKey[14] = 0xfd;
	LurDesc->CntEcrKey[15] = 0x5e;
	LurDesc->CntEcrKey[16] = 0x1a;
	LurDesc->CntEcrKey[17] = 0x33;
	LurDesc->CntEcrKey[18] = 0x43;
	LurDesc->CntEcrKey[19] = 0x59;
	LurDesc->CntEcrKey[20] = 0x3a;
	LurDesc->CntEcrKey[21] = 0xc3;
	LurDesc->CntEcrKey[22] = 0x43;
	LurDesc->CntEcrKey[23] = 0x59;
	LurDesc->CntEcrKey[24] = 0x2a;
	LurDesc->CntEcrKey[25] = 0xc2;
	LurDesc->CntEcrKey[26] = 0x2d;
	LurDesc->CntEcrKey[27] = 0x5c;
	LurDesc->CntEcrKey[28] = 0x9a;
	LurDesc->CntEcrKey[29] = 0xc0;
	LurDesc->CntEcrKey[30] = 0xdd;
	LurDesc->CntEcrKey[31] = 0x5a;

#endif
}


NTSTATUS
LurCreate(
		PLURELATION_DESC	LurDesc,
		UINT32				DefaultLurFlags,
		PLURELATION			*Lur,
		PVOID				AdapterFdo,
		LURN_EVENT_CALLBACK	LurnEventCallback
	) {
	ULONG					idx_lurn;
	ULONG					idx_child;
	PLURELATION_NODE_DESC	cur_lurndesc;
	PLURELATION_NODE		cur_lurn;
	LURNDESC_ENTRY			lurndesc_table[LUR_MAX_LURNS_PER_LUR];
	NTSTATUS				status;
	ULONG					child;
	PLURELATION				tmpLur;
	ULONG					tmpLurLength;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
	ASSERT(LurDesc->LurnDescCount > 0);
	ASSERT(LurDesc->LurnDescCount <= LUR_MAX_LURNS_PER_LUR);

	if(LurDesc->LurnDescCount <= 0) {
		KDPrintM(DBG_LURN_ERROR, ("No child node.\n"));
		return STATUS_INVALID_PARAMETER;
	}

	RtlZeroMemory(lurndesc_table, sizeof(LURNDESC_ENTRY) * LUR_MAX_LURNS_PER_LUR);
	tmpLur = NULL;

	//
	//	create LURELATION
	//
	tmpLurLength = sizeof(LURELATION) + sizeof(PLURELATION_NODE) * (LurDesc->LurnDescCount - 1);
	tmpLur = (PLURELATION)ExAllocatePoolWithTag(NonPagedPool, tmpLurLength, LUR_POOL_TAG);
	if(!tmpLur) {
		KDPrintM(DBG_LURN_ERROR, ("ExAllocatePoolWithTag() failed.\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	tmpLur->Type				= LSSTRUC_TYPE_LUR;
	tmpLur->Length				= sizeof(LURELATION);
	tmpLur->MaxBlocksPerRequest = LurDesc->MaxBlocksPerRequest;
	tmpLur->DesiredAccess		= tmpLur->GrantedAccess = LurDesc->AccessRight;
	tmpLur->DevType				= LurDesc->DevType;
	tmpLur->NodeCount			= LurDesc->LurnDescCount;
	RtlCopyMemory(tmpLur->LurId, LurDesc->LurId, LURID_LENGTH);

	tmpLur->AdapterFdo			= AdapterFdo;
	tmpLur->LurnEventCallback	= LurnEventCallback;

	//
	//	Set LurFlags to default values or values set by a user.
	//
	LurSetDefaultConfiguration(tmpLur, DefaultLurFlags);
	LurModifyConfiguration(tmpLur, LurDesc);

	//
	//	Content encryption
	//
	tmpLur->CntEcrMethod		= LurDesc->CntEcrMethod;
	tmpLur->CntEcrKeyLength		= LurDesc->CntEcrKeyLength;
	if(LurDesc->CntEcrKeyLength) {
		RtlCopyMemory(tmpLur->CntEcrKey, LurDesc->CntEcrKey, tmpLur->CntEcrKeyLength);
#if DBG
		{
			ULONG	keyIdx;
			KDPrintM(DBG_LURN_INFO, ("Encryption key method:%02x, key length:%d * 4bytes, ",
												LurDesc->CntEcrMethod,
												(int)LurDesc->CntEcrKeyLength)
												);
			for(keyIdx = 0; keyIdx<tmpLur->CntEcrKeyLength; keyIdx++) {
				KDPrintM(DBG_LURN_INFO, ("%02x ", (int)tmpLur->CntEcrKey[keyIdx]));
				if((keyIdx%4) == 0)
					KDPrintM(DBG_LURN_INFO, (" "));
			}
			KDPrintM(DBG_LURN_INFO, ("\n"));
		}
#endif

	}

	//
	//	Build Lurndesc Table and sanity check.
	//
	RtlZeroMemory(lurndesc_table, sizeof(LURNDESC_ENTRY)*LUR_MAX_LURNS_PER_LUR);
	cur_lurndesc = LurDesc->LurnDesc;
	for(idx_lurn = 0; idx_lurn < LurDesc->LurnDescCount; idx_lurn++) {

		KDPrintM(DBG_LURN_INFO, ("Idx:%d LurnDesc:%p NextOffset:%d\n", idx_lurn, cur_lurndesc, cur_lurndesc->NextOffset)) ;
		lurndesc_table[idx_lurn].LurnDesc = cur_lurndesc;

		if(cur_lurndesc->NextOffset == 0 && idx_lurn + 1 < LurDesc->LurnDescCount) {
			KDPrintM(DBG_LURN_ERROR, ("Invaild NextOffset.\n"));
			status = STATUS_INVALID_PARAMETER;
			goto error_out;
		}
		if(cur_lurndesc->LurnType < 0 || cur_lurndesc->LurnType >= LurnInterfaceCnt) {
			KDPrintM(DBG_LURN_ERROR, ("Invaild LurnType:%x.\n", cur_lurndesc->LurnType));
			status = STATUS_INVALID_PARAMETER;
			goto error_out;
		}

		status = LurnAllocate(&lurndesc_table[idx_lurn].Lurn, lurndesc_table[idx_lurn].LurnDesc->LurnChildrenCnt);
		if(!NT_SUCCESS(status)) {
			KDPrintM(DBG_LURN_ERROR, ("LurnAllocate() failed.\n"));
			goto error_out;
		}

		//
		//	Also initialize LURN tables in the LUR.
		//
		tmpLur->Nodes[idx_lurn] = lurndesc_table[idx_lurn].Lurn;

		cur_lurndesc = (PLURELATION_NODE_DESC)((PBYTE)LurDesc + cur_lurndesc->NextOffset);
	}

	//
	//	Build tree. Set up LURN.
	//
retry_init:

	for(idx_lurn = 0; idx_lurn < LurDesc->LurnDescCount; idx_lurn++) {
		cur_lurndesc= lurndesc_table[idx_lurn].LurnDesc;
		cur_lurn	= lurndesc_table[idx_lurn].Lurn;

		// set parent
		//	set reconnection parameters
		//	NOTE: ReconnInterval of LurnDesc is in the millisecond unit.
		//		  Need to convert to the 100 nanosecond unit.
		cur_lurn->LurnParent		= lurndesc_table[cur_lurndesc->LurnParent].Lurn;

		cur_lurndesc->AccessRight = tmpLur->GrantedAccess;
		status = LurnInitialize(cur_lurn, tmpLur, cur_lurndesc);

		if((LURN_IDE_ODD != cur_lurndesc->LurnType) 
			&& (LURN_IDE_MO != cur_lurndesc->LurnType) ){


			if(tmpLur->LurFlags & LURFLAG_WRITESHARE_PS) {

				if(status == STATUS_ACCESS_DENIED && (tmpLur->GrantedAccess & GENERIC_WRITE)) {
						ULONG	idx_closinglurn;

					if(LurDesc->LurOptions & LUROPTION_OFF_FAKEWRITE) {
						KDPrintM(DBG_LURN_ERROR, ("LUROPTION_OFF_FAKEWRITE is set. Can not go to Secondary mode.\n"));
					} else {
						for(idx_closinglurn = 0; idx_closinglurn < idx_lurn; idx_closinglurn++) {
							SendStopCcbToLurn(lurndesc_table[idx_closinglurn].Lurn);
							LurnDestroy(lurndesc_table[idx_closinglurn].Lurn);
						}

						KDPrintM(DBG_LURN_ERROR, ("Degrade access right and retry.\n"));
						tmpLur->GrantedAccess &= ~GENERIC_WRITE;
						tmpLur->LurFlags |= LURFLAG_FAKEWRITE;

						goto retry_init;
					}
				}
			}
		}
		if(!NT_SUCCESS(status)) {
			ULONG	idx_closinglurn;

			KDPrintM(DBG_LURN_ERROR, ("LurnInitialize() failed. LURN#:%d NTSTATUS:%08lx\n", idx_lurn, status));
			for(idx_closinglurn = 0; idx_closinglurn < idx_lurn; idx_closinglurn++) {
				SendStopCcbToLurn(lurndesc_table[idx_closinglurn].Lurn);
				LurnDestroy(lurndesc_table[idx_closinglurn].Lurn);
			}
			goto error_out;
		}

		//
		// set LurnId. It must be the index number in LUR->Nodes[] array.
		//
		cur_lurn->LurnId = cur_lurndesc->LurnId;

		// set children
		cur_lurn->LurnChildrenCnt = cur_lurndesc->LurnChildrenCnt;
		for(idx_child = 0; idx_child < cur_lurndesc->LurnChildrenCnt; idx_child++) {
			child = cur_lurndesc->LurnChildren[idx_child];
			if(child < 0 || child > LurDesc->LurnDescCount)
			{
				if(!NT_SUCCESS(status)) {
					KDPrintM(DBG_LURN_ERROR, ("invalid child number.\n"));
					goto error_out;
				}
			}
			cur_lurn->LurnChildren[idx_child] = lurndesc_table[child].Lurn;
//			lurndesc_table[child].Lurn->LurnParent = cur_lurn;
		}
	}

	//
	// set Lur
	//
	*Lur = tmpLur;

	return STATUS_SUCCESS;

error_out:
	for(idx_lurn = 0; idx_lurn < LurDesc->LurnDescCount; idx_lurn++) {
		if(lurndesc_table[idx_lurn].Lurn) {
			KDPrintM(DBG_LURN_ERROR, ("Free LURN:%p(%d)\n", lurndesc_table[idx_lurn].Lurn, idx_lurn));
			LurnFree(lurndesc_table[idx_lurn].Lurn);
			lurndesc_table[idx_lurn].Lurn = 0;
		}
	}

	if(tmpLur) {
		ExFreePoolWithTag(tmpLur, LUR_POOL_TAG);

	}

	return status;
}

static
VOID
LurCloseRecur(
	  PLURELATION_NODE			Lurn
	) {
	ULONG					idx_lurn;

	for(idx_lurn = 0; idx_lurn < Lurn->LurnChildrenCnt; idx_lurn++ ) {

		LurCloseRecur(Lurn->LurnChildren[idx_lurn]);
	}

	LurnDestroy(Lurn);
	ExFreePoolWithTag(Lurn, LURN_POOL_TAG);
}


VOID
LurClose(
		PLURELATION			Lur
	) {

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	if(LUR_GETROOTNODE(Lur) == NULL) {
		KDPrintM(DBG_LURN_INFO, ("No LURN.\n"));
	} else {
		LurCloseRecur(LUR_GETROOTNODE(Lur));
	}

	ExFreePoolWithTag(Lur, LUR_POOL_TAG);
}

NTSTATUS
LurRequest(
		PLURELATION			Lur,
		PCCB				Ccb
	) {

	if(!Lur) {
		KDPrintM(DBG_LURN_ERROR, ("LUR pointer NULL!\n"));
		return STATUS_INVALID_PARAMETER;
	}
	if(Lur->NodeCount <= 0) {
		KDPrintM(DBG_LURN_ERROR, ("No child LURN.\n"));
		return STATUS_INVALID_PARAMETER;
	}
	return LurnRequest(LUR_GETROOTNODE(Lur),Ccb);
}


#define TRANSLATE_UNITDISK_TO_LURN(LURN_POINTER, UNITDISK_POINER, NEXTOFFSET, LURNID, LURNTYPE, STARTADDR, ENDADDR, UNITBLOCKS, MAXBLOCKREQ, BYTESINBLOCK, CHILDCNT, PARENT) {	\
		(LURN_POINTER)->NextOffset			= (NEXTOFFSET);															\
		(LURN_POINTER)->LurnId				= (LURNID);																\
		(LURN_POINTER)->LurnType			= (LURNTYPE);															\
		(LURN_POINTER)->AccessRight			= 0;																	\
		(LURN_POINTER)->StartBlockAddr		= (STARTADDR);															\
		(LURN_POINTER)->EndBlockAddr		= (ENDADDR);															\
		(LURN_POINTER)->UnitBlocks			= (UNITBLOCKS);															\
		(LURN_POINTER)->MaxBlocksPerRequest	= (MAXBLOCKREQ);														\
		(LURN_POINTER)->BytesInBlock		= (BYTESINBLOCK);														\
		(LURN_POINTER)->LurnOptions			= (UNITDISK_POINER)->LurnOptions;										\
		(LURN_POINTER)->ReconnTrial			= (UNITDISK_POINER)->ReconnTrial;										\
		(LURN_POINTER)->ReconnInterval		= (UNITDISK_POINER)->ReconnInterval;									\
		LSTRANS_COPY_LPXADDRESS(&(LURN_POINTER)->LurnIde.BindingAddress, &(UNITDISK_POINER)->NICAddr);				\
		LSTRANS_COPY_LPXADDRESS(&(LURN_POINTER)->LurnIde.TargetAddress, &(UNITDISK_POINER)->Address);				\
		RtlCopyMemory(&(LURN_POINTER)->LurnIde.UserID, &(UNITDISK_POINER)->iUserID, LSPROTO_USERID_LENGTH);			\
		RtlCopyMemory(&(LURN_POINTER)->LurnIde.Password, &(UNITDISK_POINER)->iPassword, LSPROTO_PASSWORD_LENGTH);	\
		(LURN_POINTER)->LurnIde.HWType		= (UNITDISK_POINER)->ucHWType;											\
		(LURN_POINTER)->LurnIde.HWVersion	= (UNITDISK_POINER)->ucHWVersion;										\
		LSTRANS_COPY_LPXADDRESS(&(LURN_POINTER)->LurnIde.TargetAddress, &(UNITDISK_POINER)->Address);				\
		(LURN_POINTER)->LurnIde.LanscsiTargetID = (UNITDISK_POINER)->ucUnitNumber;									\
		(LURN_POINTER)->LurnIde.LanscsiLU	= 0;																	\
		(LURN_POINTER)->LurnChildrenCnt		= (CHILDCNT);															\
		(LURN_POINTER)->LurnParent			= (PARENT); }


NTSTATUS
LurTranslateAddTargetDataToLURDesc(
	   PLANSCSI_ADD_TARGET_DATA	AddTargetData,
	   ULONG					MaxBlocksPerRequest,
	   LONG						LurDescLengh,
	   PLURELATION_DESC			LurDesc
	) {
	LONG						ChildLurnCnt;
	LONG						idx_childlurn;
	PBYTE						cur_location;
	PLURELATION_NODE_DESC		cur_lurn;
	PLSBUS_UNITDISK				cur_unitdisk;


	UNREFERENCED_PARAMETER(LurDescLengh);

	KDPrintM(DBG_LURN_ERROR, ("In.\n"));

	ChildLurnCnt	= AddTargetData->ulNumberOfUnitDiskList;
	cur_location	= (PBYTE)LurDesc;
	if(ChildLurnCnt <= 0) {
		KDPrintM(DBG_LURN_ERROR, ("ERROR : AddTargetData->ulNumberOfUnitDiskList = %d\n", AddTargetData->ulNumberOfUnitDiskList));
		return STATUS_INVALID_PARAMETER;
	}

	//
	//	LUR descriptor
	//
	LurDesc->Type = LSSTRUC_TYPE_LUR;
	LurDesc->Length = sizeof(LURELATION_DESC);
	LurDesc->LurOptions = AddTargetData->LurOptions;
	LurDesc->LurId[0] = 0;
	LurDesc->LurId[1] = 0;
	LurDesc->LurId[2] = 0;
	LurDesc->MaxBlocksPerRequest = MaxBlocksPerRequest;
	LurDesc->LurnDescCount = 0;
	LurDesc->AccessRight = AddTargetData->DesiredAccess;
	cur_location += SIZE_OF_LURELATION_DESC();
	cur_lurn = (PLURELATION_NODE_DESC)cur_location;
	cur_unitdisk = AddTargetData->UnitDiskList;

	//
	//	Encryption key
	//
	LurDesc->CntEcrMethod		= AddTargetData->CntEcrMethod;
	LurDesc->CntEcrKeyLength	= AddTargetData->CntEcrKeyLength;
	if(LurDesc->CntEcrMethod && LurDesc->CntEcrKeyLength) {
		RtlCopyMemory(LurDesc->CntEcrKey, AddTargetData->CntEcrKey, AddTargetData->CntEcrKeyLength);
#if DBG
		{
			ULONG	keyIdx;
			KDPrintM(DBG_LURN_INFO, ("Encryption key method:%02x, key length:%d, ",
				LurDesc->CntEcrMethod,
				(int)LurDesc->CntEcrKeyLength)
				);
			for(keyIdx = 0; keyIdx<LurDesc->CntEcrKeyLength; keyIdx++) {
				KDPrintM(DBG_LURN_INFO, ("%02x ", (int)LurDesc->CntEcrKey[keyIdx]));
				if((keyIdx%4) == 0)
					KDPrintM(DBG_LURN_INFO, (" "));
			}
			KDPrintM(DBG_LURN_INFO, ("\n"));
		}
#endif

	}

	switch(AddTargetData->ucTargetType) {
//	Added by ILGU HONG 2004_07_06
	case DISK_TYPE_DVD:

		LurDesc->DevType = LUR_DEVTYPE_ODD;

		LurDesc->Length += sizeof(LURELATION_NODE_DESC) - sizeof(LONG);
		LurDesc->LurnDescCount ++;
		TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
									cur_unitdisk,
									0,
                                    0,
									LURN_IDE_ODD,
									0,
									cur_unitdisk->ulUnitBlocks-1,
									cur_unitdisk->ulUnitBlocks,
									MaxBlocksPerRequest,
									BLOCK_SIZE,
									0,
									0
									);
		KDPrintM(DBG_LURN_INFO, ("TYPE : DVD UserID:%08lx Password:%I64x\n",
										cur_lurn->LurnIde.UserID,
										cur_lurn->LurnIde.Password
									));

		break;
	case DISK_TYPE_MO:

		LurDesc->DevType = LUR_DEVTYPE_MOD;

		LurDesc->Length += sizeof(LURELATION_NODE_DESC) - sizeof(LONG);
		LurDesc->LurnDescCount ++;
		TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
									cur_unitdisk,
									0,
                                    0,
									LURN_IDE_MO,
									0,
									cur_unitdisk->ulUnitBlocks-1,
									cur_unitdisk->ulUnitBlocks,
									MaxBlocksPerRequest,
									BLOCK_SIZE,
									0,
									0
									);
		KDPrintM(DBG_LURN_INFO, ("TYPE : MO UserID:%08lx Password:%I64x\n",
										cur_lurn->LurnIde.UserID,
										cur_lurn->LurnIde.Password
									));
		break;
//	Added by ILGU HONG 2004_07_06 end
	case DISK_TYPE_NORMAL:

		LurDesc->DevType = LUR_DEVTYPE_HDD;

		LurDesc->Length += sizeof(LURELATION_NODE_DESC) - sizeof(LONG);
		LurDesc->LurnDescCount ++;
		TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
									cur_unitdisk,
									0,
									0,
									LURN_IDE_DISK,
									0,
									cur_unitdisk->ulUnitBlocks-1,
									cur_unitdisk->ulUnitBlocks,
									MaxBlocksPerRequest,
									BLOCK_SIZE,
									0,
									0
									);
		KDPrintM(DBG_LURN_INFO, ("UserID:%08lx Password:%I64x\n",
										cur_lurn->LurnIde.UserID,
										cur_lurn->LurnIde.Password
									));
		break;
	case DISK_TYPE_MIRROR: {
		UINT16					length;
		PLURELATION_NODE_DESC	RootNode;

		LurDesc->DevType = LUR_DEVTYPE_HDD;

		//
		//	Build a LurnDesc for mirroring
		//
		RootNode = cur_lurn;
		length = sizeof(LURELATION_NODE_DESC) + sizeof(LONG);
		LurDesc->Length += length;
		LurDesc->LurnDescCount ++;
		TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
									cur_unitdisk,
									(cur_location - (PBYTE)LurDesc) + length,
									0,
									LURN_MIRRORING,
									0,
									cur_unitdisk->ulUnitBlocks-1,
									cur_unitdisk->ulUnitBlocks,
									MaxBlocksPerRequest,
									BLOCK_SIZE,
									2,
									0
									);

		//
		//	Build two children
		//
		for(idx_childlurn = 0; idx_childlurn < ChildLurnCnt; idx_childlurn++ ) {

			cur_location += length;
			cur_lurn = (PLURELATION_NODE_DESC)(cur_location);
			cur_unitdisk = AddTargetData->UnitDiskList + idx_childlurn;
			RootNode->LurnChildren[idx_childlurn] = idx_childlurn + 1;
			length = sizeof(LURELATION_NODE_DESC) - sizeof(LONG);
			LurDesc->Length	+= length;
			LurDesc->LurnDescCount ++;
			TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
									cur_unitdisk,
									(cur_location - (PBYTE)LurDesc) + length,
									idx_childlurn + 1,
									LURN_IDE_DISK,
									0,
									cur_unitdisk->ulUnitBlocks-1,
									cur_unitdisk->ulUnitBlocks,
									MaxBlocksPerRequest,
									BLOCK_SIZE,
									0,
									0
									);
		}
		//
		//	set 0 offset to the last one.
		//
		cur_lurn->NextOffset = 0;
		break;
	}
	case DISK_TYPE_AGGREGATION:
		{
		UINT16					length;
		PLURELATION_NODE_DESC	RootNode;
		ULONG					StartBlockAddress;
		ULONG					UnitBlocks;

		LurDesc->DevType = LUR_DEVTYPE_HDD;
		//
		//	Build a LurnDesc for aggregation
		//
//#if 1
//		AddTargetData->UnitDiskList[0].ulUnitBlocks = 66;
//#endif

		RootNode = cur_lurn;
		length = sizeof(LURELATION_NODE_DESC) + sizeof(LONG) * ((UINT16)ChildLurnCnt-1);
		LurDesc->Length += length;
		LurDesc->LurnDescCount ++;
		UnitBlocks = 0;
		for(idx_childlurn = 0; idx_childlurn < ChildLurnCnt; idx_childlurn++ ) {
				UnitBlocks += AddTargetData->UnitDiskList[idx_childlurn].ulUnitBlocks;
		}
		TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
									cur_unitdisk,
									(cur_location - (PBYTE)LurDesc) + length,
									0,
									LURN_AGGREGATION,
									0,
									UnitBlocks-1,
									UnitBlocks,
									MaxBlocksPerRequest,
									BLOCK_SIZE,
									ChildLurnCnt,
									0
									);
		//
		//	Build childrens
		//
		StartBlockAddress = 0;
		for(idx_childlurn = 0; idx_childlurn < ChildLurnCnt; idx_childlurn++ ) {

			cur_location += length;
			cur_lurn = (PLURELATION_NODE_DESC)(cur_location);
			cur_unitdisk = AddTargetData->UnitDiskList + idx_childlurn;
			LurDesc->LurnDescCount ++;

			//	set a child index to the root.
			RootNode->LurnChildren[idx_childlurn] = idx_childlurn + 1;

			// initialize the child.
			length = sizeof(LURELATION_NODE_DESC) - sizeof(LONG);
			LurDesc->Length += length;
			TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
									cur_unitdisk,
									(cur_location - (PBYTE)LurDesc) + length,
									idx_childlurn + 1,
									LURN_IDE_DISK,
									StartBlockAddress,
									StartBlockAddress + cur_unitdisk->ulUnitBlocks-1,
									cur_unitdisk->ulUnitBlocks,
									MaxBlocksPerRequest,
									BLOCK_SIZE,
									0,
									0
									);
			StartBlockAddress += cur_unitdisk->ulUnitBlocks;
		}
		//
		//	set 0 offset to the last one.
		//
		cur_lurn->NextOffset = 0;
		break;
	}
	case DISK_TYPE_BIND_RAID0:
	{
		UINT16					length;
		PLURELATION_NODE_DESC	RootNode;
		ULONG					UnitBlocks;

		LurDesc->DevType = LUR_DEVTYPE_HDD;
		// root
		RootNode = cur_lurn;
		length = (UINT16)SIZE_OF_LURELATION_NODE_DESC(ChildLurnCnt);
		LurDesc->Length += length;
		LurDesc->LurnDescCount ++;
		UnitBlocks = 0;

		UnitBlocks = AddTargetData->UnitDiskList[0].ulUnitBlocks;

		for(idx_childlurn = 0; idx_childlurn < ChildLurnCnt; idx_childlurn++ ) {
			if(AddTargetData->UnitDiskList[idx_childlurn].ulUnitBlocks != AddTargetData->UnitDiskList[0].ulUnitBlocks)
				ASSERT(FALSE);
		}

		TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
									cur_unitdisk,
									(cur_location - (PBYTE)LurDesc) + length,
									0,
									LURN_RAID0,
									0,
									UnitBlocks-1,
									UnitBlocks,
									MaxBlocksPerRequest,
									BLOCK_SIZE,
									ChildLurnCnt,
									0
									);

		for(idx_childlurn = 0; idx_childlurn < ChildLurnCnt; idx_childlurn++)
		{
			cur_location += length;
			cur_lurn = (PLURELATION_NODE_DESC)(cur_location);
			length = (UINT16)SIZE_OF_LURELATION_NODE_DESC(0);
			LurDesc->Length += length;
			LurDesc->LurnDescCount++;
			UnitBlocks = AddTargetData->UnitDiskList[idx_childlurn].ulUnitBlocks;
			cur_unitdisk = &AddTargetData->UnitDiskList[idx_childlurn];

			// set a child index to the root.
			RootNode->LurnChildren[idx_childlurn] = 1 + idx_childlurn;

			TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
										cur_unitdisk, // use leaf 0
										(cur_location - (PBYTE)LurDesc) + length,
										RootNode->LurnChildren[idx_childlurn],
										LURN_IDE_DISK,
										0,
										UnitBlocks,
										UnitBlocks,
										MaxBlocksPerRequest,
										BLOCK_SIZE,
										0,
										0
										);

		}

		//
		//	set 0 offset to the last one.
		//
		cur_lurn->NextOffset = 0;
	}
	break;
	case DISK_TYPE_BIND_RAID1:
		{
		UINT16					length;
		PLURELATION_NODE_DESC	RootNode;
		ULONG					StartBlockAddress;
		ULONG					UnitBlocks;
		LONG					nMirrorDisks = ChildLurnCnt /2;
		PLURELATION_NODE_DESC	cur_lurn_mirror;
		LONG					idx_leaf;

		LurDesc->DevType = LUR_DEVTYPE_HDD;
//#if 1
//		AddTargetData->UnitDiskList[0].ulUnitBlocks = 66;
//#endif

		// root
		RootNode = cur_lurn;
		length = (UINT16)SIZE_OF_LURELATION_NODE_DESC(nMirrorDisks);
		LurDesc->Length += length;
		LurDesc->LurnDescCount ++;
		UnitBlocks = 0;
		for(idx_childlurn = 0; idx_childlurn < nMirrorDisks; idx_childlurn++ ) {
				UnitBlocks += AddTargetData->UnitDiskList[idx_childlurn *2].ulUnitBlocks;
		}
		TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
									cur_unitdisk,
									(cur_location - (PBYTE)LurDesc) + length,
									0,
									LURN_AGGREGATION,
									0,
									UnitBlocks-1,
									UnitBlocks,
									MaxBlocksPerRequest,
									BLOCK_SIZE,
									nMirrorDisks,
									0
									);
		// mirrored disks
		StartBlockAddress = 0;
		for(idx_childlurn = 0; idx_childlurn < nMirrorDisks; idx_childlurn++ )
		{
			cur_location += length;
			cur_lurn = (PLURELATION_NODE_DESC)(cur_location);
			length = (UINT16)SIZE_OF_LURELATION_NODE_DESC(2);
			LurDesc->Length += length;
			LurDesc->LurnDescCount ++;
			UnitBlocks = AddTargetData->UnitDiskList[idx_childlurn *2].ulUnitBlocks;
			cur_unitdisk =  &AddTargetData->UnitDiskList[idx_childlurn *2];

			//	set a child index to the root.
			RootNode->LurnChildren[idx_childlurn] = 1 + idx_childlurn *3;
			
			// mirror disk
			TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
										cur_unitdisk, // use leaf 0
										(cur_location - (PBYTE)LurDesc) + length,
										RootNode->LurnChildren[idx_childlurn],
										LURN_RAID1,
										StartBlockAddress,
										StartBlockAddress + UnitBlocks-1,
										UnitBlocks,
										MaxBlocksPerRequest,
										BLOCK_SIZE,
										2,
										0
										);

			// copy RAID 1 information
			RtlCopyMemory(&cur_lurn->LurnInfoRAID[0], &AddTargetData->UnitDiskList[idx_childlurn *2].RAID_Info, sizeof(INFO_RAID));
			RtlCopyMemory(&cur_lurn->LurnInfoRAID[1], &AddTargetData->UnitDiskList[idx_childlurn *2 +1].RAID_Info, sizeof(INFO_RAID));
			
			// mirror leaf disks

			cur_lurn_mirror = cur_lurn;
			for(idx_leaf = 0; idx_leaf < 2; idx_leaf++)
			{
				cur_lurn_mirror->LurnChildren[idx_leaf] = 1 + idx_childlurn *3 + (1 + idx_leaf); // set children node

				cur_location += length;
				cur_lurn = (PLURELATION_NODE_DESC)(cur_location);
				length = (UINT16)SIZE_OF_LURELATION_NODE_DESC(0);
				LurDesc->Length += length;
				LurDesc->LurnDescCount ++;

				TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
											cur_unitdisk + idx_leaf,
											((PBYTE)cur_lurn - (PBYTE)LurDesc) + length,
											cur_lurn_mirror->LurnChildren[idx_leaf],
											LURN_IDE_DISK,
											StartBlockAddress,
											StartBlockAddress + UnitBlocks-1,
											UnitBlocks,
											MaxBlocksPerRequest,
											BLOCK_SIZE,
											0,
											(1 + idx_childlurn *3)
											);
			}

			StartBlockAddress += UnitBlocks;
		}

		//
		//	set 0 offset to the last one.
		//
		cur_lurn->NextOffset = 0;
		break;
	}
	case DISK_TYPE_BIND_RAID4:
	{
		UINT16					length;
		PLURELATION_NODE_DESC	RootNode;
		ULONG					UnitBlocks;

		LurDesc->DevType = LUR_DEVTYPE_HDD;
		// root
		RootNode = cur_lurn;
		length = (UINT16)SIZE_OF_LURELATION_NODE_DESC(ChildLurnCnt);
		LurDesc->Length += length;
		LurDesc->LurnDescCount ++;
		UnitBlocks = 0;

		UnitBlocks = AddTargetData->UnitDiskList[0].ulUnitBlocks; // RAID4 property

		for(idx_childlurn = 0; idx_childlurn < ChildLurnCnt; idx_childlurn++ ) {
			if(AddTargetData->UnitDiskList[idx_childlurn].ulUnitBlocks != AddTargetData->UnitDiskList[0].ulUnitBlocks)
				ASSERT(FALSE);
		}

		TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
									cur_unitdisk,
									(cur_location - (PBYTE)LurDesc) + length,
									0,
									LURN_RAID4,
									0,
									UnitBlocks-1,
									UnitBlocks,
									MaxBlocksPerRequest,
									BLOCK_SIZE,
									ChildLurnCnt,
									0
									);

		for(idx_childlurn = 0; idx_childlurn < ChildLurnCnt; idx_childlurn++)
		{
			cur_location += length;
			cur_lurn = (PLURELATION_NODE_DESC)(cur_location);
			length = (UINT16)SIZE_OF_LURELATION_NODE_DESC(0);
			LurDesc->Length += length;
			LurDesc->LurnDescCount++;
			UnitBlocks = AddTargetData->UnitDiskList[idx_childlurn].ulUnitBlocks;
			cur_unitdisk = &AddTargetData->UnitDiskList[idx_childlurn];

			// set a child index to the root.
			RootNode->LurnChildren[idx_childlurn] = 1 + idx_childlurn;

			TRANSLATE_UNITDISK_TO_LURN(	cur_lurn,
										cur_unitdisk, // use leaf 0
										(cur_location - (PBYTE)LurDesc) + length,
										RootNode->LurnChildren[idx_childlurn],
										LURN_IDE_DISK,
										0,
										UnitBlocks,
										UnitBlocks,
										MaxBlocksPerRequest,
										BLOCK_SIZE,
										0,
										0
										);
			// copy RAID4 information
			RtlCopyMemory(&RootNode->LurnInfoRAID[idx_childlurn], &AddTargetData->UnitDiskList[idx_childlurn].RAID_Info, sizeof(INFO_RAID));
		}

		//
		//	set 0 offset to the last one.
		//
		cur_lurn->NextOffset = 0;
	}
	break;
	default:
		return STATUS_INVALID_PARAMETER;
	}

	return STATUS_SUCCESS;
}

//
//	Call back to the miniport device.
//
VOID
LurCallBack(
	PLURELATION	Lur,
	PLURN_EVENT	LurnEvent
) {
	ASSERT(Lur);

	if(Lur->LurnEventCallback) {
		Lur->LurnEventCallback(Lur, LurnEvent);
	}
}


//////////////////////////////////////////////////////////////////////////
//
//	LU Relation node
//
UINT32			LurnInterfaceCnt	= NR_LURN_INTERFACE;
PLURN_INTERFACE	LurnInterfaceList[NR_LURN_MAX_INTERFACE] = {
					&LurnAggrInterface,
					&LurnMirrorInterface,
					&LurnIdeDiskInterface,
					&LurnIdeODDInterface,
					&LurnIdeMOInterface,
					&LurnRAID1Interface,
					&LurnRAID4Interface,
					&LurnRAID0Interface,
			};


static
void
LurnSetDefaultConfiguration(
	PLURELATION_NODE	Lurn
) {
	Lurn->ReconnTrial		= RECONNECTION_MAX_TRY;
	Lurn->ReconnInterval	= MAX_RECONNECTION_INTERVAL;
}


static
void
LurnModifyConfiguration(
	PLURELATION_NODE		Lurn,
	PLURELATION_NODE_DESC	LurDesc
) {

	if(LurDesc->LurnOptions & LURNOPTION_SET_RECONNECTION) {
		Lurn->ReconnTrial		= LurDesc->ReconnTrial;
		Lurn->ReconnInterval	= (UINT64)LurDesc->ReconnInterval * 10000;
	}

}


NTSTATUS
LurnInitialize(
		PLURELATION_NODE		Lurn,
		PLURELATION				Lur,
		PLURELATION_NODE_DESC	LurnDesc
	) {
	LONG				Length;
	NTSTATUS			status;

	KDPrintM(DBG_LURN_ERROR, ("In. Lurn : %p, Lur : %p, LurnDesc : %p\n", Lurn, Lur, LurnDesc));

	ASSERT(Lurn);
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	if(Lur && LurnDesc)
	{
		// normal initialize

		if(LurnDesc->LurnType < 0 || LurnDesc->LurnType >= LurnInterfaceCnt ) {
			KDPrintM(DBG_LURN_ERROR, ("Error in Lurn Type %x\n", LurnDesc->LurnType));
			return STATUS_INVALID_PARAMETER;
		}

		//
		//	set default values.
		//	Do not zero pointers to children.
		//
		Length = sizeof(LURELATION_NODE) + (LurnDesc->LurnChildrenCnt-1) * sizeof(PLURELATION_NODE);
		RtlZeroMemory(Lurn, Length);
		KeInitializeSpinLock(&Lurn->LurnSpinLock);

		Lurn->LurnId = LurnDesc->LurnId;
		Lurn->Length = (UINT16)Length;
		Lurn->Type = LSSTRUC_TYPE_LURN;
		Lurn->LurnType = LurnDesc->LurnType;
		Lurn->Lur = Lur;
		Lurn->LurnChildrenCnt = LurnDesc->LurnChildrenCnt;
		Lurn->LurnInterface = LurnInterfaceList[LurnDesc->LurnType];

		Lurn->StartBlockAddr = LurnDesc->StartBlockAddr;
		Lurn->EndBlockAddr = LurnDesc->EndBlockAddr;
		Lurn->UnitBlocks = LurnDesc->UnitBlocks;
		Lurn->AccessRight = LurnDesc->AccessRight;

		LurnSetDefaultConfiguration(Lurn);
		LurnModifyConfiguration(Lurn, LurnDesc);
	}
	else
	{
		// revive mode
		KDPrintM(DBG_LURN_ERROR, ("Revive Lurn : %08x\n", Lurn));
	}

	if(!Lurn->LurnInterface->LurnFunc.LurnInitialize) {
		KDPrintM(DBG_LURN_ERROR, ("lurntype %x interface not implements LurnInitialize furnction\n", LurnDesc->LurnType ));
		return STATUS_NOT_IMPLEMENTED;
	}
	status = Lurn->LurnInterface->LurnFunc.LurnInitialize(
					Lurn,
					LurnDesc
				);
	if(NT_SUCCESS(status)) {
		Lurn->LurnStatus = LURN_STATUS_RUNNING;
	}

	KDPrintM(DBG_LURN_ERROR, ("return 0x%08lx : Idx:%d\n", status, Lurn->LurnId));

	return status;
}


NTSTATUS
LurnDestroy(
		PLURELATION_NODE Lurn
	) {
	NTSTATUS ntStatus;

	KDPrintM(DBG_LURN_ERROR, ("In. Lurn %p\n", Lurn));

	ASSERT(Lurn);
	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	if(!Lurn->LurnInterface->LurnFunc.LurnDestroy) {
		return STATUS_NOT_IMPLEMENTED;
	}

	ntStatus = Lurn->LurnInterface->LurnFunc.LurnDestroy(
					Lurn
				);
	ASSERT(NT_SUCCESS(ntStatus));
	if(!NT_SUCCESS(ntStatus)) {
		return ntStatus;
	}

	KDPrintM(DBG_LURN_ERROR, ("return : Idx:%d\n", Lurn->LurnId));

	return STATUS_SUCCESS;
}


NTSTATUS
LurnRequest(
		PLURELATION_NODE Lurn,
		PCCB Ccb
	) {
	ASSERT(Lurn);
	ASSERT(Ccb->CcbCurrentStackLocation == &Ccb->CcbStackLocation[Ccb->CcbCurrentStackLocationIndex]);

	if(!Lurn->LurnInterface->LurnFunc.LurnRequest) {
		return STATUS_NOT_IMPLEMENTED;
	}

	Ccb->CcbCurrentStackLocation--;
	Ccb->CcbCurrentStackLocationIndex--;
	Ccb->CcbCurrentStackLocation->Lurn = Lurn;

	if(Ccb->CcbCurrentStackLocationIndex < 0) {
		return STATUS_DATA_OVERRUN;
	}

	return Lurn->LurnInterface->LurnFunc.LurnRequest(
					Lurn,
					Ccb
				);
}



NTSTATUS
LurnAllocate(
		PLURELATION_NODE	*Lurn,
		LONG				ChildrenCnt
	) {
	ULONG	size;

	size = sizeof(LURELATION_NODE) + (ChildrenCnt-1) * sizeof(PLURELATION_NODE);
	*Lurn = ExAllocatePoolWithTag(
					NonPagedPool,
					size,
					LURN_POOL_TAG
				);
	RtlZeroMemory(*Lurn, size);

	return (*Lurn == NULL)?STATUS_INSUFFICIENT_RESOURCES:STATUS_SUCCESS;

}


VOID
LurnFree(
		PLURELATION_NODE Lurn
	) {

	ExFreePoolWithTag(
		Lurn,
		LURN_POOL_TAG
    );

}



//////////////////////////////////////////////////////////////////////////
//
//	LURN interface dafault functions
//
NTSTATUS
LurnInitializeDefault(
		PLURELATION_NODE		Lurn,
		PLURELATION_NODE_DESC	LurnDesc
	) {

	UNREFERENCED_PARAMETER(Lurn);
	UNREFERENCED_PARAMETER(LurnDesc);

	return STATUS_SUCCESS;
}

NTSTATUS
LurnDestroyDefault(
		PLURELATION_NODE Lurn
	) {

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	if(Lurn->LurnExtension)
		ExFreePoolWithTag(Lurn->LurnExtension, LURNEXT_POOL_TAG);

	return STATUS_SUCCESS;
}

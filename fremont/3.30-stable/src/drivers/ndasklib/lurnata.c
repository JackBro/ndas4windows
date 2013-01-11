#include "ndasscsiproc.h"

#ifdef __MODULE__
#undef __MODULE__
#endif // __MODULE__
#define __MODULE__ "LurnIde"


//////////////////////////////////////////////////////////////////////////
//
//	common to all IDE interface
//
void
ConvertString(
		PCHAR	result,
		PCHAR	source,
		int	size
	) {
	int	i;

	for(i = 0; i < size / 2; i++) {
		result[i * 2] = source[i * 2 + 1];
		result[i * 2 + 1] = source[i * 2];
	}
	result[size] = '\0';
	
}


BOOLEAN
Lba_capacity_is_ok(
				   struct hd_driveid *id
				   )
{
	unsigned _int32	lba_sects, chs_sects, head, tail;

	if((id->command_set_2 & 0x0400) && (id->cfs_enable_2 & 0x0400)) {
		// 48 Bit Drive.
		return TRUE;
	}

	/*
		The ATA spec tells large drivers to return
		C/H/S = 16383/16/63 independent of their size.
		Some drives can be jumpered to use 15 heads instead of 16.
		Some drives can be jumpered to use 4092 cyls instead of 16383
	*/
	if((id->cyls == 16383 || (id->cyls == 4092 && id->cur_cyls== 16383)) 
		&& id->sectors == 63 
		&& (id->heads == 15 || id->heads == 16)
		&& id->lba_capacity >= (unsigned)(16383 * 63 * id->heads))
		return TRUE;
	
	lba_sects = id->lba_capacity;
	chs_sects = id->cyls * id->heads * id->sectors;

	/* Perform a rough sanity check on lba_sects: within 10% is OK */
	if((lba_sects - chs_sects) < chs_sects / 10) {
		return TRUE;
	}

	/* Some drives have the word order reversed */
	head = ((lba_sects >> 16) & 0xffff);
	tail = (lba_sects & 0xffff);
	lba_sects = (head | (tail << 16));
	if((lba_sects - chs_sects) < chs_sects / 10) {
		id->lba_capacity = lba_sects;
		KDPrintM(DBG_LURN_ERROR, ("Lba_capacity_is_ok: Capacity reversed....\n"));
		return TRUE;
	}

	return FALSE;
}

//
//	Ide query
//
NTSTATUS
LurnIdeQuery(
		PLURELATION_NODE	Lurn,
		PLURNEXT_IDE_DEVICE	IdeDev,
		PCCB				Ccb
	) {
    PLUR_QUERY			LurQuery;
	NTSTATUS			status;


    //
    // Start off being paranoid.
    //
    if (Ccb->DataBuffer == NULL) {
		KDPrintM(DBG_LURN_ERROR, ("DataBuffer is NULL\n"));
        return STATUS_UNSUCCESSFUL;
    }
	status = STATUS_SUCCESS;
	Ccb->CcbStatus = CCB_STATUS_SUCCESS;

	//
    // Extract the query
    //
    LurQuery = (PLUR_QUERY)Ccb->DataBuffer;

    switch (LurQuery->InfoClass)
	{
	case LurPrimaryLurnInformation:
		{
			PLURN_PRIMARYINFORMATION	ReturnInfo;
			PLURN_INFORMATION			LurnInfo;

			KDPrintM(DBG_LURN_INFO, ("LurPrimaryLurnInformation\n"));

			ReturnInfo = (PLURN_PRIMARYINFORMATION)LUR_QUERY_INFORMATION(LurQuery);
			ReturnInfo->Length = sizeof(LURN_PRIMARYINFORMATION);
			LurnInfo = &ReturnInfo->PrimaryLurn;

			RtlZeroMemory( LurnInfo, sizeof(LURN_INFORMATION) );

			LurnInfo->Length = sizeof(LURN_INFORMATION);
			LurnInfo->UnitBlocks = Lurn->UnitBlocks;
			LurnInfo->BlockBytes = Lurn->BlockBytes;
			LurnInfo->AccessRight = Lurn->AccessRight;

			if(IdeDev) {
				LurnInfo->UnitDiskId = IdeDev->LuHwData.LanscsiTargetID;
				LurnInfo->UnitDiskId = IdeDev->LuHwData.LanscsiTargetID;

				if(Lurn->Lur->EnabledNdasFeatures & NDASFEATURE_SECONDARY)
					LurnInfo->Connections = 1;
				else
					LurnInfo->Connections = 2;

				LurnInfo->NdasBindingAddress = IdeDev->LanScsiSession->NdasBindAddress;
				LurnInfo->NdasNetDiskAddress = IdeDev->LanScsiSession->NdasNodeAddress; 

				RtlCopyMemory(&LurnInfo->UserID, &IdeDev->LanScsiSession->UserID, LSPROTO_USERID_LENGTH);
				RtlCopyMemory(&LurnInfo->Password, &IdeDev->LanScsiSession->Password, LSPROTO_PASSWORD_LENGTH);

			} else if (Lurn->LurnDesc) {
				
				// IdeDev can be null if in degraded mode, but RAID will send this request always to first member.
				// We should set some value for this case.
				
				LurnInfo->UnitDiskId = Lurn->LurnDesc->LurnIde.LanscsiTargetID;
				LurnInfo->Connections = 0;

				LurnInfo->NdasBindingAddress = Lurn->LurnDesc->LurnIde.BindingAddress;	
				LurnInfo->NdasNetDiskAddress = Lurn->LurnDesc->LurnIde.TargetAddress;
				
				RtlCopyMemory( &LurnInfo->UserID, &Lurn->LurnDesc->LurnIde.UserID, LSPROTO_USERID_LENGTH );
				RtlCopyMemory( &LurnInfo->Password, &Lurn->LurnDesc->LurnIde.Password, LSPROTO_PASSWORD_LENGTH );
			} else {
				LurnInfo->UnitDiskId = 0;
				LurnInfo->Connections = 0;

				RtlZeroMemory( &LurnInfo->NdasBindingAddress, sizeof(TA_NDAS_ADDRESS) ); 

				LurnInfo->NdasBindingAddress.Address[0].AddressType = TDI_ADDRESS_TYPE_INVALID;

				RtlZeroMemory( &LurnInfo->NdasNetDiskAddress, sizeof(TA_NDAS_ADDRESS) );

				LurnInfo->NdasNetDiskAddress.Address[0].AddressType = TDI_ADDRESS_TYPE_INVALID;

				RtlZeroMemory( &LurnInfo->UserID, LSPROTO_USERID_LENGTH );
				RtlZeroMemory( &LurnInfo->Password, LSPROTO_PASSWORD_LENGTH );
			}

			RtlZeroMemory( LurnInfo->PrimaryId, LURN_PRIMARY_ID_LENGTH );

#if 1
			RtlCopyMemory( &LurnInfo->PrimaryId[0], LurnInfo->NdasNetDiskAddress.Address, 8 );
#else
			RtlCopyMemory( &LurnInfo->PrimaryId[0], &LurnInfo->NdasNetDiskAddress.Address[0].NdasAddress.Lpx.Port, 2 );
			RtlCopyMemory( &LurnInfo->PrimaryId[2], LurnInfo->NdasNetDiskAddress.Address[0].NdasAddress.Lpx.Node, 6 );
#endif

			//RtlCopyMemory( &LurnInfo->PrimaryId[0], LurnInfo->NdasNetDiskAddress.Address, 8 );

			LurnInfo->PrimaryId[8] = LurnInfo->UnitDiskId;

			DebugTrace( NDASSCSI_DBG_LURN_IDE_ERROR, 
						("NdscId: %x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x:%x\n", 
						 LurnInfo->PrimaryId[0], LurnInfo->PrimaryId[1], LurnInfo->PrimaryId[2], LurnInfo->PrimaryId[3],
						 LurnInfo->PrimaryId[4], LurnInfo->PrimaryId[5], LurnInfo->PrimaryId[6], LurnInfo->PrimaryId[7],
						 LurnInfo->PrimaryId[8], LurnInfo->PrimaryId[9], LurnInfo->PrimaryId[10], LurnInfo->PrimaryId[11],
						 LurnInfo->PrimaryId[12], LurnInfo->PrimaryId[13], LurnInfo->PrimaryId[14], LurnInfo->PrimaryId[15]) );
		}
		break;
	case LurEnumerateLurn:
		{
			PLURN_ENUM_INFORMATION	ReturnInfo;
			PLURN_INFORMATION		LurnInfo;

			KDPrintM(DBG_LURN_ERROR, ("LurEnumerateLurn\n"));

			ReturnInfo = (PLURN_ENUM_INFORMATION)LUR_QUERY_INFORMATION(LurQuery);
			LurnInfo = &ReturnInfo->Lurns[Lurn->LurnId];

			LurnInfo->Length = sizeof(LURN_INFORMATION);


			LurnInfo->UnitBlocks  = Lurn->UnitBlocks;
			LurnInfo->BlockBytes  = Lurn->BlockBytes;
			LurnInfo->AccessRight = Lurn->AccessRight;

			if(IdeDev) {
				LurnInfo->UnitDiskId = IdeDev->LuHwData.LanscsiTargetID;
				if(Lurn->Lur->EnabledNdasFeatures & NDASFEATURE_SECONDARY)
					LurnInfo->Connections = 1;
				else
					LurnInfo->Connections = 2;

				LurnInfo->NdasBindingAddress = IdeDev->LanScsiSession->NdasBindAddress;

				LurnInfo->NdasNetDiskAddress = IdeDev->LanScsiSession->NdasNodeAddress;

				RtlCopyMemory(&LurnInfo->UserID, &IdeDev->LanScsiSession->UserID, LSPROTO_USERID_LENGTH);
				RtlCopyMemory(&LurnInfo->Password, &IdeDev->LanScsiSession->Password, LSPROTO_PASSWORD_LENGTH);
			} else {
				LurnInfo->UnitDiskId = 0;
				LurnInfo->Connections = 0;

				RtlZeroMemory( &LurnInfo->NdasBindingAddress, sizeof(TA_NDAS_ADDRESS) ); 

				LurnInfo->NdasBindingAddress.Address[0].AddressType = TDI_ADDRESS_TYPE_INVALID;

				RtlZeroMemory( &LurnInfo->NdasNetDiskAddress, sizeof(TA_NDAS_ADDRESS) ); 

				LurnInfo->NdasNetDiskAddress.Address[0].AddressType = TDI_ADDRESS_TYPE_INVALID;

				RtlZeroMemory(&LurnInfo->UserID, LSPROTO_USERID_LENGTH);
				RtlZeroMemory(&LurnInfo->Password, LSPROTO_PASSWORD_LENGTH);
			}

			LurnInfo->LurnId = Lurn->LurnId;
			LurnInfo->LurnType = Lurn->LurnType;
			LurnInfo->StatusFlags = Lurn->LurnStatus;

		}
		break;

	case LurRefreshLurn:
		{
			PLURN_REFRESH			ReturnInfo;

			KDPrintM(DBG_LURN_TRACE, ("LurRefreshLurn\n"));

			ReturnInfo = (PLURN_REFRESH)LUR_QUERY_INFORMATION(LurQuery);

			if(LURN_STATUS_STOP == Lurn->LurnStatus)
			{
				KDPrintM(DBG_LURN_ERROR, ("!!!!!!!! LURN_STATUS_STOP == Lurn->LurnStatus !!!!!!!!\n"));
				ReturnInfo->CcbStatusFlags |= CCBSTATUS_FLAG_LURN_STOP;
			}
		}
		break;

	default:
		Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
		break;
	}


	return status;
}

//
//	Request dispatcher for all IDE devices.
//
NTSTATUS
LurnIdeUpdate(
		PLURELATION_NODE		Lurn,
		PLURNEXT_IDE_DEVICE		IdeExt,
		PCCB					Ccb
	) {
	NTSTATUS		status;
	PLURN_UPDATE	LurnUpdate;
	KIRQL			oldIrql;

	LurnUpdate = (PLURN_UPDATE)Ccb->DataBuffer;

	//
	//	Update the LanscsiSession
	//
	switch(LurnUpdate->UpdateClass) {
	case LURN_UPDATECLASS_WRITEACCESS_USERID:
	case LURN_UPDATECLASS_READONLYACCESS:
		{
		LSSLOGIN_INFO		LoginInfo;
		LSPROTO_TYPE		LSProto;
		PLANSCSI_SESSION	NewLanScsiSession;

		TA_NDAS_ADDRESS		BindingAddress;
		TA_NDAS_ADDRESS  	TargetAddress;

		BYTE				pdu_response;
		LARGE_INTEGER		genericTimeOut;

		//
		//	Send NOOP to make sure that the Lanscsi Node is reachable.
		//
		status = LspNoOperation(IdeExt->LanScsiSession, IdeExt->LuHwData.LanscsiTargetID, &pdu_response, NULL);
		if(!NT_SUCCESS(status) || pdu_response != LANSCSI_RESPONSE_SUCCESS) {
			KDPrintM(DBG_LURN_ERROR, ("NOOP failed during update\n"));
			Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
			break;
		}

#if 0
		{
			static LONG	debugCount = 0;
			LONG	ccbSuccess;

			ccbSuccess = InterlockedIncrement(&debugCount);
			if((ccbSuccess%2) == 1) {
				KDPrintM(DBG_LURN_ERROR, ("force Update CCB to fail.\n"));
				Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
				break;
			}
		}
#endif

		//
		//	Try to make a new connection with write rights.
		//
		NewLanScsiSession = ExAllocatePoolWithTag(NonPagedPool, sizeof(LANSCSI_SESSION), LSS_POOLTAG);
		if(NewLanScsiSession == NULL) {
			KDPrintM(DBG_LURN_ERROR, ("LURN_UPDATECLASS_WRITEACCESS_USERID: ExAllocatePool() failed.\n"));
			Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
			break;
		}
		RtlZeroMemory(NewLanScsiSession, sizeof(LANSCSI_SESSION));
		LspGetAddresses(IdeExt->LanScsiSession, &BindingAddress, &TargetAddress);

		//
		//	Set timeouts.
		//

		genericTimeOut.QuadPart = -LURN_IDE_GENERIC_TIMEOUT;
		LspSetDefaultTimeOut(NewLanScsiSession, &genericTimeOut);

		status = LspConnectMultiBindTaAddr( NewLanScsiSession,
										    &BindingAddress,
										    NULL,
										    NULL,
										    &TargetAddress,
										    TRUE,
											NULL,
											NULL,
										    &genericTimeOut );

		if(!NT_SUCCESS(status)) {
			KDPrintM(DBG_LURN_ERROR, ("LURN_UPDATECLASS_WRITEACCESS_USERID: LspConnect(), Can't Connect to the LS node. STATUS:0x%08x\n", status));
			Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
			ExFreePool(NewLanScsiSession);
			break;
		}

		//
		//	Upgrade the access right.
		//	Login to the Lanscsi Node.
		//
		LspBuildLoginInfo(IdeExt->LanScsiSession, &LoginInfo);
		status = LspLookupProtocol(LoginInfo.HWType, LoginInfo.HWVersion, &LSProto);
		if(!NT_SUCCESS(status)) {
			KDPrintM(DBG_LURN_ERROR, ("Wrong hardware version.\n"));
			LspDisconnect(
					NewLanScsiSession
				);
			Ccb->CcbStatus = CCB_STATUS_COMMAND_FAILED;
			ExFreePool(NewLanScsiSession);
			break;
		}

		if(LurnUpdate->UpdateClass == LURN_UPDATECLASS_WRITEACCESS_USERID) {

			if(LoginInfo.UserID & 0xffff0000) {
				KDPrintM(DBG_LURN_ERROR, ("WRITE/READONLYACCESS: the write-access has been in UserID(%08lx)."
											"We don't need to do it again.\n", LoginInfo.UserID));
				LspDisconnect(
						NewLanScsiSession
					);
				Ccb->CcbStatus = CCB_STATUS_SUCCESS;
				ExFreePool(NewLanScsiSession);
				break;
			}

			//
			//	Add Write access.
			//

			LoginInfo.UserID = LoginInfo.UserID | (LoginInfo.UserID << 16);

		} else if(LurnUpdate->UpdateClass == LURN_UPDATECLASS_READONLYACCESS) {

			if(!(LoginInfo.UserID & 0xffff0000)) {
				KDPrintM(DBG_LURN_ERROR, ("WRITE/READONLYACCESS: the readonly-access has been in UserID(%08lx)."
					"We don't need to do it again.\n", LoginInfo.UserID));
				LspDisconnect(
					NewLanScsiSession
					);
				Ccb->CcbStatus = CCB_STATUS_SUCCESS;
				ExFreePool(NewLanScsiSession);
				break;
			}

			//
			//	Remove Write access.
			//

			LoginInfo.UserID = LoginInfo.UserID & ~(LoginInfo.UserID << 16);

		}

		status = LspLogin(
						NewLanScsiSession,
						&LoginInfo,
						LSProto,
						NULL,
						TRUE
					);
		if(!NT_SUCCESS(status)) {
			KDPrintM(DBG_LURN_ERROR, ("WRITE/READONLYACCESS: LspLogin(), Can't log into the LS node. STATUS:0x%08x\n", status));
			LspDisconnect(
					NewLanScsiSession
				);
			// Assume we can connect and negotiate but we can't get RW right due to another connection.
			Ccb->CcbStatus = CCB_STATUS_NO_ACCESS;
			ExFreePool(NewLanScsiSession);
			break;
		}

		//
		//	Disconnect the original session.
		//	And copy NewLanscsiSession to LanscsiSession.
		//
		LspLogout(
				IdeExt->LanScsiSession,
				NULL
			);

		LspDisconnect(
				IdeExt->LanScsiSession
			);

		ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);

		if(LurnUpdate->UpdateClass == LURN_UPDATECLASS_WRITEACCESS_USERID) {
			Lurn->AccessRight |= GENERIC_WRITE;

			//
			//	If this is root LURN, update LUR access right.
			//
			if (LURN_IS_ROOT_NODE(Lurn)) {
				Lurn->Lur->EnabledNdasFeatures &= ~NDASFEATURE_SECONDARY;
			}
		} else if(LurnUpdate->UpdateClass == LURN_UPDATECLASS_READONLYACCESS) {
			Lurn->AccessRight &= ~GENERIC_WRITE;

			//
			//	If this is root LURN, update LUR access right.
			//
			if (LURN_IS_ROOT_NODE(Lurn)) {
				Lurn->Lur->EnabledNdasFeatures |= NDASFEATURE_SECONDARY;
			}
		}

		LspMove(IdeExt->LanScsiSession, NewLanScsiSession, TRUE);

		RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

		ExFreePool(NewLanScsiSession);

		Ccb->CcbStatus = CCB_STATUS_SUCCESS;
		break;
	}
	default:
		Ccb->CcbStatus = CCB_STATUS_INVALID_COMMAND;
		break;
	}

	return STATUS_SUCCESS;
}

VOID
LurnIdeStop(
		PLURELATION_NODE Lurn,
		PCCB Ccb
	) {
	PLURNEXT_IDE_DEVICE		IdeDisk;
	KIRQL					oldIrql;

	ASSERT(Lurn->IdeDisk);
	UNREFERENCED_PARAMETER(Ccb);

	KDPrintM(DBG_LURN_INFO, ("entered.\n"));

	IdeDisk = Lurn->IdeDisk;

	ACQUIRE_SPIN_LOCK(&Lurn->SpinLock, &oldIrql);
	CcbCompleteList(&IdeDisk->CcbQueue, CCB_STATUS_RESET, CCBSTATUS_FLAG_LURN_STOP |
		Lurn->LurnStopReasonCcbStatusFlags);
	RELEASE_SPIN_LOCK(&Lurn->SpinLock, oldIrql);

	LsCcbSetStatus(Ccb, CCB_STATUS_SUCCESS);
}

NTSTATUS
LurnIdeRestart(
		PLURELATION_NODE	Lurn,
		PCCB				Ccb
	) {
	NTSTATUS	status;

	UNREFERENCED_PARAMETER(Lurn);
	UNREFERENCED_PARAMETER(Ccb);

	KDPrintM(DBG_LURN_INFO, ("entered.\n"));
	status = STATUS_NOT_IMPLEMENTED;


	return status;
}

NTSTATUS
LurnIdeAbortCommand(
		PLURELATION_NODE Lurn,
		PCCB Ccb
	) {
	NTSTATUS	status;

	UNREFERENCED_PARAMETER(Lurn);
	UNREFERENCED_PARAMETER(Ccb);

	KDPrintM(DBG_LURN_INFO, ("entered.\n"));
	status = STATUS_NOT_IMPLEMENTED;


	return status;
}


NTSTATUS
LSLurnIdeNoop(
		PLURELATION_NODE		Lurn,
		PLURNEXT_IDE_DEVICE		IdeExt,
		PCCB					Ccb
	) {
	BYTE		PduResponse;
	NTSTATUS	status;

	KDPrintM(DBG_LURN_TRACE, ("Send NOOP to Remote.\n"));

	UNREFERENCED_PARAMETER(Lurn);

	status = LspNoOperation(IdeExt->LanScsiSession, IdeExt->LuHwData.LanscsiTargetID, &PduResponse, NULL);
	if(!NT_SUCCESS(status)) {
		KDPrintM(DBG_LURN_ERROR, ("LspNoOperation() failed. NTSTATUS:%08lx\n", status));
		if(Ccb) Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
		return STATUS_PORT_DISCONNECTED;

	} else if(PduResponse != LANSCSI_RESPONSE_SUCCESS) {
		KDPrintM(DBG_LURN_ERROR, ("Failure reply. PDUSTATUS:%08lx\n", (int)PduResponse));

		if(Ccb) Ccb->CcbStatus = CCB_STATUS_COMMUNICATION_ERROR;
		return STATUS_PORT_DISCONNECTED;
	}

	if(Ccb) Ccb->CcbStatus = CCB_STATUS_SUCCESS;

	return STATUS_SUCCESS;
}

VOID
IdeLurnClose (
	PLURELATION_NODE Lurn
	) 
{
	PLURNEXT_IDE_DEVICE	IdeDev = Lurn->IdeDisk;

	NDAS_ASSERT( KeGetCurrentIrql() == PASSIVE_LEVEL );

	if (IdeDev) {

		NDAS_ASSERT( IdeDev->PrimaryLanScsiSession.LanscsiProtocol == NULL );
		NDAS_ASSERT( IdeDev->CommLanScsiSession.LanscsiProtocol == NULL );

		LMDestroy( &IdeDev->BuffLockCtl );

		if (IdeDev->CntEcrBuffer && IdeDev->CntEcrBufferLength) {
		
			ExFreePoolWithTag( IdeDev->CntEcrBuffer, LURNEXT_ENCBUFF_TAG );
			IdeDev->CntEcrBuffer = NULL;
		}

		if (IdeDev->WriteCheckBuffer) {
		
			ExFreePoolWithTag( IdeDev->WriteCheckBuffer, LURNEXT_WRITECHECK_BUFFER_POOLTAG );
			IdeDev->WriteCheckBuffer = NULL;
		}

		if (IdeDev->CntEcrKey) {

			CloseCipherKey( IdeDev->CntEcrKey );
		}

		if (IdeDev->CntEcrCipher) {

			CloseCipher( IdeDev->CntEcrCipher );
		}
	}

	if(Lurn->IdeDisk) {
		ExFreePoolWithTag(Lurn->IdeDisk, LURNEXT_POOL_TAG);
		Lurn->IdeDisk =NULL;
	}

	return;
}



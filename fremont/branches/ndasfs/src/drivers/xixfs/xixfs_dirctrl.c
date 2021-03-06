#include "xixfs_types.h"
#include "xixfs_drv.h"
#include "SocketLpx.h"
#include "lpxtdi.h"
#include "xixfs_global.h"
#include "xixfs_debug.h"
#include "xixfs_internal.h"
#include "xixcore/dir.h"


NTSTATUS
xixfs_InitializeEnumeration (
	IN PXIXFS_IRPCONTEXT pIrpContext,
	IN PIO_STACK_LOCATION pIrpSp,
	IN PXIXFS_FCB pFCB,
	IN OUT PXIXFS_CCB pCCB,
	IN OUT PXIXCORE_DIR_EMUL_CONTEXT DirContext,
	OUT PBOOLEAN ReturnNextEntry,
	OUT PBOOLEAN ReturnSingleEntry,
	OUT PBOOLEAN InitialQuery
);

NTSTATUS
xixfs_NameInExpression (
	IN PXIXFS_IRPCONTEXT IrpContext,
	IN PUNICODE_STRING CurrentName,
	IN PUNICODE_STRING SearchExpression,
	IN BOOLEAN Wild
);

BOOLEAN
xixfs_EnumerateIndex (
	IN PXIXFS_IRPCONTEXT IrpContext,
	IN PXIXFS_CCB Ccb,
	IN OUT PXIXCORE_DIR_EMUL_CONTEXT DirContext,
	IN BOOLEAN ReturnNextEntry
);

NTSTATUS
xixfs_QueryDirectory(
	IN PXIXFS_IRPCONTEXT		pIrpContext, 
	IN PIRP					pIrp, 
	IN PIO_STACK_LOCATION 	pIrpSp, 
	IN PFILE_OBJECT			pFileObject, 
	IN PXIXFS_FCB			pFCB, 
	IN PXIXFS_CCB			pCCB
);

BOOLEAN
xixfs_NotifyCheck (
    IN PXIXFS_CCB pCCB,
    IN PXIXFS_FCB pFCB,
    IN PSECURITY_SUBJECT_CONTEXT SubjectContext
);

NTSTATUS
xixfs_NotifyDirectory(
	IN PXIXFS_IRPCONTEXT		pIrpContext, 
	IN PIRP					pIrp, 
	IN PIO_STACK_LOCATION 	pIrpSp, 
	IN PFILE_OBJECT			pFileObject, 
	IN PXIXFS_FCB			pFCB, 
	IN PXIXFS_CCB			pCCB
);






NTSTATUS
xixfs_InitializeEnumeration (
	IN PXIXFS_IRPCONTEXT pIrpContext,
	IN PIO_STACK_LOCATION pIrpSp,
	IN PXIXFS_FCB pFCB,
	IN OUT PXIXFS_CCB pCCB,
	IN OUT PXIXCORE_DIR_EMUL_CONTEXT DirContext,
	OUT PBOOLEAN ReturnNextEntry,
	OUT PBOOLEAN ReturnSingleEntry,
	OUT PBOOLEAN InitialQuery
)
{
	NTSTATUS Status;

	PUNICODE_STRING		FileName;
	UNICODE_STRING		SearchExpression;
	


	PUNICODE_STRING		RestartName = NULL;

	ULONG CcbFlags;
	BOOLEAN		LockFCB = FALSE;
	uint64		FileIndex;
	uint64		HighFileIndex;
	BOOLEAN		KnownIndex;

	NTSTATUS Found;

	//PAGED_CODE();

	DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_DIRINFO, ("Enter xixfs_InitializeEnumeration .\n"));

	//
	//  Check inputs.
	//

	ASSERT_IRPCONTEXT( pIrpContext );
	ASSERT_FCB( pFCB );
	ASSERT_CCB( pCCB );




	// CCB is not set initial search
	if(!XIXCORE_TEST_FLAGS(pCCB->CCBFlags, XIXFSD_CCB_FLAG_ENUM_INITIALIZED))
	{
		FileName = (PUNICODE_STRING) pIrpSp->Parameters.QueryDirectory.FileName;
		CcbFlags = 0;

		//
		//  If the filename is not specified or is a single '*' then we will
		//  match all names.
		//

		if ((FileName == NULL) ||
			(FileName->Buffer == NULL) ||
			(FileName->Length == 0) ||
			((FileName->Length == sizeof( WCHAR )) &&
			 (FileName->Buffer[0] == L'*'))) 
		{

			XIXCORE_SET_FLAGS( CcbFlags, XIXFSD_CCB_FLAG_ENUM_MATCH_ALL);
			SearchExpression.Length =
			SearchExpression.MaximumLength = 0;
			SearchExpression.Buffer = NULL;



		} else {

			if (FileName->Length == 0) {

				XifsdRaiseStatus( pIrpContext, STATUS_INVALID_PARAMETER );
			}

			if (FsRtlDoesNameContainWildCards( FileName)) {

				XIXCORE_SET_FLAGS( CcbFlags, XIXFSD_CCB_FLAG_ENUM_NAME_EXP_HAS_WILD );
			}

			SearchExpression.Buffer = FsRtlAllocatePoolWithTag( PagedPool,
																FileName->Length,
																TAG_EXP	 );

			SearchExpression.MaximumLength = FileName->Length;

	
			if(XIXCORE_TEST_FLAGS(pCCB->CCBFlags, XIXFSD_CCB_FLAGS_IGNORE_CASE)){
				
				RtlDowncaseUnicodeString(&SearchExpression, FileName, FALSE);

			}else{
				RtlCopyMemory( SearchExpression.Buffer,
							   FileName->Buffer,
							   FileName->Length );
			}

			

			SearchExpression.Length = FileName->Length;
		}

		//
		//  But we do not want to return the constant "." and ".." entries for
		//  the root directory, for consistency with the rest of Microsoft's
		//  filesystems.
		//

		
		if (pFCB == pFCB->PtrVCB->RootDirFCB) 
		{
			XIXCORE_SET_FLAGS( CcbFlags, XIXFSD_CCB_FLAG_ENUM_NOMATCH_CONSTANT_ENTRY );
		}
		

		XifsdLockFcb( pIrpContext, pFCB );
		LockFCB = TRUE;

		//
		//  Check again that this is the initial search.
		//

		if (!XIXCORE_TEST_FLAGS( pCCB->CCBFlags, XIXFSD_CCB_FLAG_ENUM_INITIALIZED )) {

			//
			//  Update the values in the Ccb.
			//

			pCCB->currentFileIndex = 0;
			pCCB->SearchExpression = SearchExpression;

			XIXCORE_SET_FLAGS( pCCB->CCBFlags, (CcbFlags | XIXFSD_CCB_FLAG_ENUM_INITIALIZED) );

		//
		//  Otherwise cleanup any buffer allocated here.
		//
		}else {

			if (!XIXCORE_TEST_FLAGS( CcbFlags, XIXFSD_CCB_FLAG_ENUM_MATCH_ALL )) {

				ExFreePool( &SearchExpression.Buffer );
			}
		}

	} 
	else 
	{

		XifsdLockFcb( pIrpContext, pFCB );
		LockFCB = TRUE;
	}


	if ( XIXCORE_TEST_FLAGS( pIrpSp->Flags, SL_INDEX_SPECIFIED ) 
		&& (pIrpSp->Parameters.QueryDirectory.FileName != NULL)) 
	{

		KnownIndex = FALSE;
		FileIndex = pIrpSp->Parameters.QueryDirectory.FileIndex;

		DebugTrace(DEBUG_LEVEL_INFO, DEBUG_TARGET_DIRINFO, ("SL_INDEX_SPECIFIED FileIndex(%I64d)\n", FileIndex));
		
		RestartName = (PUNICODE_STRING) pIrpSp->Parameters.QueryDirectory.FileName;
		*ReturnNextEntry = TRUE; 
		HighFileIndex = pCCB->highestFileIndex;

	//
	//  If we are restarting the scan then go from the self entry.
	//

	} else if (XIXCORE_TEST_FLAGS( pIrpSp->Flags, SL_RESTART_SCAN )) {

		KnownIndex = TRUE;
		FileIndex = 0;
		DebugTrace(DEBUG_LEVEL_INFO, DEBUG_TARGET_DIRINFO, ("SL_RESTART_SCAN FileIndex(%I64d)\n", FileIndex));
		*ReturnNextEntry = FALSE;

	} else { //SL_RETURN_SINGLE_ENTRY

		KnownIndex = TRUE;
		FileIndex = pCCB->currentFileIndex;
		DebugTrace(DEBUG_LEVEL_INFO, DEBUG_TARGET_DIRINFO,("pCCB->currentFileIndex FileIndex(%I64d)\n", FileIndex));
		*ReturnNextEntry = XIXCORE_TEST_FLAGS( pCCB->CCBFlags, XIXFSD_CCB_FLAG_ENUM_RETURN_NEXT );
	}

	//
	//  Unlock the Fcb.
	//
	if(LockFCB){
		XifsdUnlockFcb( pIrpContext, pFCB );
		LockFCB = FALSE;
	}

	*InitialQuery = FALSE;

	if ((FileIndex == 0) 
		&& !(*ReturnNextEntry)) 
	{
		*InitialQuery = TRUE;
	}

	if (KnownIndex) 
	{

		Found = xixcore_LookupSetFileIndex(DirContext, FileIndex);

		if(!NT_SUCCESS( Found )){
			ASSERT(TRUE);
			return STATUS_NO_SUCH_FILE;
		}

	} 
	else 
	{
    
		if (xixfs_FCBTLBFullCompareNames( pIrpContext,
								 RestartName,
								 &XifsdUnicodeDirectoryNames[SELF_ENTRY] ) == EqualTo) 
		{

			FileIndex = 0;

			Found = xixcore_LookupSetFileIndex(DirContext, FileIndex);

			if(!NT_SUCCESS( Found )){
				ASSERT(TRUE);
				return Found;
			}
    
		} else {

			//
			//  See if we need the high water mark.
			//
        
			if (FileIndex == 0)
			{

				//
				//  We know that this is good.
				//
            
				FileIndex = Max( pCCB->highestFileIndex, 1 );
				KnownIndex = TRUE;
        
			}

			Found = xixcore_LookupSetFileIndex(DirContext, FileIndex );
        


			if (KnownIndex) {
            
				//
				//  Walk forward to discover an entry named per the caller's expectation.
				//
            
				while(1){
					UNICODE_STRING ChildName;

					Found = xixcore_UpdateDirNames(DirContext);
					if(!NT_SUCCESS( Found )){
						break;
					}

					ChildName.Buffer = (PWSTR)DirContext->ChildName;
					ChildName.Length = ChildName.MaximumLength =  (uint16)DirContext->ChildNameLength;

					if(XIXCORE_TEST_FLAGS(pCCB->CCBFlags, XIXFSD_CCB_FLAGS_IGNORE_CASE)){
						

						if (RtlCompareUnicodeString( &ChildName,RestartName, TRUE ) == 0) 
						{
							break;
						}


					}else{

						if (RtlCompareUnicodeString(&ChildName,RestartName, FALSE ) == 0) 
						{
							break;
						}
					}
				}
        
			} else if (!NT_SUCCESS(Found)) {

				uint64	LastFileIndex;
				//
				//  Perform the search for the entry by index from the beginning of the physical directory.
				//

				LastFileIndex = 1;

				Found = xixcore_LookupSetFileIndex(DirContext, LastFileIndex);

				if(!NT_SUCCESS(Found)){
					ASSERT(TRUE);
					return Found;
				}


				while(1)
				{


					Found = xixcore_UpdateDirNames(DirContext);
					if(!NT_SUCCESS(Found)) {
						break;
					}

					if(DirContext->SearchedVirtualDirIndex > FileIndex){
						LastFileIndex = DirContext->SearchedVirtualDirIndex;
						break;
					}

				} 

				//
				//  If we didn't find the entry then go back to the last known entry.
				//

				if (!NT_SUCCESS(Found)) 
				{
					Found = xixcore_LookupSetFileIndex(DirContext, LastFileIndex );

					if(!NT_SUCCESS(Found)) {
						ASSERT(TRUE);
						return Found;
					}
				}
			}
		}
	}

	//
	//  Only update the dirent name if we will need it for some reason.
	//  Don't update this name if we are returning the next entry, and
	//  don't update it if it was already done.
	//

	if (!(*ReturnNextEntry) &&
		DirContext->ChildName == NULL) {

		//
		//  If the caller specified an index that corresponds to a
		//  deleted file, they are trying to be tricky. Don't let them.
		//

		return STATUS_INVALID_PARAMETER;
    
	}

	//
	//  Look at the flag in the IrpSp indicating whether to return just
	//  one entry.
	//

	*ReturnSingleEntry = FALSE;

	if (XIXCORE_TEST_FLAGS( pIrpSp->Flags, SL_RETURN_SINGLE_ENTRY )) {

		*ReturnSingleEntry = TRUE;
	}

	DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_DIRINFO, ("Exit xixfs_InitializeEnumeration .\n"));

	return STATUS_SUCCESS;
}


NTSTATUS
xixfs_NameInExpression (
	IN PXIXFS_IRPCONTEXT IrpContext,
	IN PUNICODE_STRING CurrentName,
	IN PUNICODE_STRING SearchExpression,
	IN BOOLEAN Wild
)
{
	BOOLEAN Match = TRUE;

	//PAGED_CODE();

	DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_DIRINFO, ("Enter xixfs_NameInExpression .\n"));
	//
	//  Check inputs.
	//

	ASSERT_IRPCONTEXT( IrpContext );

	//
	//  If there are wildcards in the expression then we call the
	//  appropriate FsRtlRoutine.
	//

	if (Wild) {

		Match = FsRtlIsNameInExpression( SearchExpression,
										 CurrentName,
										 FALSE,
										 NULL );

	//
	//  Otherwise do a direct memory comparison for the name string.
	//

	} else {

		if ((CurrentName->Length != SearchExpression->Length) ||
			(!RtlEqualMemory( CurrentName->Buffer,
							  SearchExpression->Buffer,
							  CurrentName->Length ))) {

			Match = FALSE;
		}
	}

	DebugTrace((DEBUG_LEVEL_TRACE|DEBUG_LEVEL_INFO), DEBUG_TARGET_DIRINFO, 
		("Exit xixfs_NameInExpression (%s) .\n", ((Match )?"MATCH":"NonMatch")));

	if(Match){
		return STATUS_SUCCESS;
	}else{
		return STATUS_UNSUCCESSFUL;
	}
}





BOOLEAN
xixfs_EnumerateIndex (
	IN PXIXFS_IRPCONTEXT IrpContext,
	IN PXIXFS_CCB Ccb,
	IN OUT PXIXCORE_DIR_EMUL_CONTEXT DirContext,
	IN BOOLEAN ReturnNextEntry
)
{
	BOOLEAN Found = FALSE;
	NTSTATUS RC= STATUS_SUCCESS;
	UNICODE_STRING IgnoreTempName;
	UNICODE_STRING ChildName;
	uint8			*Buffer = NULL;
	//PAGED_CODE();

	DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_DIRINFO, ("Enter xixfs_EnumerateIndex .\n"));

	//
	//  Check inputs.
	//

	ASSERT_IRPCONTEXT( IrpContext );
	ASSERT_CCB( Ccb );


	if(XIXCORE_TEST_FLAGS(Ccb->CCBFlags, XIXFSD_CCB_FLAGS_IGNORE_CASE)){
		Buffer = ExAllocatePoolWithTag(NonPagedPool, 2048, TAG_BUFFER);
		if(!Buffer){
			return FALSE;
		}
		IgnoreTempName.Buffer = (PWSTR)Buffer;
		IgnoreTempName.MaximumLength = 2048;
	}

	//
	//  Loop until we find a match or exaust the directory.
	//

	while (TRUE) {

		RC= STATUS_SUCCESS;
		//
		//  Move to the next entry unless we want to consider the current
		//  entry.
		//

		if (ReturnNextEntry) {

			RC = xixcore_UpdateDirNames(DirContext);

			if(!NT_SUCCESS(RC))
			{
				Found = FALSE;
				break;
			}
		} else {

			ReturnNextEntry = TRUE;
		}
        
		//
		//  Don't bother if we have a constant entry and are ignoring them.
		//
    
		
		
		if ((DirContext->SearchedVirtualDirIndex < 2 ) 
			&& XIXCORE_TEST_FLAGS( Ccb->CCBFlags, XIXFSD_CCB_FLAG_ENUM_NOMATCH_CONSTANT_ENTRY )) 
		{
			DebugTrace(DEBUG_LEVEL_INFO, DEBUG_TARGET_DIRINFO, 
					("Root Dir Not Support parent and current dir entry \n"));
			continue;
		}

		DebugTrace(DEBUG_LEVEL_INFO, DEBUG_TARGET_DIRINFO,
				("[find result] VIndex(%I64d): CCBFlags(0x%x): Ccb Exp(%wZ) : ChildName(%wZ)\n", 
				DirContext->SearchedRealDirIndex,Ccb->CCBFlags, &Ccb->SearchExpression,&DirContext->ChildName ));

		//
		//  If we match all names then return to our caller.
		//


		if (XIXCORE_TEST_FLAGS( Ccb->CCBFlags, XIXFSD_CCB_FLAG_ENUM_MATCH_ALL )) {
			Found = TRUE;

			break;
		}

		//
		//  Check if the long name matches the search expression.
		//
		
		ChildName.Buffer = (PWSTR)DirContext->ChildName;
		ChildName.Length = ChildName.MaximumLength = (uint16)DirContext->ChildNameLength;

		if(XIXCORE_TEST_FLAGS(Ccb->CCBFlags, XIXFSD_CCB_FLAGS_IGNORE_CASE)){
			RtlZeroMemory(IgnoreTempName.Buffer, 2048);
			IgnoreTempName.Length = ChildName.Length;
			RtlDowncaseUnicodeString(&IgnoreTempName, &(ChildName), FALSE);

			RC = xixfs_NameInExpression( IrpContext,
									   &IgnoreTempName,
									   &Ccb->SearchExpression,
									   XIXCORE_TEST_FLAGS( Ccb->CCBFlags, XIXFSD_CCB_FLAG_ENUM_NAME_EXP_HAS_WILD ));
		}else{
			RC = xixfs_NameInExpression( IrpContext,
									   &ChildName,
									   &Ccb->SearchExpression,
									   XIXCORE_TEST_FLAGS( Ccb->CCBFlags, XIXFSD_CCB_FLAG_ENUM_NAME_EXP_HAS_WILD ));
		}


		if (NT_SUCCESS(RC)) 
		{
			Found = TRUE;
			break;
		}
	}

	if(XIXCORE_TEST_FLAGS(Ccb->CCBFlags, XIXFSD_CCB_FLAGS_IGNORE_CASE)){
		ExFreePool(IgnoreTempName.Buffer);
	}

	DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_DIRINFO, ("Exit xixfs_EnumerateIndex .\n"));

	return Found ;
}




NTSTATUS
xixfs_QueryDirectory(
	IN PXIXFS_IRPCONTEXT		pIrpContext, 
	IN PIRP					pIrp, 
	IN PIO_STACK_LOCATION 	pIrpSp, 
	IN PFILE_OBJECT			pFileObject, 
	IN PXIXFS_FCB			pFCB, 
	IN PXIXFS_CCB			pCCB
	)
{
	NTSTATUS RC = STATUS_SUCCESS;
	ULONG Information = 0;

	ULONG LastEntry = 0;
	ULONG NextEntry = 0;

	ULONG FileNameBytes;
	ULONG BytesConverted;

	PXIXFS_VCB	pVCB = NULL;

	XIXCORE_DIR_EMUL_CONTEXT DirContext;
	XIFS_FILE_EMUL_CONTEXT FileContext;
	PXIDISK_FILE_INFO pFileHeader = NULL;
	PXIDISK_CHILD_INFORMATION	pChildInfo = NULL;

	BOOLEAN	CanWait = FALSE;

	uint64	PreviousFileIndex;
	uint64	ThisFid;

	BOOLEAN InitialQuery;
	BOOLEAN ReturnNextEntry;
	BOOLEAN ReturnSingleEntry;
	BOOLEAN Found = FALSE;
	BOOLEAN	SetInit = FALSE;
	BOOLEAN	AcquireVCB = FALSE;
	BOOLEAN	AcquireFCB = FALSE;
	

	BOOLEAN DirContextClean = FALSE;
	BOOLEAN FileContextClean = FALSE;

	PCHAR UserBuffer;
	ULONG BytesRemainingInBuffer;
	ULONG BaseLength;

	PFILE_BOTH_DIR_INFORMATION		DirInfo;
	PFILE_NAMES_INFORMATION			NamesInfo;
	PFILE_ID_FULL_DIR_INFORMATION	IdFullDirInfo;
	PFILE_ID_BOTH_DIR_INFORMATION	IdBothDirInfo;

	//PAGED_CODE();

	DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_DIRINFO, ("Enter xixfs_QueryDirectory .\n"));

	ASSERT_FCB(pFCB);
	pVCB = pFCB->PtrVCB;
	ASSERT_VCB(pVCB);



	CanWait = XIXCORE_TEST_FLAGS(pIrpContext->IrpContextFlags, XIFSD_IRP_CONTEXT_WAIT);

	
	if(CanWait != TRUE){
		DebugTrace(DEBUG_LEVEL_ERROR, DEBUG_TARGET_ALL, 
			("xixfs_QueryDirectory Post request IrpContext %p .\n", pIrpContext));
		RC = xixfs_PostRequest(pIrpContext, pIrp);
		return RC;		
	}
	
	RtlZeroMemory(&DirContext, sizeof(XIXCORE_DIR_EMUL_CONTEXT));
	RtlZeroMemory(&FileContext, sizeof(XIFS_FILE_EMUL_CONTEXT));



	SetInit = (BOOLEAN)( (pCCB->SearchExpression.Buffer == NULL) &&
							(!XIXCORE_TEST_FLAGS( pCCB->CCBFlags, XIXFSD_CCB_FLAG_ENUM_RETURN_NEXT )) );

	if(SetInit){
		XifsdAcquireVcbExclusive(CanWait, pVCB, FALSE);
		AcquireVCB = TRUE;
	}else{
		XifsdAcquireFcbShared(CanWait, pFCB, FALSE);
		AcquireFCB = TRUE;
	}

	
	
	DebugTrace(DEBUG_LEVEL_ALL, DEBUG_TARGET_ALL,
				 ("call xixfs_QueryDirectory 0x%x\n", pIrpSp->Parameters.QueryDirectory.FileInformationClass));


	switch (pIrpSp->Parameters.QueryDirectory.FileInformationClass) {

	case FileDirectoryInformation:

		BaseLength = FIELD_OFFSET( FILE_DIRECTORY_INFORMATION,
								   FileName[0] );
		break;

	case FileFullDirectoryInformation:

		BaseLength = FIELD_OFFSET( FILE_FULL_DIR_INFORMATION,
								   FileName[0] );
		break;

	case FileIdFullDirectoryInformation:

		BaseLength = FIELD_OFFSET( FILE_ID_FULL_DIR_INFORMATION,
								   FileName[0] );
		break;

	case FileNamesInformation:

		BaseLength = FIELD_OFFSET( FILE_NAMES_INFORMATION,
								   FileName[0] );
		break;

	case FileBothDirectoryInformation:

		BaseLength = FIELD_OFFSET( FILE_BOTH_DIR_INFORMATION,
								   FileName[0] );
		break;

	case FileIdBothDirectoryInformation:

		BaseLength = FIELD_OFFSET( FILE_ID_BOTH_DIR_INFORMATION,
								   FileName[0] );
		break;

	default:

		DebugTrace(DEBUG_LEVEL_ALL,DEBUG_TARGET_ALL , 
			("xixfs_QueryDirectory CCB(%p) Complete1 RC(%x).\n", pCCB, RC));
	
		if(AcquireFCB){
			XifsdReleaseFcb(CanWait, pFCB);
		}
		
		if(AcquireVCB){
			XifsdReleaseVcb(CanWait, pVCB);
		}
			
		xixfs_CompleteRequest( pIrpContext, STATUS_INVALID_INFO_CLASS, 0 );

		return STATUS_INVALID_INFO_CLASS;
	}

	
	RC = xixcore_InitializeDirContext(&(pVCB->XixcoreVcb), &DirContext);
	
	if(!NT_SUCCESS(RC)){
		DebugTrace(DEBUG_LEVEL_ALL,DEBUG_TARGET_ALL , 
			("xixfs_QueryDirectory  CCB(%p) Complete2 RC(%x).\n", pCCB, RC));
		
		if(AcquireFCB){
			XifsdReleaseFcb(CanWait, pFCB);
		}
		
		if(AcquireVCB){
			XifsdReleaseVcb(CanWait, pVCB);
		}
		
		xixfs_CompleteRequest( pIrpContext, RC, 0 );
		
		return RC;
	}

	DirContextClean = TRUE;


	


	try
	{


		RC = xixfs_InitializeFileContext(&FileContext);
		if(!NT_SUCCESS(RC)){
			try_return(RC);
		}

		FileContextClean = TRUE;
		

		RC = xixcore_LookupInitialDirEntry(
							&(pVCB->XixcoreVcb),
							&(pFCB->XixcoreFcb),
							&DirContext, 
							0
							);
		
		if(!NT_SUCCESS(RC)){
			try_return(RC);
		}
			
		UserBuffer = xixfs_GetCallersBuffer(pIrp);




		if(!xixfs_VerifyFCBOperation( pIrpContext, pFCB )){
			RC = STATUS_INVALID_PARAMETER;
			try_return(RC);
		}

		RC = xixfs_InitializeEnumeration (pIrpContext,
									pIrpSp,
									pFCB,
									pCCB,
									&DirContext,
									&ReturnNextEntry,
									&ReturnSingleEntry,
									&InitialQuery
									);
		if(!NT_SUCCESS(RC)){
			if (NextEntry == 0) {

				RC = STATUS_NO_MORE_FILES;

				if (InitialQuery) {

					RC = STATUS_NO_SUCH_FILE;
				}
			}
			try_return(RC);
		}

		while(1)
		{

			if ((NextEntry != 0) && ReturnSingleEntry) 
			{

				try_return(RC);
			}

			PreviousFileIndex = DirContext.SearchedVirtualDirIndex;

			try {
				
				Found = xixfs_EnumerateIndex( pIrpContext, pCCB, &DirContext, ReturnNextEntry );
			} except (((0 != NextEntry) && 
					 ((GetExceptionCode() == STATUS_FILE_CORRUPT_ERROR) || 
					  (GetExceptionCode() == STATUS_CRC_ERROR)))
					 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)  
			{

				ReturnNextEntry = TRUE;

				DirContext.VirtualDirIndex = PreviousFileIndex;

				RC = STATUS_SUCCESS;
				try_return(RC);
			}





			ReturnNextEntry = TRUE;

			if (!Found) {

				if (NextEntry == 0) {

					RC = STATUS_NO_MORE_FILES;

					if (InitialQuery) {

						RC = STATUS_NO_SUCH_FILE;
					}
				}

				
				try_return(RC);
			}
			else {

				if(!DirContext.IsChildCache && !DirContext.IsVirtual){

					RC = xixfs_SetFileContext(pVCB, DirContext.LotNumber,DirContext.FileType, &FileContext);
					
					if(!NT_SUCCESS(RC)){
						try_return(RC);
					}

					RC = xixfs_FileInfoFromContext(CanWait, &FileContext);

					if(!NT_SUCCESS(RC)){
						if((RC == STATUS_FILE_CORRUPT_ERROR) || (RC == STATUS_FILE_CORRUPT_ERROR)) {
							continue;
						}else {
							try_return(RC);
						}
					}

					pFileHeader = (PXIDISK_FILE_INFO)xixcore_GetDataBufferWithOffset(FileContext.Buffer);
					pChildInfo = (PXIDISK_CHILD_INFORMATION)xixcore_GetDataBufferWithOffset(DirContext.ChildEntryBuff);
					xixcore_CreateChildCacheEntry(&(pVCB->XixcoreVcb), pFCB->XixcoreFcb.LotNumber, pChildInfo, pFileHeader);
				}
			}
			

			ThisFid = DirContext.LotNumber;
			FileNameBytes = DirContext.ChildNameLength;



			if (NextEntry > pIrpSp->Parameters.QueryDirectory.Length) {
    
				ReturnNextEntry = FALSE;
				RC = STATUS_SUCCESS;
				try_return(RC);
			}


			BytesRemainingInBuffer = pIrpSp->Parameters.QueryDirectory.Length - NextEntry;
			XIXCORE_CLEAR_FLAGS( BytesRemainingInBuffer, 1 );

			if ((BaseLength + FileNameBytes) > BytesRemainingInBuffer) 
			{

				//
				//  If we already found an entry then just exit.
				//

				if (NextEntry != 0) {

					ReturnNextEntry = FALSE;
					RC = STATUS_SUCCESS;
					try_return(RC);
				}

				//
				//  Reduce the FileNameBytes to just fit in the buffer.
				//

				FileNameBytes = BytesRemainingInBuffer - BaseLength;

				//
				//  Use a status code of STATUS_BUFFER_OVERFLOW.  Also set
				//  ReturnSingleEntry so that we will exit the loop at the top.
				//

				RC = STATUS_BUFFER_OVERFLOW;
				ReturnSingleEntry = TRUE;
			}


			DebugTrace( DEBUG_LEVEL_INFO, DEBUG_TARGET_DIRINFO,
					 ("Final Searched LotNumber(%I64d) FileType(%ld)\n", 
					 DirContext.LotNumber,DirContext.FileType));
			
			/*
			RC = xixfs_SetFileContext(pVCB, DirContext.LotNumber,DirContext.FileType, &FileContext);
			
			if(!NT_SUCCESS(RC)){
				try_return(RC);
			}

			RC = xixfs_FileInfoFromContext(CanWait, &FileContext);

			if(!NT_SUCCESS(RC)){
				try_return(RC);
			}
			*/

			


			try
			{
				RtlZeroMemory( Add2Ptr( UserBuffer, NextEntry),
							   BaseLength );

				//
				//  Now we have an entry to return to our caller.
				//  We'll case on the type of information requested and fill up
				//  the user buffer if everything fits.
				//

				switch (pIrpSp->Parameters.QueryDirectory.FileInformationClass) {

				case FileBothDirectoryInformation:
				case FileFullDirectoryInformation:
				case FileIdBothDirectoryInformation:
				case FileIdFullDirectoryInformation:
				case FileDirectoryInformation:

					DirInfo = Add2Ptr( UserBuffer, NextEntry);

					if(DirContext.IsChildCache){
						PXIXCORE_CHILD_CACHE_INFO pChildCacheInfo = NULL;
						pChildCacheInfo = (PXIXCORE_CHILD_CACHE_INFO)xixcore_GetDataBufferWithOffset(DirContext.ChildEntryBuff);
						
						DirInfo->LastWriteTime.QuadPart =
						DirInfo->ChangeTime.QuadPart = pChildCacheInfo->Change_time;
						DirInfo->LastAccessTime.QuadPart = pChildCacheInfo->Access_time;
						DirInfo->CreationTime.QuadPart = pChildCacheInfo->Create_time;
						DirInfo->FileAttributes = pChildCacheInfo->FileAttribute;
						if(DirContext.FileType == FCB_TYPE_DIR){
							DirInfo->EndOfFile.QuadPart = DirInfo->AllocationSize.QuadPart = 0;
							XIXCORE_SET_FLAGS(DirInfo->FileAttributes, FILE_ATTRIBUTE_DIRECTORY );
						}else{
							DirInfo->EndOfFile.QuadPart = pChildCacheInfo->FileSize;
							DirInfo->AllocationSize.QuadPart = pChildCacheInfo->AllocationSize;
						}
    
						DirInfo->FileIndex = (uint32)DirContext.SearchedVirtualDirIndex;
						DirInfo->FileNameLength = FileNameBytes;

					}else{
						if(!DirContext.IsVirtual){
							pFileHeader = (PXIDISK_FILE_INFO)xixcore_GetDataBufferWithOffset(FileContext.Buffer);
							
							DirInfo->LastWriteTime.QuadPart =
							DirInfo->ChangeTime.QuadPart = pFileHeader->Change_time;
							DirInfo->LastAccessTime.QuadPart = pFileHeader->Access_time;
							DirInfo->CreationTime.QuadPart = pFileHeader->Create_time;
							DirInfo->FileAttributes = pFileHeader->FileAttribute;

							if(DirContext.FileType == FCB_TYPE_DIR){
								DirInfo->EndOfFile.QuadPart = DirInfo->AllocationSize.QuadPart = 0;
								XIXCORE_SET_FLAGS(DirInfo->FileAttributes, FILE_ATTRIBUTE_DIRECTORY );
							}else{
								DirInfo->EndOfFile.QuadPart = pFileHeader->FileSize;
								DirInfo->AllocationSize.QuadPart = pFileHeader->AllocationSize;
							}

						
						}else{
							DirInfo->LastWriteTime.QuadPart =
							DirInfo->ChangeTime.QuadPart = pFCB->XixcoreFcb.Modified_time;
							DirInfo->LastAccessTime.QuadPart = pFCB->XixcoreFcb.Access_time;
							DirInfo->CreationTime.QuadPart = pFCB->XixcoreFcb.Create_time;
							DirInfo->FileAttributes = pFCB->XixcoreFcb.FileAttribute;	
							
							DirInfo->EndOfFile.QuadPart = DirInfo->AllocationSize.QuadPart = 0;
							XIXCORE_SET_FLAGS(DirInfo->FileAttributes, FILE_ATTRIBUTE_DIRECTORY );
						}
    
						DirInfo->FileIndex = (uint32)DirContext.SearchedVirtualDirIndex;
						DirInfo->FileNameLength = FileNameBytes;

					}

					DebugTrace(DEBUG_LEVEL_INFO, (DEBUG_TARGET_DIRINFO|DEBUG_TARGET_FCB),
						("[DIR ENTRY INFO]FileIndex(%ld):EndofFile(%I64d):AllocationSize(%I64d):AtrriBute(0x%x):"
							"ChangeT(%I64d):LastAccessT(%I64d):CreationT(%I64d)\n",
							DirInfo->FileIndex,
							DirInfo->EndOfFile.QuadPart,
							DirInfo->AllocationSize.QuadPart,
							DirInfo->FileAttributes,
							DirInfo->ChangeTime.QuadPart,
							DirInfo->LastAccessTime.QuadPart,
							DirInfo->CreationTime.QuadPart));




					break;

				case FileNamesInformation:

					NamesInfo = Add2Ptr( UserBuffer, NextEntry);
					NamesInfo->FileIndex = (uint32)DirContext.SearchedVirtualDirIndex;
					NamesInfo->FileNameLength = FileNameBytes;

					break;
				}

				//
				//  Fill in the FileId
				//

				switch (pIrpSp->Parameters.QueryDirectory.FileInformationClass) {

				case FileIdBothDirectoryInformation:

					IdBothDirInfo = Add2Ptr( UserBuffer, NextEntry);
					if(DirContext.FileType == FCB_TYPE_DIR){
						IdBothDirInfo->FileId.QuadPart 
							= (FCB_TYPE_DIR_INDICATOR|(FCB_ADDRESS_MASK & DirContext.LotNumber));
					}else{
						IdBothDirInfo->FileId.QuadPart  
							= (FCB_TYPE_FILE_INDICATOR|(FCB_ADDRESS_MASK & DirContext.LotNumber));
					}

					DebugTrace(DEBUG_LEVEL_INFO, (DEBUG_TARGET_DIRINFO|DEBUG_TARGET_FCB),
						("File LotNumber(%I64d)\nFileId(0x%08x)\n",
							DirContext.LotNumber,
							IdBothDirInfo->FileId.QuadPart));


					break;

				case FileIdFullDirectoryInformation:

					IdFullDirInfo = Add2Ptr( UserBuffer, NextEntry);
					if(DirContext.FileType == FCB_TYPE_DIR){
						IdFullDirInfo->FileId.QuadPart  
							= (FCB_TYPE_DIR_INDICATOR|(FCB_ADDRESS_MASK & DirContext.LotNumber));
					}else{
						IdFullDirInfo->FileId.QuadPart  
							= (FCB_TYPE_FILE_INDICATOR|(FCB_ADDRESS_MASK & DirContext.LotNumber));
					}

					DebugTrace(DEBUG_LEVEL_INFO, (DEBUG_TARGET_DIRINFO|DEBUG_TARGET_FCB),
						("File LotNumber(%I64d)\nFileId(0x%08x)\n",
							DirContext.LotNumber,
							IdBothDirInfo->FileId.QuadPart));
					break;

				default:
					break;
				}

				//
				//  Now copy as much of the name as possible.
				//

				if (FileNameBytes != 0) {

					//
					//  This is a Unicode name, we can copy the bytes directly.
					//

					RtlCopyMemory( Add2Ptr( UserBuffer, NextEntry + BaseLength),
								   DirContext.ChildName,
								   FileNameBytes );

					DebugTrace(DEBUG_LEVEL_INFO, (DEBUG_TARGET_DIRINFO|DEBUG_TARGET_FCB),
						("Org FileName NameSize(%ld) FileNameBytes(%ld)\n", 
						DirContext.ChildNameLength, FileNameBytes));


						
						//DbgPrint("Org FileName(%wZ) NameSize(%ld) FileNameBytes(%ld)\n", 
						//&DirContext.ChildName, DirContext.ChildName.Length, FileNameBytes);

				}

				Information = NextEntry + BaseLength + FileNameBytes;

				*((uint32 *)(Add2Ptr( UserBuffer, LastEntry))) = NextEntry - LastEntry;

				//
				//  Set up our variables for the next dirent.
				//

				InitialQuery = FALSE;

				LastEntry = NextEntry;
				NextEntry = XifsdQuadAlign( Information );

			} 
			except (!FsRtlIsNtstatusExpected(GetExceptionCode()) ?
					  EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {

				  //
				  //  We must have had a problem filling in the user's buffer, so stop
				  //  and fail this request.
				  //
      
				  Information = 0;
				  RC = GetExceptionCode();
				  try_return(RC);
			}
		
		}

        xixcore_CleanupDirContext(&DirContext );
		DirContextClean = FALSE;
		xixfs_ClearFileContext(&FileContext);
		FileContextClean = FALSE;

		;
	}finally{
		if (!AbnormalTermination() && !NT_ERROR( RC)) {

			//
			//  Update the Ccb to show the current state of the enumeration.
			//

			XifsdLockFcb( pIrpContext, pFCB );

			pCCB->currentFileIndex = DirContext.SearchedVirtualDirIndex;

			if ((DirContext.SearchedVirtualDirIndex > 1)
				&& (DirContext.SearchedVirtualDirIndex > pCCB->highestFileIndex)) {

					pCCB->highestFileIndex = DirContext.SearchedVirtualDirIndex;
			}

			//
			//  Mark in the CCB whether or not to skip the current entry on next call
			//  (if we  returned it in the current buffer).
			//
    
			XIXCORE_CLEAR_FLAGS( pCCB->CCBFlags, XIXFSD_CCB_FLAG_ENUM_RETURN_NEXT );

			if (ReturnNextEntry) {

				XIXCORE_SET_FLAGS( pCCB->CCBFlags, XIXFSD_CCB_FLAG_ENUM_RETURN_NEXT );
			}
			
			XifsdUnlockFcb( pIrpContext, pFCB );
		}

		if(DirContextClean){
			xixcore_CleanupDirContext(&DirContext );
		}

		if(FileContextClean){
			xixfs_ClearFileContext(&FileContext);
		}

		if(AcquireFCB){
			XifsdReleaseFcb(CanWait, pFCB);
		}
		
		if(AcquireVCB){
			XifsdReleaseVcb(CanWait, pVCB);
		}
	}
	DebugTrace(DEBUG_LEVEL_ALL,DEBUG_TARGET_ALL , 
			("xixfs_QueryDirectory  CCB(%p) Complete3 RC(%x).\n", pCCB, RC));

	xixfs_CompleteRequest( pIrpContext, RC, Information );
	DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_DIRINFO, ("Exit xixfs_QueryDirectory .\n"));
	return RC;
}




BOOLEAN
xixfs_NotifyCheck (
    IN PXIXFS_CCB pCCB,
    IN PXIXFS_FCB pFCB,
    IN PSECURITY_SUBJECT_CONTEXT SubjectContext
    )
{

	//PAGED_CODE();

	DebugTrace(DEBUG_LEVEL_TRACE|DEBUG_LEVEL_ALL, DEBUG_TARGET_DIRINFO|DEBUG_TARGET_ALL, ("Enter xixfs_NotifyCheck .\n"));

	if(pCCB != NULL){
		DebugTrace(DEBUG_LEVEL_INFO|DEBUG_LEVEL_ALL, DEBUG_TARGET_DIRINFO|DEBUG_TARGET_ALL,
				 ("xixfs_NotifyCheck Check pCCB (%p)\n", pCCB));	
	}

	if(pFCB != NULL){
		DebugTrace(DEBUG_LEVEL_INFO|DEBUG_LEVEL_ALL, DEBUG_TARGET_DIRINFO|DEBUG_TARGET_ALL,
				 ("xixfs_NotifyCheck Check pFCB (%p) LotNumber(%I64d)\n", pFCB, pFCB->XixcoreFcb.LotNumber));
	}

	DebugTrace(DEBUG_LEVEL_TRACE|DEBUG_LEVEL_ALL, DEBUG_TARGET_DIRINFO|DEBUG_TARGET_ALL, ("Exit xixfs_NotifyCheck .\n"));
	return TRUE;
}




NTSTATUS
xixfs_NotifyDirectory(
	IN PXIXFS_IRPCONTEXT		pIrpContext, 
	IN PIRP					pIrp, 
	IN PIO_STACK_LOCATION 	pIrpSp, 
	IN PFILE_OBJECT			pFileObject, 
	IN PXIXFS_FCB			pFCB, 
	IN PXIXFS_CCB			pCCB
	)
{


	NTSTATUS				RC = STATUS_SUCCESS;
	BOOLEAN					CompleteRequest = FALSE;
	BOOLEAN					PostRequest = FALSE;
	BOOLEAN					CanWait = FALSE;
	BOOLEAN					WatchTree = FALSE;
	PXIXFS_VCB				pVCB = NULL;
	BOOLEAN					AcquiredFCB = FALSE;
	PCHECK_FOR_TRAVERSE_ACCESS CallBack = NULL;

	//PAGED_CODE();

	DebugTrace(DEBUG_LEVEL_TRACE|DEBUG_LEVEL_ALL, (DEBUG_TARGET_DIRINFO|DEBUG_TARGET_ALL),
		("Enter xixfs_NotifyDirectory .\n"));

	ASSERT_FCB(pFCB);
	pVCB = pFCB->PtrVCB;
	ASSERT_VCB(pVCB);


	CallBack = xixfs_NotifyCheck;


	CanWait = XIXCORE_TEST_FLAGS(pIrpContext->IrpContextFlags , XIFSD_IRP_CONTEXT_WAIT);
	if(!CanWait){
		DebugTrace( DEBUG_LEVEL_INFO|DEBUG_LEVEL_ALL, ( DEBUG_TARGET_DIRINFO| DEBUG_TARGET_IRPCONTEXT|DEBUG_TARGET_ALL),
				 ("post request pIrpContext(%p) pIrp(%p)\n", pIrpContext, pIrp));
		RC = xixfs_PostRequest(pIrpContext, pIrp);
		return RC; 
	}



	if(!XifsdAcquireVcbExclusive(TRUE, pVCB, FALSE)){
		DebugTrace( DEBUG_LEVEL_INFO|DEBUG_LEVEL_ALL, ( DEBUG_TARGET_DIRINFO| DEBUG_TARGET_IRPCONTEXT|DEBUG_TARGET_ALL),
				 ("post request pIrpContext(%p) pIrp(%p)\n", pIrpContext, pIrp));
		RC = xixfs_PostRequest(pIrpContext, pIrp);
		return RC; 
	}
	
	

	WatchTree = XIXCORE_TEST_FLAGS( pIrpSp->Flags, SL_WATCH_TREE );

	//XifsdAcquireVcbShared(TRUE, pVCB, FALSE);
	




	try {

		/*	
		FsRtlNotifyFilterChangeDirectory (
			pVCB->NotifyIRPSync,
			&(pVCB->NextNotifyIRP),
			(void *)pCCB,
			(PSTRING) &pIrpSp->FileObject->FileName, 
			WatchTree, 
			FALSE, 
			pIrpSp->Parameters.NotifyDirectory.CompletionFilter, 
			pIrp,
			CallBack,
			NULL,
			NULL
			);
		*/


		DebugTrace( DEBUG_LEVEL_INFO|DEBUG_LEVEL_ALL, (DEBUG_TARGET_DIRINFO|DEBUG_TARGET_IRPCONTEXT|DEBUG_TARGET_ALL),
				 ("!!CompletionFilter  Filter(%x) !!\n", pIrpSp->Parameters.NotifyDirectory.CompletionFilter ));
		
		DebugTrace( DEBUG_LEVEL_INFO|DEBUG_LEVEL_ALL, (DEBUG_TARGET_DIRINFO|DEBUG_TARGET_IRPCONTEXT|DEBUG_TARGET_ALL),
				("!! Set notifier pCCB %p pFCB %p LotNumber(%I64d)\n", pCCB, pFCB, pFCB->XixcoreFcb.LotNumber ));
		
		if(!XIXCORE_TEST_FLAGS(pCCB->CCBFlags, XIXFSD_CCB_FLAG_NOFITY_SET)){
			XIXCORE_SET_FLAGS(pCCB->CCBFlags, XIXFSD_CCB_FLAG_NOFITY_SET);
		}

		FsRtlNotifyFullChangeDirectory(
						pVCB->NotifyIRPSync, 
						&(pVCB->NextNotifyIRP), 
						(void *)pCCB,
						(PSTRING) &pFCB->FCBFullPath,
						WatchTree, 
						FALSE, 
						pIrpSp->Parameters.NotifyDirectory.CompletionFilter, 
						pIrp,
						NULL,//CallBack,
						NULL);	


		/*
		DbgPrint("!!DirNotification Name pCCB(%p) Filter(%x) WatchTree(%s) !!\n",
				pCCB,
				pIrpSp->Parameters.NotifyDirectory.CompletionFilter,
				(WatchTree?"TRUE":"FALSE"));
		*/



		RC = STATUS_PENDING;


	} finally {
		XifsdReleaseVcb(TRUE, pVCB);
	}

	xixfs_ReleaseIrpContext(pIrpContext);

	DebugTrace(DEBUG_LEVEL_TRACE|DEBUG_LEVEL_ALL, (DEBUG_TARGET_DIRINFO|DEBUG_TARGET_ALL),
		("Exit xixfs_NotifyDirectory .\n"));
	
	return(RC);
}


NTSTATUS
xixfs_CommonDirectoryControl(
	IN PXIXFS_IRPCONTEXT pIrpContext
	)
{
	NTSTATUS				RC = STATUS_SUCCESS;
	PIRP						pIrp = NULL;
	PIO_STACK_LOCATION		pIrpSp = NULL;
	PFILE_OBJECT				pFileObject = NULL;
	PXIXFS_FCB				pFCB = NULL;
	PXIXFS_CCB				pCCB = NULL;

	//PAGED_CODE();

	DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_DIRINFO, 
		("Enter xixfs_CommonDirectoryControl .\n"));

	ASSERT_IRPCONTEXT(pIrpContext);
	ASSERT(pIrpContext->Irp);

	pIrp = pIrpContext->Irp;
	pIrpSp = IoGetCurrentIrpStackLocation(pIrp);
	ASSERT(pIrpSp);

	pFileObject = pIrpSp->FileObject;
	ASSERT(pFileObject);

    if (xixfs_DecodeFileObject( pFileObject,
                             &pFCB,
                             &pCCB ) != UserDirectoryOpen) {


		DebugTrace(DEBUG_LEVEL_ERROR, DEBUG_TARGET_ALL,
				 ("Is Not Dir Object !!!!\n"));	

        xixfs_CompleteRequest( pIrpContext,STATUS_INVALID_PARAMETER, 0 );
        return STATUS_INVALID_PARAMETER;
    }

	//DebugTrace(DEBUG_LEVEL_CRITICAL, DEBUG_TARGET_ALL, 
	//				("!!!!DirectoryControl pCCB(%p)\n", pCCB));


	try{
		switch(pIrpSp->MinorFunction){
		case IRP_MN_QUERY_DIRECTORY:
		{
			DebugTrace(DEBUG_LEVEL_CRITICAL, DEBUG_TARGET_ALL, 
					("!!!!DirectoryControl pCCB(%p) : xixfs_QueryDirectory\n",  pCCB));
			
			RC = xixfs_QueryDirectory(pIrpContext, pIrp, pIrpSp, pFileObject, pFCB, pCCB);
			
			//DbgPrint("Query Dir pIrpCtonext %p  Status %x\n",pIrpContext, RC);

			if(!NT_SUCCESS(RC)){
				try_return(RC);
			}
		}break;
		case IRP_MN_NOTIFY_CHANGE_DIRECTORY:			
		{
			DebugTrace(DEBUG_LEVEL_CRITICAL, DEBUG_TARGET_ALL, 
					("!!!!DirectoryControl  pCCB(%p) : xixfs_NotifyDirectory\n", pCCB));

			RC = xixfs_NotifyDirectory(pIrpContext, pIrp, pIrpSp, pFileObject, pFCB, pCCB);

			if(!NT_SUCCESS(RC)){
				try_return(RC);
			}			
		}break;
		default:
			DebugTrace(DEBUG_LEVEL_CRITICAL, DEBUG_TARGET_ALL, 
					("!!!!DirectoryControl  pCCB(%p) : INVALID REQUEST\n",  pCCB));

			RC = STATUS_INVALID_DEVICE_REQUEST;
			xixfs_CompleteRequest(pIrpContext, RC, 0);
			break;
		}

	}finally{
		;
	}
	
	DebugTrace(DEBUG_LEVEL_CRITICAL, DEBUG_TARGET_ALL, 
					("!!!!DirectoryControl pCCB(%p) End !!!!\n",  pCCB));

	DebugTrace(DEBUG_LEVEL_TRACE, DEBUG_TARGET_DIRINFO, 
		("Exit xixfs_CommonDirectoryControl .\n"));
	return RC;
	
}
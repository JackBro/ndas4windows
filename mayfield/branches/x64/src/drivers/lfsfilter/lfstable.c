#include "LfsProc.h"


static
BOOLEAN
LfsTable_Lookup(
	IN PLFS_TABLE			LfsTable,
	IN PNETDISK_PARTITION_INFO	NetDiskPartitionInfo,
	OUT	PLFSTAB_ENTRY		*LfsTabEntry
	) ;

static
BOOLEAN
LfsTable_LookupEx(
	IN PLFS_TABLE			LfsTable,
	IN PNETDISK_PARTITION_INFO	NetDiskPartitionInfo,
	IN BOOLEAN				Primary,
	OUT	PLFSTAB_ENTRY		*LfsTabEntry
	) ;

static
NTSTATUS
LfsTable_CreateEntry(
	IN PNETDISK_PARTITION_INFO	NetDiskPartitionInfo,
	IN PLPX_ADDRESS			BindAddress,
	IN BOOLEAN				Primary,
	OUT PLFSTAB_ENTRY		*Entry
	) ;

static
VOID
LfsTable_ReferenceEntry (
		IN PLFSTAB_ENTRY	LfsTableEntry
	) ;

static
VOID
LfsTable_DereferenceEntry (
		IN PLFSTAB_ENTRY	LfsTableEntry
	) ;


PLFS_TABLE
LfsTable_Create(
	IN PLFS	Lfs
	)
{
	PLFS_TABLE	lfsTable;


	LfsReference (Lfs);

	lfsTable = ExAllocatePoolWithTag( 
						NonPagedPool, 
                        sizeof(LFS_TABLE),
						LFS_ALLOC_TAG
						);
	
	if (lfsTable == NULL)
	{
		ASSERT(LFS_INSUFFICIENT_RESOURCES);
		LfsDereference (Lfs);
		return NULL;
	}

	RtlZeroMemory(
		lfsTable,
		sizeof(LFS_TABLE)
		);

	KeInitializeSpinLock(&lfsTable->SpinLock);
	InitializeListHead(&lfsTable->LfsTabPartitionList) ;
	lfsTable->ReferenceCount = 1;
	lfsTable->Lfs = Lfs;
	

	
	return lfsTable;
}


VOID
LfsTable_Close (
	IN PLFS_TABLE	LfsTable
	)
{
	LfsTable_Dereference(LfsTable);

	return;
}


VOID
LfsTable_Reference (
	IN PLFS_TABLE	LfsTable
	)
{
    LONG result;

    result = InterlockedIncrement (&LfsTable->ReferenceCount);

    ASSERT (result >= 0);
}


VOID
LfsTable_Dereference (
	IN PLFS_TABLE	LfsTable
	)
{
    LONG result;


    result = InterlockedDecrement (&LfsTable->ReferenceCount);
    ASSERT (result >= 0);

    if (result == 0) 
	{
		ASSERT(LfsTable->EntryCount == 0);

		LfsDereference (LfsTable->Lfs);

		ExFreePoolWithTag(
			LfsTable,
			LFS_ALLOC_TAG
			);

		SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
				("LfsTable_Dereference: Lfs Table is Freed\n"));
	}
}


VOID
LfsTable_InsertNetDiskPartitionInfoUser(
	IN PLFS_TABLE				LfsTable,
	IN PNETDISK_PARTITION_INFO	NetDiskPartitionInfo,
	IN PLPX_ADDRESS				BindAddress,
	BOOLEAN						Primary
	) {
	NTSTATUS		ntStatus ;
	PLFSTAB_ENTRY	Entry ;

	LfsTable ;
	NetDiskPartitionInfo ;
	BindAddress ;
	Primary ;


	ntStatus = LfsTable_CreateEntry(
					NetDiskPartitionInfo,
					BindAddress,
					Primary,
					&Entry
	) ;
	if(!NT_SUCCESS(ntStatus)) {
		SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
				("LfsTable_InsertNetDiskPartitionInfoUser: LfsTable_CreateEntry() failed.\n"));
		return ;
	}

	Entry->LfsTable = LfsTable ;
	ExInterlockedInsertHeadList(
			&LfsTable->LfsTabPartitionList,
			&Entry->LfsTabPartitionEntry,
			&LfsTable->SpinLock
		) ;
	InterlockedIncrement(&LfsTable->EntryCount) ;
	LfsTable_Reference(LfsTable) ;

	SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
				("LfsTable_InsertNetDiskPartitionInfoUser: inserted a Partition entry.\n"));
}


VOID
LfsTable_DeleteNetDiskPartitionInfoUser(
	IN PLFS_TABLE				LfsTable,
	IN PNETDISK_PARTITION_INFO	NetDiskPartitionInfo,
	IN BOOLEAN					Primary
	) {
	PLFSTAB_ENTRY	entry ;
	BOOLEAN			Found ;

	Found = LfsTable_LookupEx(LfsTable, NetDiskPartitionInfo, Primary, &entry) ;


	if(Found) {
		LfsTable_DereferenceEntry (
				entry
			) ;
		LfsTable_DereferenceEntry (
				entry
			) ;
	} else {
		SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
			("LFS: LfsTable_DeleteNetDiskPartitionInfoUser: Could not find NetDisk:%02x:%02x:%02x:%02x:%02x:%02x/%d\n",
				NetDiskPartitionInfo->NetDiskAddress.Node[0],
				NetDiskPartitionInfo->NetDiskAddress.Node[1],
				NetDiskPartitionInfo->NetDiskAddress.Node[2],
				NetDiskPartitionInfo->NetDiskAddress.Node[3],
				NetDiskPartitionInfo->NetDiskAddress.Node[4],
				NetDiskPartitionInfo->NetDiskAddress.Node[5],
				NTOHS(NetDiskPartitionInfo->NetDiskAddress.Port)
			));
	}
}


VOID
LfsTable_CleanCachePrimaryAddress(
	IN PLFS_TABLE				LfsTable,
	IN  PNETDISK_PARTITION_INFO	NetDiskPartitionInfo,
	OUT PLPX_ADDRESS			PrimaryAddress
	)
{
	BOOLEAN			Found ;
	PLFSTAB_ENTRY	entry ;

	UNREFERENCED_PARAMETER(PrimaryAddress);

	Found = LfsTable_Lookup(LfsTable, NetDiskPartitionInfo, &entry) ;
	if(Found) {
		SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
			("LFS: LfsTable_CleanCachePrimaryAddress: Found\n"));

		entry->Flags &= ~LFSTABENTRY_FLAG_VAILDPRIMADDRESS;
		LfsTable_DereferenceEntry (
				entry
			); // LfsTable_Lookup
	}

	return;	
}


#define QUERYPRIMARYADDRESS_MAX_TRIAL			2
#define	QUERYPRIMARYADDRESS_DELAY_INTERVAL		(1 * HZ)

NTSTATUS
LfsTable_QueryPrimaryAddress(
		IN PLFS_TABLE			LfsTable,
		IN  PNETDISK_PARTITION_INFO	NetDiskPartitionInfo,
		OUT PLPX_ADDRESS		PrimaryAddress
	) {
	NTSTATUS	ntStatus = STATUS_SUCCESS ;
	PLFSTAB_ENTRY	entry ;
	BOOLEAN			Found ;
	KIRQL			oldIrql ;
	LONG			Trial ;
	LARGE_INTEGER	Interval ;

	ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL) ;

	ASSERT(PrimaryAddress) ;

	Trial = 0 ;

	while(Trial < QUERYPRIMARYADDRESS_MAX_TRIAL) {

		Interval.QuadPart = - QUERYPRIMARYADDRESS_DELAY_INTERVAL;
		ntStatus = KeDelayExecutionThread(
				KernelMode,
				TRUE,
				&Interval
			);

		if(!NT_SUCCESS(ntStatus)) {
			SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
				("LFS: LfsTable_QueryPrimaryAddress: KeDelayExecutionThread() failed.\n"));
		}

		Found = LfsTable_Lookup(LfsTable, NetDiskPartitionInfo, &entry) ;
		if(Found) {

			KeAcquireSpinLock(&entry->SpinLock, &oldIrql) ;

			if(entry->Flags & LFSTABENTRY_FLAG_VAILDPRIMADDRESS) {

				RtlCopyMemory(PrimaryAddress, &entry->PrimaryAddress, sizeof(LPX_ADDRESS)) ;
				entry->Flags &= ~LFSTABENTRY_FLAG_VAILDPRIMADDRESS;
				KeReleaseSpinLock(&entry->SpinLock, oldIrql) ;
				LfsTable_DereferenceEntry (
						entry
					) ;

				break ;
			} else {
				KeReleaseSpinLock(&entry->SpinLock, oldIrql) ;
				LfsTable_DereferenceEntry (
						entry
					) ;
			}
		}

		Found = FALSE ;
		Trial ++ ;

		SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
				("LFS: LfsTable_QueryPrimaryAddress: Retry #%d\n", Trial));

	}

	if(!Found) {
		SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
				("LFS: LfsTable_QueryPrimaryAddress: Could not resolve the primary address.\n", Trial));
		ntStatus = STATUS_NO_SUCH_DEVICE ;
	}

	return ntStatus ;
}

VOID
LfsTable_UpdatePrimaryInfo(
	IN PLFS_TABLE		LfsTable,
	IN PLPX_ADDRESS		NetDiskAddress,
	IN UCHAR			UnitDiskNo,
	IN PLPX_ADDRESS		PrimaryAddress
	) {
	PLIST_ENTRY		listEntry ;
	KIRQL			oldIrql ;
	KIRQL			oldIrql2 ;
	PLFSTAB_ENTRY	entry ;

	ExAcquireSpinLock(&LfsTable->SpinLock, &oldIrql) ;
	listEntry = LfsTable->LfsTabPartitionList.Flink ;
	while(listEntry != &LfsTable->LfsTabPartitionList) {
		entry = CONTAINING_RECORD(listEntry, LFSTAB_ENTRY, LfsTabPartitionEntry) ;
		
		if(		RtlCompareMemory(																			// NetDisk Address
						&entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.NetDiskAddress.Node,
						NetDiskAddress->Node, 
						LPXADDR_NODE_LENGTH) == LPXADDR_NODE_LENGTH &&
						entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.NetDiskAddress.Port == NetDiskAddress->Port &&
						entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.UnitDiskNo == UnitDiskNo 		// UnitDisk No
			) {

			ExAcquireSpinLock(&entry->SpinLock, &oldIrql2) ;

			RtlCopyMemory(&entry->PrimaryAddress, PrimaryAddress, sizeof(LPX_ADDRESS)) ;
			entry->Flags |= LFSTABENTRY_FLAG_VAILDPRIMADDRESS;

			ExReleaseSpinLock(&entry->SpinLock, oldIrql2) ;


			SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
			("LFS: LfsTable_UpdatePrimaryInfo: updated the primary address of  NetDisk:%02x:%02x:%02x:%02x:%02x:%02x/%d UnitDisk:%d\n",
				entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.NetDiskAddress.Node[0],
				entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.NetDiskAddress.Node[1],
				entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.NetDiskAddress.Node[2],
				entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.NetDiskAddress.Node[3],
				entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.NetDiskAddress.Node[4],
				entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.NetDiskAddress.Node[5],
				NTOHS(entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.NetDiskAddress.Port),
				(int)entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.UnitDiskNo
			));

		}

		listEntry = listEntry->Flink ;
	}
	ExReleaseSpinLock(&LfsTable->SpinLock, oldIrql) ;
}


//////////////////////////////////////////////////////////////////////////
//
//	Static functions.
//
static
BOOLEAN
LfsTable_LookupEx(
	IN PLFS_TABLE			LfsTable,
	IN PNETDISK_PARTITION_INFO	NetDiskPartitionInfo,
	IN BOOLEAN				Primary,
	OUT	PLFSTAB_ENTRY		*LfsTabEntry
	) {
	PLIST_ENTRY		listEntry ;
	KIRQL			oldIrql ;
	PLFSTAB_ENTRY	entry ;
	BOOLEAN			Found ;

	//UNREFERENCED_PARAMETER(BindAddress);

	Found = FALSE ;
	ExAcquireSpinLock(&LfsTable->SpinLock, &oldIrql) ;
	listEntry = LfsTable->LfsTabPartitionList.Flink ;
	while(listEntry != &LfsTable->LfsTabPartitionList) {
		entry = CONTAINING_RECORD(listEntry, LFSTAB_ENTRY, LfsTabPartitionEntry) ;
		
		if(		RtlCompareMemory(																					// NetDisk Address
						&entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.NetDiskAddress,
						&NetDiskPartitionInfo->NetDiskAddress,
						sizeof(LPX_ADDRESS)) == sizeof(LPX_ADDRESS) &&	
				entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.UnitDiskNo == NetDiskPartitionInfo->UnitDiskNo &&
				entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.StartingOffset.QuadPart == NetDiskPartitionInfo->StartingOffset.QuadPart &&
				//RtlCompareMemory(&entry->LocalNetDiskPartitionInfo.BindAddress, BindAddress, sizeof(LPX_ADDRESS))==sizeof(LPX_ADDRESS) &&
				((entry->Flags & LFSTABENTRY_FLAG_ACTPRIMARY) != 0 )== Primary
			) {

			LfsTable_ReferenceEntry (
					entry
				) ;
			*LfsTabEntry = entry ;
			Found = TRUE ;
			break ;
		}

		listEntry = listEntry->Flink ;
	}
	ExReleaseSpinLock(&LfsTable->SpinLock, oldIrql) ;

#if DBG
	if(!Found) {
		SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
			("LFS: LfsTable_LookupEx: updated the primary address of  NetDisk:%02x:%02x:%02x:%02x:%02x:%02x/%d UnitDisk:%d\n",
				NetDiskPartitionInfo->NetDiskAddress.Node[0],
				NetDiskPartitionInfo->NetDiskAddress.Node[1],
				NetDiskPartitionInfo->NetDiskAddress.Node[2],
				NetDiskPartitionInfo->NetDiskAddress.Node[3],
				NetDiskPartitionInfo->NetDiskAddress.Node[4],
				NetDiskPartitionInfo->NetDiskAddress.Node[5],
				NTOHS(NetDiskPartitionInfo->NetDiskAddress.Port),
				(int)NetDiskPartitionInfo->UnitDiskNo
			));
	}
#endif

	return Found ;
}

static
BOOLEAN
LfsTable_Lookup(
	IN PLFS_TABLE				LfsTable,
	IN PNETDISK_PARTITION_INFO	NetDiskPartitionInfo,
	OUT	PLFSTAB_ENTRY			*LfsTabEntry
	) {
	PLIST_ENTRY		listEntry ;
	KIRQL			oldIrql ;
	PLFSTAB_ENTRY	entry ;
	BOOLEAN			Found ;

	Found = FALSE ;
	ExAcquireSpinLock(&LfsTable->SpinLock, &oldIrql) ;
	listEntry = LfsTable->LfsTabPartitionList.Flink ;
	while(listEntry != &LfsTable->LfsTabPartitionList) {
		entry = CONTAINING_RECORD(listEntry, LFSTAB_ENTRY, LfsTabPartitionEntry) ;
		
		if(		RtlCompareMemory(																			// NetDisk Address
						&entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.NetDiskAddress,
						&NetDiskPartitionInfo->NetDiskAddress, 
						sizeof(LPX_ADDRESS)) == sizeof(LPX_ADDRESS) &&
				entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.UnitDiskNo == NetDiskPartitionInfo->UnitDiskNo 							// UnitDisk No
				&& entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo.StartingOffset.QuadPart == NetDiskPartitionInfo->StartingOffset.QuadPart	//	Partition Starting Byte Offset
			) {

			LfsTable_ReferenceEntry (
					entry
				) ;
			*LfsTabEntry = entry ;
			Found = TRUE ;
			break ;
		}

		listEntry = listEntry->Flink ;
	}
	ExReleaseSpinLock(&LfsTable->SpinLock, oldIrql) ;

#if DBG
	if(!Found) {
		SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
			("LFS: LfsTable_Lookup: updated the primary address of  NetDisk:%02x:%02x:%02x:%02x:%02x:%02x/%d UnitDisk:%d\n",
				NetDiskPartitionInfo->NetDiskAddress.Node[0],
				NetDiskPartitionInfo->NetDiskAddress.Node[1],
				NetDiskPartitionInfo->NetDiskAddress.Node[2],
				NetDiskPartitionInfo->NetDiskAddress.Node[3],
				NetDiskPartitionInfo->NetDiskAddress.Node[4],
				NetDiskPartitionInfo->NetDiskAddress.Node[5],
				NTOHS(NetDiskPartitionInfo->NetDiskAddress.Port),
				(int)NetDiskPartitionInfo->UnitDiskNo
			));
	}
#endif

	return Found ;
}

static
NTSTATUS
LfsTable_CreateEntry(
	IN PNETDISK_PARTITION_INFO	NetDiskPartitionInfo,
	IN PLPX_ADDRESS			BindAddress,
	IN BOOLEAN				Primary,
	OUT PLFSTAB_ENTRY		*Entry
	) {
	NTSTATUS		ntStatus = STATUS_SUCCESS ;
	PLFSTAB_ENTRY	entry ;

	entry = (PLFSTAB_ENTRY)ExAllocatePoolWithTag(NonPagedPool, sizeof(LFSTAB_ENTRY), LFSTAB_ENTRY_TAG) ;
	if(!entry) {

		SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
				("LfsTable_CreateEntry: ExAllocatePoolWithTag() failed.\n"));

		ntStatus = STATUS_INSUFFICIENT_RESOURCES ;
		goto cleanup ;
	}

	entry->ReferenceCount = 1 ;
	entry->Flags = Primary?LFSTABENTRY_FLAG_ACTPRIMARY:0 ;
	InitializeListHead(&entry->LfsTabPartitionEntry) ;
	KeInitializeSpinLock(&entry->SpinLock) ;
	RtlCopyMemory(&entry->LocalNetDiskPartitionInfo.BindAddress, BindAddress, sizeof(LPX_ADDRESS)) ;
	RtlCopyMemory(&entry->LocalNetDiskPartitionInfo.NetDiskPartitionInfo, NetDiskPartitionInfo, sizeof(NETDISK_PARTITION_INFO)) ;

	*Entry = entry ;

cleanup:

	return ntStatus ;
}

static
VOID
LfsTable_ReferenceEntry (
		IN PLFSTAB_ENTRY	LfsTableEntry
	)
{
    LONG result;

    result = InterlockedIncrement (&LfsTableEntry->ReferenceCount);

    ASSERT (result >= 1);
}

static
VOID
LfsTable_DereferenceEntry (
		IN PLFSTAB_ENTRY	LfsTableEntry
	)
{
    LONG result;
	PLFS_TABLE	LfsTable = LfsTableEntry->LfsTable ;
	KIRQL		oldIrql ;


    result = InterlockedDecrement (&LfsTableEntry->ReferenceCount);
    ASSERT (result >= 0);

    if (result == 0) 
	{
		ExAcquireSpinLock(&LfsTable->SpinLock, &oldIrql) ;
		RemoveEntryList(&LfsTableEntry->LfsTabPartitionEntry) ;
		ExReleaseSpinLock(&LfsTable->SpinLock, oldIrql) ;

		InterlockedDecrement(&LfsTable->EntryCount) ;

		LfsTable_Dereference(LfsTable) ;

		ExFreePoolWithTag(
			LfsTableEntry,
			LFSTAB_ENTRY_TAG
			);

		SPY_LOG_PRINT( LFS_DEBUG_TABLE_INFO,
				("LfsTable_DereferenceEntry: Lfs Table is Freed\n"));
	}
}


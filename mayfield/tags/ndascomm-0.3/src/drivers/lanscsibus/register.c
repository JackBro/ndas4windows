#include <ntddk.h>
#include <tdikrnl.h>
#include <scsi.h>
#include <ntddscsi.h>

#include "ndasbus.h"
#include "devreg.h"
#include "lsbusioctl.h"
#include "lsminiportioctl.h"
#include "ndas/ndasdib.h"

#include "busenum.h"
#include "stdio.h"
#include "ndasbuspriv.h"


#ifdef __MODULE__
#undef __MODULE__
#endif // __MODULE__
#define __MODULE__ "Register"


//
//	Internal declarations
//

NTSTATUS
Reg_InitializeTdiPnPHandlers(
	PFDO_DEVICE_DATA	FdoData
);

VOID
Reg_DeregisterTdiPnPHandlers (
	PFDO_DEVICE_DATA	FdoData
);

VOID
Reg_AddAddressHandler ( 
	IN PTA_ADDRESS NetworkAddress,
	IN PUNICODE_STRING  DeviceName,
	IN PTDI_PNP_CONTEXT Context
);

VOID
Reg_DelAddressHandler ( 
	IN PTA_ADDRESS NetworkAddress,
	IN PUNICODE_STRING DeviceName,
	IN PTDI_PNP_CONTEXT Context
);

#ifdef ALLOC_PRAGMA
#pragma alloc_text( PAGE, Reg_InitializeTdiPnPHandlers )
#pragma alloc_text( PAGE, Reg_AddAddressHandler )
#pragma alloc_text( PAGE, Reg_DelAddressHandler )
#endif


//
//	Pool tags
//

#define NDBUSREG_POOLTAG_PLUGIN		'ipRN'
#define NDBUSREG_POOLTAG_ADDTARGET	'taRN'
#define NDBUSREG_POOLTAG_WORKITEM	'iwRN'
#define	NDBUSREG_POOLTAG_KEYINFO	'ikRN'
#define NDBUSREG_POOLTAG_NAMEBUFF	'bnRN'


//
//	Registry key name of NDAS devices
//

#define NDBUS_REG_NAME			L"Devices"


//
//	NDAS device information
//

#define NDBUS_REG_DEVREGNAME	L"Dev%ld"					// Key Name

//	Miniport

#define NDBUS_REG_SLOTNO		L"SlotNo"					// 4 bytes
#define NDBUS_REG_HWIDLEN		L"HardwareIDLen"			// 4 bytes
#define NDBUS_REG_HWIDS			L"HardwareIDs"				// Variable length.
#define NDBUS_REG_DEVMAXBPR		L"MaxBlocksPerRequest"		// 4 bytes
#define NDBUS_REG_FLAGS			L"Flags"					// 4 bytes


// AddTargetData

#define NDBUS_REG_TARGETNAME	L"Target%ld"				// Key name

#define NDBUS_REG_TARGETID		L"TargetId"					// 4 bytes
#define NDBUS_REG_TARGETTYPE	L"TargetType"				// 4 bytes
#define NDBUS_REG_DESIREACC		L"DesiredAccess"			// 4 bytes
#define NDBUS_REG_TARGETBLKS	L"TargetBlks"				// 4 bytes
#define NDBUS_REG_CNTECR		L"CntEcrMethod"				// 4 bytes
#define NDBUS_REG_CNTECRKEYLEN	L"CntEcrKeyLen"				// 4 bytes
#define NDBUS_REG_CNTECRKEY		L"CntEcrKey"				// Variable length.
#define NDBUS_REG_LUROPTIONS	L"LurOptions"				// 4 bytes
#define NDBUS_REG_LURFLAGS		L"LurFlags"					// 4 bytes
#define NDBUS_REG_UNITCNT		L"UnitCount"				// 4 bytes


//
//	NDAS Unit
//

#define NDBUS_REG_UNITREG_NAME	L"Unit%ld"					// Key name

#define NDBUS_REG_ADDR			L"Address"					// 18 bytes
#define NDBUS_REG_NICADDR		L"NicAddress"				// 18 bytes
#define	NDBUS_REG_ISWAN			L"IsWan"					// 4 bytes
#define NDBUS_REG_UNITNO		L"UnitNo"					// 4 bytes
#define NDBUS_REG_UNITBLKS		L"UnitBlks"					// 8 bytes
#define NDBUS_REG_PHYBLKS		L"PhyBlks"					// 8 bytes
#define NDBUS_REG_HWTYPE		L"HWType"					// 4 bytes
#define NDBUS_REG_HWVER			L"HWVersion"				// 4 bytes
#define NDBUS_REG_USERID		L"UserID"					// 4 bytes
#define NDBUS_REG_PW			L"PW"						// 8 bytes
#define NDBUS_REG_LURNOPT		L"LurnOpt"					// 4 bytes
#define NDBUS_REG_RECONTRIAL	L"ReconnTrial"				// 4 bytes
#define NDBUS_REG_RECONINTERV	L"ReconnInterval"			// 4 bytes


//
//	RAID specific for each NDAS Unit
//

#define NDBUS_REG_RAID_SECTOR		L"RAID_Sector"				// 8 bytes
#define NDBUS_REG_RAID_OFFSETFLAG	L"RAID_OffsetFlag"			// 4 bytes
#define NDBUS_REG_RAID_BMSTART		L"RAID_BMLocat"				// 8 bytes
#define NDBUS_REG_RAID_SPB			L"RAID_SectorPerBit"		// 4 bytes
#define NDBUS_REG_RAID_LASTWRIT		L"RAID_LastWrittenSector"	// 8 bytes


//
//	Max numbers
//

#define	MAX_DEVICES_IN_NDAS_REGISTRY	(MAXLONG)



//////////////////////////////////////////////////////////////////////////
//
//	Open registry
//

//
//	Open a registry key of NDAS devices.
//
NTSTATUS
Reg_OpenNdasDeviceRoot(
		PHANDLE			NdasDeviceReg,
		ACCESS_MASK		AccessMask,
		HANDLE			BusDevRoot
	){
    NTSTATUS			status;
	HANDLE				regKey;
	OBJECT_ATTRIBUTES	objectAttributes;
	UNICODE_STRING		objectName;


	RtlInitUnicodeString(&objectName, NDBUS_REG_NAME);
	InitializeObjectAttributes( &objectAttributes,
								&objectName,
								OBJ_KERNEL_HANDLE,
								BusDevRoot,
								NULL
							);
	status = ZwCreateKey(
						&regKey,
						AccessMask,
						&objectAttributes,
						0,
						NULL,
						REG_OPTION_NON_VOLATILE,
						NULL
					);

	*NdasDeviceReg = regKey;

    return status;
}


//
//	Open a child key in the parent key
//

NTSTATUS
Reg_OpenChildKey(
		PHANDLE	ChildKey,
		PWCHAR	ChildNameTpl,
		ULONG	PostfixNo,
		BOOLEAN	DeleteExisting,
		HANDLE	ParentKey
		) {
	NTSTATUS					status;
	OBJECT_ATTRIBUTES			objectAttributes;
	UNICODE_STRING				objectName;
	PWSTR						nameBuffer;
	HANDLE						devReg;
	ULONG						dispos;

	status = STATUS_SUCCESS;

	nameBuffer = (PWSTR)ExAllocatePoolWithTag(PagedPool, 512, NDBUSREG_POOLTAG_NAMEBUFF);
	if(!nameBuffer) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ExAllocatePoolWithTag(KEY_BASIC_INFORMATION) failed.\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	objectName.Buffer = nameBuffer;
	objectName.MaximumLength = 512;

	status = swprintf(objectName.Buffer, ChildNameTpl, PostfixNo);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("RtlStringCbPrintfW() failed. NTSTATUS:%08lx\n", status));
		goto cleanup;
	}
	objectName.Length = (USHORT)wcslen(objectName.Buffer) * sizeof(WCHAR);

	InitializeObjectAttributes(&objectAttributes, &objectName, OBJ_KERNEL_HANDLE, ParentKey, NULL);


	status = ZwCreateKey(
							&devReg,
							KEY_ALL_ACCESS,
							&objectAttributes,
							0,
							NULL,
							REG_OPTION_NON_VOLATILE,
							&dispos
						);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Inital ZwCreateKey() failed. NTSTATUS:%08lx\n", status));
		ASSERT(FALSE);
		goto cleanup;
	}


	//
	//	Delete a key if a caller specify.
	//

	if(dispos == REG_OPENED_EXISTING_KEY && DeleteExisting) {
		status = DrDeleteAllSubKeys(devReg);
		if(!NT_SUCCESS(status)) {
			ZwClose(devReg);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ZwDeleteKey() failed. NTSTATUS:%08lx\n", status));
			goto cleanup;
		}
	}

	//
	//	Set return values
	//
	*ChildKey = devReg;

cleanup:
	if(nameBuffer)
		ExFreePool(nameBuffer);

	return status;
}


//
//	Open a registry key of an NDAS device instance
//

NTSTATUS
Reg_OpenDeviceControlRoot(
		PHANDLE		DevControlKey,
		ACCESS_MASK	AccessMask
){
	NTSTATUS					status;
	OBJECT_ATTRIBUTES			objectAttributes;
	UNICODE_STRING				objectName;
	PWSTR						nameBuffer;
	HANDLE						devReg;
	ULONG						dispos;

	status = STATUS_SUCCESS;

	nameBuffer = (PWSTR)ExAllocatePoolWithTag(PagedPool, 512, NDBUSREG_POOLTAG_NAMEBUFF);
	if(!nameBuffer) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ExAllocatePoolWithTag(KEY_BASIC_INFORMATION) failed.\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	objectName.Buffer = nameBuffer;
	objectName.MaximumLength = 512;
	RtlInitUnicodeString(&objectName, L"\\REGISTRY\\MACHINE\\SYSTEM\\CurrentControlSet\\Control\\NDAS");
	objectName.Length = (USHORT)wcslen(objectName.Buffer) * sizeof(WCHAR);

	InitializeObjectAttributes(&objectAttributes, &objectName, OBJ_KERNEL_HANDLE|OBJ_CASE_INSENSITIVE, NULL, NULL);

	status = ZwCreateKey(
					&devReg,
					AccessMask,
					&objectAttributes,
					0,
					NULL,
					REG_OPTION_NON_VOLATILE,
					&dispos);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Inital ZwCreateKey() failed. NTSTATUS:%08lx\n", status));
		ASSERT(FALSE);
		goto cleanup;
	}

	//
	//	Set return values
	//
	*DevControlKey = devReg;

cleanup:
	if(nameBuffer)
		ExFreePool(nameBuffer);

	return status;
}


//
//	Open a registry key of an NDAS device instance
//

NTSTATUS
Reg_OpenDeviceInst(
		PHANDLE	DevInstKey,
		ULONG	SlotNo,
		BOOLEAN	DeleteExisting,
		HANDLE	NdasDeviceRoot
){
	return Reg_OpenChildKey(DevInstKey, NDBUS_REG_DEVREGNAME, SlotNo, DeleteExisting, NdasDeviceRoot);
}

//
//	Open a registry key of an NDAS device instance
//
NTSTATUS
Reg_OpenTarget(
		PHANDLE	TargetKey,
		ULONG	TargetId,
		BOOLEAN	DeleteExisting,
		HANDLE	NdasDeviceInst
){

	return Reg_OpenChildKey(TargetKey, NDBUS_REG_TARGETNAME, TargetId, DeleteExisting, NdasDeviceInst);
}

//
//	Open a registry key of an Unit device
//
NTSTATUS
Reg_OpenUnit(
		 PHANDLE	UnitKey,
		 ULONG		UnitIdx,
		 BOOLEAN	DeleteExisting,
		 HANDLE		TargetKey
){

	return Reg_OpenChildKey(UnitKey, NDBUS_REG_UNITREG_NAME, UnitIdx, DeleteExisting, TargetKey);
}


//////////////////////////////////////////////////////////////////////////
//
//	Read registry
//


//
//	Read an NDAS Unit device information from the registry
//

NTSTATUS
ReadUnitFromRegistry(
		HANDLE				UnitDevKey,
		PLSBUS_UNITDISK		Unit
) {
	NTSTATUS	status;
	ULONG		tempUlong;
	//
	//	read NDAS unit device information
	//

	//	Address
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_ADDR,
						REG_BINARY,
						&Unit->Address,
						sizeof(Unit->Address),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_ADDR));
		return	status;
	}
	//	NIC Address
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_NICADDR,
						REG_BINARY,
						&Unit->NICAddr,
						sizeof(Unit->NICAddr),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_NICADDR));
		return	status;
	}
	//	IsWAN
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_ISWAN,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_ISWAN));
		return	status;
	}
	Unit->IsWANConnection = (UCHAR)tempUlong;

	//	Unit number
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_UNITNO,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_UNITNO));
		return	status;
	}
	Unit->ucUnitNumber = (UCHAR)tempUlong;

	//	Unit blocks
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_UNITBLKS,
						REG_BINARY,
						&Unit->ulUnitBlocks,
						sizeof(Unit->ulUnitBlocks),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_UNITBLKS));
		return	status;
	}

	//	Physical blocks
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_PHYBLKS,
						REG_BINARY,
						&Unit->ulPhysicalBlocks,
						sizeof(Unit->ulPhysicalBlocks),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_PHYBLKS));
		return	status;
	}

	//	Hardware type
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_HWTYPE,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_HWTYPE));
		return	status;
	}
	Unit->ucHWType = (UCHAR)tempUlong;

	//	Hardware version
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_HWVER,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_HWVER));
		return	status;
	}
	Unit->ucHWVersion = (UCHAR)tempUlong;

	//	User ID
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_USERID,
						REG_DWORD,
						&Unit->iUserID,
						sizeof(Unit->iUserID),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_USERID));
		return	status;
	}

	//	Password
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_PW,
						REG_BINARY,
						&Unit->iPassword,
						sizeof(Unit->iPassword),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_PW));
		return	status;
	}

	//	LURN options
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_LURNOPT,
						REG_DWORD,
						&Unit->LurnOptions,
						sizeof(Unit->LurnOptions),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_LURNOPT));
		return	status;
	}

	//	Reconnection trial
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_RECONTRIAL,
						REG_DWORD,
						&Unit->ReconnTrial,
						sizeof(Unit->ReconnTrial),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RECONTRIAL));
		return	status;
	}
	//	Reconnection interval
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_RECONINTERV,
						REG_DWORD,
						&Unit->ReconnInterval,
						sizeof(Unit->ReconnInterval),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RECONINTERV));
		return	status;
	}
	//	RAID: Sector information
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_RAID_SECTOR,
						REG_BINARY,
						&Unit->RAID_Info.SectorInfo,
						sizeof(Unit->RAID_Info.SectorInfo),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RAID_SECTOR));
		return	status;
	}
	//	RAID: offset flags
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_RAID_OFFSETFLAG,
						REG_DWORD,
						&Unit->RAID_Info.OffsetFlagInfo,
						sizeof(Unit->RAID_Info.OffsetFlagInfo),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RAID_OFFSETFLAG));
		return	status;
	}
	//	Bitmap location
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_RAID_BMSTART,
						REG_BINARY,
						&Unit->RAID_Info.SectorBitmapStart,
						sizeof(Unit->RAID_Info.SectorBitmapStart),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RAID_BMSTART));
		return	status;
	}
	//	Sectors per bit
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_RAID_SPB,
						REG_DWORD,
						&Unit->RAID_Info.SectorsPerBit,
						sizeof(Unit->RAID_Info.SectorsPerBit),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RAID_SPB));
		return	status;
	}
	//	Last written sector
	status = DrReadKeyValue(
						UnitDevKey,
						NDBUS_REG_RAID_LASTWRIT,
						REG_BINARY,
						&Unit->RAID_Info.SectorLastWrittenSector,
						sizeof(Unit->RAID_Info.SectorLastWrittenSector),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RAID_LASTWRIT));
		return	status;
	}

	return STATUS_SUCCESS;
}


//
//	Read a target and unit devices.
//

NTSTATUS
ReadTargetAndUnitFromRegistry(
		HANDLE						TargetKey,
		ULONG						AddTargetDataLen,
		PLANSCSI_ADD_TARGET_DATA	AddTargetData,
		PULONG						OutLength
) {
	NTSTATUS				status;
	ULONG					unitCnt;
	HANDLE					unitKey;
	PKEY_BASIC_INFORMATION	keyInfo;
	ULONG					outLength;
	ULONG					idxKey;
	ULONG					tempUlong;

	//
	//	Read unit device counter to check buffer size first.
	//
	status = DrReadKeyValue(
						TargetKey,
						NDBUS_REG_UNITCNT,
						REG_DWORD,
						&unitCnt,
						sizeof(unitCnt),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_UNITCNT));
		return	status;
	}

	//
	//	Check buffer size.
	//
	outLength = FIELD_OFFSET(LANSCSI_ADD_TARGET_DATA, UnitDiskList) + sizeof(LSBUS_UNITDISK) * unitCnt;
	if(outLength > AddTargetDataLen) {
		if(OutLength)
			*OutLength = outLength;
		return STATUS_BUFFER_TOO_SMALL;
	}

	RtlZeroMemory(AddTargetData, outLength);
	AddTargetData->ulNumberOfUnitDiskList = unitCnt;
	AddTargetData->ulSize = outLength;

	//
	//	read Target information
	//

	//	Target ID
	status = DrReadKeyValue(
						TargetKey,
						NDBUS_REG_TARGETID,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_TARGETID));
		return	status;
	}
	AddTargetData->ucTargetId = (UCHAR)tempUlong;

	// Target type
	status = DrReadKeyValue(
						TargetKey,
						NDBUS_REG_TARGETTYPE,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_TARGETTYPE));
		return	status;
	}
	AddTargetData->ucTargetType = (UCHAR)tempUlong;;

	// Desired access
	status = DrReadKeyValue(
						TargetKey,
						NDBUS_REG_DESIREACC,
						REG_DWORD,
						&AddTargetData->DesiredAccess,
						sizeof(AddTargetData->DesiredAccess),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_DESIREACC));
		return	status;
	}

	// Target blocks
	status = DrReadKeyValue(
						TargetKey,
						NDBUS_REG_TARGETBLKS,
						REG_BINARY,
						&AddTargetData->ulTargetBlocks,
						sizeof(AddTargetData->ulTargetBlocks),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_TARGETBLKS));
		return	status;
	}

	// Content encryption method
	status = DrReadKeyValue(
						TargetKey,
						NDBUS_REG_CNTECR,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_CNTECR));
		return	status;
	}
	AddTargetData->CntEcrMethod = (UCHAR)tempUlong;

	// Content encryption key length
	status = DrReadKeyValue(
						TargetKey,
						NDBUS_REG_CNTECRKEYLEN,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_CNTECRKEYLEN));
		return	status;
	}
	AddTargetData->CntEcrKeyLength = (UCHAR)tempUlong;

	// Content encryption key
	status = DrReadKeyValue(
						TargetKey,
						NDBUS_REG_CNTECRKEY,
						REG_BINARY,
						&AddTargetData->CntEcrKey,
						sizeof(AddTargetData->CntEcrKey),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_CNTECRKEY));
		return	status;
	}

	// LUR options
	status = DrReadKeyValue(
						TargetKey,
						NDBUS_REG_LUROPTIONS,
						REG_DWORD,
						&AddTargetData->LurOptions,
						sizeof(AddTargetData->LurOptions),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_LUROPTIONS));
		return	status;
	}

	// LUR flags
	status = DrReadKeyValue(
						TargetKey,
						NDBUS_REG_LURFLAGS,
						REG_DWORD,
						&AddTargetData->LurFlags,
						sizeof(AddTargetData->LurFlags),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_LURFLAGS));
		return	status;
	}

	//
	//	read Unit device information.
	//
	keyInfo = (PKEY_BASIC_INFORMATION)ExAllocatePoolWithTag(PagedPool, 512, NDBUSREG_POOLTAG_KEYINFO);
	if(!keyInfo) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ExAllocatePoolWithTag(KEY_BASIC_INFORMATION) failed.\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	for(idxKey = 0 ; idxKey < unitCnt; idxKey ++) {

		status = Reg_OpenUnit(&unitKey, idxKey, FALSE, TargetKey);
		if(!NT_SUCCESS(status)) {
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenUnit() failed. NTSTATUS:%08lx\n", status));
			ExFreePool(keyInfo);
			return status;
		}

		status = ReadUnitFromRegistry(unitKey, AddTargetData->UnitDiskList + idxKey);

		ZwClose(unitKey);

		if(!NT_SUCCESS(status)) {
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ReadLurnDescFromRegistry() failed. NTSTATUS:%08lx\n", status));
			ExFreePool(keyInfo);
			return status;
		}
	}

	ExFreePool(keyInfo);

	//
	//	Set return values
	//
	if(OutLength)
		*OutLength = outLength;

	return STATUS_SUCCESS;
}


//
//	read NDAS device instance information
//

NTSTATUS
ReadNDASDevInstFromRegistry(
		HANDLE							NdasDevInst,
		ULONG							PlugInLen,
		PBUSENUM_PLUGIN_HARDWARE_EX2	PlugIn,
		PULONG							OutLength
) {
	NTSTATUS	status;
	ULONG		outLength;
	ULONG		hwIDBufferLen;

	//
	//	Read unit device counter to check buffer size first.
	//
	status = DrReadKeyValue(
						NdasDevInst,
						NDBUS_REG_HWIDLEN,
						REG_DWORD,
						&hwIDBufferLen,
						sizeof(hwIDBufferLen),
						NULL
						);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_HWIDLEN));
		return	status;
	}

	//
	//	Check buffer size.
	//
	outLength = FIELD_OFFSET(BUSENUM_PLUGIN_HARDWARE_EX2, HardwareIDs) + hwIDBufferLen * sizeof(WCHAR);
	if(PlugInLen < outLength) {
		if(OutLength)
			*OutLength = outLength;
		return STATUS_BUFFER_TOO_SMALL;
	}
	PlugIn->Size = outLength;
	PlugIn->HardwareIDLen = hwIDBufferLen;


	//	Slot number
	status = DrReadKeyValue(
						NdasDevInst,
						NDBUS_REG_SLOTNO,
						REG_DWORD,
						&PlugIn->SlotNo,
						sizeof(PlugIn->SlotNo),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_SLOTNO));
		return	status;
	}

	//	Hardware IDs
	status = DrReadKeyValue(
						NdasDevInst,
						NDBUS_REG_HWIDS,
						REG_MULTI_SZ,
						&PlugIn->HardwareIDs,
						hwIDBufferLen * sizeof(WCHAR),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_HWIDS));
		return	status;
	}

	//	Device max blocks per request
	status = DrReadKeyValue(
						NdasDevInst,
						NDBUS_REG_DEVMAXBPR,
						REG_DWORD,
						&PlugIn->MaxRequestBlocks,
						sizeof(PlugIn->MaxRequestBlocks),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_DEVMAXBPR));
		return	status;
	}

	//	Plug in flags
	status = DrReadKeyValue(
						NdasDevInst,
						NDBUS_REG_FLAGS,
						REG_DWORD,
						&PlugIn->Flags,
						sizeof(PlugIn->Flags),
						NULL
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrReadKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_FLAGS));
		return	status;
	}


	return STATUS_SUCCESS;
}

//////////////////////////////////////////////////////////////////////////
//
//	Write registry
//


//
//	Write an NDAS unit device to the registry.
//

NTSTATUS
WriteUnitToRegistry(
		HANDLE				UnitReg,
		PLSBUS_UNITDISK		Unit
){
	NTSTATUS	status;
	ULONG		tempUlong;
	//
	//	read LUR Node information
	//

	//	Address
	status = DrWriteKeyValue(
						UnitReg,
						NDBUS_REG_ADDR,
						REG_BINARY,
						&Unit->Address,
						sizeof(Unit->Address)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_ADDR));
		return	status;
	}

	//	NicAddress
	status = DrWriteKeyValue(
						UnitReg,
						NDBUS_REG_NICADDR,
						REG_BINARY,
						&Unit->NICAddr,
						sizeof(Unit->NICAddr)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_NICADDR));
		return	status;
	}
	//	IsWan
	tempUlong = Unit->IsWANConnection;
	status = DrWriteKeyValue(
						UnitReg,
						NDBUS_REG_ISWAN,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_ISWAN));
		return	status;
	}
	//	UnitNo
	tempUlong = Unit->ucUnitNumber;
	status = DrWriteKeyValue(
		UnitReg,
		NDBUS_REG_UNITNO,
		REG_DWORD,
		&tempUlong,
		sizeof(tempUlong)
		);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_UNITNO));
		return	status;
	}

	//	Unit blocks
	status = DrWriteKeyValue(
						UnitReg,
						NDBUS_REG_UNITBLKS,
						REG_BINARY,
						&Unit->ulUnitBlocks,
						sizeof(Unit->ulUnitBlocks)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_UNITBLKS));
		return	status;
	}
	//	Physical blocks
	status = DrWriteKeyValue(
						UnitReg,
						NDBUS_REG_PHYBLKS,
						REG_BINARY,
						&Unit->ulPhysicalBlocks,
						sizeof(Unit->ulPhysicalBlocks)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_PHYBLKS));
		return	status;
	}
	//	Hardware type
	tempUlong = Unit->ucHWType;
	status = DrWriteKeyValue(
						UnitReg,
						NDBUS_REG_HWTYPE,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_HWTYPE));
		return	status;
	}
	//	Hardware version
	tempUlong = Unit->ucHWVersion;
	status = DrWriteKeyValue(
						UnitReg,
						NDBUS_REG_HWVER,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_HWVER));
		return	status;
	}
	//	User ID
	status = DrWriteKeyValue(
						UnitReg,
						NDBUS_REG_USERID,
						REG_DWORD,
						&Unit->iUserID,
						sizeof(Unit->iUserID)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_USERID));
		return	status;
	}
	//	Password
	status = DrWriteKeyValue(
						UnitReg,
						NDBUS_REG_PW,
						REG_BINARY,
						&Unit->iPassword,
						sizeof(Unit->iPassword)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_PW));
		return	status;
	}
	//	LURN options
	status = DrWriteKeyValue(
						UnitReg,
						NDBUS_REG_LURNOPT,
						REG_DWORD,
						&Unit->LurnOptions,
						sizeof(Unit->LurnOptions)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_LURNOPT));
		return	status;
	}
	//	Reconnection trials
	status = DrWriteKeyValue(
						UnitReg,
						NDBUS_REG_RECONTRIAL,
						REG_DWORD,
						&Unit->ReconnTrial,
						sizeof(Unit->ReconnTrial)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RECONTRIAL));
		return	status;
	}
	//	Reconnection interval
	status = DrWriteKeyValue(
						UnitReg,
						NDBUS_REG_RECONINTERV,
						REG_DWORD,
						&Unit->ReconnInterval,
						sizeof(Unit->ReconnInterval)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RECONINTERV));
		return	status;
	}

	//	RAID: sector information
	status = DrWriteKeyValue(
		UnitReg,
		NDBUS_REG_RAID_SECTOR,
		REG_BINARY,
		&Unit->RAID_Info.SectorInfo,
		sizeof(Unit->RAID_Info.SectorInfo)
		);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RAID_SECTOR));
		return	status;
	}
	//	RAID: Offset flags
	status = DrWriteKeyValue(
		UnitReg,
		NDBUS_REG_RAID_OFFSETFLAG,
		REG_DWORD,
		&Unit->RAID_Info.OffsetFlagInfo,
		sizeof(Unit->RAID_Info.OffsetFlagInfo)
		);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RAID_OFFSETFLAG));
		return	status;
	}
	//	RAID: Bitmap location
	status = DrWriteKeyValue(
		UnitReg,
		NDBUS_REG_RAID_BMSTART,
		REG_BINARY,
		&Unit->RAID_Info.SectorBitmapStart,
		sizeof(Unit->RAID_Info.SectorBitmapStart)
		);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RAID_BMSTART));
		return	status;
	}
	//	RAID: sectors per bit
	status = DrWriteKeyValue(
		UnitReg,
		NDBUS_REG_RAID_SPB,
		REG_DWORD,
		&Unit->RAID_Info.SectorsPerBit,
		sizeof(Unit->RAID_Info.SectorsPerBit)
		);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RAID_SPB));
		return	status;
	}
	//	RAID: last written sector
	status = DrWriteKeyValue(
		UnitReg,
		NDBUS_REG_RAID_LASTWRIT,
		REG_BINARY,
		&Unit->RAID_Info.SectorLastWrittenSector,
		sizeof(Unit->RAID_Info.SectorLastWrittenSector)
		);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_RAID_LASTWRIT));
		return	status;
	}

	return STATUS_SUCCESS;
}


//
//	Write  AddTargetData to the registry.
//

NTSTATUS
WriteTargetToRegistry(
		HANDLE						NdasTargetKey,
		PLANSCSI_ADD_TARGET_DATA	AddTargetData
) {
	NTSTATUS				status;
	HANDLE					unitKey;
	PWSTR					nameBuffer;
	ULONG					idxKey;
	UNICODE_STRING			objectName;
	PLSBUS_UNITDISK			unit;
	ULONG					tempUlong;


	//
	//	Write LUR information.
	//

	//	Target ID
	tempUlong = AddTargetData->ucTargetId;
	status = DrWriteKeyValue(
						NdasTargetKey,
						NDBUS_REG_TARGETID,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_TARGETID));
		return	status;
	}
	//	Target type
	tempUlong = AddTargetData->ucTargetType;
	status = DrWriteKeyValue(
						NdasTargetKey,
						NDBUS_REG_TARGETTYPE,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong)
						);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_TARGETTYPE));
		return	status;
	}
	// AccessRight
	status = DrWriteKeyValue(
						NdasTargetKey,
						NDBUS_REG_DESIREACC,
						REG_DWORD,
						&AddTargetData->DesiredAccess,
						sizeof(AddTargetData->DesiredAccess)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_DESIREACC));
		return	status;
	}
	// Target blocks
	status = DrWriteKeyValue(
						NdasTargetKey,
						NDBUS_REG_TARGETBLKS,
						REG_BINARY,
						&AddTargetData->ulTargetBlocks,
						sizeof(AddTargetData->ulTargetBlocks)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_TARGETBLKS));
		return	status;
	}
	// Content encryption method
	tempUlong = AddTargetData->CntEcrMethod;
	status = DrWriteKeyValue(
						NdasTargetKey,
						NDBUS_REG_CNTECR,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_CNTECR));
		return	status;
	}
	// Content encryption method
	tempUlong = AddTargetData->CntEcrKeyLength;
	status = DrWriteKeyValue(
						NdasTargetKey,
						NDBUS_REG_CNTECRKEYLEN,
						REG_DWORD,
						&tempUlong,
						sizeof(tempUlong)
		);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_CNTECRKEYLEN));
		return	status;
	}
	// Content encryption key
	status = DrWriteKeyValue(
						NdasTargetKey,
						NDBUS_REG_CNTECRKEY,
						REG_BINARY,
						&AddTargetData->CntEcrKey,
						sizeof(AddTargetData->CntEcrKey)
		);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_CNTECRKEY));
		return	status;
	}
	//	LUR options
	status = DrWriteKeyValue(
						NdasTargetKey,
						NDBUS_REG_LUROPTIONS,
						REG_DWORD,
						&AddTargetData->LurOptions,
						sizeof(AddTargetData->LurOptions)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_LUROPTIONS));
		return	status;
	}
	//	LUR flags
	status = DrWriteKeyValue(
						NdasTargetKey,
						NDBUS_REG_LURFLAGS,
						REG_DWORD,
						&AddTargetData->LurFlags,
						sizeof(AddTargetData->LurFlags)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_LURFLAGS));
		return	status;
	}
	//	NDAS unit count
	status = DrWriteKeyValue(
						NdasTargetKey,
						NDBUS_REG_UNITCNT,
						REG_DWORD,
						&AddTargetData->ulNumberOfUnitDiskList,
						sizeof(AddTargetData->ulNumberOfUnitDiskList)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_UNITCNT));
		return	status;
	}

	//
	//	Write NDAS units
	//
	nameBuffer = (PWSTR)ExAllocatePoolWithTag(PagedPool, 512, NDBUSREG_POOLTAG_NAMEBUFF);
	if(!nameBuffer) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ExAllocatePoolWithTag() failed.\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	objectName.Length = 0;
	objectName.MaximumLength = 512;
	objectName.Buffer = nameBuffer;

	for(idxKey = 0 ; idxKey < AddTargetData->ulNumberOfUnitDiskList; idxKey ++) {
		unit = AddTargetData->UnitDiskList + idxKey;

		status = Reg_OpenUnit(	&unitKey,
								idxKey,
								TRUE,
								NdasTargetKey);
		if(!NT_SUCCESS(status)) {
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenUnit() failed. NTSTATUS:%08lx\n", status));
			break;
		}

		status = WriteUnitToRegistry(unitKey, unit);

		ZwClose(unitKey);

		if(!NT_SUCCESS(status)) {
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("WriteUnitToRegistry() failed. NTSTATUS:%08lx\n", status));
			break;
		}

	}

	ExFreePool(nameBuffer);

	return status;
}


//
//	Write an NDAS device to the registry.
//	NOTE: It does not write any target information.
//

NTSTATUS
WriteNDASDevToRegistry(
	HANDLE							NdasDevInst,
	PBUSENUM_PLUGIN_HARDWARE_EX2	Plugin
){

	NTSTATUS					status;


	//	Slot number
	status = DrWriteKeyValue(
					NdasDevInst,
					NDBUS_REG_SLOTNO,
					REG_DWORD,
					&Plugin->SlotNo,
					sizeof(Plugin->SlotNo)
				);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_SLOTNO));
		goto cleanup;
	}

	//	Max request blocks
	status = DrWriteKeyValue(
					NdasDevInst,
					NDBUS_REG_DEVMAXBPR,
					REG_DWORD,
					&Plugin->MaxRequestBlocks,
					sizeof(Plugin->MaxRequestBlocks)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_DEVMAXBPR));
		goto cleanup;
	}

	//	Hardware ID length
	status = DrWriteKeyValue(
					NdasDevInst,
					NDBUS_REG_HWIDLEN,
					REG_DWORD,
					&Plugin->HardwareIDLen,
					sizeof(Plugin->HardwareIDLen)
					);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_HWIDLEN));
		goto cleanup;
	}

	//	HardwareIDs
	status = DrWriteKeyValue(
					NdasDevInst,
					NDBUS_REG_HWIDS,
					REG_MULTI_SZ,
					&Plugin->HardwareIDs,
					Plugin->HardwareIDLen * sizeof(WCHAR)
		);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_HWIDS));
		goto cleanup;
	}

	//	Flags
	status = DrWriteKeyValue(
					NdasDevInst,
					NDBUS_REG_FLAGS,
					REG_DWORD,
					&Plugin->Flags,
					sizeof(Plugin->Flags)
				);
	if(!NT_SUCCESS(status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrWriteKeyValue() failed. ValueKeyName:%ws\n", NDBUS_REG_FLAGS));
		goto cleanup;
	}


cleanup:
	return status;
}


//
//	Allocate AddTargetData and read the registry.
//

NTSTATUS
ReadTargetInstantly(
	HANDLE						NdasDevInst,
	ULONG						TargetId,
	PLANSCSI_ADD_TARGET_DATA	*AddTargetData
){
	NTSTATUS					status;
	HANDLE						targetKey;
	ULONG						outLength;
	PLANSCSI_ADD_TARGET_DATA	addTargetData;

	do {

		status =Reg_OpenTarget(&targetKey, TargetId, FALSE, NdasDevInst);
		if(!NT_SUCCESS(status)) {
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenTarget() failed. NTSTATUS:%08lx\n", status));
			break;
		}

		status = ReadTargetAndUnitFromRegistry(targetKey, 0, NULL, &outLength);
		if(status != STATUS_BUFFER_TOO_SMALL) {
			ZwClose(targetKey);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ReadTargetAndUnitFromRegistry() failed. NTSTATUS:%08lx\n", status));
			break;
		}
		addTargetData = ExAllocatePoolWithTag(PagedPool, outLength, NDBUSREG_POOLTAG_ADDTARGET);
		if(addTargetData == NULL) {
			ZwClose(targetKey);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ExAllocatePoolWithTag() failed. NTSTATUS:%08lx\n", status));
			break;
		}
		status = ReadTargetAndUnitFromRegistry(targetKey, outLength, addTargetData,  &outLength);
		if(!NT_SUCCESS(status)) {
			ExFreePool(addTargetData);
			ZwClose(targetKey);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ReadTargetAndUnitFromRegistry() failed. NTSTATUS:%08lx\n", status));
			break;
		}


		//
		//	Set return values
		//

		*AddTargetData = addTargetData;

	} while(FALSE);

	return status;
}


NTSTATUS
RewriteTargetInstantly(
	HANDLE						NdasDevInst,
	ULONG						TargetId,
	PLANSCSI_ADD_TARGET_DATA	AddTargetData
){
	NTSTATUS status;
	HANDLE	targetKey;

	do {

		//
		//	Clean the target registry to rewrite AddTargetData
		//
		status = Reg_OpenTarget(&targetKey, TargetId, FALSE, NdasDevInst);
		if(!NT_SUCCESS(status)) {
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenTarget() failed. NTSTATUS:%08lx\n", status));
			break;
		}
		DrDeleteAllSubKeys(targetKey);


		//
		//	Rewrite AddTargetData
		//
		status = WriteTargetToRegistry(targetKey, AddTargetData);
		if(!NT_SUCCESS(status)) {
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("WriteTargetToRegistry() failed. NTSTATUS:%08lx\n", status));
			break;
		}

		ZwClose(targetKey);
	} while(FALSE);

	return status;
}

//////////////////////////////////////////////////////////////////////////
//
//	Plugin NDAS devices in the registry
//


//
//	Enumerate NDAS devices by reading registry.
//

NTSTATUS
EnumerateByRegistry(
		PFDO_DEVICE_DATA	FdoData,
		HANDLE				NdasDeviceReg,
		PTA_ADDRESS			AddedAddress,
		ULONG				PlugInTimeMask
	) {

	NTSTATUS					status;
	PKEY_BASIC_INFORMATION		keyInfo;
	ULONG						outLength;
	LONG						idxKey;
	OBJECT_ATTRIBUTES			objectAttributes;
	UNICODE_STRING				objectName;
	HANDLE						ndasDevInst;
	PPDO_DEVICE_DATA			pdoData;
	PBUSENUM_PLUGIN_HARDWARE_EX2	plugIn;
	PLANSCSI_ADD_TARGET_DATA		addTargetData;
	TA_LSTRANS_ADDRESS			bindAddr;

	UNREFERENCED_PARAMETER(PlugInTimeMask);

	keyInfo = (PKEY_BASIC_INFORMATION)ExAllocatePoolWithTag(PagedPool, 512, NDBUSREG_POOLTAG_KEYINFO);
	if(!keyInfo) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ExAllocatePoolWithTag(KEY_BASIC_INFORMATION) failed.\n"));
		return STATUS_INSUFFICIENT_RESOURCES;
	}

	status = STATUS_SUCCESS;
	for(idxKey = 0 ; idxKey < MAX_DEVICES_IN_NDAS_REGISTRY; idxKey ++) {

		//
		//	Enumerate subkeys under the NDAS device root
		//

		status = ZwEnumerateKey(
						NdasDeviceReg,
						idxKey,
						KeyBasicInformation,
						keyInfo,
						512,
						&outLength
						);

		if(status == STATUS_NO_MORE_ENTRIES) {
			status = STATUS_SUCCESS;
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("No more entry\n"));
			break;
		}
		if(status != STATUS_SUCCESS) {
			ASSERT(status != STATUS_BUFFER_OVERFLOW && status != STATUS_BUFFER_TOO_SMALL);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ZwEnumerateKey() failed. NTSTATUS:%08lx\n", status));
			ExFreePool(keyInfo);
			return STATUS_SUCCESS;
		}

		//
		//	Name verification
		//
		//	TODO
		//


		//
		//	Open a sub key (NdasDevices\Devxx) and plug in device with the registry key.
		//

		objectName.Length = objectName.MaximumLength = (USHORT)keyInfo->NameLength;
		objectName.Buffer = keyInfo->Name;
		InitializeObjectAttributes(		&objectAttributes,
										&objectName,
										OBJ_KERNEL_HANDLE,
										NdasDeviceReg,
										NULL
								);
		status = ZwOpenKey(&ndasDevInst, KEY_READ, &objectAttributes);
		if(!NT_SUCCESS(status)) {
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ZwOpenKey() failed. NTSTATUS:%08lx\n", status));
			continue;
		}

		Bus_KdPrint_Def(BUS_DBG_SS_INFO, ("'%wZ' opened.\n", &objectName));

		//
		//	Read NDAS device instance.
		//

		status = ReadNDASDevInstFromRegistry(ndasDevInst, 0, NULL, &outLength);
		if(status != STATUS_BUFFER_TOO_SMALL) {
			ZwClose(ndasDevInst);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ReadNDASDevInstFromRegistry() failed. NTSTATUS:%08lx\n", status));
			continue;
		}
		plugIn = ExAllocatePoolWithTag(PagedPool, outLength, NDBUSREG_POOLTAG_PLUGIN);
		if(plugIn == NULL) {
			ZwClose(ndasDevInst);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ExAllocatePoolWithTag() failed. NTSTATUS:%08lx\n", status));
			continue;
		}
		status = ReadNDASDevInstFromRegistry(ndasDevInst, outLength, plugIn, &outLength);
		if(!NT_SUCCESS(status)) {
			ExFreePool(plugIn);
			ZwClose(ndasDevInst);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ReadNDASDevInstFromRegistry() failed. NTSTATUS:%08lx\n", status));
			continue;
		}


		//
		//	Allocate AddTarget Data and read target and unit devices.
		//

		status = ReadTargetInstantly(ndasDevInst, 0, &addTargetData);
		if(!NT_SUCCESS(status)) {
			ExFreePool(plugIn);
			ZwClose(ndasDevInst);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenTarget() failed. NTSTATUS:%08lx\n", status));
			continue;
		}
		addTargetData->ulSlotNo = plugIn->SlotNo;


		//
		//	Check to see if the NDAS device already plug-ined
		//

		pdoData = LookupPdoData(FdoData, plugIn->SlotNo);
		if(pdoData) {
			ExFreePool(addTargetData);
			ExFreePool(plugIn);
			ZwClose(ndasDevInst);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("PDO already exists. NTSTATUS:%08lx\n", status));
			continue;
		}


		//
		//	Verify AddTargetData structure with DIBs in NDAS devices.
		//

		bindAddr.TAAddressCount = 1;
		bindAddr.Address[0].AddressLength = AddedAddress->AddressLength;
		RtlCopyMemory(&bindAddr.Address[0].Address, AddedAddress->Address, AddedAddress->AddressLength);

		status = NCommVerifyNdasDevWithDIB(addTargetData, &bindAddr, plugIn->MaxRequestBlocks);
		if(!NT_SUCCESS(status)) {
			ExFreePool(addTargetData);
			ExFreePool(plugIn);
			ZwClose(ndasDevInst);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("LSBus_VerifyLurDescWithDIB() failed. NTSTATUS:%08lx\n", status));
			continue;
		}


		//
		//	Rewrite AddTargetData
		//

		status =RewriteTargetInstantly(ndasDevInst, 0, addTargetData);
		if(!NT_SUCCESS(status)) {
			ExFreePool(addTargetData);
			ExFreePool(plugIn);
			ZwClose(ndasDevInst);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenTarget() failed. NTSTATUS:%08lx\n", status));
			continue;
		}


		//
		//	Close handle here
		//

		ZwClose(ndasDevInst);


		//
		//	Plug in a LanscsiBus device.
		//

		status = LSBus_PlugInLSBUSDevice(FdoData, plugIn);
		if(!NT_SUCCESS(status)) {
			ExFreePool(addTargetData);
			ExFreePool(plugIn);
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("LSBus_PlugInLSBUSDevice() failed. NTSTATUS:%08lx\n", status));
			continue;
		}


		//
		//	Add a target to the device which was just plugged in.
		//

		status = LSBus_AddTarget(FdoData, addTargetData);
		if(!NT_SUCCESS(status)) {
			LSBus_PlugOutLSBUSDevice(FdoData, plugIn->SlotNo, FALSE);

			ExFreePool(addTargetData);
			ExFreePool(plugIn);

			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("LSBus_AddTarget() failed. NTSTATUS:%08lx\n", status));
			continue;
		}


		//
		//	Free resources
		//

		ExFreePool(addTargetData);
		ExFreePool(plugIn);
	}

	ExFreePool(keyInfo);

	Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Enumerating NDAS devices from registry is completed. NTSTATUS:%08lx\n", status));

	return STATUS_SUCCESS;
}


//
//	Plug in NDAS devices by reading registry.
//

NTSTATUS
LSBus_PlugInDeviceFromRegistry(
		PFDO_DEVICE_DATA	FdoData,
		PTA_ADDRESS			AddedAddress,
		ULONG				PlugInTimeMask
) {
	NTSTATUS			status;
	HANDLE				DeviceReg;
	HANDLE				NdasDeviceReg;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);


	UNREFERENCED_PARAMETER(PlugInTimeMask);

	DeviceReg = NULL;
	NdasDeviceReg = NULL;

	//
	//	Parameter check
	//
	if(!FdoData) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("FdoData NULL!\n"));
		return STATUS_INVALID_PARAMETER;
	}
	if(FdoData->PersistentPdo == FALSE) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("PersistentPDO has been off.\n"));
		return STATUS_INVALID_PARAMETER;
	}

	//
	//	Open the bus registry and NDAS device root.
	//
	ExAcquireFastMutexUnsafe(&FdoData->RegMutex);

	status = Reg_OpenDeviceControlRoot(&DeviceReg, KEY_READ|KEY_WRITE);
	if(!NT_SUCCESS(status)) {
		ExReleaseFastMutexUnsafe(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("OpenServiceRegistry() failed.\n"));
		return status;
	}
	status = Reg_OpenNdasDeviceRoot(&NdasDeviceReg, KEY_READ, DeviceReg);
	if(!NT_SUCCESS(status)) {
		ZwClose(DeviceReg);
		ExReleaseFastMutexUnsafe(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("OpenNdasDeviceRegistry() failed.\n"));
		return status;
	}


	//
	//	Start to enumerate devices by reading the registry.
	//

	status = EnumerateByRegistry(FdoData, NdasDeviceReg, AddedAddress, PlugInTimeMask);


	//
	//	Close handles
	//

	if(NdasDeviceReg)
		ZwClose(NdasDeviceReg);
	if(DeviceReg)
		ZwClose(DeviceReg);

	ExReleaseFastMutexUnsafe(&FdoData->RegMutex);

	return status;
}


//////////////////////////////////////////////////////////////////////////
//
//	Worker to enumerate NDAS devices at late time.
//

#define NDBUSWRK_MAX_ADDRESSLEN	128

typedef struct _NDBUS_ENUMWORKER {

	PIO_WORKITEM		IoWorkItem;
	PFDO_DEVICE_DATA	FdoData;
	BOOLEAN				AddedAddressVaild;
	UCHAR				AddedTaAddress[NDBUSWRK_MAX_ADDRESSLEN];
	ULONG				PlugInTimeMask;

} NDBUS_ENUMWORKER, *PNDBUS_ENUMWORKER;


VOID
EnumWorker(
	IN PDEVICE_OBJECT DeviceObject,
	IN PVOID Context
){
	PNDBUS_ENUMWORKER	ctx = (PNDBUS_ENUMWORKER)Context;
	PFDO_DEVICE_DATA	fdoData = ctx->FdoData;
	ULONG				timeMask = ctx->PlugInTimeMask;

	UNREFERENCED_PARAMETER(DeviceObject);


	//
	//	IO_WORKITEM is rare resource, give it back to the system now.
	//

	IoFreeWorkItem(ctx->IoWorkItem);


	//
	//	Start enumerating
	//

	if(ctx->AddedAddressVaild)
		LSBus_PlugInDeviceFromRegistry(fdoData,  (PTA_ADDRESS)ctx->AddedTaAddress, timeMask);
	else
		LSBus_PlugInDeviceFromRegistry(fdoData,  NULL, timeMask);

	ExFreePool(Context);

}


//
//	Queue a workitem to plug in NDAS device by reading registry.
//

NTSTATUS
LSBUS_QueueWorker_PlugInByRegistry(
		PFDO_DEVICE_DATA	FdoData,
		PTA_ADDRESS			AddedAddress,
		ULONG				PlugInTimeMask
	) {
	NTSTATUS			status;
	PNDBUS_ENUMWORKER	workItemCtx;
	ULONG				addrLen;

	Bus_KdPrint_Def(BUS_DBG_SS_TRACE, ("entered.\n"));

	//
	//	Parameter check
	//
	if(!FdoData) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("FdoData NULL!\n"));
		return STATUS_INVALID_PARAMETER;
	}
	if(FdoData->PersistentPdo == FALSE) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("PersistentPDO has been off.\n"));
		return STATUS_INVALID_PARAMETER;
	}


	workItemCtx = (PNDBUS_ENUMWORKER)ExAllocatePoolWithTag(NonPagedPool, sizeof(NDBUS_ENUMWORKER), NDBUSREG_POOLTAG_WORKITEM);
	if(!workItemCtx) {
		return STATUS_INSUFFICIENT_RESOURCES;
	}
	workItemCtx->PlugInTimeMask = PlugInTimeMask;
	workItemCtx->FdoData		= FdoData;


	//
	//	set primary address
	//
	addrLen = FIELD_OFFSET(TA_ADDRESS,Address) + AddedAddress->AddressLength;
	if(AddedAddress && addrLen <= NDBUSWRK_MAX_ADDRESSLEN) {
		RtlCopyMemory(	&workItemCtx->AddedTaAddress,
						AddedAddress,
						addrLen
						);
		workItemCtx->AddedAddressVaild = TRUE;
	} else {
		workItemCtx->AddedAddressVaild = FALSE;
	}

	workItemCtx->IoWorkItem = IoAllocateWorkItem(FdoData->Self);
	if(workItemCtx->IoWorkItem == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto cleanup;
	}

	IoQueueWorkItem(
		workItemCtx->IoWorkItem,
		EnumWorker,
		DelayedWorkQueue,
		workItemCtx
		);

	return STATUS_SUCCESS;

cleanup:
	if(workItemCtx) {
		ExFreePool(workItemCtx);
	}

	return status;
}
//////////////////////////////////////////////////////////////////////////
//
//	TDI client
//
NTSTATUS
Reg_InitializeTdiPnPHandlers(
	PFDO_DEVICE_DATA	FdoData
)

/*++

Routine Description:

    Register address handler routinges with TDI

Arguments:
    
    None


Return Value:

    NTSTATUS -- Indicates whether registration succeded

--*/

{
    NTSTATUS                    status;
    TDI_CLIENT_INTERFACE_INFO   info;
    UNICODE_STRING              clientName;
    
    PAGED_CODE ();

 
    //
    // Setup the TDI request structure
    //

    RtlZeroMemory (&info, sizeof (info));
    RtlInitUnicodeString(&clientName, L"NDASBUS");
#ifdef TDI_CURRENT_VERSION
    info.TdiVersion = TDI_CURRENT_VERSION;
#else
    info.MajorTdiVersion = 2;
    info.MinorTdiVersion = 0;
#endif
    info.Unused = 0;
    info.ClientName = &clientName;
    info.BindingHandler = NULL;
    info.AddAddressHandlerV2 = Reg_AddAddressHandler;
    info.DelAddressHandlerV2 = Reg_DelAddressHandler;
    info.PnPPowerHandler = NULL;

    //
    // Register handlers with TDI
    //

    status = TdiRegisterPnPHandlers (&info, sizeof (info), &FdoData->TdiClient);
    if (!NT_SUCCESS (status)) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, (
					"AfdInitializeAddressList: Failed to register PnP handlers: %lx .\n",
					status));
        return status;
    }

    return STATUS_SUCCESS;
}


VOID
Reg_DeregisterTdiPnPHandlers (
	PFDO_DEVICE_DATA	FdoData
){

    if (FdoData->TdiClient) {
        TdiDeregisterPnPHandlers (FdoData->TdiClient);
        FdoData->TdiClient = NULL;
	}
}

VOID
Reg_AddAddressHandler ( 
	IN PTA_ADDRESS NetworkAddress,
	IN PUNICODE_STRING  DeviceName,
	IN PTDI_PNP_CONTEXT Context
    )
/*++

Routine Description:

    TDI add address handler

Arguments:
    
    NetworkAddress  - new network address available on the system

    DeviceName      - name of the device to which address belongs

    Context         - PDO to which address belongs


Return Value:

    None

--*/
{
	UNICODE_STRING	lpxPrefix;

    PAGED_CODE ();

	UNREFERENCED_PARAMETER(Context);

	if (DeviceName==NULL) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, (
			"AfdDelAddressHandler: "
			"NO DEVICE NAME SUPPLIED when deleting address of type %d.\n",
			NetworkAddress->AddressType));
		return;
	}
	Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DeviceName=%ws AddrType=%u AddrLen=%u\n",
										DeviceName->Buffer,
										(ULONG)NetworkAddress->AddressType,
										(ULONG)NetworkAddress->AddressLength));

	//
	//	LPX
	//
	RtlInitUnicodeString(&lpxPrefix, LPX_DEVICE_NAME_PREFIX);

	if(	RtlPrefixUnicodeString(&lpxPrefix, DeviceName, TRUE) &&
		NetworkAddress->AddressType == TDI_ADDRESS_TYPE_LPX
		){
			PTDI_ADDRESS_LPX	lpxAddr;

			lpxAddr = (PTDI_ADDRESS_LPX)NetworkAddress->Address;
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("LPX: %02x:%02x:%02x:%02x:%02x:%02x\n",
									lpxAddr->Node[0],
									lpxAddr->Node[1],
									lpxAddr->Node[2],
									lpxAddr->Node[3],
									lpxAddr->Node[4],
									lpxAddr->Node[5]));

			//
			//	Check to see if FdoData for TdiPnP is created.
			//

			ExAcquireFastMutex(&Globals.Mutex);
			if(Globals.PersistentPdo && Globals.FdoDataTdiPnP) {
				LSBUS_QueueWorker_PlugInByRegistry(Globals.FdoDataTdiPnP, NetworkAddress, 0);
				ExReleaseFastMutex(&Globals.Mutex);
			} else {
				ExReleaseFastMutex(&Globals.Mutex);
				Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("LPX: Binding address came up, but there is no FdoData for TdiPnP\n"));
			}

	//
	//	IP	address
	//
	} else if(NetworkAddress->AddressType == TDI_ADDRESS_TYPE_IP) {
		PTDI_ADDRESS_IP	ipAddr;
		PUCHAR			digit;

		ipAddr = (PTDI_ADDRESS_IP)NetworkAddress->Address;
		digit = (PUCHAR)&ipAddr->in_addr;
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("IP: %u.%u.%u.%u\n",digit[0],digit[1],digit[2],digit[3]));
	} else {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("AddressType %u discarded.\n", (ULONG)NetworkAddress->AddressType));
	}
}

VOID
Reg_DelAddressHandler ( 
	IN PTA_ADDRESS NetworkAddress,
	IN PUNICODE_STRING DeviceName,
	IN PTDI_PNP_CONTEXT Context
    )
/*++

Routine Description:

    TDI delete address handler

Arguments:
    
    NetworkAddress  - network address that is no longer available on the system

    Context1        - name of the device to which address belongs

    Context2        - PDO to which address belongs


Return Value:

    None

--*/
{
	UNICODE_STRING	lpxPrefix;

	PAGED_CODE ();

	UNREFERENCED_PARAMETER(Context);

	if (DeviceName==NULL) {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, (
			"AfdDelAddressHandler: "
			"NO DEVICE NAME SUPPLIED when deleting address of type %d.\n",
			NetworkAddress->AddressType));
		return;
	}
	Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DeviceName=%ws AddrType=%u AddrLen=%u\n",
		DeviceName->Buffer,
		(ULONG)NetworkAddress->AddressType,
		(ULONG)NetworkAddress->AddressLength));

	//
	//	LPX
	//
	RtlInitUnicodeString(&lpxPrefix, LPX_DEVICE_NAME_PREFIX);

	if(	RtlPrefixUnicodeString(&lpxPrefix, DeviceName, TRUE)){
		PTDI_ADDRESS_LPX	lpxAddr;

		lpxAddr = (PTDI_ADDRESS_LPX)NetworkAddress->Address;
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("LPX: %02x:%02x:%02x:%02x:%02x:%02x\n",
			lpxAddr->Node[0],
			lpxAddr->Node[1],
			lpxAddr->Node[2],
			lpxAddr->Node[3],
			lpxAddr->Node[4],
			lpxAddr->Node[5]));

		//
		//	IP	address
		//
	} else if(NetworkAddress->AddressType == TDI_ADDRESS_TYPE_IP) {
		PTDI_ADDRESS_IP	ipAddr;
		PUCHAR			digit;

		ipAddr = (PTDI_ADDRESS_IP)NetworkAddress->Address;
		digit = (PUCHAR)&ipAddr->in_addr;
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("IP: %u.%u.%u.%u\n",digit[0],digit[1],digit[2],digit[3]));
	} else {
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("AddressType %u discarded.\n", (ULONG)NetworkAddress->AddressType));
	}
}


//////////////////////////////////////////////////////////////////////////
//
//	Exported functions to IOCTL.
//

//
//	Register a device by writing registry.
//

NTSTATUS
LSBus_RegisterDevice(
		PFDO_DEVICE_DATA				FdoData,
		PBUSENUM_PLUGIN_HARDWARE_EX2	Plugin
){
	NTSTATUS			status;
	HANDLE				busDevReg;
	HANDLE				ndasDevRoot;
	HANDLE				ndasDevInst;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	busDevReg = NULL;
	ndasDevRoot = NULL;
	ndasDevInst = NULL;


	//
	//	Open a BUS device registry, an NDAS device root, and device instance key.
	//

	ExAcquireFastMutex(&FdoData->RegMutex);

	status = Reg_OpenDeviceControlRoot(&busDevReg, KEY_READ|KEY_WRITE);
	if(!NT_SUCCESS(status)) {
		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("OpenServiceRegistry() failed.\n"));
		return status;
	}

	status = Reg_OpenNdasDeviceRoot(&ndasDevRoot, KEY_READ|KEY_WRITE, busDevReg);
	if(!NT_SUCCESS(status)) {

		ZwClose(busDevReg);

		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("OpenNdasDeviceRegistry() failed.\n"));
		return status;
	}

	status = Reg_OpenDeviceInst(&ndasDevInst, Plugin->SlotNo, TRUE, ndasDevRoot);
	if(!NT_SUCCESS(status)) {

		ZwClose(busDevReg);
		ZwClose(ndasDevRoot);

		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenDeviceInst() failed.\n"));
		return	status;
	}


	//
	//	Write plug in information
	//

	status = WriteNDASDevToRegistry(ndasDevInst, Plugin);


	//
	//	Close handles
	//

	if(ndasDevInst)
		ZwClose(ndasDevInst);
	if(ndasDevRoot)
		ZwClose(ndasDevRoot);

	if(busDevReg)
		ZwClose(busDevReg);

	ExReleaseFastMutex(&FdoData->RegMutex);

	return status;
}


NTSTATUS
LSBus_RegisterTarget(
	PFDO_DEVICE_DATA			FdoData,
	PLANSCSI_ADD_TARGET_DATA	AddTargetData
){
	NTSTATUS	status;
	HANDLE		busDevReg;
	HANDLE		ndasDevRoot;
	HANDLE		ndasDevInst;
	HANDLE		targetKey;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	busDevReg = NULL;
	ndasDevRoot = NULL;
	ndasDevInst = NULL;
	targetKey = NULL;

	//
	//	Open a BUS device registry, an NDAS device root, and device instance key.
	//
	ExAcquireFastMutex(&FdoData->RegMutex);

	status = Reg_OpenDeviceControlRoot(&busDevReg, KEY_READ|KEY_WRITE);
	if(!NT_SUCCESS(status)) {

		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenDeviceControlRoot() failed.\n"));
		return status;
	}

	status = Reg_OpenNdasDeviceRoot(&ndasDevRoot, KEY_READ|KEY_WRITE, busDevReg);
	if(!NT_SUCCESS(status)) {
		ZwClose(busDevReg);

		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenNdasDeviceRoot() failed.\n"));
		return status;
	}

	status = Reg_OpenDeviceInst(&ndasDevInst, AddTargetData->ulSlotNo, FALSE, ndasDevRoot);
	if(!NT_SUCCESS(status)) {
		ZwClose(busDevReg);
		ZwClose(ndasDevRoot);

		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenDeviceInst() failed.\n"));
		return	status;
	}

	status = Reg_OpenTarget(&targetKey, AddTargetData->ucTargetId, TRUE, ndasDevInst);
	if(!NT_SUCCESS(status)) {
		ZwClose(busDevReg);
		ZwClose(ndasDevRoot);
		ZwClose(ndasDevInst);

		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenTarget() failed.\n"));

		return	status;
	}

	//
	//	Write target information
	//

	status = WriteTargetToRegistry(targetKey, AddTargetData);


	//
	//	Close handles
	//
	if(targetKey)
		ZwClose(targetKey);
	if(ndasDevInst)
		ZwClose(ndasDevInst);
	if(ndasDevRoot)
		ZwClose(ndasDevRoot);
	if(busDevReg)
		ZwClose(busDevReg);

	ExReleaseFastMutex(&FdoData->RegMutex);

	Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Adding an NDAS device into registry is completed. NTSTATUS:%08lx\n", status));

	return status;
}



NTSTATUS
LSBus_UnregisterDevice(
		PFDO_DEVICE_DATA	FdoData,
		ULONG				SlotNo
) {
	NTSTATUS			status;
	HANDLE				busDevReg;
	HANDLE				ndasDevRoot;
	HANDLE				devInstTobeDeleted;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	status = STATUS_SUCCESS;
	busDevReg = NULL;
	ndasDevRoot = NULL;
	devInstTobeDeleted = NULL;


	//
	//	Open a BUS device registry, an NDAS device root, and device instance key.
	//
	ExAcquireFastMutex(&FdoData->RegMutex);

	status = Reg_OpenDeviceControlRoot(&busDevReg, KEY_READ|KEY_WRITE);
	if(!NT_SUCCESS(status)) {
		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenDeviceControlRoot() failed.\n"));
		return status;
	}
	status = Reg_OpenNdasDeviceRoot(&ndasDevRoot, KEY_READ|KEY_WRITE, busDevReg);
	if(!NT_SUCCESS(status)) {
		ZwClose(busDevReg);

		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenNdasDeviceRoot() failed.\n"));
		return status;
	}

	if(SlotNo != NDASBUS_SLOT_ALL) {
		status = Reg_OpenDeviceInst(&devInstTobeDeleted, SlotNo, FALSE, ndasDevRoot);
		if(NT_SUCCESS(status)) {

			//
			//	Delete a NDAS device instance.
			//
			status = DrDeleteAllSubKeys(devInstTobeDeleted);
			if(NT_SUCCESS(status)) {
				status = ZwDeleteKey(devInstTobeDeleted);
			}
#if DBG
			else {
				Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrDeleteAllSubkeys() failed. SlotNo:%u NTSTATUS:%08lx\n", SlotNo, status));
			}
#endif

			ZwClose(devInstTobeDeleted);

#if DBG
			if(NT_SUCCESS(status)) {
				Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("A device(Slot %d) is deleted.\n", SlotNo));
			} else {
				Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ZwDeleteKey() failed. SlotNo:%u NTSTATUS:%08lx\n", SlotNo, status));
			}
#endif
		}
	} else {
		status = DrDeleteAllSubKeys(ndasDevRoot);
	}


	//
	//	Close handles
	//

	if(ndasDevRoot)
		ZwClose(ndasDevRoot);
	if(busDevReg)
		ZwClose(busDevReg);

	ExReleaseFastMutex(&FdoData->RegMutex);

	Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Adding a target from registry is completed. NTSTATUS:%08lx\n", status));

	return status;
}


NTSTATUS
LSBus_UnregisterTarget(
		PFDO_DEVICE_DATA	FdoData,
		ULONG				SlotNo,
		ULONG				TargetId
) {
	NTSTATUS			status;
	HANDLE				busDevReg;
	HANDLE				ndasDevRoot;
	HANDLE				ndasDevInst;
	HANDLE				targetIdTobeDeleted;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	status = STATUS_SUCCESS;
	busDevReg = NULL;
	ndasDevRoot = NULL;
	ndasDevInst = NULL;
	targetIdTobeDeleted = NULL;

	ExAcquireFastMutex(&FdoData->RegMutex);

	//
	//	Open a BUS device registry, an NDAS device root, and device instance key.
	//

	status = Reg_OpenDeviceControlRoot(&busDevReg, KEY_READ|KEY_WRITE);
	if(!NT_SUCCESS(status)) {
		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenDeviceControlRoot() failed.\n"));
		return status;
	}
	status = Reg_OpenNdasDeviceRoot(&ndasDevRoot, KEY_READ|KEY_WRITE, busDevReg);
	if(!NT_SUCCESS(status)) {
		ZwClose(busDevReg);
		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenNdasDeviceRoot() failed.\n"));
		return status;
	}

	status = Reg_OpenDeviceInst(&ndasDevInst, SlotNo, FALSE, ndasDevRoot);
	if(!NT_SUCCESS(status)) {
		ZwClose(busDevReg);
		ZwClose(ndasDevInst);
		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenDeviceInst() failed.\n"));
		return status;
	}

	status = Reg_OpenTarget(&targetIdTobeDeleted, TargetId, FALSE, ndasDevInst);
	if(NT_SUCCESS(status)) {

		//
		//	Delete an NDAS device instance.
		//
		status = DrDeleteAllSubKeys(targetIdTobeDeleted);
		if(NT_SUCCESS(status)) {
			status = ZwDeleteKey(targetIdTobeDeleted);
		}
#if DBG
		else {
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("DrDeleteAllSubKeys() failed. SlotNo:%u Target %u NTSTATUS:%08lx\n", SlotNo, TargetId, status));
		}
#endif
		ZwClose(targetIdTobeDeleted);
#if DBG
		if(NT_SUCCESS(status)) {
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("A device(Slot %d Target %u) is deleted.\n", SlotNo, TargetId));
		} else {
			Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("ZwDeleteKey() failed. SlotNo:%u Target %u NTSTATUS:%08lx\n", SlotNo, TargetId, status));
		}
#endif
	}


	//
	//	Close handles
	//
	if(ndasDevInst)
		ZwClose(ndasDevInst);
	if(ndasDevRoot)
		ZwClose(ndasDevRoot);
	if(busDevReg)
		ZwClose(busDevReg);

	ExReleaseFastMutex(&FdoData->RegMutex);

	Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Removing a target into registry is completed. NTSTATUS:%08lx\n", status));

	return status;
}


NTSTATUS
LSBus_IsRegistered(
		PFDO_DEVICE_DATA	FdoData,
		ULONG				SlotNo
){
	NTSTATUS			status;
	HANDLE				busDevReg;
	HANDLE				ndasDevRoot;
	HANDLE				devInstTobeDeleted;

	ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

	status = STATUS_SUCCESS;
	busDevReg = NULL;
	ndasDevRoot = NULL;
	devInstTobeDeleted = NULL;


	//
	//	Open a BUS device registry, an NDAS device root, and device instance key.
	//
	ExAcquireFastMutex(&FdoData->RegMutex);

	status = Reg_OpenDeviceControlRoot(&busDevReg, KEY_READ|KEY_WRITE);
	if(!NT_SUCCESS(status)) {
		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenDeviceControlRoot() failed.\n"));
		return status;
	}
	status = Reg_OpenNdasDeviceRoot(&ndasDevRoot, KEY_READ|KEY_WRITE, busDevReg);
	if(!NT_SUCCESS(status)) {
		ZwClose(busDevReg);
		ExReleaseFastMutex(&FdoData->RegMutex);

		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Reg_OpenNdasDeviceRoot() failed.\n"));
		return status;
	}

	status = Reg_OpenDeviceInst(&devInstTobeDeleted, SlotNo, FALSE, ndasDevRoot);

	//
	//	Close handles
	//

	if(ndasDevRoot)
		ZwClose(ndasDevRoot);
	if(busDevReg)
		ZwClose(busDevReg);

	ExReleaseFastMutex(&FdoData->RegMutex);

	return status;
}


//////////////////////////////////////////////////////////////////////////
//
//	Init & Destory the registrar
//
NTSTATUS
LSBus_RegInitialize(
	PFDO_DEVICE_DATA	FdoData
){
	NTSTATUS status;
	
	ExAcquireFastMutex(&Globals.Mutex);
	if(Globals.FdoDataTdiPnP == NULL) {
		Globals.FdoDataTdiPnP = FdoData;
		ExReleaseFastMutex(&Globals.Mutex);
	} else {
		ExReleaseFastMutex(&Globals.Mutex);
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Another FDO owns it.\n"));
		return STATUS_UNSUCCESSFUL;
	}

	status = Reg_InitializeTdiPnPHandlers(FdoData);

	return status;
}


VOID
LSBus_RegDestroy(
	PFDO_DEVICE_DATA	FdoData
){
	ExAcquireFastMutex(&Globals.Mutex);
	if(Globals.FdoDataTdiPnP == FdoData) {
		Globals.FdoDataTdiPnP = NULL;
		ExReleaseFastMutex(&Globals.Mutex);
	} else {
		ExReleaseFastMutex(&Globals.Mutex);
		Bus_KdPrint_Def(BUS_DBG_SS_ERROR, ("Not owner.\n"));
		return;
	}

	Reg_DeregisterTdiPnPHandlers(FdoData);

}
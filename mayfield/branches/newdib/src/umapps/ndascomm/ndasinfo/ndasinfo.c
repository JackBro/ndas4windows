/*
ndasinfo.cpp : Retrieves informations of the NDAS Device
Copyright(C) 2002-2005 XIMETA, Inc.

Gi youl Kim <kykim@ximeta.com>

This library is provided to support manufacturing processes and
for diagnostic purpose. Do not redistribute this library.
*/

#include <windows.h>
#include <tchar.h>
#include <stdio.h>
#include <crtdbg.h>
#include <ndas/ndascomm.h> /* include this header file to use NDASComm API */
#include <ndas/ndasmsg.h> /* included to process Last Error from NDASComm API */
#include "hdreg.h"

#define API_CALL(_stmt_) \
	do { \
		if (!(_stmt_)) { \
			print_last_error_message(); \
		} \
	} while(0)

#define API_CALL_JMP(jmp_loc, _stmt_) \
	do { \
		if (!(_stmt_)) { \
			print_last_error_message(); \
			goto jmp_loc; \
		} \
	} while(0)

BOOL check_ndascomm_api_version()
{
    DWORD apiver = NdasCommGetAPIVersion();
    if (NDASCOMM_API_VERSION_MAJOR != LOWORD(apiver) ||
        NDASCOMM_API_VERSION_MINOR != HIWORD(apiver))
    {
        _tprintf(
            _T("Loaded NDASCOMM.DLL API Version %d.%d does not comply with this program.")
            _T(" Expecting API Version %d.%d.\n"),
            LOWORD(apiver), HIWORD(apiver),
            NDASCOMM_API_VERSION_MAJOR, NDASCOMM_API_VERSION_MINOR);
        return FALSE;
    }
    return TRUE;
}

void 
print_last_error_message();

void 
usage()
{
	_tprintf(
		_T("usage: ndasinfo (-i ID | -m Mac)\n")
		_T("\n")
		_T("ID : 20 chars of id and 5 chars of key of the NDAS Device ex(01234ABCDE56789FGHIJ13579)\n")
		_T("Mac : Mac address of the NDAS Device ex(00:01:10:00:12:34)\n")
		);
}

LPCTSTR 
media_type_string(
	WORD MediaType)
{
	switch (MediaType)
	{
	case NDAS_UNITDEVICE_MEDIA_TYPE_UNKNOWN_DEVICE: 
		return _T("Unknown device");
	case NDAS_UNITDEVICE_MEDIA_TYPE_BLOCK_DEVICE: 
		return _T("Non-packet mass-storage device (HDD)");
	case NDAS_UNITDEVICE_MEDIA_TYPE_COMPACT_BLOCK_DEVICE:
		return _T("Non-packet compact storage device (Flash card)");
	case NDAS_UNITDEVICE_MEDIA_TYPE_CDROM_DEVICE:
		return _T("CD-ROM device (CD/DVD)");
	case NDAS_UNITDEVICE_MEDIA_TYPE_OPMEM_DEVICE:
		return _T("Optical memory device (MO)");
	default:
		return _T("Unknown Device");
	}
}

LPCTSTR
bool_string(
	BOOL Value)
{
	return Value ? _T("YES") : _T("NO");
}

void
convert_byte_to_tchar(
	const BYTE* Data,
	DWORD DataLen,
	TCHAR* CharBuffer,
	DWORD BufferCharCount)
{
	int nCharsUsed;
	_ASSERTE(BufferCharCount >= DataLen + 1);
#ifdef _UNICODE
	nCharsUsed = MultiByteToWideChar(
		CP_ACP,
		0,
		Data, DataLen,
		CharBuffer, BufferCharCount);
	_ASSERTE(nCharsUsed > 0);
#else
	CopyMemory(
		CharBuffer,
		Data,
		DataLen);
	nCharsUsed = DataLen;
#endif
	CharBuffer[nCharsUsed] = _T('\0');
}

void
print_last_error_message()
{
	DWORD dwError = GetLastError();

	HMODULE hModule = LoadLibraryEx(
		_T("ndasmsg.dll"),
		NULL,
		LOAD_LIBRARY_AS_DATAFILE);

	LPTSTR lpszErrorMessage = NULL;

	if (dwError & APPLICATION_ERROR_MASK) 
	{
		if (NULL != hModule) 
		{
			INT iChars = FormatMessage(
				FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE,
				hModule,
				dwError,
				0,
				(LPTSTR) &lpszErrorMessage,
				0,
				NULL);
			iChars;
		}
	}
	else
	{
		INT iChars = FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			NULL,
			dwError,
			0,
			(LPTSTR) &lpszErrorMessage,
			0,
			NULL);
		iChars;
	}

	if (NULL != lpszErrorMessage) 
	{
		_tprintf(_T("Error : %d(%08x) : %s\n"), dwError, dwError, lpszErrorMessage);
		LocalFree(lpszErrorMessage);
	}
	else
	{
		_tprintf(_T("Unknown error : %d(%08x)\n"), dwError, dwError);
	}

	/* refresh error */
	SetLastError(dwError);
}



BOOL 
parse_connection_info(
	NDASCOMM_CONNECTION_INFO* pci,
	int argc, 
	TCHAR** argv)
{
	int i, j;

	for(i = 0; i < argc; i++)
	{
		if(!lstrcmp(argv[i], _T("-i")) && ++i < argc)
		{
			pci->address_type = NDASCOMM_CONNECTION_INFO_TYPE_ID_A;
			if(IsBadReadPtr(argv[i], 20))
			{
				return FALSE;
			}

			for (j = 0; j < 20; ++j)
			{
				pci->DeviceIDA.szDeviceStringId[j] = (CHAR) argv[i][j];
			}

			return TRUE;
		}
		else if(!lstrcmp(argv[i], _T("-m")) && ++i < argc)
		{
			pci->address_type = NDASCOMM_CONNECTION_INFO_TYPE_ADDR_LPX;
			if(IsBadReadPtr(argv[i], 17))
			{
				return FALSE;
			}
			_tscanf(argv[i], _T("%02x:%02x:%02x:%02x:%02x:%02x"),
				&pci->AddressLPX[0],
				&pci->AddressLPX[1],
				&pci->AddressLPX[2],
				&pci->AddressLPX[3],
				&pci->AddressLPX[4],
				&pci->AddressLPX[5]);

			return TRUE;
		}
	}
	return FALSE;
}

BOOL
print_ndas_device_info(
	const NDASCOMM_CONNECTION_INFO* pci)
{
	BOOL success = FALSE;
	HNDAS hNdas = NULL;
	BYTE pbData[8];
	const DWORD cbData = sizeof(pbData);

	API_CALL_JMP(fail,
	hNdas = NdasCommConnect(pci, 0, NULL));

	/* NDAS device information */

	API_CALL_JMP(fail, 
	NdasCommGetDeviceInfo(hNdas,ndascomm_handle_info_hw_type, pbData, cbData));
	_tprintf(_T("Hardware Type : %d\n"), *(BYTE*)pbData);

	API_CALL_JMP(fail,
	NdasCommGetDeviceInfo(hNdas,ndascomm_handle_info_hw_version, pbData, cbData));
	_tprintf(_T("Hardware Version : %d\n"), *(BYTE*)pbData);

	API_CALL_JMP(fail,
	NdasCommGetDeviceInfo(hNdas,ndascomm_handle_info_hw_proto_type, pbData, cbData));
	_tprintf(_T("Hardware Protocol Type : %d\n"), *(BYTE*)pbData);

	API_CALL_JMP(fail,
	NdasCommGetDeviceInfo(hNdas,ndascomm_handle_info_hw_proto_version, pbData, cbData));
	_tprintf(_T("Hardware Protocol Version : %d\n"), *(BYTE*)pbData);

	API_CALL_JMP(fail,
	NdasCommGetDeviceInfo(hNdas,ndascomm_handle_info_hw_num_slot, pbData, cbData));
	_tprintf(_T("Number of slot : %d\n"), *(DWORD*)pbData);

	API_CALL_JMP(fail,
	NdasCommGetDeviceInfo(hNdas,ndascomm_handle_info_hw_max_blocks, pbData, cbData));
	_tprintf(_T("Maximum transfer blocks : %d\n"), *(DWORD*)pbData);

	API_CALL_JMP(fail,
	NdasCommGetDeviceInfo(hNdas,ndascomm_handle_info_hw_max_targets, pbData, cbData));
	_tprintf(_T("Maximum targets : %d\n"), *(DWORD*)pbData);

	API_CALL_JMP(fail,
	NdasCommGetDeviceInfo(hNdas,ndascomm_handle_info_hw_max_lus, pbData, cbData));
	_tprintf(_T("Maximum LUs : %d\n"), *(DWORD*)pbData);

	API_CALL_JMP(fail,
	NdasCommGetDeviceInfo(hNdas,ndascomm_handle_info_hw_header_encrypt_algo, pbData, cbData));
	_tprintf(_T("Header Encryption : %s\n"), bool_string(*(WORD*)pbData));

	API_CALL_JMP(fail,
	NdasCommGetDeviceInfo(hNdas,ndascomm_handle_info_hw_data_encrypt_algo, pbData, cbData));
	_tprintf(_T("Data Encryption : %s\n"), bool_string(*(WORD*)pbData));

	_tprintf(_T("\n"));

	success = TRUE;

fail:

	if (NULL != hNdas)
	{
		API_CALL(NdasCommDisconnect(hNdas));
	}

	return success;
}

BOOL
print_ndas_unitdevice_stat(
	const NDASCOMM_CONNECTION_INFO* pci)
{
	BOOL success = FALSE;
	int i;

	NDASCOMM_UNIT_DEVICE_STAT stat;

	/* NdasCommGetUnitDeviceStat requires DISCOVER login type */
	_ASSERTE(NDASCOMM_LOGIN_TYPE_DISCOVER == pci->login_type);

	API_CALL_JMP(fail,
	NdasCommGetUnitDeviceStat(pci, &stat, 0, NULL));

	_tprintf(_T("  Present: %s\n"), bool_string(stat.bPresent));

	if (NDAS_HOST_COUNT_UNKNOWN == stat.NRROHost)
	{
		_tprintf(_T("  NDAS hosts with RO access: N/A\n"));
	}
	else
	{
		_tprintf(_T("  NDAS hosts with RO access: %d\n"), stat.NRROHost);
	}

	if (NDAS_HOST_COUNT_UNKNOWN == stat.NRRWHost)
	{
		_tprintf(_T("  NDAS hosts with RW access: N/A\n"));
	}
	else
	{
		_tprintf(_T("  NDAS hosts with RW access: %d\n"), stat.NRRWHost);
	}

	_tprintf(_T("  Target data : "));
	for (i = 0; i < 8; i++)
	{
		int x = (int)(((PBYTE)&stat.TargetData)[i]);
		_tprintf(_T("%02X "), x);
	}
	_tprintf(_T("\n"));

	success = TRUE;

fail:

	return success;
}

BOOL
print_ndas_unitdevice_info(
	const NDASCOMM_CONNECTION_INFO* pci)
{
	BOOL success;
	int i;
	HNDAS hNdas = NULL;
	NDASCOMM_UNIT_DEVICE_INFO UnitInfo;
	NDASCOMM_IDE_REGISTER ideRegister;
	struct hd_driveid ideInfo;

	TCHAR szBuffer[100];
	const DWORD cchBuffer = sizeof(szBuffer) / sizeof(szBuffer[0]);

	/* NdasCommGetUnitDeviceStat requires NORMAL login type */
	_ASSERTE(NDASCOMM_LOGIN_TYPE_NORMAL == pci->login_type);

	API_CALL_JMP(fail,
	hNdas = NdasCommConnect(pci, 0, NULL));

	/* Unit Device Information */
	API_CALL_JMP(fail,
	NdasCommGetUnitDeviceInfo(hNdas, &UnitInfo));

	_tprintf(_T("  Sector count : %I64d\n"), UnitInfo.SectorCount);
	_tprintf(_T("  Supports LBA : %s\n"), bool_string(UnitInfo.bLBA));
	_tprintf(_T("  Supports LBA48 : %s\n"), bool_string(UnitInfo.bLBA48));
	_tprintf(_T("  Supports PIO : %s\n"), bool_string(UnitInfo.bPIO));
	_tprintf(_T("  Supports DMA : %s\n"), bool_string(UnitInfo.bDma));
	_tprintf(_T("  Supports UDMA : %s\n"), bool_string(UnitInfo.bUDma));

	convert_byte_to_tchar(
		UnitInfo.Model, sizeof(UnitInfo.Model),
		szBuffer, cchBuffer);
	_tprintf(_T("  Model : %s\n"), szBuffer);

	convert_byte_to_tchar(
		UnitInfo.FwRev, sizeof(UnitInfo.FwRev),
		szBuffer, cchBuffer);
	_tprintf(_T("  Firmware Rev : %s\n"), szBuffer);

	convert_byte_to_tchar(
		UnitInfo.SerialNo, sizeof(UnitInfo.SerialNo),
		szBuffer, cchBuffer);
	_tprintf(_T("  Serial number : %s\n"), szBuffer);

	_tprintf(_T("  Media type : %s\n"), media_type_string(UnitInfo.MediaType));
	_tprintf(_T("\n"));

	/* Additional IDE information using WIN_IDENTIFY command */

	ideRegister.use_dma = 0;
	ideRegister.use_48 = 0;
	ideRegister.device.lba_head_nr = 0;
	ideRegister.device.dev = (0 == pci->UnitNo) ? 0 : 1;
	ideRegister.device.lba = 0;
	ideRegister.command.command = WIN_IDENTIFY;

	API_CALL_JMP(fail,
	NdasCommIdeCommand(hNdas, &ideRegister, NULL, 0, (PBYTE)&ideInfo, sizeof(ideInfo)));

	_tprintf(_T("  Supports FLUSH CACHE : Supports - %s, Enabled - %s\n"), 
		bool_string(ideInfo.command_set_2 & 0x1000), 
		bool_string(ideInfo.cfs_enable_2 & 0x1000));

	_tprintf(_T("  Supports FLUSH CACHE EXT : Supports - %s, Enabled - %s\n"), 
		bool_string(ideInfo.command_set_2 & 0x2000),
		bool_string(ideInfo.cfs_enable_2 & 0x2000));

	/* Check Ultra DMA mode */
	for (i = 7; i >= 0; i--)
	{
		if (ideInfo.dma_ultra & (0x01 << i))
		{
			_tprintf(_T("  Ultra DMA mode: supports up to %d\n"), i);
			break;
		}
	}
	for (i = 7; i >= 0; i--)
	{
		if (ideInfo.dma_ultra & (0x01 << (i + 8)))
		{
			_tprintf(_T("  Current Ultra DMA mode: %d\n"), i);
			break;
		}
	}
	if (i < 0)
	{
		_tprintf(_T("  Ultra DMA mode is not selected\n"));
	}

	/* Check DMA mode */
	for (i = 2; i >= 0; i--)
	{
		if (ideInfo.dma_mword & (0x01 << i))
		{
			_tprintf(_T("  DMA mode: %d and below are supported\n"), i);
			break;
		}
	}
	for (i = 2; i >= 0; i--)
	{
		if (ideInfo.dma_mword & (0x01 << (i + 8)))
		{
			_tprintf(_T("  DMA mode %d selected\n"), i);
			break;
		}
	}
	if (i < 0)
	{
		_tprintf(_T("  DMA mode is not selected\n"));
	}
	_tprintf(_T("\n"));

	success = TRUE;

fail:

	if (hNdas)
	{
		API_CALL(NdasCommDisconnect(hNdas));
	}

	return success;
}

UINT
get_ndas_unit_device_count(
	const NDASCOMM_CONNECTION_INFO* pci)
{
	NDASCOMM_UNIT_DEVICE_STAT UnitStat;

	/* NdasCommGetUnitDeviceStat requires DISCOVER login type */
	_ASSERTE(NDASCOMM_LOGIN_TYPE_DISCOVER == pci->login_type);

	API_CALL_JMP(fail,
	NdasCommGetUnitDeviceStat(pci, &UnitStat, 0, NULL));

	return UnitStat.iNRTargets;

fail:

	return 0;
}

int 
__cdecl 
_tmain(int argc, TCHAR** argv)
{
	BOOL success;
	UINT32 i;
	UINT32 nUnitDevices;

	NDASCOMM_CONNECTION_INFO ci = {0};

    if (!check_ndascomm_api_version())
    {
        return 1;
    }
    
//	ci.address_type = NDASCOMM_CONNECTION_INFO_TYPE_ID_A; /* ASCII char set */
	ci.UnitNo = 0; /* Use first Unit Device */
	ci.bWriteAccess = FALSE; /* Connect with read-write privilege */
	ci.protocol = NDASCOMM_TRANSPORT_LPX; /* Use LPX protocol */
	ci.ui64OEMCode = 0; /* Use default password */
	ci.bSupervisor = FALSE; /* Log in as normal user */

	success = (argc > 1) && parse_connection_info(&ci, argc - 1, argv + 1);
	if (!success)
	{
		usage();
		return 1;
	}

	API_CALL(success = NdasCommInitialize());

	if (!success)
	{
		_tprintf(_T("Subsystem initialization failed\n"));
		print_last_error_message();
		return 1;
	}

	ci.login_type = NDASCOMM_LOGIN_TYPE_NORMAL; /* Normal operations */
	_tprintf(_T("NDAS Device Information\n\n"));
	success = print_ndas_device_info(&ci);

	ci.login_type = NDASCOMM_LOGIN_TYPE_DISCOVER;
	nUnitDevices = get_ndas_unit_device_count(&ci);

	if (0 == nUnitDevices)
	{
		_tprintf(_T("No Unit Device.\n"));
	}

	for (i = 0; i < nUnitDevices; i++)
	{
		_tprintf(_T("Unit Device [%d/%d]\n\n"), i + 1, nUnitDevices);

		ci.UnitNo = i;

		ci.login_type = NDASCOMM_LOGIN_TYPE_NORMAL;
		success = print_ndas_unitdevice_info(&ci);

		ci.login_type = NDASCOMM_LOGIN_TYPE_DISCOVER;
		success = print_ndas_unitdevice_stat(&ci);
	}

	API_CALL(NdasCommUninitialize());

	return GetLastError();
}

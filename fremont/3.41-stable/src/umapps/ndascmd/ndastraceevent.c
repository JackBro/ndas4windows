#include "precomp.h"
#include "ndascmd.h"

LPTSTR
ndas_device_status_to_string(
	NDAS_DEVICE_STATUS status,
	LPTSTR buf, 
	DWORD bufsize);

LPTSTR
ndas_logicaldevice_status_to_string(
	NDAS_LOGICALDEVICE_STATUS status,
	LPTSTR buf, 
	DWORD bufsize);

int pusage(LPCTSTR str);

void 
CALLBACK
ndas_event_callback(
	DWORD dwError,
	PNDAS_EVENT_INFO pEventInfo,
	LPVOID lpContext);

int ndascmd_trace_events(int argc, TCHAR** argv)
{
	static LPCTSTR USAGE = _T("");

	BOOL success;
	HANDLE hErrorEvent = NULL;
	HNDASEVENTCALLBACK hCallback = NULL;
	INPUT_RECORD inputRecord[128];
	HANDLE hStdIn = INVALID_HANDLE_VALUE;
	BOOL bTerminate = FALSE;

	UNREFERENCED_PARAMETER(argv);

	if (-1 == argc) return pusage(USAGE);

	hErrorEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (NULL == hErrorEvent) 
	{
		return print_last_error_message(), EXIT_FAILURE;
	}

	hCallback = NdasRegisterEventCallback(
		ndas_event_callback, 
		(LPVOID)hErrorEvent);

	if (NULL == hCallback)
	{
		return print_last_error_message(), EXIT_FAILURE;
	}

	hStdIn = GetStdHandle(STD_INPUT_HANDLE);

	if (INVALID_HANDLE_VALUE == hStdIn) 
	{
		success = NdasUnregisterEventCallback(hCallback);
		_ASSERTE(success);

		return print_last_error_message(), EXIT_FAILURE;
	}

	bTerminate = FALSE;
	while (!bTerminate) 
	{
		DWORD dwWaitResult = WaitForSingleObject(hErrorEvent, 500);

		if (WAIT_OBJECT_0 == dwWaitResult) 
		{
			_tprintf(_T("Event subscription error. Terminating.\n"));
			bTerminate = TRUE;
		} 
		else if (WAIT_TIMEOUT == dwWaitResult) 
		{
			DWORD dwEvents = 0;
			PeekConsoleInput(hStdIn, inputRecord, 128, &dwEvents);
			if (dwEvents > 0) 
			{
				DWORD i;
				ReadConsoleInput(hStdIn, inputRecord, 128, &dwEvents);
				for (i = 0; i < dwEvents; ++i) 
				{
#ifdef UNICODE
#define TCHAR_IN_UCHAR(x) x.UnicodeChar
#else
#define TCHAR_IN_UCHAR(x) x.AsciiChar
#endif
					if (_T('q') == TCHAR_IN_UCHAR(inputRecord[i].Event.KeyEvent.uChar)) 
					{
						bTerminate = TRUE;
					}
				}
			}
		}
		else 
		{
			print_last_error_message();
		}
	}

	success = NdasUnregisterEventCallback(hCallback);
	_ASSERTE(success);

	return EXIT_SUCCESS;
}

void CALLBACK
ndas_event_callback(
	DWORD dwError,
	PNDAS_EVENT_INFO pEventInfo,
	LPVOID lpContext)
{
	TCHAR buf1[32], buf2[32];
	BOOL success;
	HANDLE hErrorEvent = (HANDLE)(lpContext);

	if (ERROR_SUCCESS != dwError && ERROR_IO_PENDING != dwError) 
	{
		print_error_message(dwError);
		success = SetEvent(hErrorEvent);
		_ASSERTE(success);
		return;
	}

	switch (pEventInfo->EventType) 
	{
	case NDAS_EVENT_TYPE_DEVICE_STATUS_CHANGED:
		_tprintf(_T("NDAS Device (%d) status changed: %s -> %s.\n"),
			pEventInfo->EventInfo.DeviceInfo.SlotNo,
			ndas_device_status_to_string(pEventInfo->EventInfo.DeviceInfo.OldStatus, buf1, RTL_NUMBER_OF(buf1)),
			ndas_device_status_to_string(pEventInfo->EventInfo.DeviceInfo.NewStatus, buf2, RTL_NUMBER_OF(buf2)));
		break;
	case NDAS_EVENT_TYPE_LOGICALDEVICE_STATUS_CHANGED:
		_tprintf(_T("NDAS Logical Device (%d) status changed: %s -> %s.\n"),
			pEventInfo->EventInfo.LogicalDeviceInfo.LogicalDeviceId,
			ndas_logicaldevice_status_to_string(pEventInfo->EventInfo.LogicalDeviceInfo.OldStatus, buf1, RTL_NUMBER_OF(buf1)),
			ndas_logicaldevice_status_to_string(pEventInfo->EventInfo.LogicalDeviceInfo.NewStatus, buf2, RTL_NUMBER_OF(buf2)));
		break;
	case NDAS_EVENT_TYPE_LOGICALDEVICE_DISCONNECTED:
		_tprintf(_T("NDAS Logical Device (%d) is disconnected.\n"),
			pEventInfo->EventInfo.LogicalDeviceInfo.LogicalDeviceId);
		break;
	case NDAS_EVENT_TYPE_LOGICALDEVICE_ALARMED:
		_tprintf(_T("NDAS Logical Device (%d) is alarmed(%08lx).\n"),
			pEventInfo->EventInfo.LogicalDeviceInfo.LogicalDeviceId,
			pEventInfo->EventInfo.LogicalDeviceInfo.AdapterStatus);
		break;
	case NDAS_EVENT_TYPE_LOGICALDEVICE_RELATION_CHANGED:
		_tprintf(_T("NDAS Logical Device (%d) Relation Changed.\n"),
			pEventInfo->EventInfo.LogicalDeviceInfo.LogicalDeviceId);
		break;
	case NDAS_EVENT_TYPE_LOGICALDEVICE_PROPERTY_CHANGED:
		_tprintf(_T("NDAS Logical Device (%d) Property Changed.\n"),
			pEventInfo->EventInfo.LogicalDeviceInfo.LogicalDeviceId);
		break;
	case NDAS_EVENT_TYPE_DEVICE_ENTRY_CHANGED:
		_tprintf(_T("NDAS Device Entry Changed.\n"));
		break;
	case NDAS_EVENT_TYPE_LOGICALDEVICE_ENTRY_CHANGED:
		_tprintf(_T("NDAS Logical Device Entry Changed.\n"));
		break;
	case NDAS_EVENT_TYPE_TERMINATING:
		_tprintf(_T("Termination.\n"));
		SetEvent(hErrorEvent);
		break;
	case NDAS_EVENT_TYPE_CONNECTED:
		_tprintf(_T("Connected.\n"));
		break;
	case NDAS_EVENT_TYPE_RETRYING_CONNECTION:
		_tprintf(_T("Reconnecting.\n"));
		break;
	case NDAS_EVENT_TYPE_CONNECTION_FAILED:
		_tprintf(_T("Connection Failure.\n"));
		break;
	default:
		_tprintf(_T("Unknown Event: 0x%04X.\n"), pEventInfo->EventType);
	}

	_flushall();

	return;
}

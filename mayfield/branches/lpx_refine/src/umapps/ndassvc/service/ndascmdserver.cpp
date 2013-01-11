/*++

Copyright (C)2002-2004 XIMETA, Inc.
All rights reserved.

--*/

#include "stdafx.h"
#include "task.h"
#include <process.h>

#include "ndas/ndascmd.h"
#include "ndascmdserver.h"
#include "ndascmdproc.h"

#include "autores.h"
#include "transport.h"

#include "ndascfg.h"

#include "xdbgflags.h"
#define XDBG_MODULE_FLAG XDF_CMDSERVER
#include "xdebug.h"

CNdasCommandServer::
CNdasCommandServer() :
	CTask(_T("NdasCommandServer Task")),
	m_dwMaxPipeInstances(NdasServiceConfig::Get(nscCommandPipeInstancesMax)),
	m_dwPipeTimeout(NdasServiceConfig::Get(nscCommandPipeTimeout))
{
	NdasServiceConfig::Get(nscCommandPipeName, m_szPipeName, MAX_PATH);
}

CNdasCommandServer::
~CNdasCommandServer()
{
}

BOOL
CNdasCommandServer::
Initialize()
{
	return CTask::Initialize();
}

DWORD 
CNdasCommandServer::
OnTaskStart()
{
	HANDLE hTerminateThreadEvent = m_hTaskTerminateEvent;

	HANDLE* hObjects;
	DWORD* dwThreadIds;
	HANDLE* hThreads;

	hObjects = new HANDLE[m_dwMaxPipeInstances + 1];
	dwThreadIds = new DWORD[m_dwMaxPipeInstances];
	hThreads = new HANDLE[m_dwMaxPipeInstances];

	hObjects[0] = hTerminateThreadEvent;

	DWORD dwCreatedThreadCount = 0;
	for (DWORD i = 0; i < m_dwMaxPipeInstances; ++i) {
#ifdef USE_WINAPI_THREAD
		hThreads[i] = ::CreateThread(
			NULL, 
			0, 
			CNdasCommandServer::PipeInstanceThreadProc, 
			this,
			0,
			&dwThreadIds[i]);
#else
		hThreads[i] = (HANDLE) _beginthreadex(
			NULL,
			0,
			CNdasCommandServer::PipeInstanceThreadProc,
			this,
			0,
			(unsigned int*)&dwThreadIds[i]);
#endif
		if (NULL == hThreads[i]) {
			// CreateThread failed
			DWORD dwError = ::GetLastError();
			// Issue a warning here

			// We are to use only created threads
			break;
		}
		hObjects[i+1] = hThreads[i];
		++dwCreatedThreadCount;
	}

	DWORD dwWaitResult = ::WaitForMultipleObjects(
		dwCreatedThreadCount + 1,
		hObjects,
		TRUE,
		INFINITE);

	for (DWORD i = 0; i < dwCreatedThreadCount; ++i) {
		(VOID) ::CloseHandle(hThreads[i]);
	}

	delete [] hObjects;
	delete [] dwThreadIds;
	delete [] hThreads;

	return 0;
}

#if DONT_USE_CRT_THREAD
DWORD WINAPI
CNdasCommandServer::
#else
unsigned int __stdcall
CNdasCommandServer::
#endif
PipeInstanceThreadProc(LPVOID lpParam)
{
	BOOL fSuccess = FALSE;
	BOOL bTerminateThread = FALSE;

	CNdasCommandServer* pCmdServer = 
		reinterpret_cast<CNdasCommandServer*>(lpParam);

	HANDLE hTerminateThreadEvent = pCmdServer->m_hTaskTerminateEvent;

	// Named Pipe Connection Event
	AutoObjectHandle hConnectEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	if (NULL == (HANDLE) hConnectEvent) {
		// Log Error Here
		DBGPRT_ERR_EX(_FT("Creating ndascmd connection event failed: "));
		ExitThread(1);
		return 1;
	}

	HANDLE hWaitEvents[2] = { hTerminateThreadEvent, hConnectEvent };

	//
	// Create a named pipe instance
	//
	AutoFileHandle hPipe = ::CreateNamedPipe(
		pCmdServer->m_szPipeName,
		PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED, // | FILE_FLAG_FIRST_PIPE_INSTANCE,
		PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
		pCmdServer->m_dwMaxPipeInstances, 
		0,
		0,
		pCmdServer->m_dwPipeTimeout,
		NULL);

	// AutoFileHandle autoPipeHandle(hPipe);

	if (INVALID_HANDLE_VALUE == (HANDLE) hPipe) {
		// Log Error Here
		DWORD dwError = ::GetLastError();
		DBGPRT_ERR_EX(_FT("Creating pipe server failed: "));
		ExitThread(2);
		return 2;
	}

	while (FALSE == bTerminateThread) {

		//
		// Initialize overlapped structure
		//
		OVERLAPPED overlapped = {0};
		overlapped.hEvent = hConnectEvent;

		CNamedPipeTransport npt(hPipe);
		CNamedPipeTransport* pTransport = &npt;

		fSuccess = pTransport->Accept(&overlapped);
		if (!fSuccess) { 
			DBGPRT_ERR_EX(_FT("Transport Accept (Named Pipe) failed: "));
			break;
		}

		DWORD dwWaitResult = ::WaitForMultipleObjects(
			2,
			hWaitEvents,
			FALSE,
			INFINITE);

		switch (dwWaitResult) {

		case WAIT_OBJECT_0: // Terminate Thread Event
			bTerminateThread = TRUE;
			break;

		case WAIT_OBJECT_0 + 1: // Connect Event

			//
			// Process the request
			// (Safely ignore error)
			//

			{
				CNdasCommandProcessor processor(pTransport);
				processor.Process();
			}
			
			//
			// After processing the request, reset the pipe instance
			//
			::FlushFileBuffers(hPipe);
			fSuccess = ::DisconnectNamedPipe(hPipe);
			if (!fSuccess) {
				// issue an warning
				DWORD dwError = ::GetLastError();
				bTerminateThread = TRUE;
			}

			break;
		default:
			DBGPRT_ERR_EX(_FT("Wait failed. "));
		}

	}

#ifdef USE_WINAPI_THREAD
	ExitThread(0);
#else
	_endthreadex(0);
#endif

	return 0;
}


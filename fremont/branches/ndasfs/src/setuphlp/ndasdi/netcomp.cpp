#include "stdafx.h"
#include "netcomp.h"
#include <xtl/xtlautores.h>

#include "trace.h"
#ifdef RUN_WPP
#include "netcomp.tmh"
#endif

#define RFP_NO_PATH_PREFIX 0x0001

static LPTSTR ResolveFullPath(
	IN LPCTSTR szPath, 
	OUT LPDWORD pcchFullPath = NULL,
	OUT LPTSTR* ppszFilePart = NULL,
	IN DWORD Flags = 0);

//
// Warning this function may NOT be safe for Multithreaded apps
//
NDASDI_API
LPCTSTR 
pNetCfgHrString(HRESULT hr)
{
	static TCHAR buffer[256] = {0};

	switch (hr) {
	case NETCFG_E_ALREADY_INITIALIZED: 
		return _T("NETCFG_E_ALREADY_INITIALIZED");
	case NETCFG_E_NOT_INITIALIZED: 
		return _T("NETCFG_E_NOT_INITIALIZED");
	case NETCFG_E_IN_USE: 
		return _T("NETCFG_E_IN_USE"); 
	case NETCFG_E_NO_WRITE_LOCK: 
		return _T("NETCFG_E_NO_WRITE_LOCK"); 
	case NETCFG_E_NEED_REBOOT: 
		return _T("NETCFG_E_NEED_REBOOT"); 
	case NETCFG_E_ACTIVE_RAS_CONNECTIONS: 
		return _T("NETCFG_E_ACTIVE_RAS_CONNECTIONS"); 
	case NETCFG_E_ADAPTER_NOT_FOUND: 
		return _T("NETCFG_E_ADAPTER_NOT_FOUND"); 
	case NETCFG_E_COMPONENT_REMOVED_PENDING_REBOOT: 
		return _T("NETCFG_E_COMPONENT_REMOVED_PENDING_REBOOT"); 

	case NETCFG_S_REBOOT: 
		return _T("NETCFG_S_REBOOT"); 
	case NETCFG_S_DISABLE_QUERY: 
		return _T("NETCFG_S_DISABLE_QUERY"); 
	case NETCFG_S_STILL_REFERENCED: 
		return _T("NETCFG_S_STILL_REFERENCED"); 
	case NETCFG_S_CAUSED_SETUP_CHANGE: 
		return _T("NETCFG_S_CAUSED_SETUP_CHANGE");
	default:
		{
			HRESULT hr2 = StringCchPrintf(
				buffer,
				256,
				_T("NON_NETCFG_HR_0x%08X"), 
				hr);
			_ASSERTE(SUCCEEDED(hr2));
			return buffer;
		}
	}
}

//----------------------------------------------------------------------------
// Globals
//
static const GUID* c_aguidClass[] =
{
	&GUID_DEVCLASS_NET,
		&GUID_DEVCLASS_NETTRANS,
		&GUID_DEVCLASS_NETSERVICE,
		&GUID_DEVCLASS_NETCLIENT
};

HRESULT 
HrInstallNetComponent(
	IN INetCfg* pnc,
	IN PCWSTR szComponentId,
	IN const GUID* pguidClass);

inline ULONG ReleaseObj(IUnknown* punk)
{
	return (punk) ? punk->Release () : 0;
}

//+---------------------------------------------------------------------------
//
// Function:  HrGetINetCfg
//
// Purpose:   Initialize COM, create and initialize INetCfg.
//            Obtain write lock if indicated.
//
// Arguments:
//    fGetWriteLock [in]  whether to get write lock
//    ppnc          [in]  pointer to pointer to INetCfg object
//
// Returns:   S_OK on success, otherwise an error code
//
// Notes:
//
HRESULT HrGetINetCfg(IN BOOL fGetWriteLock,
					 INetCfg** ppnc)
{
	HRESULT hr=S_OK;

	// Initialize the output parameters.
	*ppnc = NULL;

	// initialize COM
	hr = CoInitializeEx(NULL,
		COINIT_DISABLE_OLE1DDE | COINIT_APARTMENTTHREADED );

	if (SUCCEEDED(hr))
	{
		// Create the object implementing INetCfg.
		//
		INetCfg* pnc;
		hr = CoCreateInstance(CLSID_CNetCfg, NULL, CLSCTX_INPROC_SERVER,
			IID_INetCfg, (void**)&pnc);
		if (SUCCEEDED(hr))
		{
			INetCfgLock * pncLock = NULL;
			if (fGetWriteLock)
			{
				// Get the locking interface
				hr = pnc->QueryInterface(IID_INetCfgLock,
					(LPVOID *)&pncLock);
				if (SUCCEEDED(hr))
				{
					// Attempt to lock the INetCfg for read/write
					static const ULONG c_cmsTimeout = 15000;
					static const WCHAR c_szSampleNetcfgApp[] =
						L"NDAS Network Component Installer";
					PWSTR szLockedBy;

					hr = pncLock->AcquireWriteLock(c_cmsTimeout,
						c_szSampleNetcfgApp,
						&szLockedBy);
					if (S_FALSE == hr)
					{
						hr = NETCFG_E_NO_WRITE_LOCK;
						_tprintf(L"Could not lock INetcfg, it is already locked by '%s'", szLockedBy);
					}
				}
			}

			if (SUCCEEDED(hr))
			{
				// Initialize the INetCfg object.
				//
				hr = pnc->Initialize(NULL);
				if (SUCCEEDED(hr))
				{
					*ppnc = pnc;
					pnc->AddRef();
				}
				else
				{
					// initialize failed, if obtained lock, release it
					if (pncLock)
					{
						pncLock->ReleaseWriteLock();
					}
				}
			}
			ReleaseObj(pncLock);
			ReleaseObj(pnc);
		}

		if (FAILED(hr))
		{
			CoUninitialize();
		}
	}

	return hr;
}

//+---------------------------------------------------------------------------
//
// Function:  HrReleaseINetCfg
//
// Purpose:   Uninitialize INetCfg, release write lock (if present)
//            and uninitialize COM.
//
// Arguments:
//    fHasWriteLock [in]  whether write lock needs to be released.
//    pnc           [in]  pointer to INetCfg object
//
// Returns:   S_OK on success, otherwise an error code
//
// Notes:
//
HRESULT HrReleaseINetCfg(BOOL fHasWriteLock, INetCfg* pnc)
{
	HRESULT hr = S_OK;

	// uninitialize INetCfg
	hr = pnc->Uninitialize();

	// if write lock is present, unlock it
	if (SUCCEEDED(hr) && fHasWriteLock)
	{
		INetCfgLock* pncLock;

		// Get the locking interface
		hr = pnc->QueryInterface(IID_INetCfgLock,
			(LPVOID *)&pncLock);
		if (SUCCEEDED(hr))
		{
			hr = pncLock->ReleaseWriteLock();
			ReleaseObj(pncLock);
		}
	}

	ReleaseObj(pnc);

	CoUninitialize();

	return hr;
}

//+---------------------------------------------------------------------------
//
// Function:  HrIsNetComponentInstalled
//
// Purpose:   Find out if a component is installed
//
// Arguments:
//    szComponentId [in]  id of component to search
//
// Returns:   S_OK    if installed,
//            S_FALSE if not installed,
//            otherwise an error code
//

NDASDI_API HRESULT
HrIsNetComponentInstalled(IN PCWSTR szComponentId)
{
	HRESULT hr=S_OK;
	INetCfg* pnc;
	INetCfgComponent* pncc;

	hr = HrGetINetCfg(FALSE, &pnc);
	if (S_OK != hr) {
		return hr;
	}

	hr = pnc->FindComponent(szComponentId, &pncc);
	if (S_OK == hr)
	{
		ReleaseObj(pncc);
	}

	(void) HrReleaseINetCfg(FALSE, pnc);

	return hr;
}

//+---------------------------------------------------------------------------
//
// Function:  HrInstallNetComponent
//
// Purpose:   Install the specified net component
//
// Arguments:
//    szComponentId [in]  component to install
//    nc            [in]  class of the component
//    szInfFullPath [in]  full path to primary INF file
//                        required if the primary INF and other
//                        associated files are not pre-copied to
//                        the right destination dirs.
//                        Not required when installing MS components
//                        since the files are pre-copied by
//                        Windows NT Setup.
//
// Returns:   S_OK or NETCFG_S_REBOOT on success, otherwise an error code
//
// Notes:
//

NDASDI_API HRESULT 
HrInstallNetComponent(
	IN PCWSTR szComponentId,
	IN NetClass nc,
	IN PCWSTR szInfPath OPTIONAL,
	OUT PWSTR szCopiedInfPath OPTIONAL,
	IN DWORD cchCopiedInfPath OPTIONAL,
	IN LPDWORD pcchUsed OPTIONAL,
	OUT PWSTR* ppCopiedInfFilePart OPTIONAL)
{
	BOOL fSuccess = FALSE;
	HRESULT hr=S_OK;
	INetCfg* pnc;

	XTLTRACE1(TRACE_LEVEL_INFORMATION, _T("Installing network component %s from %s...\n"), 
		szComponentId,
		szInfPath);

	// cannot install net adapters this way. they have to be
	// enumerated/detected and installed by PnP

	if ((nc != NC_NetProtocol) &&
		(nc != NC_NetService) &&
		(nc != NC_NetClient))
	{
		XTLTRACE1(TRACE_LEVEL_ERROR, 
			_T("Invalid class type specified.\n"));
		hr = HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
		return hr;
	}

	LPTSTR szFullInfPath = ResolveFullPath(szInfPath);
	if (NULL == szFullInfPath) {
		XTLTRACE1(TRACE_LEVEL_ERROR, 
			_T("Resolve Full INF File Path failed, error=0x%X\n"), GetLastError());
		hr = HRESULT_FROM_WIN32(GetLastError());
		return hr;
	}

	//
	// Auto Release szFullInfPath
	//
	XTL::AutoLocalHandle hLocal = szFullInfPath;

	XTLTRACE1(TRACE_LEVEL_INFORMATION, _T("INF File Path: %s\n"), szFullInfPath);

	// if full path to INF has been specified, the INF
	// needs to be copied using Setup API to ensure that any other files
	// that the primary INF copies will be correctly found by Setup API
	//
	if (szFullInfPath && wcslen(szFullInfPath)) {
		fSuccess = SetupCopyOEMInf(
			szFullInfPath,
			NULL,               // other files are in the same dir. as primary INF
			SPOST_PATH,         // first param. contains path to INF
			0,                  // default copy style
			szCopiedInfPath,    // receives the name of the INF
				                // after it is copied to %windir%\inf
			cchCopiedInfPath,   // max buf. size for the above
			pcchUsed,           // receives required size if non-null
			ppCopiedInfFilePart); // optionally retrieves filename
									// component of szInfNameAfterCopy
		if (!fSuccess) {
			DWORD dwError = GetLastError();
			XTLTRACE1(TRACE_LEVEL_ERROR, 
				_T("SetupCopyOEMInf failed, error=0x%X\n"), GetLastError());
			hr = HRESULT_FROM_WIN32(dwError);
			return hr;
		}

		XTLTRACE1(TRACE_LEVEL_INFORMATION, _T("INF File is copied to %s\n"), szCopiedInfPath);

	}

	// get INetCfg interface
	hr = HrGetINetCfg(TRUE, &pnc);

	if (FAILED(hr)) {
		XTLTRACE1(TRACE_LEVEL_ERROR, 
			_T("Getting INetCfg failed, hr=0x%X\n"), hr);
		return hr;
	}

	// install szComponentId
	hr = HrInstallNetComponent(pnc, szComponentId,
		c_aguidClass[nc]);

	if (FAILED(hr)) 
	{
		XTLTRACE1(TRACE_LEVEL_ERROR, 
			_T("InstallNetComponent failed, hr=0x%X\n"), hr);

	} else {

		// Apply the changes
		hr = pnc->Apply();

		if (FAILED(hr)) {
			XTLTRACE1(TRACE_LEVEL_ERROR, _T("Applying changes failed, hr=0x%X\n"), hr);
		} else {
			XTLTRACE1(TRACE_LEVEL_ERROR, _T("Change applied successfully, hr=0x%X\n"), hr);
		}
	}

	// release INetCfg
	(void) HrReleaseINetCfg(TRUE, pnc);

	return hr;
}

//+---------------------------------------------------------------------------
//
// Function:  HrInstallNetComponent
//
// Purpose:   Install the specified net component
//
// Arguments:
//    pnc           [in]  pointer to INetCfg object
//    szComponentId [in]  component to install
//    pguidClass    [in]  class guid of the component
//
// Returns:   S_OK or NETCFG_S_REBOOT on success, otherwise an error code
//
// Notes:
//
HRESULT 
HrInstallNetComponent(
	IN INetCfg* pnc,
	IN PCWSTR szComponentId,
	IN const GUID* pguidClass)
{
	HRESULT hr=S_OK;
	OBO_TOKEN OboToken;
	INetCfgClassSetup* pncClassSetup;
	INetCfgComponent* pncc;

	XTLTRACE1(TRACE_LEVEL_INFORMATION, 
		_T("Installing network component %s...\n"), szComponentId);

	// OBO_TOKEN specifies the entity on whose behalf this
	// component is being installed

	// set it to OBO_USER so that szComponentId will be installed
	// On-Behalf-Of "user"
	ZeroMemory (&OboToken, sizeof(OboToken));
	OboToken.Type = OBO_USER;

	hr = pnc->QueryNetCfgClass (pguidClass, IID_INetCfgClassSetup,
		(void**)&pncClassSetup);

	if (FAILED(hr)) {
		XTLTRACE1(TRACE_LEVEL_ERROR, 
			_T("Querying NetCfgClass failed, hr=0x%X\n"), hr);
		return hr;
	}

	hr = pncClassSetup->Install(szComponentId,
		&OboToken,
		NSF_POSTSYSINSTALL,
		0,       // <upgrade-from-build-num>
		NULL,    // answerfile name
		NULL,    // answerfile section name
		&pncc);

	if (S_OK == hr)
	{
		XTLTRACE1(TRACE_LEVEL_INFORMATION, 
			_T("Installation %s completed successfully.\n"), szComponentId);
		// we don't want to use pncc (INetCfgComponent), release it
		ReleaseObj(pncc);
	}
	else
	{
		XTLTRACE1(TRACE_LEVEL_ERROR,
			_T("Installing %s failed, hr=0x%X\n"), szComponentId, hr);
	}

	ReleaseObj(pncClassSetup);

	return hr;
}

//+---------------------------------------------------------------------------
//
// Function:  HrUninstallNetComponent
//
// Purpose:   Uninstall the specified component.
//
// Arguments:
//    pnc           [in]  pointer to INetCfg object
//    szComponentId [in]  component to uninstall
//
// Returns:   S_OK or NETCFG_S_REBOOT on success, otherwise an error code
//
// Notes:
//
HRESULT 
HrUninstallNetComponent(
	IN INetCfg* pnc, 
	IN PCWSTR szComponentId)
{
	HRESULT hr=S_OK;
	OBO_TOKEN OboToken;
	INetCfgComponent* pncc;
	GUID guidClass;
	INetCfgClass* pncClass;
	INetCfgClassSetup* pncClassSetup;

	XTLTRACE1(TRACE_LEVEL_INFORMATION, 
		_T("Uninstalling Net Component %s...\n"), szComponentId);

	// OBO_TOKEN specifies the entity on whose behalf this
	// component is being uninstalld

	// set it to OBO_USER so that szComponentId will be uninstalld
	// On-Behalf-Of "user"
	ZeroMemory (&OboToken, sizeof(OboToken));
	OboToken.Type = OBO_USER;

	// see if the component is really installed
	hr = pnc->FindComponent(szComponentId, &pncc);

	if (S_OK != hr) 
	{
		return hr;
	}

	// yes, it is installed. obtain INetCfgClassSetup and DeInstall
	hr = pncc->GetClassGuid(&guidClass);

	if (S_OK == hr)
	{
		hr = pnc->QueryNetCfgClass(&guidClass, IID_INetCfgClass,
			(void**)&pncClass);
		if (SUCCEEDED(hr))
		{
			hr = pncClass->QueryInterface(IID_INetCfgClassSetup,
				(void**)&pncClassSetup);
			if (SUCCEEDED(hr))
			{
				hr = pncClassSetup->DeInstall (pncc, &OboToken, NULL);

				ReleaseObj (pncClassSetup);
			}
			ReleaseObj(pncClass);
		}
	}
	ReleaseObj(pncc);

	return hr;
}

//+---------------------------------------------------------------------------
//
// Function:  HrUninstallNetComponent
//
// Purpose:   Initialize INetCfg and uninstall a component
//
// Arguments:
//    szComponentId [in]  InfId of component to uninstall (e.g. MS_TCPIP)
//
// Returns:   S_OK or NETCFG_S_REBOOT on success, otherwise an error code
//
// Notes:
//
NDASDI_API HRESULT 
HrUninstallNetComponent(
	IN PCWSTR szComponentId)
{
	HRESULT hr=S_OK;
	INetCfg* pnc;

	XTLTRACE1(TRACE_LEVEL_INFORMATION, 
		_T("Uninstalling Net Component %s...\n"), szComponentId);

	// get INetCfg interface
	hr = HrGetINetCfg(TRUE, &pnc);

	if (FAILED(hr)) 
	{
		XTLTRACE1(TRACE_LEVEL_ERROR, 
			_T("Getting INetCfg failed with hr 0x%08x)\n"), hr);
		return hr;
	}

	// uninstall szComponentId
	hr = HrUninstallNetComponent(pnc, szComponentId);

	if (S_OK == hr)
	{
		// Apply the changes
		hr = pnc->Apply();
		if (FAILED(hr)) 
		{
			XTLTRACE1(TRACE_LEVEL_ERROR, 
				_T("Applying changes failed (hr = 0x%08X).\n"), hr);
		}
		else if (S_OK != hr) 
		{
			XTLTRACE1(TRACE_LEVEL_INFORMATION, 
				_T("Net Component %ws is uninstalled successfully.\n"), szComponentId);

			XTLTRACE1(TRACE_LEVEL_WARNING, 
				_T("Applying changes may require reboot (hr = 0x%08X)\n"), hr);
		} 
		else 
		{
			XTLTRACE1(TRACE_LEVEL_INFORMATION,
				_T("Net Component %ws is uninstalled successfully.\n"), szComponentId);
		}
	}
	else 
	{
		XTLTRACE1(TRACE_LEVEL_ERROR,
			_T("Uninstalling Net Component %s failed with hr 0x%08X.\n"), 
			szComponentId, hr);
	}

	// release INetCfg
	(void) HrReleaseINetCfg(TRUE, pnc);

	return hr;
}

//
// Returns the full path of the szPath
//
// If the UNICODE is defined, 
// path prefix "\\?\" is appended.
//
// Returns NULL is any error occurs, call GetLastError
// for extended information.
//
// Required to free the returned string with LocalFree 
// if not null
//

static LPTSTR 
ResolveFullPath(
	IN LPCTSTR szPath, 
	OUT LPDWORD pcchFullPath, 
	OUT LPTSTR* ppszFilePart,
	IN DWORD Flags)
{

#ifdef UNICODE
	static const TCHAR PATH_PREFIX[] = L"\\\\?\\";
	static const DWORD PATH_PREFIX_LEN = RTL_NUMBER_OF(PATH_PREFIX);
#else
	static const TCHAR PATH_PREFIX[] = "";
	static const DWORD PATH_PREFIX_LEN = RTL_NUMBER_OF(PATH_PREFIX);
#endif

	//
	// szPathBuffer = \\?\
	// lpszPathBuffer ptr ^
	//
	LPTSTR lpszLongPathBuffer = NULL;
	BOOL fSuccess = FALSE;

	// Inf must be a full pathname
	DWORD cch = GetFullPathName(
		szPath,
		0,
		NULL,
		NULL);

	if (0 == cch) {
		return NULL;
	}

	// cch contains required buffer size
	lpszLongPathBuffer = (LPTSTR) LocalAlloc(
		LPTR,
		(PATH_PREFIX_LEN - 1 + cch) * sizeof(TCHAR));

	if (NULL == lpszLongPathBuffer) {
		// out of memory
		return NULL;
	}


	// lpsz is a path without path prefix
	LPTSTR lpsz = lpszLongPathBuffer + (PATH_PREFIX_LEN - 1);

	cch = GetFullPathName(
		szPath,
		cch,
		lpsz,
		ppszFilePart);

	if (0 == cch) {
		LocalFree(lpszLongPathBuffer);
		return NULL;
	}

	if (NULL != pcchFullPath) {
		*pcchFullPath = cch;
	}

	if (!(Flags & RFP_NO_PATH_PREFIX)) {
		if (_T('\\') != lpsz[0] || _T('\\') != lpsz[1]) {
			// UNC path does not support path prefix "\\?\" 
			// Also path with \\?\ does not need path prefix
			::CopyMemory(
				lpszLongPathBuffer, 
				PATH_PREFIX, 
				(PATH_PREFIX_LEN - 1) * sizeof(TCHAR));
			lpsz = lpszLongPathBuffer;
			if (NULL != pcchFullPath) {
				*pcchFullPath += PATH_PREFIX_LEN;
			}
		}
	}

	return lpsz;
}

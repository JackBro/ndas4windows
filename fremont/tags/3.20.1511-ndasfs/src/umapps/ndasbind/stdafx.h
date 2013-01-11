// stdafx.h : include file for standard system include files,
//  or project specific include files that are used frequently, but
//      are changed infrequently
//

#pragma once

// Change these values to use different versions
#ifndef WINVER		
#define WINVER		0x0400
#endif 
//#define _WIN32_WINNT	0x0400
#ifndef WINVER		
#define _WIN32_IE	0x0400
#endif 
#define _RICHEDIT_VER	0x0100
// #define _WTL_USE_CSTRING
#define _ATL_USE_CSTRING_FLOAT
#define _WTL_USE_CSTRING

#include <atlbase.h>
#include <atlapp.h>

extern CAppModule _Module;

#include <atlwin.h>

// The one caveat here, is that if you are using WTL's CString, 
// you must include AtlMisc.h before AtlDDx.h.
#include <atlmisc.h>

#include <atlframe.h>
#include <atlctrls.h>
#include <shellapi.h>
#include <atlctrlx.h>
#include <atlctrlw.h>
#include <atlsplit.h>
#include <atldlgs.h>
#include <atlddx.h>
#include <atlcrack.h>

#include <ndas/ndasmsg.h>

#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>
#include <crtdbg.h>
#include <map>


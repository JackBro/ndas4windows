////////////////////////////////////////////////////////////////////////////
//
// Interface of CNBSelectDeviceDlg class 
//
// @author Gi Youl Kim(kykim@ximeta.com)
//
////////////////////////////////////////////////////////////////////////////

#ifndef _NBDEVICELISTDLG_H_
#define _NBDEVICELISTDLG_H_

#include <ndas/nbdev.h>

#include "nblistviewctrl.h"

typedef BOOL (CALLBACK* NBDEVICELISTCALLBACK)(CNBLogicalDev *pUnitDevice, HWND hWnd, LPVOID lpContext);

class CNBSelectDeviceDlg :
	public CDialogImpl<CNBSelectDeviceDlg>,
	public CWinDataExchange<CNBSelectDeviceDlg>
{
protected:
	NBLogicalDevPtrList m_listDevices;
	NBLogicalDevPtrList m_listDevicesSelected;
	NBDEVICELISTCALLBACK m_fnCallBack;
	LPVOID			m_lpContext;
	int				m_nSelectCount;

	CNBListViewCtrl m_wndListSingle;
	int m_nCaptionID, m_nMessageID;

	BOOL IsOK();

public:
	int IDD;
	
	CNBSelectDeviceDlg(int nDialogID, int nCaptionID, int nMessageID, 
		NBLogicalDevPtrList &listDevices, int nSelectCount,
		NBDEVICELISTCALLBACK fnCallBack = NULL, LPVOID lpContext = NULL)
	{
		IDD = nDialogID;
		m_nCaptionID = nCaptionID;
		m_nMessageID = nMessageID;
		m_listDevices = listDevices;
		m_fnCallBack = fnCallBack;
		m_lpContext = lpContext;
		m_nSelectCount = nSelectCount;
	}

	BEGIN_DDX_MAP(CNBSelectDeviceDlg)
	END_DDX_MAP()

	BEGIN_MSG_MAP_EX(CNBSelectDeviceDlg)
		MSG_WM_INITDIALOG(OnInitDialog)
		COMMAND_ID_HANDLER_EX(IDOK, OnOK)
		COMMAND_ID_HANDLER_EX(IDCANCEL, OnCancel)
//		NOTIFY_CODE_HANDLER_EX(LVN_ITEMCHANGED, OnListSelChanged)
		if (uMsg == WM_NOTIFY && LVN_ITEMCHANGED == ((LPNMHDR)lParam)->code)
		{
			SetMsgHandled(TRUE);
			lResult = OnListSelChanged((LPNMHDR)lParam);
			if(IsMsgHandled())
				return TRUE;
		}
		REFLECT_NOTIFICATIONS()
	END_MSG_MAP()

	LRESULT OnInitDialog(HWND hWndCtl, LPARAM lParam);
	void OnOK(UINT wNotifyCode, int wID, HWND hwndCtl);
	void OnCancel(UINT wNotifyCode, int wID, HWND hwndCtl);
	LRESULT OnListSelChanged(LPNMHDR /*lpNMHDR*/);

	NBLogicalDevPtrList GetSelectedDevicesList() { return m_listDevicesSelected; }
	CNBLogicalDev *GetSelectedDevice() { return m_listDevicesSelected.front(); }
};



#endif // _NBSELDISKDLG_H_
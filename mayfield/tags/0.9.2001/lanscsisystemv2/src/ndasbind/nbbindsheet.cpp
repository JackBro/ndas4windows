////////////////////////////////////////////////////////////////////////////
//
// Implementation of CBindSheet class 
//
// @author Ji Young Park(jypark@ximeta.com)
//
////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "nbbindsheet.h"

CBindSheet::CBindSheet(void)
{
	SetWizardMode();
	m_psh.dwFlags |= PSH_NOAPPLYNOW;

	AddPage( m_page1 );
	AddPage( m_page2 );
	m_page1.SetParentSheet(this);
	m_page2.SetParentSheet(this);
}

LRESULT CBindSheet::OnInitDialog(HWND /*hWndFocus*/, LPARAM /*lParam*/)
{
	return 0;
}

void CBindSheet::SetBindType(UINT nType)
{
	m_nBindType = nType;
}
UINT CBindSheet::GetBindType() const
{
	return m_nBindType;
}

void CBindSheet::SetDiskCount(UINT nCount)
{
	m_nDiskCount = nCount;
}
UINT CBindSheet::GetDiskCount() const
{
	return m_nDiskCount;
}
void CBindSheet::SetSingleDisks(CDiskObjectList singleDisks)
{
	m_listSingle = singleDisks;
}
CDiskObjectList CBindSheet::GetSingleDisks() const
{
	return m_listSingle;
}
void CBindSheet::SetPrimaryDisks(CDiskObjectVector vtDisks)
{
	m_vtPrimary = vtDisks;
}
CDiskObjectVector CBindSheet::GetPrimaryDisks() const
{
	return m_vtPrimary;
}
void CBindSheet::SetMirrorDisks(CDiskObjectVector vtDisks)
{
	m_vtMirror = vtDisks;
}
CDiskObjectVector CBindSheet::GetMirrorDisks() const
{
	return m_vtMirror;
}

void CBindSheet::SetBoundDisks(CDiskObjectVector vtDisks)
{
	m_vtBound = vtDisks;
}
CDiskObjectVector CBindSheet::GetBoundDisks() const
{
	return m_vtBound;
}

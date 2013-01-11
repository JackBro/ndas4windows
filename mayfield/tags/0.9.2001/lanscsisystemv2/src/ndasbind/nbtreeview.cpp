// nbtreeview.cpp : implementation of the CNBTreeView class
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "resource.h"

#include "nbtreeview.h"
#include "nbuihandler.h"

CNBTreeView::CNBTreeView()
{
	m_nAction = NDASBINDVIEW_INSERT_OBJECT;
	m_htiParent = TVI_ROOT;
	m_htiLast = TVI_ROOT;
}

BOOL CNBTreeView::PreTranslateMessage(MSG* pMsg)
{
	pMsg;
	return FALSE;
}

void CNBTreeView::Visit(CDiskObjectPtr o)
{
	const CObjectUIHandler *pHandler = CObjectUIHandler::GetUIHandler(o.get());

	switch ( m_nAction )
	{
	case NDASBINDVIEW_INSERT_OBJECT:
		{
			m_htiLast = CTreeViewCtrlEx::InsertItem( 
				pHandler->GetTitle(o.get()), 
				pHandler->GetIconIndex(o.get()),
				pHandler->GetSelectedIconIndex(o.get()),
				m_htiParent,
				TVI_SORT 
				);
			m_htiLast.SetData( o->GetUniqueID() );
			m_mapIDToTreeItem[o->GetUniqueID()] = m_htiLast;
		}
		break;
	case NDASBINDVIEW_UPDATE_OBJECT:
		{
			CTreeItem htiUpdate = m_mapIDToTreeItem[o->GetUniqueID()];
			htiUpdate.SetText( pHandler->GetTitle(o.get()) );
			htiUpdate.SetImage(
				pHandler->GetIconIndex(o.get()),
				pHandler->GetSelectedIconIndex(o.get())
				);
		}
	}
};
void CNBTreeView::IncDepth()
{ 
	m_htiParents.push( m_htiParent );
	m_htiParent = m_htiLast;
};
void CNBTreeView::DecDepth()
{ 
	m_htiParent = m_htiParents.top();
	m_htiParents.pop();
};

void CNBTreeView::DeleteDiskObject(CDiskObjectPtr o)
{
	CTreeItem htiDelete = m_mapIDToTreeItem[o->GetUniqueID()];

	CTreeViewCtrlEx::DeleteItem( htiDelete );
}

void CNBTreeView::InsertDiskObject(CDiskObjectPtr o, CDiskObjectPtr parent)
{
	if ( parent.get() == NULL )
	{
		m_htiParent = TVI_ROOT;
	}
	else 
	{
		m_htiParent = m_mapIDToTreeItem[parent->GetUniqueID()];
		if ( m_htiParent.IsNull() )
			m_htiParent = TVI_ROOT;
	}
	m_nAction = NDASBINDVIEW_INSERT_OBJECT;
	o->Accept( o, this );
}

void CNBTreeView::UpdateDiskObject(CDiskObjectPtr o)
{
	m_nAction = NDASBINDVIEW_UPDATE_OBJECT;
	o->Accept( o, this );
}

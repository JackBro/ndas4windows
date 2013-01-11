////////////////////////////////////////////////////////////////////////////
//
// Utility classes and functions
//
// @author Ji Young Park(jypark@ximeta.com)
//
////////////////////////////////////////////////////////////////////////////

#pragma once
#ifndef _NDASUTIL_H_
#define _NDASUTIL_H_

#include <boost/shared_ptr.hpp>
//namespace ximeta
//{

// 
// Classes that frees allocated memory by HeapAlloc 
// automatically when the program leaves the scope
// where the variable is defined.
// 
// Here is an example
// void f() {
//	typedef struct {
//		int member;
//	} MYSTRUCT;
//	CHeapMemoryPtr<MYSTRUCT> pStructPtr;
//
//	pStructPtr = CHeapMemoryPtr<MYSTRUCT>( ::HeapAlloc(::GerProcessHeap(), 0, MYSTRUCT) );
//	pStructPtr->member = 10;
//
// } // Allocated Heap will be deleted automatically.
class CHeapDeleter
{
public:
	void operator()(LPVOID lpMem)
	{
		if ( lpMem != NULL )
			::HeapFree( ::GetProcessHeap(), 0, lpMem );
	}
};

template<typename T>
class CHeapMemoryPtr : public boost::shared_ptr<T>
{
public:
	CHeapMemoryPtr(LPVOID val = NULL) 
		: boost::shared_ptr<T>(reinterpret_cast<T*>(val), CHeapDeleter())
	{
	}

	T* operator-> ()
	{
		return px;
	}

	operator T* ()
	{
		return px;
	}
};


WTL::CString AddrToString(const BYTE *pbNode);



#define _LINE_STR3_(x) #x
#define _LINE_STR2_(x) _LINE_STR3_(x)
#define _LINE_STR_ _LINE_STR2_(__LINE__)

//}

#endif // _NDASUTIL_H_

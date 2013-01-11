#include "precomp.hpp"
#include "winioctlhelper.h"
#include <xtl/xtltrace.h>
#include <xtl/xtlautores.h>

PDRIVE_LAYOUT_INFORMATION_EX
pDiskGetDriveLayoutEx(HANDLE hDevice)
{
	DWORD returnedLength; // always returns 0
	DWORD bufferLength = sizeof(DRIVE_LAYOUT_INFORMATION_EX);

	XTL::AutoProcessHeapPtr<DRIVE_LAYOUT_INFORMATION_EX> buffer =
		static_cast<DRIVE_LAYOUT_INFORMATION_EX*>(
			::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, bufferLength));

	BOOL success = ::DeviceIoControl(
		hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
		NULL, 0,
		buffer, bufferLength,
		&returnedLength, NULL);
	while (!success && ERROR_INSUFFICIENT_BUFFER == ::GetLastError())
	{
		// To determine the size of output buffer that is required, caller
		// should send this IOCTL request in a loop. Every time the
		// storage stack rejects the IOCTL with an error message
		// indicating that the buffer was too small, caller should double
		// the buffer size.
		bufferLength += bufferLength;
		XTL::AutoProcessHeapPtr<DRIVE_LAYOUT_INFORMATION_EX> newBuffer = 
			static_cast<DRIVE_LAYOUT_INFORMATION_EX*>(
				::HeapReAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, buffer, bufferLength));
		if (newBuffer.IsInvalid())
		{
			// buffer is still valid in case of failure
			// buffer will be released on return.
			return NULL;
		}
		// buffer points to the non-valid memory, so invalidate it now
		buffer.Detach();
		// transfer the data from newBuffer to buffer
		buffer = newBuffer.Detach();
		// query again
		success = ::DeviceIoControl(
			 hDevice, IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
			 NULL, 0,
			 buffer, bufferLength,
			 &returnedLength, NULL);
	}
	// returns the buffer
	return buffer.Detach();
}

PSTORAGE_DESCRIPTOR_HEADER
pStorageQueryProperty(
	HANDLE hDevice,
	STORAGE_PROPERTY_ID PropertyId,
	STORAGE_QUERY_TYPE  QueryType)
{
	STORAGE_PROPERTY_QUERY spquery;
	::ZeroMemory(&spquery, sizeof(spquery));
	spquery.PropertyId = PropertyId;
	spquery.QueryType = QueryType;

	DWORD bufferLength = sizeof(STORAGE_DESCRIPTOR_HEADER);
	XTL::AutoProcessHeapPtr<STORAGE_DESCRIPTOR_HEADER> buffer =
		static_cast<STORAGE_DESCRIPTOR_HEADER*>(
			::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, bufferLength));
	if (buffer.IsInvalid())
	{
		return NULL;
	}

	DWORD returnedLength;
	BOOL success = ::DeviceIoControl(
		hDevice, IOCTL_STORAGE_QUERY_PROPERTY, 
		&spquery, sizeof(spquery),
		buffer, bufferLength,
		&returnedLength, NULL);
	if (!success)
	{
		XTLTRACE2(NdasVolTrace, TRACE_LEVEL_ERROR,
			"IOCTL_STORAGE_QUERY_PROPERTY(HEADER) failed, error=0x%X\n",
			GetLastError());
		return NULL;
	}

	// We only retrived the header, now we reallocate the buffer
	// required for the actual query
	bufferLength = buffer->Size; 
	XTL::AutoProcessHeapPtr<STORAGE_DESCRIPTOR_HEADER> newBuffer =
		static_cast<STORAGE_DESCRIPTOR_HEADER*>(
			::HeapReAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, buffer, bufferLength));
	if (newBuffer.IsInvalid())
	{
		return NULL;
	}
	// set the buffer with the new buffer
	buffer.Detach();
	buffer = newBuffer.Detach();
	
	// now we can query the actual property with the proper size
	success = ::DeviceIoControl(
		hDevice, IOCTL_STORAGE_QUERY_PROPERTY, 
		&spquery, sizeof(spquery),
		buffer, bufferLength,
		&returnedLength, NULL);
	if (!success)
	{
		XTLTRACE2(NdasVolTrace, TRACE_LEVEL_ERROR,
			"IOCTL_STORAGE_QUERY_PROPERTY(DATA) failed, error=0x%X\n",
			GetLastError());
		return NULL;
	}

	return buffer.Detach();
}

PSTORAGE_DEVICE_DESCRIPTOR
pStorageQueryDeviceProperty(HANDLE hDevice)
{
	return reinterpret_cast<PSTORAGE_DEVICE_DESCRIPTOR>(
		pStorageQueryProperty(hDevice, StorageDeviceProperty, PropertyStandardQuery));
}

PSTORAGE_ADAPTER_DESCRIPTOR
pStorageQueryAdapterProperty(HANDLE hDevice)
{
	return reinterpret_cast<PSTORAGE_ADAPTER_DESCRIPTOR>(
		pStorageQueryProperty(hDevice, StorageAdapterProperty, PropertyStandardQuery));
}

HRESULT
pScsiGetAddress(
	__in HANDLE hDevice, 
	__out PSCSI_ADDRESS ScsiAddress)
{
	DWORD returnedLength;
	BOOL success = ::DeviceIoControl(
		hDevice, IOCTL_SCSI_GET_ADDRESS,
		NULL, 0,
		ScsiAddress, sizeof(SCSI_ADDRESS),
		&returnedLength, NULL);
	return success ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

HRESULT
pStorageGetDeviceNumber(
	HANDLE hDevice, 
	STORAGE_DEVICE_NUMBER* StorDevNum)
{
	DWORD returnedLength;
	BOOL success = ::DeviceIoControl(
		hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER,
		NULL, 0,
		StorDevNum, sizeof(STORAGE_DEVICE_NUMBER),
		&returnedLength, NULL);
	return success ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

// obsolete, do not use this function
HRESULT
pDiskGetPartitionInfo(
	HANDLE hDevice,
	PPARTITION_INFORMATION PartInfo)
{
	DWORD returnedLength;
	BOOL success = ::DeviceIoControl(
		hDevice, IOCTL_DISK_GET_PARTITION_INFO,
		NULL, 0,
		PartInfo, sizeof(PARTITION_INFORMATION),
		&returnedLength, NULL);
	return success ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

PVOLUME_DISK_EXTENTS
pVolumeGetVolumeDiskExtents(
	HANDLE hVolume)
{
	DWORD returnedLength;
	DWORD bufferLength = sizeof(VOLUME_DISK_EXTENTS);

	XTL::AutoProcessHeapPtr<VOLUME_DISK_EXTENTS> buffer =
		static_cast<PVOLUME_DISK_EXTENTS>(
			::HeapAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, bufferLength));

	if (buffer.IsInvalid())
	{
		XTLTRACE2(NdasVolTrace, TRACE_LEVEL_ERROR, 
			"HeapAlloc failed, bytes=%d\n", bufferLength);
		return NULL;
	}

	BOOL success = ::DeviceIoControl(
		hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
		NULL, 0,
		buffer, bufferLength,
		&returnedLength, NULL);

	if (!success && ERROR_MORE_DATA == ::GetLastError())
	{
		//
		// An extent is a contiguous run of sectors on one disk. When the
		// number of extents returned is greater than one (1), the error
		// code ERROR_MORE_DATA is returned. You should call
		// DeviceIoControl again, allocating enough buffer space based on
		// 
		// the value of NumberOfDiskExtents after the first DeviceIoControl call.

		bufferLength = sizeof(VOLUME_DISK_EXTENTS) - sizeof(DISK_EXTENT) + 
			sizeof(DISK_EXTENT) * buffer->NumberOfDiskExtents;

		XTL::AutoProcessHeapPtr<VOLUME_DISK_EXTENTS> newBuffer = 
			static_cast<PVOLUME_DISK_EXTENTS>(
				::HeapReAlloc(::GetProcessHeap(), HEAP_ZERO_MEMORY, buffer, bufferLength));
		if (newBuffer.IsInvalid())
		{
			// buffer is still valid in case of failure
			// buffer will be released on return.
			XTLTRACE2(NdasVolTrace, TRACE_LEVEL_ERROR, 
				"HeapReAlloc failed, bytes=%d\n", bufferLength);
			return NULL;
		}
		// buffer points to the non-valid memory, so invalidate it now
		buffer.Detach();
		// transfer the data from newBuffer to buffer
		buffer = newBuffer.Detach();
		// query again
		success = ::DeviceIoControl(
			hVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
			NULL, 0,
			buffer, bufferLength,
			&returnedLength, NULL);
	}

	if (!success)
	{
		XTLTRACE2(NdasVolTrace, TRACE_LEVEL_ERROR, 
			"IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS failed, error=0x%X\n",
			GetLastError());
		return NULL;
	}

	// returns the buffer
	return buffer.Detach();
}

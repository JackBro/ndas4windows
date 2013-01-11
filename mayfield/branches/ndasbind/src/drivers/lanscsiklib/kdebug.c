#include <ntddk.h>
#include <scsi.h>
#include <stdio.h>
#include <stdarg.h>
#include "KDebug.h"

#ifdef __MODULE__
#undef __MODULE__
#endif // __MODULE__
#define __MODULE__ "KDebug"

#define DEBUG_BUFFER_LENGTH 256

ULONG	KDebugLevel = 1;
ULONG	KDebugMask = DBG_DEFAULT_MASK;
UCHAR	MiniBuffer[DEBUG_BUFFER_LENGTH + 1];
PUCHAR	MiniBufferSentinal = NULL;
UCHAR	MiniBufferWithLocation[DEBUG_BUFFER_LENGTH + 1];
PUCHAR	MiniBufferWithLocationSentinal = NULL;

PCHAR
_LSKDebugPrint(
   IN PCCHAR	DebugMessage,
   ...
   )


{
    va_list ap;

    va_start(ap, DebugMessage);

	_vsnprintf(MiniBuffer, DEBUG_BUFFER_LENGTH, DebugMessage, ap);
	ASSERTMSG("_KDebugPrint overwrote sentinal byte",
		((MiniBufferSentinal == NULL) || (*MiniBufferSentinal == 0xff)));

	if(MiniBufferSentinal) {
		*MiniBufferSentinal = 0xff;
	}

//	DbgPrint(MiniBuffer);

    va_end(ap);

	return (PCHAR)MiniBuffer;
}

VOID
_LSKDebugPrintWithLocation(
   IN PCCHAR	DebugMessage,
   PCCHAR		ModuleName,
   UINT32		LineNumber,
   PCCHAR		FunctionName
   )
{
	_snprintf(MiniBufferWithLocation, DEBUG_BUFFER_LENGTH, 
		"[%s:%04d] %s : %s", ModuleName, LineNumber, FunctionName, DebugMessage);

	ASSERTMSG("_KDebugPrintWithLocation overwrote sentinal byte",
		((MiniBufferWithLocationSentinal == NULL) || (*MiniBufferWithLocationSentinal == 0xff)));

	if(MiniBufferWithLocationSentinal) {
		*MiniBufferWithLocationSentinal = 0xff;
	}

	DbgPrint(MiniBufferWithLocation);
}

PCHAR
CdbOperationString(
	UCHAR	Code
	)
{
	switch(Code) {
	case SCSIOP_READ_CAPACITY:
		return "SCSIOP_READ_CAPACITY";
	case SCSIOP_READ:
		return "SCSIOP_READ";
	case SCSIOP_INQUIRY:
		return "SCSIOP_INQUIRY";
	case SCSIOP_RELEASE_UNIT:
		return "SCSIOP_RELEASE_UNIT";
	case SCSIOP_TEST_UNIT_READY:
		return "SCSIOP_TEST_UNIT_READY";
	case SCSIOP_MODE_SENSE:
		return "SCSIOP_MODE_SENSE";
	case SCSIOP_WRITE:               
		return "SCSIOP_WRITE";
	case SCSIOP_VERIFY:
		return "SCSIOP_VERIFY";
	case SCSIOP_MEDIUM_REMOVAL:
		return "SCSIOP_MEDIUM_REMOVAL";
	
	case SCSIOP_REZERO_UNIT:
		return "SCSIOP_REZERO_UNIT or SCSIOP_REWIND";
	case SCSIOP_REQUEST_BLOCK_ADDR:
		return "SCSIOP_REQUEST_BLOCK_ADDR";
	case SCSIOP_REQUEST_SENSE:
		return "SCSIOP_REQUEST_SENSE";
	case SCSIOP_FORMAT_UNIT:
		return "SCSIOP_FORMAT_UNIT";
	case SCSIOP_READ_BLOCK_LIMITS:
		return "SCSIOP_READ_BLOCK_LIMITS";
	case SCSIOP_REASSIGN_BLOCKS:
		return "SCSIOP_REASSIGN_BLOCKS or SCSIOP_INIT_ELEMENT_STATUS";
	case SCSIOP_READ6:
		return "SCSIOP_READ6 or SCSIOP_RECEIVE";
	case SCSIOP_WRITE6:
		return "SCSIOP_WRITE6 or SCSIOP_PRINT or SCSIOP_SEND";
	case SCSIOP_SEEK6:
		return "SCSIOP_SEEK6 or SCSIOP_TRACK_SELECT or SCSIOP_SLEW_PRINT";
	case SCSIOP_SEEK_BLOCK:
		return "SCSIOP_SEEK_BLOCK";
	case SCSIOP_PARTITION:
		return "SCSIOP_PARTITION";
	case SCSIOP_READ_REVERSE:
        return "SCSIOP_READ_REVERSE";
	case SCSIOP_WRITE_FILEMARKS:
		return "SCSIOP_WRITE_FILEMARKS or SCSIOP_FLUSH_BUFFER";
	case SCSIOP_SPACE:
		return "SCSIOP_SPACE";
	case SCSIOP_VERIFY6:
		return "SCSIOP_VERIFY6";
	case SCSIOP_RECOVER_BUF_DATA:
		return "SCSIOP_RECOVER_BUF_DATA";
	case SCSIOP_MODE_SELECT:
		return "SCSIOP_MODE_SELECT";
	case SCSIOP_RESERVE_UNIT:
        return "SCSIOP_RESERVE_UNIT";
	case SCSIOP_COPY:
		return "SCSIOP_COPY";
	case SCSIOP_ERASE:
		return "SCSIOP_ERASE";
	case SCSIOP_START_STOP_UNIT:
		return "SCSIOP_START_STOP_UNIT/STOP_PRINT/LOAD_UNLOAD";
	case SCSIOP_RECEIVE_DIAGNOSTIC:
		return "SCSIOP_RECEIVE_DIAGNOSTIC";
	case SCSIOP_SEND_DIAGNOSTIC:
		return "SCSIOP_SEND_DIAGNOSTIC";
	case SCSIOP_READ_FORMATTED_CAPACITY:
		return "SCSIOP_READ_FORMATTED_CAPACITY";
	case SCSIOP_SEEK:
		return "SCSIOP_SEEK or SCSIOP_LOCATE or SCSIOP_POSITION_TO_ELEMENT";
	case SCSIOP_WRITE_VERIFY:
        return "SCSIOP_WRITE_VERIFY";
	case SCSIOP_SEARCH_DATA_HIGH:
		return "SCSIOP_SEARCH_DATA_HIGH";
	case SCSIOP_SEARCH_DATA_EQUAL:
		return "SCSIOP_SEARCH_DATA_EQUAL";
	case SCSIOP_SEARCH_DATA_LOW:
		return "SCSIOP_SEARCH_DATA_LOW";
	case SCSIOP_SET_LIMITS:
		return "SCSIOP_SET_LIMITS";
	case SCSIOP_READ_POSITION:
		return "SCSIOP_READ_POSITION";
	case SCSIOP_SYNCHRONIZE_CACHE:
		return "SCSIOP_SYNCHRONIZE_CACHE";
	case SCSIOP_COMPARE:
		return "SCSIOP_COMPARE";
	case SCSIOP_COPY_COMPARE:
        return "SCSIOP_COPY_COMPARE";
	case SCSIOP_WRITE_DATA_BUFF:
		return "SCSIOP_WRITE_DATA_BUFF";
	case SCSIOP_READ_DATA_BUFF:
		return "SCSIOP_READ_DATA_BUFF";
	case SCSIOP_CHANGE_DEFINITION:
		return "SCSIOP_CHANGE_DEFINITION";
	case SCSIOP_READ_SUB_CHANNEL:
		return "SCSIOP_READ_SUB_CHANNEL";
	case SCSIOP_READ_TOC:
		return "SCSIOP_READ_TOC";
	case SCSIOP_READ_HEADER:
		return "SCSIOP_READ_HEADER";
	case SCSIOP_PLAY_AUDIO:
		return "SCSIOP_PLAY_AUDIO";
	case SCSIOP_PLAY_AUDIO_MSF:
		return "SCSIOP_PLAY_AUDIO_MSF";
	case SCSIOP_PLAY_TRACK_INDEX:
		return "SCSIOP_PLAY_TRACK_INDEX";
	case SCSIOP_PLAY_TRACK_RELATIVE:
		return "SCSIOP_PLAY_TRACK_RELATIVE";
	case SCSIOP_PAUSE_RESUME:
        return "SCSIOP_PAUSE_RESUME";
	case SCSIOP_LOG_SELECT:
		return "SCSIOP_LOG_SELECT";
	case SCSIOP_LOG_SENSE:
		return "SCSIOP_LOG_SENSE";
	case SCSIOP_STOP_PLAY_SCAN:
		return "SCSIOP_STOP_PLAY_SCAN";
	case SCSIOP_READ_DISK_INFORMATION :
		return "SCSIOP_READ_DISK_INFORMATION";
	case SCSIOP_READ_TRACK_INFORMATION:
		return "SCSIOP_READ_TRACK_INFORMATION";
	case SCSIOP_MODE_SELECT10:
		return "SCSIOP_MODE_SELECT10";
	case SCSIOP_MODE_SENSE10:
        return "SCSIOP_MODE_SENSE10";
	case SCSIOP_REPORT_LUNS:
		return "SCSIOP_REPORT_LUNS";
	case SCSIOP_SEND_KEY:
		return "SCSIOP_SEND_KEY";
	case SCSIOP_REPORT_KEY:
		return "SCSIOP_REPORT_KEY";
	case SCSIOP_MOVE_MEDIUM:
		return "SCSIOP_MOVE_MEDIUM";
	case SCSIOP_LOAD_UNLOAD_SLOT:
		return "SCSIOP_LOAD_UNLOAD_SLOT or SCSIOP_EXCHANGE_MEDIUM";
	case SCSIOP_SET_READ_AHEAD:
		return "SCSIOP_SET_READ_AHEAD";
	case SCSIOP_READ_DVD_STRUCTURE:
		return "SCSIOP_READ_DVD_STRUCTURE";
	case SCSIOP_REQUEST_VOL_ELEMENT:
		return "SCSIOP_REQUEST_VOL_ELEMENT";
	case SCSIOP_SEND_VOLUME_TAG:
		return "SCSIOP_SEND_VOLUME_TAG";
	case SCSIOP_READ_ELEMENT_STATUS:
		return "SCSIOP_READ_ELEMENT_STATUS";
	case SCSIOP_READ_CD_MSF         :
		return "SCSIOP_READ_CD_MSF";
	case SCSIOP_SCAN_CD:
		return "SCSIOP_SCAN_CD";
	case SCSIOP_PLAY_CD:
		return "SCSIOP_PLAY_CD";
	case SCSIOP_MECHANISM_STATUS:
		return "SCSIOP_MECHANISM_STATUS";
	case SCSIOP_READ_CD:
		return "SCSIOP_READ_CD";
	case SCSIOP_INIT_ELEMENT_RANGE:
		return "SCSIOP_INIT_ELEMENT_RANGE";
	//
	//	Scsi commands undefined in Windows.
	//
	case 0xBB:
		return "REDUNDANCY_GROUP (OUT)";
	case 0x3E:
		return "READ_LONG";
	case 0x5C:
		return "READ_BUFFER_CAPACITY";
	case 0xD8:
	case 0xF0:
	case 0xF1:
		return "Vendor-specific";
	case 0x46:
		return "GET_CONFIGURATION";
	case 0xA1:
		return "BLANK";
	case 0xAC:
		return "GET_PERFORMANCE";
	case 0x5B:
		return "CLOSE_TRACK_SESSION";
	case 0x53:
		return "RESERVE_TRACK";
	case 0xAa:
		return "WRITE12";
	case 0xA8:
		return "READ12";

	default:
		return "** Unknown Operation code **";
	}
}
PCHAR
SrbFunctionCodeString(
	ULONG	Code
	)
{
	switch(Code) {
	case 0x00:
		return "SRB_FUNCTION_EXECUTE_SCSI";
	case 0x02:
		return "SRB_FUNCTION_IO_CONTROL";
	case 0x10:
		return "SRB_FUNCTION_WMI";
	case 0x01:
		return "SRB_FUNCTION_CLAIM_DEVICE";
	case 0x03:
		return "SRB_FUNCTION_RECEIVE_EVENT";
	case 0x04:
		return "SRB_FUNCTION_RELEASE_QUEUE";
	case 0x05:
		return "SRB_FUNCTION_ATTACH_DEVICE";
	case 0x06:
		return "SRB_FUNCTION_RELEASE_DEVICE";
	case 0x07:
		return "SRB_FUNCTION_SHUTDOWN";
	case 0x08:
		return "SRB_FUNCTION_FLUSH";
	case 0x09:
		return "SRB_FUNCTION_ABORT_COMMAND";
	case 0x0a:
		return "SRB_FUNCTION_RELEASE_RECOVERY";
	case 0x0b:
		return "SRB_FUNCTION_RESET_BUS";
	case 0x0c:
		return "SRB_FUNCTION_RESET_DEVICE";
	case 0x0d:
		return "SRB_FUNCTION_TERMINATE_IO";
	case 0x0e:
		return "SRB_FUNCTION_FLUSH_QUEUE";
	case 0x0f:
		return "SRB_FUNCTION_REMOVE_DEVICE";
	case 0x11:
		return "SRB_FUNCTION_LOCK_QUEUE";
	case 0x12:
		return "SRB_FUNCTION_UNLOCK_QUEUE";
	default:
		return "***********Unknown Function...***************";
	}
}



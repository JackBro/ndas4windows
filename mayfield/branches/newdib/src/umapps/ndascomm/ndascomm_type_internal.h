#ifndef _NDASCOMM_TYPE_INTERNAL_H_
#define _NDASCOMM_TYPE_INTERNAL_H_

#pragma once

/* All structures in this header are unaligned. */
#include <pshpack1.h>

#define NDASCOMM_VENDOR_ID 0x0001
#define NDASCOMM_OP_VERSION 0x01

#define NDASCOMM_TEXT_TYPE_BINARY 0x01
#define NDASCOMM_TEXT_TYPE_TEXT 0x00


// see lsp_binparam.h for details
#define NDASCOMM_TEXT_VERSION 0x00

#define	NDASCOMM_BINPARM_TYPE_TARGET_LIST	0x3
#define	NDASCOMM_BINPARM_TYPE_TARGET_DATA	0x4

#define	NDASCOMM_BINPARM_SIZE_TEXT_TARGET_LIST_REQUEST	4
#define	NDASCOMM_BINPARM_SIZE_TEXT_TARGET_LIST_REPLY	36
#define	NDASCOMM_BINPARM_SIZE_TEXT_TARGET_DATA_REQUEST	16
#define	NDASCOMM_BINPARM_SIZE_TEXT_TARGET_DATA_REPLY	8

/* End of packing */
#include <poppack.h>

#endif // _NDASCOMM_TYPE_INTERNAL_H_
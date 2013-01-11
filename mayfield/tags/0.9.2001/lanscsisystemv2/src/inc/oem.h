
// This macro is used in LanscsiBus.h
#define RETAIL_VENDOR_ID	"NETDISK"
// This macro is used in buspdo.c and should be defined as unicode.
// Don't forget to include the prefix L
#define RETAIL_VENDOR_NAME	L"XIMETA "
#define RETAIL_MODEL		L"NetDisk "

#ifdef OEM_IOMEGA
#define OEM_VENDOR_ID	"NETHD"
#define OEM_VENDOR_NAME	L"Iomega "
#define OEM_MODEL		L"Network Hard Drive "
#endif

#ifdef OEM_LOGITEC
#define OEM_VENDOR_ID	"LHD-LU2"
#define OEM_VENDOR_NAME	L"Logitec "
#define OEM_MODEL		L"LHD-LU2 "
#endif

#ifdef OEM_MORITANI
#define OEM_VENDOR_ID	"EOSEED"
#define OEM_VENDOR_NAME	RETAIL_VENDOR_NAME
#define OEM_MODEL		L"Eoseed "
#endif

#ifdef OEM_GENNETWORKS
#define OEM_VENDOR_ID	"GENDISK"
#define OEM_VENDOR_NAME	L"Gennetworks "
#define OEM_MODEL		L"GenDisk "
#endif

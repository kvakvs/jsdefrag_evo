#pragma once

#pragma pack(push,1)                  /* Align to bytes. */
#include <memory>

struct FatBootSectorStruct
{
	UCHAR  BS_jmpBoot[3];          // 0
	UCHAR  BS_OEMName[8];          // 3
	USHORT BPB_BytsPerSec;         // 11
	UCHAR  BPB_SecPerClus;         // 13
	USHORT BPB_RsvdSecCnt;         // 14
	UCHAR  BPB_NumFATs;            // 16
	USHORT BPB_RootEntCnt;         // 17
	USHORT BPB_TotSec16;           // 19
	UCHAR  BPB_Media;              // 21
	USHORT BPB_FATSz16;            // 22
	USHORT BPB_SecPerTrk;          // 24
	USHORT BPB_NumHeads;           // 26
	ULONG  BPB_HiddSec;            // 28
	ULONG  BPB_TotSec32;           // 32

	union
	{
		struct
		{
			UCHAR  BS_DrvNum;          // 36
			UCHAR  BS_Reserved1;       // 37
			UCHAR  BS_BootSig;         // 38
			ULONG  BS_VolID;           // 39
			UCHAR  BS_VolLab[11];      // 43
			UCHAR  BS_FilSysType[8];   // 54
			UCHAR  BS_Reserved2[448];  // 62
		} Fat16;

		struct
		{
			ULONG  BPB_FATSz32;        // 36
			USHORT BPB_ExtFlags;       // 40
			USHORT BPB_FSVer;          // 42
			ULONG  BPB_RootClus;       // 44
			USHORT BPB_FSInfo;         // 48
			USHORT BPB_BkBootSec;      // 50
			UCHAR  BPB_Reserved[12];   // 52
			UCHAR  BS_DrvNum;          // 64
			UCHAR  BS_Reserved1;       // 65
			UCHAR  BS_BootSig;         // 66
			ULONG  BS_VolID;           // 67
			UCHAR  BS_VolLab[11];      // 71
			UCHAR  BS_FilSysType[8];   // 82
			UCHAR  BPB_Reserved2[420]; // 90
		} Fat32;
	};

	USHORT Signature;              // 510
};

struct FatDirStruct
{
	UCHAR  DIR_Name[11];           // 0   File name, 8 + 3.
	UCHAR  DIR_Attr;               // 11  File attributes.
	UCHAR  DIR_NTRes;              // 12  Reserved.
	UCHAR  DIR_CrtTimeTenth;       // 13  Creation time, tenths of a second, 0...199.
	USHORT DIR_CrtTime;            // 14  Creation time.
	USHORT DIR_CrtDate;            // 16  Creation date.
	USHORT DIR_LstAccDate;         // 18  Last access date.
	USHORT DIR_FstClusHI;          // 20  First cluster number, high word.
	USHORT DIR_WrtTime;            // 22  Last write time.
	USHORT DIR_WrtDate;            // 24  Last write date.
	USHORT DIR_FstClusLO;          // 26  First cluster number, low word.
	ULONG  DIR_FileSize;           // 28  File size in bytes.
};

struct FatLongNameDirStruct
{
	UCHAR LDIR_Ord;                // 0   Sequence number
	wchar_t LDIR_Name1[5];           // 1   Characters 1-5 in name
	UCHAR LDIR_Attr;               // 11  Attribute, must be ATTR_LONG_NAME
	UCHAR LDIR_Type;               // 12  Always zero
	UCHAR LDIR_Chksum;             // 13  Checksum
	wchar_t LDIR_Name2[6];           // 14  Characters 6-11
	UCHAR LDIR_FstClusLO[2];       // 26  Always zero
	wchar_t LDIR_Name3[2];           // 28  Characters 12-13
};

#pragma pack(pop)                     /* Reset byte alignment. */

/* The attribute flags. */
#define ATTR_READ_ONLY  0x01
#define ATTR_HIDDEN     0x02
#define ATTR_SYSTEM     0x04
#define ATTR_VOLUME_ID  0x08
#define ATTR_DIRECTORY  0x10
#define ATTR_ARCHIVE    0x20

#define ATTR_LONG_NAME  (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
#define ATTR_LONG_NAME_MASK (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID | ATTR_DIRECTORY | ATTR_ARCHIVE)

/* Struct used by the scanner to store disk information from the bootblock. */
struct FatDiskInfoStruct
{
	uint64_t BytesPerSector;
	uint64_t SectorsPerCluster;
	uint64_t TotalSectors;
	uint64_t RootDirSectors;
	uint64_t FirstDataSector;
	uint64_t FATSz;
	uint64_t DataSec;
	uint64_t CountofClusters;

	union
	{
		BYTE *FAT12;
		USHORT *FAT16;
		ULONG *FAT32;
	} FatData;
};

class ScanFAT
{
public:
	ScanFAT();
	~ScanFAT();

	// Get a non-owning pointer to instance of the class
	static ScanFAT *get_instance();

	BOOL analyze_fat_volume(DefragDataStruct *data);

private:
    static UCHAR calculate_short_name_check_sum(const UCHAR *name);
    static uint64_t convert_time(USHORT date, USHORT time, USHORT time10);
    static void make_fragment_list(const DefragDataStruct *data, const FatDiskInfoStruct *disk_info, ItemStruct *item, uint64_t cluster);
	BYTE *load_directory(DefragDataStruct *Data, FatDiskInfoStruct *DiskInfo, uint64_t StartCluster, uint64_t *OutLength);
	void analyze_fat_directory(DefragDataStruct *Data, FatDiskInfoStruct *DiskInfo, BYTE *Buffer, uint64_t Length, ItemStruct *ParentDirectory);

	// static member that is an instance of itself
	inline static std::unique_ptr<ScanFAT> instance_;

	DefragLib *defrag_lib_;
};

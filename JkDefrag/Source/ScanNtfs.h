#pragma once

const size_t MFTBUFFERSIZE = 256 * 1024; /* 256 KB seems to be the optimum. */

struct INODE_REFERENCE {
    ULONG InodeNumberLowPart;

    USHORT InodeNumberHighPart;
    USHORT SequenceNumber;
};

struct NTFS_RECORD_HEADER {
    ULONG Type; /* File type, for example 'FILE' */

    USHORT UsaOffset; /* Offset to the Update Sequence Array */
    USHORT UsaCount; /* Size in words of Update Sequence Array */

    USN Lsn; /* $LogFile Sequence Number (LSN) */
};

struct FILE_RECORD_HEADER {
    struct NTFS_RECORD_HEADER RecHdr;

    USHORT SequenceNumber; /* Sequence number */
    USHORT LinkCount; /* Hard link count */
    USHORT AttributeOffset; /* Offset to the first Attribute */
    USHORT Flags; /* Flags. bit 1 = in use, bit 2 = directory, bit 4 & 8 = unknown. */

    ULONG BytesInUse; /* Real size of the FILE record */
    ULONG BytesAllocated; /* Allocated size of the FILE record */

    INODE_REFERENCE BaseFileRecord; /* File reference to the base FILE record */

    USHORT NextAttributeNumber; /* Next Attribute Id */
    USHORT Padding; /* Align to 4 UCHAR boundary (XP) */

    ULONG MFTRecordNumber; /* Number of this MFT Record (XP) */

    USHORT UpdateSeqNum; /*  */
};

enum ATTRIBUTE_TYPE {
    AttributeInvalid = 0x00,
    /* Not defined by Windows */
    AttributeStandardInformation = 0x10,
    AttributeAttributeList = 0x20,
    AttributeFileName = 0x30,
    AttributeObjectId = 0x40,
    AttributeSecurityDescriptor = 0x50,
    AttributeVolumeName = 0x60,
    AttributeVolumeInformation = 0x70,
    AttributeData = 0x80,
    AttributeIndexRoot = 0x90,
    AttributeIndexAllocation = 0xA0,
    AttributeBitmap = 0xB0,
    AttributeReparsePoint = 0xC0,
    /* Reparse Point = Symbolic link */
    AttributeEAInformation = 0xD0,
    AttributeEA = 0xE0,
    AttributePropertySet = 0xF0,
    AttributeLoggedUtilityStream = 0x100
};

struct ATTRIBUTE {
    enum ATTRIBUTE_TYPE AttributeType;

    ULONG Length;

    BOOLEAN Nonresident;

    UCHAR NameLength;

    USHORT NameOffset;
    USHORT Flags; /* 0x0001 = Compressed, 0x4000 = Encrypted, 0x8000 = Sparse */
    USHORT AttributeNumber;
};

struct RESIDENT_ATTRIBUTE {
    struct ATTRIBUTE Attribute;

    ULONG ValueLength;

    USHORT ValueOffset;
    USHORT Flags; // 0x0001 = Indexed
};

struct NONRESIDENT_ATTRIBUTE {
    struct ATTRIBUTE Attribute;

    ULONGLONG StartingVcn;
    ULONGLONG LastVcn;

    USHORT RunArrayOffset;

    UCHAR CompressionUnit;
    UCHAR AlignmentOrReserved[5];

    ULONGLONG AllocatedSize;
    ULONGLONG DataSize;
    ULONGLONG InitializedSize;
    ULONGLONG CompressedSize; // Only when compressed
};

struct STANDARD_INFORMATION {
    uint64_t CreationTime;
    uint64_t FileChangeTime;
    uint64_t MftChangeTime;
    uint64_t LastAccessTime;

    ULONG FileAttributes; /* READ_ONLY=0x01, HIDDEN=0x02, SYSTEM=0x04, VOLUME_ID=0x08, ARCHIVE=0x20, DEVICE=0x40 */
    ULONG MaximumVersions;
    ULONG VersionNumber;
    ULONG ClassId;
    ULONG OwnerId; // NTFS 3.0 only
    ULONG SecurityId; // NTFS 3.0 only

    ULONGLONG QuotaCharge; // NTFS 3.0 only

    USN Usn; // NTFS 3.0 only
};

struct ATTRIBUTE_LIST {
    enum ATTRIBUTE_TYPE AttributeType;

    USHORT Length;

    UCHAR NameLength;
    UCHAR NameOffset;

    ULONGLONG LowestVcn;

    INODE_REFERENCE FileReferenceNumber;

    USHORT Instance;
    USHORT AlignmentOrReserved[3];
};

struct FILENAME_ATTRIBUTE {
    struct INODE_REFERENCE ParentDirectory;

    uint64_t CreationTime;
    uint64_t ChangeTime;
    uint64_t LastWriteTime;
    uint64_t LastAccessTime;

    ULONGLONG AllocatedSize;
    ULONGLONG DataSize;

    ULONG FileAttributes;
    ULONG AlignmentOrReserved;

    UCHAR NameLength;
    UCHAR NameType; /* NTFS=0x01, DOS=0x02 */

    WCHAR Name[1];
};

struct OBJECTID_ATTRIBUTE {
    GUID ObjectId;

    union {
        struct {
            GUID BirthVolumeId;
            GUID BirthObjectId;
            GUID DomainId;
        };

        UCHAR ExtendedInfo[48];
    };
};

struct VOLUME_INFORMATION {
    LONGLONG Reserved;

    UCHAR MajorVersion;
    UCHAR MinorVersion;

    USHORT Flags; /* DIRTY=0x01, RESIZE_LOG_FILE=0x02 */
};

struct DIRECTORY_INDEX {
    ULONG EntriesOffset;
    ULONG IndexBlockLength;
    ULONG AllocatedSize;
    ULONG Flags; /* SMALL=0x00, LARGE=0x01 */
};

struct DIRECTORY_ENTRY {
    ULONGLONG FileReferenceNumber;

    USHORT Length;
    USHORT AttributeLength;

    ULONG Flags; // 0x01 = Has trailing VCN, 0x02 = Last entry

    // FILENAME_ATTRIBUTE Name;
    // ULONGLONG Vcn;      // VCN in IndexAllocation of earlier entries
};

struct INDEX_ROOT {
    enum ATTRIBUTE_TYPE Type;

    ULONG CollationRule;
    ULONG BytesPerIndexBlock;
    ULONG ClustersPerIndexBlock;

    struct DIRECTORY_INDEX DirectoryIndex;
};

struct INDEX_BLOCK_HEADER {
    struct NTFS_RECORD_HEADER Ntfs;

    ULONGLONG IndexBlockVcn;

    struct DIRECTORY_INDEX DirectoryIndex;
};

struct REPARSE_POINT {
    ULONG ReparseTag;

    USHORT ReparseDataLength;
    USHORT Reserved;

    UCHAR ReparseData[1];
};

struct EA_INFORMATION {
    ULONG EaLength;
    ULONG EaQueryLength;
};

struct EA_ATTRIBUTE {
    ULONG NextEntryOffset;

    UCHAR Flags;
    UCHAR EaNameLength;

    USHORT EaValueLength;

    CHAR EaName[1];
    // UCHAR EaData[];
};

struct ATTRIBUTE_DEFINITION {
    WCHAR AttributeName[64];

    ULONG AttributeNumber;
    ULONG Unknown[2];
    ULONG Flags;

    ULONGLONG MinimumSize;
    ULONGLONG MaximumSize;
};

/*
   The NTFS scanner will construct an ItemStruct list in memory, but needs some
   extra information while constructing it. The following structs wrap the ItemStruct
   into a new struct with some extra info, discarded when the ItemStruct list is
   ready.

   A single Inode can contain multiple streams of data. Every stream has it's own
   list of fragments. The name of a stream is the same as the filename plus two
   extensions separated by colons:
         filename:"stream name":"stream type"

   For example:
         myfile.dat:stream1:$DATA

   The "stream name" is an empty string for the default stream, which is the data
   of regular files. The "stream type" is one of the following strings:
      0x10      $STANDARD_INFORMATION
      0x20      $ATTRIBUTE_LIST
      0x30      $FILE_NAME
      0x40  NT  $VOLUME_VERSION
      0x40  2K  $OBJECT_ID
      0x50      $SECURITY_DESCRIPTOR
      0x60      $VOLUME_NAME
      0x70      $VOLUME_INFORMATION
      0x80      $DATA
      0x90      $INDEX_ROOT
      0xA0      $INDEX_ALLOCATION
      0xB0      $BITMAP
      0xC0  NT  $SYMBOLIC_LINK
      0xC0  2K  $REPARSE_POINT
      0xD0      $EA_INFORMATION
      0xE0      $EA
      0xF0  NT  $PROPERTY_SET
      0x100 2K  $LOGGED_UTILITY_STREAM
*/

struct StreamStruct {
    struct StreamStruct* Next;

    WCHAR* StreamName; /* "stream name" */

    ATTRIBUTE_TYPE StreamType; /* "stream type" */

    struct FragmentListStruct* Fragments; /* The fragments of the stream. */

    uint64_t Clusters; /* Total number of clusters. */
    uint64_t Bytes; /* Total number of bytes. */
};

struct InodeDataStruct {
    uint64_t Inode; /* The Inode number. */
    uint64_t ParentInode; /* The Inode number of the parent directory. */

    bool Directory; /* true: it's a directory. */

    WCHAR* LongFilename; /* Long filename. */
    WCHAR* ShortFilename; /* Short filename (8.3 DOS). */

    uint64_t Bytes; /* Total number of bytes. */
    uint64_t CreationTime; /* 1 second = 10000000 */
    uint64_t MftChangeTime;
    uint64_t LastAccessTime;

    struct StreamStruct* Streams; /* List of StreamStruct. */
    struct FragmentListStruct* MftDataFragments; /* The Fragments of the $MFT::$DATA stream. */

    uint64_t MftDataBytes; /* Length of the $MFT::$DATA. */

    struct FragmentListStruct* MftBitmapFragments; /* The Fragments of the $MFT::$BITMAP stream. */

    uint64_t MftBitmapBytes; /* Length of the $MFT::$BITMAP. */
};

struct NtfsDiskInfoStruct {
    uint64_t BytesPerSector;
    uint64_t SectorsPerCluster;
    uint64_t TotalSectors;
    uint64_t MftStartLcn;
    uint64_t Mft2StartLcn;
    uint64_t BytesPerMftRecord;
    uint64_t ClustersPerIndexRecord;

    struct {
        BYTE Buffer[MFTBUFFERSIZE];

        uint64_t Offset;

        int Age;
    } Buffers[3];
};

class ScanNtfs {
public:
    ScanNtfs();
    ~ScanNtfs();

    // Get instance of the class
    static ScanNtfs* getInstance();

    BOOL AnalyzeNtfsVolume(struct DefragDataStruct* Data);

private:
    WCHAR* StreamTypeNames(ATTRIBUTE_TYPE StreamType);

    BOOL FixupRawMftdata(struct DefragDataStruct* Data, struct NtfsDiskInfoStruct* DiskInfo, BYTE* Buffer,
                         uint64_t BufLength);

    BYTE* ReadNonResidentData(struct DefragDataStruct* Data, struct NtfsDiskInfoStruct* DiskInfo, BYTE* RunData,
                              uint32_t RunDataLength,
                              uint64_t Offset, uint64_t WantedLength);

    BOOL TranslateRundataToFragmentlist(
        struct DefragDataStruct* Data,
        struct InodeDataStruct* InodeData,
        WCHAR* StreamName,
        ATTRIBUTE_TYPE StreamType,
        BYTE* RunData,
        uint32_t RunDataLength,
        uint64_t StartingVcn,
        uint64_t Bytes);

    void CleanupStreams(struct InodeDataStruct* InodeData, BOOL CleanupFragments);

    WCHAR* ConstructStreamName(WCHAR* FileName1, WCHAR* FileName2, struct StreamStruct* Stream);

    BOOL ProcessAttributes(
        struct DefragDataStruct* Data,
        struct NtfsDiskInfoStruct* DiskInfo,
        struct InodeDataStruct* InodeData,
        BYTE* Buffer,
        uint64_t BufLength,
        USHORT Instance,
        int Depth);

    void ProcessAttributeList(
        struct DefragDataStruct* Data,
        struct NtfsDiskInfoStruct* DiskInfo,
        struct InodeDataStruct* InodeData,
        BYTE* Buffer,
        uint64_t BufLength,
        int Depth);

    BOOL InterpretMftRecord(
        struct DefragDataStruct* Data,
        struct NtfsDiskInfoStruct* DiskInfo,
        struct ItemStruct** InodeArray,
        uint64_t InodeNumber,
        uint64_t MaxInode,
        struct FragmentListStruct** MftDataFragments,
        uint64_t* MftDataBytes,
        struct FragmentListStruct** MftBitmapFragments,
        uint64_t* MftBitmapBytes,
        BYTE* Buffer,
        uint64_t BufLength);

    // static member that is an instance of itself
    static ScanNtfs* m_jkScanNtfs;

    //	JKDefragGui *m_jkGui;
    DefragLib* m_jkLib;
};

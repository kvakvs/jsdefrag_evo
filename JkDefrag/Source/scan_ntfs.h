#pragma once
#include <memory>

constexpr size_t mftbuffersize = 256 * 1024; /* 256 KB seems to be the optimum. */

struct INODE_REFERENCE {
    ULONG inode_number_low_part_;
    USHORT inode_number_high_part_;
    USHORT sequence_number_;
};

struct NTFS_RECORD_HEADER {
    ULONG type_; /* File type, for example 'FILE' */

    USHORT usa_offset_; /* Offset to the Update Sequence Array */
    USHORT usa_count_; /* Size in words of Update Sequence Array */

    USN lsn_; /* $LogFile Sequence Number (LSN) */
};

struct FILE_RECORD_HEADER {
    NTFS_RECORD_HEADER rec_hdr_;

    USHORT sequence_number_;
    // Hard link count 
    USHORT link_count_;
    // Offset to the first Attribute 
    USHORT attribute_offset_;
    // Flags. bit 1 = in use, bit 2 = directory, bit 4 & 8 = unknown. 
    USHORT flags_; 

    ULONG bytes_in_use_; /* Real size of the FILE record */
    ULONG bytes_allocated_; /* Allocated size of the FILE record */

    INODE_REFERENCE base_file_record_; /* File reference to the base FILE record */

    USHORT next_attribute_number_; /* Next Attribute Id */
    USHORT padding_; /* Align to 4 UCHAR boundary (XP) */

    ULONG mft_record_number_; /* Number of this MFT Record (XP) */

    USHORT update_seq_num_; 
};

enum class ATTRIBUTE_TYPE {
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
    ATTRIBUTE_TYPE attribute_type_;
    ULONG length_;
    BOOLEAN nonresident_;
    UCHAR name_length_;
    USHORT name_offset_;
    // 0x0001 = Compressed, 0x4000 = Encrypted, 0x8000 = Sparse 
    USHORT flags_; 
    USHORT attribute_number_;
};

struct RESIDENT_ATTRIBUTE {
    ATTRIBUTE attribute_;
    ULONG value_length_;
    USHORT value_offset_;
    // 0x0001 = Indexed
    USHORT flags_; 
};

struct NONRESIDENT_ATTRIBUTE {
    ATTRIBUTE attribute_;
    ULONGLONG starting_vcn_;
    ULONGLONG last_vcn_;

    USHORT run_array_offset_;

    UCHAR compression_unit_;
    UCHAR alignment_or_reserved_[5];

    ULONGLONG allocated_size_;
    ULONGLONG data_size_;
    ULONGLONG initialized_size_;
    ULONGLONG compressed_size_; // Only when compressed
};

struct STANDARD_INFORMATION {
    uint64_t creation_time_;
    uint64_t file_change_time_;
    uint64_t mft_change_time_;
    uint64_t last_access_time_;

    // READ_ONLY=0x01, HIDDEN=0x02, SYSTEM=0x04, VOLUME_ID=0x08, ARCHIVE=0x20, DEVICE=0x40 
    ULONG file_attributes_; 
    ULONG maximum_versions_;
    ULONG version_number_;
    ULONG class_id_;

    //
    // NTFS 3.0 only
    //

    ULONG owner_id_;
    ULONG security_id_; 
    ULONGLONG quota_charge_; 
    USN usn_;
};

struct ATTRIBUTE_LIST {
    ATTRIBUTE_TYPE attribute_type_;

    USHORT length_;

    UCHAR name_length_;
    UCHAR name_offset_;

    ULONGLONG lowest_vcn_;

    INODE_REFERENCE file_reference_number_;

    USHORT instance_;
    USHORT alignment_or_reserved_[3];
};

struct FILENAME_ATTRIBUTE {
    INODE_REFERENCE parent_directory_;

    uint64_t creation_time_;
    uint64_t change_time_;
    uint64_t last_write_time_;
    uint64_t last_access_time_;

    ULONGLONG allocated_size_;
    ULONGLONG data_size_;

    ULONG file_attributes_;
    ULONG alignment_or_reserved_;

    UCHAR name_length_;
    // NTFS=0x01, DOS=0x02 
    UCHAR name_type_; 

    wchar_t name_[1];
};

struct OBJECTID_ATTRIBUTE {
    GUID object_id_;

    union {
        struct {
            GUID birth_volume_id_;
            GUID birth_object_id_;
            GUID domain_id_;
        };

        UCHAR extended_info[48];
    };
};

struct VOLUME_INFORMATION {
    LONGLONG reserved_;

    UCHAR major_version_;
    UCHAR minor_version_;

    USHORT flags_; /* DIRTY=0x01, RESIZE_LOG_FILE=0x02 */
};

struct DIRECTORY_INDEX {
    ULONG entries_offset_;
    ULONG index_block_length_;
    ULONG allocated_size_;
    // SMALL=0x00, LARGE=0x01 
    ULONG flags_; 
};

struct DIRECTORY_ENTRY {
    ULONGLONG file_reference_number_;

    USHORT length_;
    USHORT attribute_length_;

    // 0x01 = Has trailing VCN, 0x02 = Last entry
    ULONG flags_; 

    // FILENAME_ATTRIBUTE Name;
    // ULONGLONG Vcn;      // VCN in IndexAllocation of earlier entries
};

struct INDEX_ROOT {
    ATTRIBUTE_TYPE type_;

    ULONG collation_rule_;
    ULONG bytes_per_index_block_;
    ULONG clusters_per_index_block_;

    DIRECTORY_INDEX directory_index_;
};

struct INDEX_BLOCK_HEADER {
    NTFS_RECORD_HEADER ntfs_;

    ULONGLONG index_block_vcn_;

    DIRECTORY_INDEX directory_index_;
};

struct REPARSE_POINT {
    ULONG reparse_tag_;

    USHORT reparse_data_length_;
    USHORT reserved_;

    UCHAR reparse_data_[1];
};

struct EA_INFORMATION {
    ULONG ea_length_;
    ULONG ea_query_length_;
};

struct EA_ATTRIBUTE {
    ULONG next_entry_offset_;

    UCHAR flags_;
    UCHAR ea_name_length_;

    USHORT ea_value_length_;

    CHAR ea_name_[1];
    // UCHAR EaData[];
};

struct ATTRIBUTE_DEFINITION {
    wchar_t attribute_name_[64];

    ULONG attribute_number_;
    ULONG unknown_[2];
    ULONG flags_;

    ULONGLONG minimum_size_;
    ULONGLONG maximum_size_;
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
    StreamStruct* next_;

    wchar_t* stream_name_; /* "stream name" */

    ATTRIBUTE_TYPE stream_type_; /* "stream type" */

    FragmentListStruct* fragments_; /* The fragments of the stream. */

    uint64_t clusters_; /* Total number of clusters. */
    uint64_t bytes_; /* Total number of bytes. */
};

struct InodeDataStruct {
    uint64_t inode_; /* The Inode number. */
    uint64_t parent_inode_; /* The Inode number of the parent directory. */

    bool is_directory_; /* true: it's a directory. */

    wchar_t* long_filename_; /* Long filename. */
    wchar_t* short_filename_; /* Short filename (8.3 DOS). */

    uint64_t bytes_; /* Total number of bytes. */
    uint64_t creation_time_; /* 1 second = 10000000 */
    uint64_t mft_change_time_;
    uint64_t last_access_time_;

    StreamStruct* streams_; /* List of StreamStruct. */
    FragmentListStruct* mft_data_fragments_; /* The Fragments of the $MFT::$DATA stream. */

    uint64_t mft_data_bytes_; /* Length of the $MFT::$DATA. */

    FragmentListStruct* mft_bitmap_fragments_; /* The Fragments of the $MFT::$BITMAP stream. */

    uint64_t mft_bitmap_bytes_; /* Length of the $MFT::$BITMAP. */
};

struct NtfsDiskInfoStruct {
    uint64_t bytes_per_sector_;
    uint64_t sectors_per_cluster_;
    uint64_t total_sectors_;
    uint64_t mft_start_lcn_;
    uint64_t mft2_start_lcn_;
    uint64_t bytes_per_mft_record_;
    uint64_t clusters_per_index_record_;

    struct {
        BYTE buffer_[mftbuffersize];
        uint64_t offset_;
        int age_;
    } buffers_[3];
};

class ScanNTFS {
public:
    ScanNTFS();
    ~ScanNTFS();

    // Get instance of the class
    static ScanNTFS* get_instance();

    BOOL analyze_ntfs_volume(DefragDataStruct* data);

private:
    static const wchar_t * stream_type_names(const ATTRIBUTE_TYPE stream_type);

    bool fixup_raw_mftdata(DefragDataStruct* data, const NtfsDiskInfoStruct* disk_info, BYTE* buffer,
                           uint64_t buf_length) const;

    BYTE* read_non_resident_data(const DefragDataStruct* data, const NtfsDiskInfoStruct* disk_info, const BYTE* run_data,
                                 uint32_t run_data_length, uint64_t offset, uint64_t wanted_length) const;

    static BOOL translate_rundata_to_fragmentlist(
        const DefragDataStruct* data,
        InodeDataStruct* inode_data,
        wchar_t* stream_name,
        ATTRIBUTE_TYPE stream_type,
        const BYTE* run_data,
        uint32_t run_data_length,
        uint64_t starting_vcn,
        uint64_t bytes);

    void cleanup_streams(InodeDataStruct* InodeData, BOOL CleanupFragments);

    wchar_t* construct_stream_name(const wchar_t *file_name_1, const wchar_t *file_name_2, StreamStruct* stream);

    BOOL process_attributes(
        DefragDataStruct* Data,
        NtfsDiskInfoStruct* DiskInfo,
        InodeDataStruct* InodeData,
        BYTE* Buffer,
        uint64_t BufLength,
        USHORT Instance,
        int Depth);

    void process_attribute_list(
        DefragDataStruct* Data,
        NtfsDiskInfoStruct* DiskInfo,
        InodeDataStruct* InodeData,
        BYTE* Buffer,
        uint64_t BufLength,
        int Depth);

    BOOL interpret_mft_record(
        DefragDataStruct* Data,
        NtfsDiskInfoStruct* DiskInfo,
        ItemStruct** InodeArray,
        uint64_t InodeNumber,
        uint64_t MaxInode,
        FragmentListStruct** MftDataFragments,
        uint64_t* MftDataBytes,
        FragmentListStruct** MftBitmapFragments,
        uint64_t* MftBitmapBytes,
        BYTE* Buffer,
        uint64_t BufLength);

    // static member that is an instance of itself
    inline static std::unique_ptr<ScanNTFS> instance_;

    //	JKDefragGui *m_jkGui;
    DefragLib* defrag_lib_{};
};

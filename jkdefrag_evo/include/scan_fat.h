#pragma once

#pragma pack(push, 1) // Align to bytes

#include <memory>

struct FatBootSectorStruct {
    UCHAR bs_jmp_boot_[3]; // 0
    UCHAR bs_oem_name_[8]; // 3
    USHORT bpb_byts_per_sec_; // 11
    UCHAR bpb_sec_per_clus_; // 13
    USHORT bpb_rsvd_sec_cnt_; // 14
    UCHAR bpb_num_fats_; // 16
    USHORT bpb_root_ent_cnt_; // 17
    USHORT bpb_tot_sec16_; // 19
    UCHAR bpb_media_; // 21
    USHORT bpb_fat_sz16_; // 22
    USHORT bpb_sec_per_trk_; // 24
    USHORT bpb_num_heads_; // 26
    ULONG bpb_hidd_sec_; // 28
    ULONG bpb_tot_sec32_; // 32

    union {
        struct {
            UCHAR bs_drv_num_; // 36
            UCHAR bs_reserved1_; // 37
            UCHAR bs_boot_sig_; // 38
            ULONG bs_vol_id_; // 39
            UCHAR bs_vol_lab_[11]; // 43
            UCHAR bs_fil_sys_type_[8]; // 54
            UCHAR bs_reserved2_[448]; // 62
        } fat16;

        struct {
            ULONG bpb_fat_sz32_; // 36
            USHORT bpb_ext_flags_; // 40
            USHORT bpb_fs_ver_; // 42
            ULONG bpb_root_clus_; // 44
            USHORT bpb_fs_info_; // 48
            USHORT bpb_bk_boot_sec_; // 50
            UCHAR bpb_reserved_[12]; // 52
            UCHAR bs_drv_num_; // 64
            UCHAR bs_reserved1_; // 65
            UCHAR bs_boot_sig_; // 66
            ULONG bs_vol_id_; // 67
            UCHAR bs_vol_lab_[11]; // 71
            UCHAR bs_fil_sys_type_[8]; // 82
            UCHAR bpb_reserved2_[420]; // 90
        } fat32;
    };

    USHORT signature_; // 510
};

struct FatDirStruct {
    UCHAR dir_name_[11]; // 0   File name, 8 + 3.
    UCHAR dir_attr_; // 11  File attributes.
    UCHAR dir_nt_res_; // 12  Reserved.
    UCHAR dir_crt_time_tenth_; // 13  Creation time, tenths of a second, 0...199.
    USHORT dir_crt_time_; // 14  Creation time.
    USHORT dir_crt_date_; // 16  Creation date.
    USHORT dir_lst_acc_date_; // 18  Last access date.
    USHORT dir_fst_clus_hi_; // 20  First cluster number, high word.
    USHORT dir_wrt_time_; // 22  Last write time.
    USHORT dir_wrt_date_; // 24  Last write date.
    USHORT dir_fst_clus_lo_; // 26  First cluster number, low word.
    ULONG dir_file_size_; // 28  File size in bytes.
};

struct FatLongNameDirStruct {
    UCHAR ldir_ord_; // 0   Sequence number
    wchar_t ldir_name1_[5]; // 1   Characters 1-5 in name
    UCHAR ldir_attr_; // 11  Attribute, must be ATTR_LONG_NAME
    UCHAR ldir_type_; // 12  Always zero
    UCHAR ldir_chksum_; // 13  Checksum
    wchar_t ldir_name2_[6]; // 14  Characters 6-11
    UCHAR ldir_fst_clus_lo_[2]; // 26  Always zero
    wchar_t ldir_name3_[2]; // 28  Characters 12-13
};

#pragma pack(pop) // Reset byte alignment

// The attribute flags
enum {
    ATTR_READ_ONLY = 0x01,
    ATTR_HIDDEN = 0x02,
    ATTR_SYSTEM = 0x04,
    ATTR_VOLUME_ID = 0x08,
    ATTR_DIRECTORY = 0x10,
    ATTR_ARCHIVE = 0x20,
    ATTR_LONG_NAME = (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID),
    ATTR_LONG_NAME_MASK = (ATTR_LONG_NAME | ATTR_DIRECTORY | ATTR_ARCHIVE)
};

// Struct used by the scanner to store disk information from the bootblock
struct FatDiskInfoStruct {
    uint64_t bytes_per_sector_;
    uint64_t sectors_per_cluster_;
    uint64_t total_sectors_;
    uint64_t root_dir_sectors_;
    uint64_t first_data_sector_;
    uint64_t fat_sz_;
    uint64_t data_sec_;
    uint64_t countof_clusters_;

    union {
        BYTE *fat12;
        USHORT *fat16;
        ULONG *fat32;
    } fat_data_;
};

class ScanFAT {
public:
    ScanFAT();

    ~ScanFAT();

    // Get a non-owning pointer to instance of the class
    static ScanFAT *get_instance();

    bool analyze_fat_volume(DefragState &data);

private:
    static uint8_t calculate_short_name_check_sum(const UCHAR *name);

    static filetime64_t convert_time(const USHORT date, const USHORT time, const USHORT time10);

    static void make_fragment_list(const DefragState &data, const FatDiskInfoStruct *disk_info, ItemStruct *item,
                                   uint64_t cluster);

    static BYTE *load_directory(
            const DefragState &data, const FatDiskInfoStruct *disk_info, uint64_t start_cluster,
            uint64_t *out_length);

    void analyze_fat_directory(DefragState &data, FatDiskInfoStruct *disk_info, BYTE *buffer, uint64_t length,
                               ItemStruct *parent_directory);

    // static member that is an instance of itself
    inline static std::unique_ptr<ScanFAT> instance_;

    DefragLib *defrag_lib_;
};
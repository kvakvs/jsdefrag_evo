#include "precompiled_header.h"
#include "../../include/defrag_data_struct.h"

#include <memory>

ScanFAT::ScanFAT() {
    defrag_lib_ = DefragLib::get_instance();
}

ScanFAT::~ScanFAT() = default;

ScanFAT *ScanFAT::get_instance() {
    if (instance_ == nullptr) {
        instance_ = std::make_unique<ScanFAT>();
    }

    return instance_.get();
}

// Calculate the checksum of 8.3 filename
uint8_t ScanFAT::calculate_short_name_check_sum(const UCHAR *name) {
    uint8_t check_sum = 0;

    for (short index = 11; index != 0; index--) {
        check_sum = (check_sum & 1 ? 0x80 : 0) + (check_sum >> 1) + *name++;
    }

    return check_sum;
}

/*

Convert the FAT time fields into a uint64_t time.
Note: the FAT stores times in local time, not in GMT time. This subroutine converts
that into GMT time, to be compatible with the NTFS date/times.

*/
micro64_t ScanFAT::convert_time(const USHORT date, const USHORT time, const USHORT time10) {
    FILETIME time1;
    FILETIME time2;
    ULARGE_INTEGER time3;

    if (DosDateTimeToFileTime(date, time, &time1) == 0) return {};
    if (LocalFileTimeToFileTime(&time1, &time2) == 0) return {};

    time3.LowPart = time2.dwLowDateTime;
    time3.HighPart = time2.dwHighDateTime;
    time3.QuadPart = time3.QuadPart + time10 * 100000;

    return std::chrono::microseconds(time3.QuadPart);
}

/*

Determine the number of clusters in an item and translate the FAT clusterlist
into a FragmentList.
- The first cluster number of an item is recorded in it's directory entry. The second
and next cluster numbers are recorded in the FAT, which is simply an array of "next"
cluster numbers.
- A zero-length file has a first cluster number of 0.
- The FAT contains either an EOC mark (End Of Clusterchain) or the cluster number of
the next cluster of the file.

*/
void ScanFAT::make_fragment_list(const DefragDataStruct *data, const FatDiskInfoStruct *disk_info,
                                 ItemStruct *item, uint64_t cluster) {
    FragmentListStruct *new_fragment;
    FragmentListStruct *last_fragment;

    int max_iterate;

    DefragGui *gui = DefragGui::get_instance();

    item->clusters_count_ = 0;
    item->fragments_ = nullptr;

    // If cluster is zero then return zero
    if (cluster == 0) return;

    // Loop through the FAT cluster list, counting the clusters and creating items in the fragment list
    uint64_t first_cluster = cluster;
    uint64_t last_cluster = 0;
    uint64_t vcn = 0;

    for (max_iterate = 0; max_iterate < disk_info->countof_clusters_ + 1; max_iterate++) {
        // Exit the loop when we have reached the end of the cluster list
        if (data->disk_.type_ == DiskType::FAT12 && cluster >= 0xFF8) break;
        if (data->disk_.type_ == DiskType::FAT16 && cluster >= 0xFFF8) break;
        if (data->disk_.type_ == DiskType::FAT32 && cluster >= 0xFFFFFF8) break;

        // Sanity check, test if the cluster is within the range of valid cluster numbers
        if (cluster < 2) break;
        if (cluster > disk_info->countof_clusters_ + 1) break;

        // Increment the cluster counter
        item->clusters_count_ = item->clusters_count_ + 1;

        /* If this is a new fragment then create a record for the previous fragment. If not then
            add the cluster to the counters and continue. */
        if (cluster != last_cluster + 1 && last_cluster != 0) {
            new_fragment = new FragmentListStruct();

            new_fragment->lcn_ = first_cluster - 2;
            vcn = vcn + last_cluster - first_cluster + 1;
            new_fragment->next_vcn_ = vcn;
            new_fragment->next_ = nullptr;

            if (item->fragments_ == nullptr) {
                item->fragments_ = new_fragment;
            } else {
                if (last_fragment != nullptr) last_fragment->next_ = new_fragment;
            }

            last_fragment = new_fragment;
            first_cluster = cluster;
        }

        last_cluster = cluster;

        // Get next cluster from FAT
        switch (data->disk_.type_) {
            case DiskType::FAT12:
                if ((cluster & 1) == 1) {
                    cluster = *(WORD *) &disk_info->fat_data_.fat12[cluster + cluster / 2] >> 4;
                } else {
                    cluster = *(WORD *) &disk_info->fat_data_.fat12[cluster + cluster / 2] & 0xFFF;
                }

                break;

            case DiskType::FAT16:
                cluster = disk_info->fat_data_.fat16[cluster];
                break;
            case DiskType::FAT32:
                cluster = disk_info->fat_data_.fat32[cluster] & 0xFFFFFFF;
                break;
        }
    }

    // If too many iterations (infinite loop in FAT) then exit
    if (max_iterate >= disk_info->countof_clusters_ + 1) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        L"Infinite loop in FAT detected, perhaps the disk is corrupted.");

        return;
    }

    // Create the last fragment
    if (last_cluster != 0) {
        new_fragment = new FragmentListStruct();

        new_fragment->lcn_ = first_cluster - 2;
        vcn = vcn + last_cluster - first_cluster + 1;
        new_fragment->next_vcn_ = vcn;
        new_fragment->next_ = nullptr;

        if (item->fragments_ == nullptr) {
            item->fragments_ = new_fragment;
        } else {
            if (last_fragment != nullptr) last_fragment->next_ = new_fragment;
        }
    }
}

/**
 * \brief Load a directory from disk into a new memory buffer. 
 * \return Return nullptr if error. Note: the caller owns the returned buffer. 
 */
BYTE *
ScanFAT::load_directory(const DefragDataStruct *data, const FatDiskInfoStruct *disk_info, const uint64_t start_cluster,
                        uint64_t *out_length) {
    std::unique_ptr<BYTE[]> buffer;
    uint64_t fragment_length;
    OVERLAPPED g_overlapped;
    ULARGE_INTEGER trans;
    DWORD bytes_read;
    int max_iterate;
    wchar_t s1[BUFSIZ];
    DefragGui *gui = DefragGui::get_instance();

    // Reset the OutLength to zero, in case we exit for an error
    if (out_length != nullptr) *out_length = 0;

    // If cluster is zero then return nullptr
    if (start_cluster == 0) return nullptr;

    // Count the size of the directory
    uint64_t buffer_length = 0;
    uint64_t cluster = start_cluster;

    for (max_iterate = 0; max_iterate < disk_info->countof_clusters_ + 1; max_iterate++) {
        // Exit the loop when we have reached the end of the cluster list
        if (data->disk_.type_ == DiskType::FAT12 && cluster >= 0xFF8) break;
        if (data->disk_.type_ == DiskType::FAT16 && cluster >= 0xFFF8) break;
        if (data->disk_.type_ == DiskType::FAT32 && cluster >= 0xFFFFFF8) break;

        // Sanity check, test if the cluster is within the range of valid cluster numbers
        if (cluster < 2) return nullptr;
        if (cluster > disk_info->countof_clusters_ + 1) return nullptr;

        // Increment the BufferLength counter
        buffer_length = buffer_length + disk_info->sectors_per_cluster_ * disk_info->bytes_per_sector_;

        // Get next cluster from FAT
        switch (data->disk_.type_) {
            case DiskType::FAT12:
                if ((cluster & 1) == 1) {
                    cluster = *(WORD *) &disk_info->fat_data_.fat12[cluster + cluster / 2] >> 4;
                } else {
                    cluster = *(WORD *) &disk_info->fat_data_.fat12[cluster + cluster / 2] & 0xFFF;
                }
                break;

            case DiskType::FAT16:
                cluster = disk_info->fat_data_.fat16[cluster];
                break;
            case DiskType::FAT32:
                cluster = disk_info->fat_data_.fat32[cluster] & 0xFFFFFFF;
                break;
        }
    }

    // If too many iterations (infinite loop in FAT) then return nullptr
    if (max_iterate >= disk_info->countof_clusters_ + 1) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        L"Infinite loop in FAT detected, perhaps the disk is corrupted.");

        return nullptr;
    }

    // Allocate buffer
    if (buffer_length > UINT_MAX) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        std::format(L"Directory is too big, " NUM_FMT " bytes", buffer_length));

        return nullptr;
    }

    buffer = std::make_unique<BYTE[]>(buffer_length);

    // Loop through the FAT cluster list and load all fragments from disk into the buffer.
    uint64_t buffer_offset = 0;
    cluster = start_cluster;

    uint64_t first_cluster = cluster;
    uint64_t last_cluster = 0;

    for (max_iterate = 0; max_iterate < disk_info->countof_clusters_ + 1; max_iterate++) {
        // Exit the loop when we have reached the end of the cluster list
        if (data->disk_.type_ == DiskType::FAT12 && cluster >= 0xFF8) break;
        if (data->disk_.type_ == DiskType::FAT16 && cluster >= 0xFFF8) break;
        if (data->disk_.type_ == DiskType::FAT32 && cluster >= 0xFFFFFF8) break;

        // Sanity check, test if the cluster is within the range of valid cluster numbers
        if (cluster < 2) break;
        if (cluster > disk_info->countof_clusters_ + 1) break;

        /* If this is a new fragment then load the previous fragment from disk. If not then
        add to the counters and continue. */
        if (cluster != last_cluster + 1 && last_cluster != 0) {
            fragment_length =
                    (last_cluster - first_cluster + 1) * disk_info->sectors_per_cluster_ * disk_info->bytes_per_sector_;
            trans.QuadPart =
                    (disk_info->first_data_sector_ + (first_cluster - 2) * disk_info->sectors_per_cluster_) *
                    disk_info->
                            bytes_per_sector_;

            g_overlapped.Offset = trans.LowPart;
            g_overlapped.OffsetHigh = trans.HighPart;
            g_overlapped.hEvent = nullptr;

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"Reading directory fragment, " NUM_FMT " bytes at offset=" NUM_FMT,
                                        fragment_length, trans.QuadPart));

            BOOL result = ReadFile(data->disk_.volume_handle_, &buffer[buffer_offset], (uint32_t) fragment_length,
                                   &bytes_read, &g_overlapped);

            if (result == FALSE) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                std::format(L"Error: {}", DefragLib::system_error_str(GetLastError())));
                return nullptr;
            }

            buffer_offset = buffer_offset + fragment_length;
            first_cluster = cluster;
        }

        last_cluster = cluster;

        // Get next cluster from FAT
        switch (data->disk_.type_) {
            case DiskType::FAT12:
                if ((cluster & 1) == 1) {
                    cluster = *(WORD *) &disk_info->fat_data_.fat12[cluster + cluster / 2] >> 4;
                } else {
                    cluster = *(WORD *) &disk_info->fat_data_.fat12[cluster + cluster / 2] & 0xFFF;
                }

                break;
            case DiskType::FAT16:
                cluster = disk_info->fat_data_.fat16[cluster];
                break;
            case DiskType::FAT32:
                cluster = disk_info->fat_data_.fat32[cluster] & 0xFFFFFFF;
                break;
        }
    }

    // Load the last fragment
    if (last_cluster != 0) {
        fragment_length =
                (last_cluster - first_cluster + 1) * disk_info->sectors_per_cluster_ * disk_info->bytes_per_sector_;
        trans.QuadPart =
                (disk_info->first_data_sector_ + (first_cluster - 2) * disk_info->sectors_per_cluster_) * disk_info->
                        bytes_per_sector_;

        g_overlapped.Offset = trans.LowPart;
        g_overlapped.OffsetHigh = trans.HighPart;
        g_overlapped.hEvent = nullptr;

        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"reading directory fragment, " NUM_FMT " bytes at offset=" NUM_FMT,
                                    fragment_length, trans.QuadPart));

        BOOL result = ReadFile(data->disk_.volume_handle_, &buffer[buffer_offset], (uint32_t) fragment_length,
                               &bytes_read, &g_overlapped);

        if (result == FALSE) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"Error: {}", DefragLib::system_error_str(GetLastError())));
            return nullptr;
        }
    }

    if (out_length != nullptr) *out_length = buffer_length;
    return buffer.release();
}

// Analyze a directory and add all the items to the item tree
void ScanFAT::analyze_fat_directory(DefragDataStruct *data, FatDiskInfoStruct *disk_info, BYTE *buffer,
                                    const uint64_t length, ItemStruct *parent_directory) {
    wchar_t short_name[13];
    wchar_t long_name[820];
    UCHAR long_name_checksum;
    uint64_t sub_dir_length;
    uint64_t start_cluster;
    int i;
    DefragGui *gui = DefragGui::get_instance();

    // Sanity check
    if (buffer == nullptr || length == 0) return;

    // Slow the program down to the percentage that was specified on the command line
    DefragLib::slow_down(data);

    //ShowHex(data,buffer,256);

    // Walk through all the directory entries, extract the info, and store in memory in the ItemTree
    int last_long_name_section = 0;

    for (uint32_t index = 0; index + 31 < length; index = index + 32) {
        if (*data->running_ != RunningState::RUNNING) break;

        const FatDirStruct *dir = (FatDirStruct *) &buffer[index];

        // Ignore free (not used) entries
        if (dir->dir_name_[0] == 0xE5) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(L"{}.\tFree (not used)", index / 32));
            continue;
        }

        // Exit at the end of the directory
        if (dir->dir_name_[0] == 0) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"{}.\tFree (not used), end of directory.", index / 32));
            break;
        }

        // If this is a long filename component then save the string and loop
        if ((dir->dir_attr_ & ATTR_LONG_NAME_MASK) == ATTR_LONG_NAME) {
            const auto l_dir = (FatLongNameDirStruct *) &buffer[index];

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"{}.\tLong filename part.", index / 32));

            i = l_dir->ldir_ord_ & 0x3F;

            if (i == 0) {
                last_long_name_section = 0;

                continue;
            }

            if ((l_dir->ldir_ord_ & 0x40) == 0x40) {
                wmemset(long_name, L'\0', 820);

                last_long_name_section = i;
                long_name_checksum = l_dir->ldir_chksum_;
            } else {
                if (i + 1 != last_long_name_section || long_name_checksum != l_dir->ldir_chksum_) {
                    last_long_name_section = 0;

                    continue;
                }

                last_long_name_section = i;
            }

            i = (i - 1) * 13;

            long_name[i++] = l_dir->ldir_name1_[0];
            long_name[i++] = l_dir->ldir_name1_[1];
            long_name[i++] = l_dir->ldir_name1_[2];
            long_name[i++] = l_dir->ldir_name1_[3];
            long_name[i++] = l_dir->ldir_name1_[4];
            long_name[i++] = l_dir->ldir_name2_[0];
            long_name[i++] = l_dir->ldir_name2_[1];
            long_name[i++] = l_dir->ldir_name2_[2];
            long_name[i++] = l_dir->ldir_name2_[3];
            long_name[i++] = l_dir->ldir_name2_[4];
            long_name[i++] = l_dir->ldir_name2_[5];
            long_name[i++] = l_dir->ldir_name3_[0];
            long_name[i++] = l_dir->ldir_name3_[1];

            continue;
        }

        /* If we are here and the long filename counter is not 1 then something is wrong
        with the long filename. Ignore the long filename. */
        if (last_long_name_section != 1) {
            long_name[0] = '\0';
        } else if (calculate_short_name_check_sum(dir->dir_name_) != long_name_checksum) {
            gui->show_debug(DebugLevel::Fatal, nullptr, L"%u.\tError: long filename is out of sync");
            long_name[0] = '\0';
        }

        last_long_name_section = 0;

        // Extract the short name
        for (i = 0; i < 8; i++) short_name[i] = dir->dir_name_[i];

        for (i = 7; i > 0; i--) if (short_name[i] != ' ') break;

        if (short_name[i] != ' ') i++;

        short_name[i] = '.';
        short_name[i + 1] = dir->dir_name_[8];
        short_name[i + 2] = dir->dir_name_[9];
        short_name[i + 3] = dir->dir_name_[10];

        if (short_name[i + 3] != ' ') {
            short_name[i + 4] = '\0';
        } else if (short_name[i + 2] != ' ') {
            short_name[i + 3] = '\0';
        } else if (short_name[i + 1] != ' ') {
            short_name[i + 2] = '\0';
        } else {
            short_name[i] = '\0';
        }
        if (short_name[0] == 0x05) short_name[0] = 0xE5;

        // If this is a VolumeID then loop. We have no use for it
        if ((dir->dir_attr_ & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) == ATTR_VOLUME_ID) {
            wchar_t *p1 = wcschr(short_name, L'.');

            if (p1 != nullptr) wcscpy_s(p1, wcslen(p1), p1 + 1);

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"{}.\t'{}' (volume ID)", index / 32, short_name));

            continue;
        }

        if ((dir->dir_attr_ & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) == (ATTR_DIRECTORY | ATTR_VOLUME_ID)) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"{}.\tInvalid directory entry", index / 32));

            continue;
        }

        // Ignore "." and ".."
        if (wcscmp(short_name, L".") == 0) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(L"{}.\t'.' current dir", index / 32));

            continue;
        }

        if (wcscmp(short_name, L"..") == 0) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(L"{}.\t'..' parent dir", index / 32));

            continue;
        }

        // Create and fill a new item record in memory
        const auto item = new ItemStruct();

        if (wcscmp(short_name, L".") == 0) {
            item->clear_short_fn();
        } else {
            item->set_short_fn(short_name);
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(L"{}.\t'{}'", index / 32, short_name));
        }

        if (long_name[0] == '\0') {
            item->clear_long_fn();
        } else {
            item->set_long_fn(long_name);
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(L"\tLong filename = '{}'", long_name));
        }

        item->clear_short_path();
        item->clear_long_path();
        item->bytes_ = dir->dir_file_size_;

        if (data->disk_.type_ == DiskType::FAT32) {
            start_cluster = MAKELONG(dir->dir_fst_clus_lo_, dir->dir_fst_clus_hi_);
        } else {
            start_cluster = dir->dir_fst_clus_lo_;
        }

        make_fragment_list(data, disk_info, item, start_cluster);

        item->creation_time_ = convert_time(dir->dir_crt_date_, dir->dir_crt_time_, dir->dir_crt_time_tenth_);
        item->mft_change_time_ = convert_time(dir->dir_wrt_date_, dir->dir_wrt_time_, 0);
        item->last_access_time_ = convert_time(dir->dir_lst_acc_date_, 0, 0);
        item->parent_inode_ = 0;
        item->parent_directory_ = parent_directory;
        item->is_dir_ = false;

        if ((dir->dir_attr_ & (ATTR_DIRECTORY | ATTR_VOLUME_ID)) == ATTR_DIRECTORY) item->is_dir_ = true;

        item->is_unmovable_ = false;
        item->is_excluded_ = false;
        item->is_hog_ = false;

        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"\tSize = " NUM_FMT " clusters, " NUM_FMT " bytes", item->clusters_count_,
                                    item->bytes_));

        // Add the item record to the sorted item tree in memory
        DefragLib::tree_insert(data, item);

        // Draw the item on the screen
        gui->show_analyze(data, item);
        //		if (*data->RedrawScreen == false) {
        defrag_lib_->colorize_disk_item(data, item, 0, 0, false);
        //		} else {
        //			show_diskmap(data);
        //		}

        // Increment counters
        if (item->is_dir_) {
            data->count_directories_ = data->count_directories_ + 1;
        }

        data->count_all_files_ = data->count_all_files_ + 1;
        data->count_all_bytes_ = data->count_all_bytes_ + item->bytes_;
        data->count_all_clusters_ = data->count_all_clusters_ + item->clusters_count_;

        if (DefragLib::get_fragment_count(item) > 1) {
            data->count_fragmented_items_ = data->count_fragmented_items_ + 1;
            data->count_fragmented_bytes_ = data->count_fragmented_bytes_ + item->bytes_;
            data->count_fragmented_clusters_ = data->count_fragmented_clusters_ + item->clusters_count_;
        }

        // If this is a directory then iterate
        if (item->is_dir_ == true) {
            BYTE *sub_dir_buf = load_directory(data, disk_info, start_cluster, &sub_dir_length);

            analyze_fat_directory(data, disk_info, sub_dir_buf, sub_dir_length, item);

            delete sub_dir_buf;

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Finished with subdirectory.");
        }
    }
}

/* Analyze a FAT disk and load into the ItemTree in memory. Return FALSE if the disk is
not a FAT disk, or could not be analyzed.

- The maximum valid cluster number for the volume is CountofClusters + 1
- The FAT runs from cluster 0 to CountofClusters + 1.
- The size of the FAT is:
FAT12: (CountofClusters + 1) * 1.5
FAT16: (CountofClusters + 1) * 2
FAT32: (CountofClusters + 1) * 4
- Compute the sector number for data cluster number N:
FirstSectorOfCluster = ((N - 2) * BootSector.BPB_SecPerClus) + DiskInfo.FirstDataSector;
- Calculate location in the FAT for a given cluster number N:
switch (FATType) {
case FAT12: FATOffset = N + (N / 2); break;            // 12 bit is 1.5 bytes.
case FAT16: FATOffset = N * 2;       break;            // 16 bit is 2 bytes.
case FAT32: FATOffset = N * 4;       break;            // 32 bit is 4 bytes.
}
ThisFATSecNum = BPB_ResvdSecCnt + (FATOffset / BPB_BytsPerSec);
ThisFATEntOffset = FATOffset % BPB_BytsPerSec;
- Determine End Of Clusterchain:
IsEOC = FALSE;
switch (FATType) {
case FAT12: if (FATContent >= 0x0FF8) IsEOC = TRUE; break;
case FAT16: if (FATContent >= 0xFFF8) IsEOC = TRUE; break;
case FAT32: if (FATContent >= 0x0FFFFFF8) IsEOC = TRUE; break;
}
- Determine Bad Cluster:
IsBadCluster = FALSE;
switch (FATType) {
case FAT12: if (FATContent == 0x0FF7) IsBadCluster = TRUE; break;
case FAT16: if (FATContent == 0xFFF7) IsBadCluster = TRUE; break;
case FAT32: if (FATContent == 0x0FFFFFF7) IsBadCluster = TRUE; break;
}
*/
bool ScanFAT::analyze_fat_volume(DefragDataStruct *data) {
    FatDiskInfoStruct disk_info{};
    ULARGE_INTEGER trans;
    OVERLAPPED g_overlapped;
    DWORD bytes_read;
    size_t fat_size;
    BYTE *root_directory;
    uint64_t root_length;
    int result;
    wchar_t s1[BUFSIZ];
    char s2[BUFSIZ];
    DefragGui *gui = DefragGui::get_instance();

    // Read the boot block from the disk
    g_overlapped.Offset = 0;
    g_overlapped.OffsetHigh = 0;
    g_overlapped.hEvent = nullptr;

    FatBootSectorStruct boot_sector{};
    result = ReadFile(data->disk_.volume_handle_, &boot_sector, sizeof(FatBootSectorStruct), &bytes_read,
                      &g_overlapped);

    if (result == 0 || bytes_read != 512) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        std::format(L"Error while reading bootblock: {}", DefragLib::system_error_str(GetLastError())));

        return false;
    }

    // Test if the boot block is a FAT boot block
    if (boot_sector.signature_ != 0xAA55 ||
        ((boot_sector.bs_jmp_boot_[0] != 0xEB || boot_sector.bs_jmp_boot_[2] != 0x90) &&
         boot_sector.bs_jmp_boot_[0] != 0xE9)) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"This is not a FAT disk (different cookie).");

        return false;
    }

    // Fetch values from the bootblock and determine what FAT this is, FAT12, FAT16, or FAT32
    disk_info.bytes_per_sector_ = boot_sector.bpb_byts_per_sec_;

    if (disk_info.bytes_per_sector_ == 0) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"This is not a FAT disk (BytesPerSector is zero).");

        return false;
    }

    disk_info.sectors_per_cluster_ = boot_sector.bpb_sec_per_clus_;

    if (disk_info.sectors_per_cluster_ == 0) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"This is not a FAT disk (SectorsPerCluster is zero).");

        return false;
    }

    disk_info.total_sectors_ = boot_sector.bpb_tot_sec16_;

    if (disk_info.total_sectors_ == 0) disk_info.total_sectors_ = boot_sector.bpb_tot_sec32_;

    disk_info.root_dir_sectors_ =
            (boot_sector.bpb_root_ent_cnt_ * 32 + (boot_sector.bpb_byts_per_sec_ - 1)) / boot_sector.
                    bpb_byts_per_sec_;

    disk_info.fat_sz_ = boot_sector.bpb_fat_sz16_;

    if (disk_info.fat_sz_ == 0) disk_info.fat_sz_ = boot_sector.fat32.bpb_fat_sz32_;

    disk_info.first_data_sector_ =
            boot_sector.bpb_rsvd_sec_cnt_ + boot_sector.bpb_num_fats_ * disk_info.fat_sz_ + disk_info.
                    root_dir_sectors_;

    disk_info.data_sec_ =
            disk_info.total_sectors_ - (boot_sector.bpb_rsvd_sec_cnt_ + boot_sector.bpb_num_fats_ * disk_info.fat_sz_ +
                                        disk_info.root_dir_sectors_);

    disk_info.countof_clusters_ = disk_info.data_sec_ / boot_sector.bpb_sec_per_clus_;

    if (disk_info.countof_clusters_ < 4085) {
        data->disk_.type_ = DiskType::FAT12;

        gui->show_debug(DebugLevel::Fatal, nullptr, L"This is a FAT12 disk.");
    } else if (disk_info.countof_clusters_ < 65525) {
        data->disk_.type_ = DiskType::FAT16;

        gui->show_debug(DebugLevel::Fatal, nullptr, L"This is a FAT16 disk.");
    } else {
        data->disk_.type_ = DiskType::FAT32;

        gui->show_debug(DebugLevel::Fatal, nullptr, L"This is a FAT32 disk.");
    }

    data->bytes_per_cluster_ = disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_;
    data->total_clusters_ = disk_info.countof_clusters_;

    // Output debug information
    strncpy_s(s2, BUFSIZ, (char *) &boot_sector.bs_oem_name_[0], 8);

    s2[8] = '\0';

    gui->show_debug(DebugLevel::Progress, nullptr, std::format(
            L"  OEMName: {}\n"
            L"\n  BytesPerSector: " NUM_FMT
            L"\n  TotalSectors: " NUM_FMT
            L"\n  SectorsPerCluster: " NUM_FMT
            L"\n  RootDirSectors: " NUM_FMT
            L"\n  FATSz: " NUM_FMT
            L"\n  FirstDataSector: " NUM_FMT
            L"\n  DataSec: " NUM_FMT
            L"\n  CountofClusters: " NUM_FMT
            L"\n  ReservedSectors: " NUM_FMT
            L"\n  NumberFATs: " NUM_FMT
            L"\n  RootEntriesCount: " NUM_FMT
            L"\n  MediaType: {}"
            L"\n  SectorsPerTrack: " NUM_FMT
            L"\n  NumberOfHeads: " NUM_FMT
            L"\n  HiddenSectors: " NUM_FMT,
            Str::from_char(s2), disk_info.bytes_per_sector_, disk_info.total_sectors_,
            disk_info.sectors_per_cluster_, disk_info.root_dir_sectors_, disk_info.fat_sz_,
            disk_info.first_data_sector_, disk_info.data_sec_, disk_info.countof_clusters_,
            boot_sector.bpb_rsvd_sec_cnt_, boot_sector.bpb_num_fats_, boot_sector.bpb_root_ent_cnt_,
            boot_sector.bpb_media_, boot_sector.bpb_sec_per_trk_, boot_sector.bpb_num_heads_,
            boot_sector.bpb_hidd_sec_));

    if (data->disk_.type_ != DiskType::FAT32) {
        gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                L"  BS_DrvNum: " NUM_FMT "\n"
                L"  BS_BootSig: " NUM_FMT "\n"
                L"  BS_VolID: " NUM_FMT "\n",
                boot_sector.fat16.bs_drv_num_, boot_sector.fat16.bs_boot_sig_, boot_sector.fat16.bs_vol_id_));

        strncpy_s(s2, BUFSIZ, (char *) &boot_sector.fat16.bs_vol_lab_[0], 11);

        s2[11] = '\0';

        gui->show_debug(DebugLevel::Progress, nullptr, std::format(L"  VolLab: {}", Str::from_char(s2)));

        strncpy_s(s2, BUFSIZ, (char *) &boot_sector.fat16.bs_fil_sys_type_[0], 8);

        s2[8] = '\0';

        gui->show_debug(DebugLevel::Progress, nullptr, std::format(L"  FilSysType: {}", Str::from_char(s2)));
    } else {
        gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                L"  FATSz32: " NUM_FMT "\n"
                L"  ExtFlags: " NUM_FMT "\n"
                L"  FSVer: " NUM_FMT "\n"
                L"  RootClus: " NUM_FMT "\n"
                L"  FSInfo: " NUM_FMT "\n"
                L"  BkBootSec: " NUM_FMT "\n"
                L"  DrvNum: " NUM_FMT "\n"
                L"  BootSig: " NUM_FMT "\n"
                L"  VolID: " NUM_FMT, boot_sector.fat32.bpb_fat_sz32_, boot_sector.fat32.bpb_ext_flags_,
                boot_sector.fat32.bpb_fs_ver_, boot_sector.fat32.bpb_root_clus_, boot_sector.fat32.bpb_fs_info_,
                boot_sector.fat32.bpb_bk_boot_sec_, boot_sector.fat32.bs_drv_num_, boot_sector.fat32.bs_boot_sig_,
                boot_sector.fat32.bs_vol_id_));

        strncpy_s(s2, BUFSIZ, (char *) &boot_sector.fat32.bs_vol_lab_[0], 11);

        s2[11] = '\0';
        gui->show_debug(DebugLevel::Progress, nullptr, std::format(L"  VolLab: {}", Str::from_char(s2)));
        strncpy_s(s2, BUFSIZ, (char *) &boot_sector.fat32.bs_fil_sys_type_[0], 8);
        s2[8] = '\0';
        gui->show_debug(DebugLevel::Progress, nullptr, std::format(L"  FilSysType: {}", Str::from_char(s2)));
    }

    // Read the FAT from disk into memory
    switch (data->disk_.type_) {
        case DiskType::FAT12:
            fat_size = (size_t) (disk_info.countof_clusters_ + 1 + (disk_info.countof_clusters_ + 1) / 2);
            break;
        case DiskType::FAT16:
            fat_size = (size_t) ((disk_info.countof_clusters_ + 1) * 2);
            break;
        case DiskType::FAT32:
            fat_size = (size_t) ((disk_info.countof_clusters_ + 1) * 4);
            break;
    }

    if (fat_size % disk_info.bytes_per_sector_ > 0) {
        fat_size = (size_t) (fat_size + disk_info.bytes_per_sector_ - fat_size % disk_info.bytes_per_sector_);
    }

    disk_info.fat_data_.fat12 = new BYTE[fat_size];

    trans.QuadPart = boot_sector.bpb_rsvd_sec_cnt_ * disk_info.bytes_per_sector_;
    g_overlapped.Offset = trans.LowPart;
    g_overlapped.OffsetHigh = trans.HighPart;
    g_overlapped.hEvent = nullptr;

    gui->show_debug(DebugLevel::Progress, nullptr,
                    std::format(L"Reading FAT, " NUM_FMT " bytes at offset=" NUM_FMT, fat_size, trans.QuadPart));

    result = ReadFile(data->disk_.volume_handle_, disk_info.fat_data_.fat12, (uint32_t) fat_size,
                      &bytes_read, &g_overlapped);

    if (result == 0) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        std::format(L"Error: {}", DefragLib::system_error_str(GetLastError())));
        return false;
    }

    //ShowHex(Data,disk_info.FatData.FAT12,32);

    // Read the root directory from disk into memory
    if (data->disk_.type_ == DiskType::FAT32) {
        root_directory = load_directory(data, &disk_info, boot_sector.fat32.bpb_root_clus_, &root_length);
    } else {
        uint64_t root_start;
        root_start = (boot_sector.bpb_rsvd_sec_cnt_ + boot_sector.bpb_num_fats_ * disk_info.fat_sz_) *
                     disk_info.bytes_per_sector_;
        root_length = boot_sector.bpb_root_ent_cnt_ * 32;

        // Sanity check
        if (root_length > UINT_MAX) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"Root directory is too big, " NUM_FMT " bytes", root_length));

            delete disk_info.fat_data_.fat12;

            return false;
        }

        if (root_start >
            (disk_info.countof_clusters_ + 1) * disk_info.sectors_per_cluster_ * disk_info.bytes_per_sector_) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"Trying to access " NUM_FMT ", but the last sector is at " NUM_FMT,
                                        root_start, (disk_info.countof_clusters_ + 1) * disk_info.sectors_per_cluster_ *
                                                    disk_info.bytes_per_sector_));

            delete disk_info.fat_data_.fat12;

            return false;
        }

        /* We have to round up the Length to the nearest sector. For some reason or other
        Microsoft has decided that raw reading from disk can only be done by whole sector,
        even though ReadFile() accepts it's parameters in bytes. */
        bytes_read = (uint32_t) root_length;

        if (root_length % disk_info.bytes_per_sector_ > 0) {
            bytes_read = (uint32_t) (root_length + disk_info.bytes_per_sector_ -
                                     root_length % disk_info.bytes_per_sector_);
        }

        // Allocate buffer
        root_directory = new BYTE[bytes_read];

        // Read data from disk
        trans.QuadPart = root_start;

        g_overlapped.Offset = trans.LowPart;
        g_overlapped.OffsetHigh = trans.HighPart;
        g_overlapped.hEvent = nullptr;

        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"Reading root directory, " NUM_FMT " bytes at offset=" NUM_FMT, bytes_read,
                                    trans.QuadPart));

        result = ReadFile(data->disk_.volume_handle_, root_directory, bytes_read, &bytes_read, &g_overlapped);

        if (result == 0) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"Error: {}", DefragLib::system_error_str(GetLastError())));

            delete disk_info.fat_data_.fat12;
            delete root_directory;

            return false;
        }
    }

    // Analyze all the items in the root directory and add to the item tree
    analyze_fat_directory(data, &disk_info, root_directory, root_length, nullptr);

    // Cleanup
    delete root_directory;
    delete disk_info.fat_data_.fat12;

    return true;
}

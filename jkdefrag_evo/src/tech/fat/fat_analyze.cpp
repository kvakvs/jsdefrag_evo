/*
 JkDefrag  --  Defragment and optimize all harddisks.

 This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 Public License as published by the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 For the full text of the license see the "License gpl.txt" file.

 Jeroen C. Kessels, Internet Engineer
 http://www.kessels.com/
 */

#include "precompiled_header.h"

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
bool ScanFAT::analyze_fat_volume(DefragState &data) {
    Bytes64 root_length;
    DefragGui *gui = DefragGui::get_instance();

    // Read the boot block from the disk
    OVERLAPPED overlapped1{
            .Offset = 0,
            .OffsetHigh = 0,
            .hEvent = nullptr,
    };

    FatBootSectorStruct boot_sector{};
    DWORD bytes_read;
    auto result = ReadFile(data.disk_.volume_handle_, &boot_sector,
                           sizeof(FatBootSectorStruct), &bytes_read, &overlapped1);

    if (result == 0 || bytes_read != 512) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        std::format(L"Error while reading bootblock: {}",
                                    Str::system_error(GetLastError())));

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
    FatDiskInfoStruct disk_info(data.disk_.type_);

    const auto disk_bytes_per_sector = disk_info.bytes_per_sector_ = Bytes64PerSector(boot_sector.bpb_byts_per_sec_);

    if (disk_bytes_per_sector.is_zero()) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"This is not a FAT disk (BytesPerSector is zero).");
        return false;
    }

    disk_info.sectors_per_cluster_ = Sectors64PerCluster(boot_sector.bpb_sec_per_clus_);

    if (disk_info.sectors_per_cluster_.is_zero()) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"This is not a FAT disk (SectorsPerCluster is zero).");

        return false;
    }

    disk_info.total_sectors_ = Sectors64(boot_sector.bpb_tot_sec16_);

    if (disk_info.total_sectors_.is_zero()) disk_info.total_sectors_ = Sectors64(boot_sector.bpb_tot_sec32_);

    disk_info.root_dir_sectors_ =
            Sectors64((boot_sector.bpb_root_ent_cnt_ * 32 + (boot_sector.bpb_byts_per_sec_ - 1))
                      / boot_sector.bpb_byts_per_sec_);

    disk_info.fat_sz_ = Sectors64(boot_sector.bpb_fat_sz16_);

    if (disk_info.fat_sz_.is_zero()) disk_info.fat_sz_ = Sectors64(boot_sector.fat32.bpb_fat_sz32_);

    disk_info.first_data_sector_ =
            Sectors64(boot_sector.bpb_rsvd_sec_cnt_ + boot_sector.bpb_num_fats_ * disk_info.fat_sz_.value())
            + disk_info.root_dir_sectors_;

    disk_info.data_sec_ = disk_info.total_sectors_ - disk_info.first_data_sector_;

    const Clusters64 disk_total_clusters
            = disk_info.countof_clusters_
                      = disk_info.data_sec_ / Sectors64PerCluster(boot_sector.bpb_sec_per_clus_);

    if (disk_total_clusters < Clusters64(4085)) {
        data.disk_.type_ = DiskType::FAT12;
        gui->show_always(L"This is a FAT12 disk.");
    } else if (disk_total_clusters < Clusters64(65525)) {
        data.disk_.type_ = DiskType::FAT16;
        gui->show_always(L"This is a FAT16 disk.");
    } else {
        data.disk_.type_ = DiskType::FAT32;
        gui->show_always(L"This is a FAT32 disk.");
    }

    data.bytes_per_cluster_ = disk_bytes_per_sector * disk_info.sectors_per_cluster_;
    data.total_clusters_ = disk_total_clusters;

    // Output debug information
    char s2[BUFSIZ];
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
            L"\n  MediaType: {:x}"
            L"\n  SectorsPerTrack: " NUM_FMT
            L"\n  NumberOfHeads: " NUM_FMT
            L"\n  HiddenSectors: " NUM_FMT,
            Str::from_char(s2), disk_bytes_per_sector, disk_info.total_sectors_,
            disk_info.sectors_per_cluster_, disk_info.root_dir_sectors_, disk_info.fat_sz_,
            disk_info.first_data_sector_, disk_info.data_sec_, disk_total_clusters,
            boot_sector.bpb_rsvd_sec_cnt_, boot_sector.bpb_num_fats_, boot_sector.bpb_root_ent_cnt_,
            boot_sector.bpb_media_, boot_sector.bpb_sec_per_trk_, boot_sector.bpb_num_heads_,
            boot_sector.bpb_hidd_sec_));

    if (data.disk_.type_ != DiskType::FAT32) {
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
    // Counted based on cluster count, but unit is: bytes
    Bytes64 fat_size;

    switch (data.disk_.type_) {
        case DiskType::FAT12:
            // 1½ bytes per cluster
            fat_size = Bytes64(disk_total_clusters.value() + 1 + (disk_total_clusters.value() + 1) / 2);
            break;
        case DiskType::FAT16:
            fat_size = Bytes64((disk_total_clusters.value() + 1) * 2);
            break;
        case DiskType::FAT32:
            fat_size = Bytes64((disk_total_clusters.value() + 1) * 4);
            break;
    }

    if (fat_size.value() % disk_bytes_per_sector.value() > 0) {
        fat_size = fat_size + Bytes64(disk_bytes_per_sector.value()) -
                   Bytes64(fat_size.value() % disk_bytes_per_sector.value());
    }

    disk_info.allocate_fat_data(fat_size);

    ULARGE_INTEGER trans{
            .QuadPart = boot_sector.bpb_rsvd_sec_cnt_ * disk_bytes_per_sector.as<ULONGLONG>()
    };
    OVERLAPPED overlapped2{
            .Offset = trans.LowPart,
            .OffsetHigh = trans.HighPart,
            .hEvent = nullptr
    };

    gui->show_debug(DebugLevel::Progress, nullptr,
                    std::format(L"Reading FAT, " NUM_FMT " bytes at offset=" NUM_FMT, fat_size, trans.QuadPart));

    result = ReadFile(data.disk_.volume_handle_, disk_info.fat12_data(), fat_size.as<DWORD>(),
                      &bytes_read, &overlapped2);

    if (result == 0) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        std::format(L"Error: {}", Str::system_error(GetLastError())));
        return false;
    }

    //ShowHex(Data,disk_info.FatData.FAT12,32);
    std::unique_ptr<uint8_t> root_directory;

    // Read the root directory from disk into memory
    if (data.disk_.type_ == DiskType::FAT32) {
        root_directory = load_directory(
                data, &disk_info, Clusters64(boot_sector.fat32.bpb_root_clus_), PARAM_OUT root_length
        );
    } else {
        const auto sec1 = Sectors64(boot_sector.bpb_rsvd_sec_cnt_) +
                          Sectors64(boot_sector.bpb_num_fats_ * disk_info.fat_sz_.value());
        auto root_start = sec1 * disk_bytes_per_sector;
        root_length = Bytes64(boot_sector.bpb_root_ent_cnt_ * 32);

        // Sanity check
        if (root_length.value() > UINT_MAX) {
            gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                    L"Root directory is too big, " NUM_FMT " bytes", root_length));
            return false;
        }

        auto disk_total_bytes =
                (disk_total_clusters + Clusters64(1)) * disk_info.sectors_per_cluster_ * disk_bytes_per_sector;
        if (root_start > disk_total_bytes) {
            gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                    L"Trying to access " NUM_FMT ", but the last sector is at " NUM_FMT,
                    root_start, disk_total_bytes));
            return false;
        }

        // We have to round up the Length to the nearest sector.
        // For some reason or another, Microsoft has decided that raw reading from disk only allowed in full sectors,
        // even though ReadFile() accepts its parameters in bytes.
        bytes_read = root_length.as<DWORD>();

        if (root_length.value() % disk_bytes_per_sector.value() > 0) {
            bytes_read = (DWORD) (root_length.value() + disk_bytes_per_sector.value() -
                                  root_length.value() % disk_bytes_per_sector.value());
        }

        // Allocate buffer
        root_directory = std::make_unique<BYTE>(bytes_read);

        // Read data from disk
        trans.QuadPart = root_start.as<ULONGLONG>();

        OVERLAPPED overlapped3{
                .Offset = trans.LowPart,
                .OffsetHigh = trans.HighPart,
                .hEvent = nullptr
        };

        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
                L"Reading root directory, " NUM_FMT " bytes at offset=" NUM_FMT, bytes_read, trans.QuadPart));

        result = ReadFile(data.disk_.volume_handle_, root_directory.get(), bytes_read,
                          &bytes_read, &overlapped3);

        if (result == 0) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"Error: {}", Str::system_error(GetLastError())));

            return false;
        }
    }

    // Analyze all the items in the root directory and add to the item tree
    auto memv = MemSlice::from_ptr(root_directory.get(), root_length);
    analyze_fat_directory(data, &disk_info, memv, nullptr);

    // Cleanup
    return true;
}

// Analyze a directory and add all the items to the item tree
void ScanFAT::analyze_fat_directory(DefragState &data, FatDiskInfoStruct *disk_info, const MemSlice &buffer,
                                    FileNode *parent_directory) {
    wchar_t short_name[13];
    wchar_t long_name[820];
    UCHAR long_name_checksum;
    Clusters64 start_cluster;
    int i;
    DefragGui *gui = DefragGui::get_instance();

    // Sanity check
    if (!buffer) return;

    // Slow the program down to the percentage that was specified on the command line
    DefragRunner::slow_down(data);

    //ShowHex(data,buffer,256);

    // Walk through all the directory entries, extract the info, and store in memory in the ItemTree
    int last_long_name_section = 0;

    for (Bytes64 index = {}; index + Bytes64(31) < buffer.length(); index = index + Bytes64(32)) {
        if (*data.running_ != RunningState::RUNNING) break;

        const FatDirStruct *dir = buffer.ptr_to<FatDirStruct>(index);

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
            const auto l_dir = buffer.ptr_to<FatLongNameDirStruct>(index);

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
            gui->show_always(L"%u.\tError: long filename is out of sync");
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
        const auto item = new FileNode();

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
        item->bytes_ = Bytes64(dir->dir_file_size_);

        if (data.disk_.type_ == DiskType::FAT32) {
            start_cluster = Clusters64(MAKELONG(dir->dir_fst_clus_lo_, dir->dir_fst_clus_hi_));
        } else {
            start_cluster = Clusters64(dir->dir_fst_clus_lo_);
        }

        make_fragment_list(data, disk_info, item, start_cluster);

        item->creation_time_ = convert_time(dir->dir_crt_date_, dir->dir_crt_time_, dir->dir_crt_time_tenth_);
        item->mft_change_time_ = convert_time(dir->dir_wrt_date_, dir->dir_wrt_time_, 0);
        item->last_access_time_ = convert_time(dir->dir_lst_acc_date_, 0, 0);
        item->parent_inode_ = {};
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
        Tree::insert(data.item_tree_, data.balance_count_, item);

        // Draw the item on the screen
        gui->show_analyze(data, item);
        defrag_lib_->colorize_disk_item(data, item, Clusters64(0), Clusters64(0), false);

        // Increment counters
        if (item->is_dir_) {
            data.count_directories_++;
        }

        data.count_all_files_++;
        data.count_all_bytes_ += item->bytes_;
        data.count_all_clusters_ += item->clusters_count_;

        if (DefragRunner::get_fragment_count(item) > 1) {
            data.count_fragmented_items_++;
            data.count_fragmented_bytes_ += item->bytes_;
            data.count_fragmented_clusters_ += item->clusters_count_;
        }

        // If this is a directory then iterate
        if (item->is_dir_) {
            Bytes64 sub_dir_length;
            auto sub_dir_buf = load_directory(data, disk_info, start_cluster, PARAM_OUT sub_dir_length);
            auto memv = UniquePtrSlice::make(std::move(sub_dir_buf), sub_dir_length);

            analyze_fat_directory(data, disk_info, memv.as_slice(), item);

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Finished with subdirectory.");
        }
    }
}

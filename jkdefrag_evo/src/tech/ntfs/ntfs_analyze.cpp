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

// Load the MFT into a list of ItemStruct records in memory
// Expensive call (can reach 1 minute runtime or more)
bool ScanNTFS::analyze_ntfs_volume(DefragState &data) {
    DefragGui *gui = DefragGui::get_instance();

    MemReader<uint8_t> buff(std::make_unique<uint8_t[]>(MFT_BUFFER_SIZE), MFT_BUFFER_SIZE);

    if (!analyze_ntfs_volume_read_bootblock(data, buff)) {
        return false;
    }

    // Extract data from the bootblock
    NtfsDiskInfoStruct disk_info{};

    data.disk_.type_ = DiskType::NTFS;
    disk_info.bytes_per_sector_ = buff.read<USHORT>(11);

    // Still to do: check for impossible values
    disk_info.sectors_per_cluster_ = buff.read<uint8_t>(13);
    disk_info.total_sectors_ = buff.read<ULONGLONG>(40);
    disk_info.mft_start_lcn_ = buff.read<ULONGLONG>(48);
    disk_info.mft2_start_lcn_ = buff.read<ULONGLONG>(56);

    auto clusters_per_mft_record = buff.read<ULONG>(64);

    if (clusters_per_mft_record >= 128) {
        disk_info.bytes_per_mft_record_ = (uint64_t) 1 << (256 - clusters_per_mft_record);
    } else {
        disk_info.bytes_per_mft_record_ =
                clusters_per_mft_record * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_;
    }

    disk_info.clusters_per_index_record_ = buff.read<ULONG>(68);

    data.bytes_per_cluster_ = disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_;

    if (disk_info.sectors_per_cluster_ > 0) {
        data.total_clusters_ = disk_info.total_sectors_ / disk_info.sectors_per_cluster_;
    }

    gui->show_always(std::format(
            L"This is an NTFS disk."
            L"\n  Disk cookie: {:x}"
            L"\n  BytesPerSector: " NUM_FMT
            L"\n  TotalSectors: " NUM_FMT
            L"\n  SectorsPerCluster: " NUM_FMT
            L"\n  SectorsPerTrack: " NUM_FMT
            L"\n  NumberOfHeads: " NUM_FMT
            L"\n  MftStartLcn: " NUM_FMT
            L"\n  Mft2StartLcn: " NUM_FMT
            L"\n  BytesPerMftRecord: " NUM_FMT
            L"\n  ClustersPerIndexRecord: " NUM_FMT
            L"\n  MediaType: {:x}"
            L"\n  VolumeSerialNumber: {:x}",
            buff.read<ULONGLONG>(3), disk_info.bytes_per_sector_, disk_info.total_sectors_,
            disk_info.sectors_per_cluster_, buff.read<USHORT>(24), buff.read<USHORT>(26), disk_info.mft_start_lcn_,
            disk_info.mft2_start_lcn_, disk_info.bytes_per_mft_record_, disk_info.clusters_per_index_record_,
            buff.read<uint8_t>(21), buff.read<ULONGLONG>(72)));

    // Calculate the size of first 16 Inodes in the MFT. The Microsoft defragmentation
    // API cannot move these inodes
    data.disk_.mft_locked_clusters_ = disk_info.bytes_per_sector_
                                      * disk_info.sectors_per_cluster_
                                      / disk_info.bytes_per_mft_record_;

    analyze_ntfs_volume_read_mft(data, disk_info, buff);

    FileFragment *mft_bitmap_fragments = nullptr;
    uint64_t mft_bitmap_bytes = 0;
    uint64_t mft_data_bytes = 0;
    FileFragment *mft_data_fragments = nullptr;

    if (!analyze_ntfs_volume_extract_mft(data, disk_info, buff, PARAM_OUT mft_bitmap_fragments,
                                         PARAM_OUT mft_bitmap_bytes, PARAM_OUT mft_data_bytes,
                                         PARAM_OUT mft_data_fragments)) {
        return false;
    }

    // Read the complete $MFT::$BITMAP into memory.
    // Note: The allocated size of the bitmap is a multiple of the cluster size. This
    // is only to make it easier to read the fragments, the extra bytes are not used
    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Reading $MFT::$BITMAP into memory");

    uint64_t vcn = 0;
    uint64_t max_mft_bitmap_bytes = 0;

    // FragmentListStruct *fragment;
    for (auto fragment = mft_bitmap_fragments; fragment != nullptr; fragment = fragment->next_) {
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            max_mft_bitmap_bytes = max_mft_bitmap_bytes +
                                   (fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ *
                                   disk_info.sectors_per_cluster_;
        }

        vcn = fragment->next_vcn_;
    }

    if (max_mft_bitmap_bytes < mft_bitmap_bytes) max_mft_bitmap_bytes = (size_t) mft_bitmap_bytes;

    auto mft_bitmap = std::make_unique<BYTE[]>(max_mft_bitmap_bytes);
    std::memset(mft_bitmap.get(), 0, (size_t) mft_bitmap_bytes);

    vcn = 0;
    uint64_t real_vcn = 0;

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Reading $MFT::$BITMAP into memory");

    // Follow the chain of MFT bitmap fragments
    for (auto fragment = mft_bitmap_fragments; fragment != nullptr; fragment = fragment->next_) {
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"  Extent Lcn=" NUM_FMT ", RealVcn=" NUM_FMT ", Size=" NUM_FMT,
                                        fragment->lcn_, real_vcn, fragment->next_vcn_ - vcn));

            ULARGE_INTEGER trans;
            trans.QuadPart = fragment->lcn_ * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_;

            OVERLAPPED overlapped = {
                    .Offset = trans.LowPart,
                    .OffsetHigh = trans.HighPart,
                    .hEvent = nullptr
            };

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"    Reading " NUM_FMT " clusters (" NUM_FMT " bytes) from LCN=" NUM_FMT,
                                        fragment->next_vcn_ - vcn,
                                        (fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ *
                                        disk_info.sectors_per_cluster_, fragment->lcn_));

            DWORD bytes_read;
            auto result = ReadFile(data.disk_.volume_handle_,
                                   &mft_bitmap[real_vcn * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_],
                                   (uint32_t) ((fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ * disk_info.
                                           sectors_per_cluster_),
                                   &bytes_read, &overlapped);

            if (result == 0 || bytes_read != (fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ * disk_info.
                    sectors_per_cluster_) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                std::format(L"  {}", Str::system_error(GetLastError())));
                Tree::delete_tree(data.item_tree_);
                data.item_tree_ = nullptr;
                return false;
            }

            real_vcn = real_vcn + fragment->next_vcn_ - vcn;
        }

        vcn = fragment->next_vcn_;
    }

    // Construct an array of all the items in memory, indexed by Inode.
    // Note: the maximum number of Inodes is primarily determined by the size of the
    // bitmap. But that is rounded up to 8 Inodes, and the MFT can be shorter.
    uint64_t max_inode = mft_bitmap_bytes * 8;

    if (max_inode > mft_data_bytes / disk_info.bytes_per_mft_record_) {
        max_inode = mft_data_bytes / disk_info.bytes_per_mft_record_;
    }

    auto inode_array = std::make_unique<FileNode *[]>(max_inode);
    inode_array[0] = data.item_tree_;
    std::fill(inode_array.get() + 1, inode_array.get() + max_inode, nullptr);

    // Read and process all the records in the MFT. The records are read into a
    // buffer and then given one by one to the interpret_mft_record() subroutine.
    FileFragment *fragment = mft_data_fragments;
    vcn = 0;
    real_vcn = 0;

    data.clusters_done_ = 0;
    data.phase_todo_ = 0;

    Clock::time_point start_time = Clock::now();

    static const BYTE bitmap_masks[8] = {1, 2, 4, 8, 16, 32, 64, 128};
    for (auto inode_number = 1; inode_number < max_inode; inode_number++) {
        if ((mft_bitmap[inode_number >> 3] & bitmap_masks[inode_number % 8]) == 0) continue;

        data.phase_todo_ += 1;
    }

    uint64_t block_start;
    uint64_t block_end = 0;

    // For all inodes, while we are in the RUNNING state
    for (auto inode_number = 1; inode_number < max_inode; inode_number++) {
        if (*data.running_ != RunningState::RUNNING) break;

        // Ignore the Inode if the bitmap says it's not in use
        if ((mft_bitmap[inode_number >> 3] & bitmap_masks[inode_number % 8]) == 0) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"Inode " NUM_FMT " is not in use.", inode_number));
            continue;
        }

        // Update the progress counter
        data.clusters_done_ += 1;

        // Read a block of inode's into memory
        if (inode_number >= block_end) {
            // Slow the program down to the percentage that was specified on the command line
            DefragRunner::slow_down(data);

            block_start = inode_number;
            block_end = block_start + MFT_BUFFER_SIZE / disk_info.bytes_per_mft_record_;

            if (block_end > mft_bitmap_bytes * 8) block_end = mft_bitmap_bytes * 8;

            uint64_t u1;

            while (fragment != nullptr) {
                // Calculate Inode at the end of the fragment
                u1 = (real_vcn + fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ *
                     disk_info.sectors_per_cluster_ / disk_info.bytes_per_mft_record_;

                if (u1 > inode_number) break;

                do {
                    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Skipping to next extent");

                    if (fragment->lcn_ != VIRTUALFRAGMENT) real_vcn = real_vcn + fragment->next_vcn_ - vcn;

                    vcn = fragment->next_vcn_;
                    fragment = fragment->next_;

                    if (fragment == nullptr) break;
                } while (fragment->lcn_ == VIRTUALFRAGMENT);

                gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
                        L"  Extent Lcn=" NUM_FMT ", RealVcn=" NUM_FMT ", Size=" NUM_FMT,
                        fragment->lcn_, real_vcn, fragment->next_vcn_ - vcn));
            }

            if (fragment == nullptr) break;
            if (block_end >= u1) block_end = u1;

            ULARGE_INTEGER trans;
            trans.QuadPart = (fragment->lcn_ - real_vcn) * disk_info.bytes_per_sector_ *
                             disk_info.sectors_per_cluster_ + block_start * disk_info.bytes_per_mft_record_;

            OVERLAPPED overlapped{
                    .Offset = trans.LowPart,
                    .OffsetHigh = trans.HighPart,
                    .hEvent = nullptr
            };

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(
                                    L"Reading block of " NUM_FMT " Inodes from MFT into memory, " NUM_FMT " bytes from LCN=" NUM_FMT,
                                    block_end - block_start,
                                    (block_end - block_start) * disk_info.bytes_per_mft_record_,
                                    trans.QuadPart / (disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_)));

            DWORD bytes_read;
            auto result = ReadFile(data.disk_.volume_handle_, buff.get(),
                                   (uint32_t) ((block_end - block_start) * disk_info.bytes_per_mft_record_),
                                   &bytes_read,
                                   &overlapped);

            if (result == 0 || bytes_read != (block_end - block_start) * disk_info.bytes_per_mft_record_) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                std::format(L"Error while reading Inodes " NUM_FMT " to " NUM_FMT ": reason {}",
                                            inode_number, block_end - 1,
                                            Str::system_error(GetLastError())));

                Tree::delete_tree(data.item_tree_);
                data.item_tree_ = nullptr;
                return FALSE;
            }
        }

        // Fixup the raw data of this Inode
        const auto raw_mftdata_offset = (inode_number - block_start) * disk_info.bytes_per_mft_record_;
        if (fixup_raw_mftdata(data, &disk_info, buff.get() + raw_mftdata_offset,
                              disk_info.bytes_per_mft_record_) == FALSE) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"The error occurred while processing Inode %I64u (max " NUM_FMT ")",
                                        inode_number, max_inode));

            continue;
        }

        // Interpret the Inode's attributes
        // auto result =
        interpret_mft_record(data, &disk_info, inode_array.get(), inode_number, max_inode,
                             PARAM_OUT mft_data_fragments, PARAM_OUT mft_data_bytes,
                             PARAM_OUT mft_bitmap_fragments, PARAM_OUT mft_bitmap_bytes,
                             buff.get() + raw_mftdata_offset,
                             disk_info.bytes_per_mft_record_);
    }

    Clock::time_point end_time = Clock::now();

    if (end_time > start_time) {
        auto diff_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        gui->show_debug(DebugLevel::Progress, nullptr,
                        std::format(L"  Analysis speed: " NUM_FMT " items per second",
                                    max_inode * 1000 / diff_ms));
    }

    if (*data.running_ != RunningState::RUNNING) {
        Tree::delete_tree(data.item_tree_);
        data.item_tree_ = nullptr;
        return false;
    }

    // Setup the ParentDirectory in all the items with the info in the InodeArray
    for (auto item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
        item->parent_directory_ = inode_array[item->parent_inode_];

        if (item->parent_inode_ == 5) item->parent_directory_ = nullptr;
    }

    return true;
}

// Read the boot block from the disk
bool ScanNTFS::analyze_ntfs_volume_read_bootblock(DefragState &data, MemReader<uint8_t> &buff) {
    StopWatch clock(L"NTFS: read bootblock");

    DefragGui *gui = DefragGui::get_instance();
    OVERLAPPED overlapped{
            .Offset = 0,
            .OffsetHigh = 0,
            .hEvent = nullptr,
    };

    DWORD bytes_read;
    auto result = ReadFile(data.disk_.volume_handle_, buff.get(), (uint32_t) 512,
                           &bytes_read, &overlapped);

    if (result == FALSE || bytes_read != 512) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        std::format(L"Error while reading bootblock: {}",
                                    Str::system_error(GetLastError())));
        return false;
    }

    // Test if the boot block is an NTFS boot block
    constexpr long long int NTFS_BOOT_BLOCK_PASTRY = 0x202020205346544E;

    if (buff.read<ULONGLONG>(3) != NTFS_BOOT_BLOCK_PASTRY) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"This is not an NTFS disk (different cookie).");
        return false;
    }

    return true;
}

// Read the $MFT record from disk into memory, which is always the first record in the MFT
bool
ScanNTFS::analyze_ntfs_volume_read_mft(DefragState &data, NtfsDiskInfoStruct &disk_info, MemReader<uint8_t> &buff) {
    DefragGui *gui = DefragGui::get_instance();
    ULARGE_INTEGER trans;
    trans.QuadPart = disk_info.mft_start_lcn_ * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_;

    OVERLAPPED overlapped{
            .Offset = 0,
            .OffsetHigh = 0,
            .hEvent = nullptr,
    };
    DWORD bytes_read;

    overlapped.Offset = trans.LowPart;
    overlapped.OffsetHigh = trans.HighPart;
    overlapped.hEvent = nullptr;
    auto result = ReadFile(data.disk_.volume_handle_, buff.get(),
                           (uint32_t) disk_info.bytes_per_mft_record_, &bytes_read,
                           &overlapped);

    if (result == 0 || bytes_read != disk_info.bytes_per_mft_record_) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        std::format(L"Error while reading first MFT record: {}",
                                    Str::system_error(GetLastError())));
        return false;
    }

    // Fixup the raw data from disk. This will also test if it's a valid $MFT record
    if (fixup_raw_mftdata(data, &disk_info, buff.get(), disk_info.bytes_per_mft_record_) == FALSE) {
        return false;
    }

    return true;
}

// Extract data from the MFT record and put into an Item struct in memory. If there was an error then exit
bool
ScanNTFS::analyze_ntfs_volume_extract_mft(DefragState &data, NtfsDiskInfoStruct &disk_info, MemReader<uint8_t> &buff,
                                          PARAM_OUT FileFragment *&mft_bitmap_fragments,
                                          PARAM_OUT uint64_t &mft_bitmap_bytes, PARAM_OUT uint64_t &mft_data_bytes,
                                          PARAM_OUT FileFragment *&mft_data_fragments) {
    DefragGui *gui = DefragGui::get_instance();

    auto result = interpret_mft_record(data, &disk_info, nullptr, 0, 0,
                                       PARAM_OUT mft_data_fragments, PARAM_OUT mft_data_bytes,
                                       PARAM_OUT mft_bitmap_fragments, PARAM_OUT mft_bitmap_bytes,
                                       buff.get(), disk_info.bytes_per_mft_record_);

    if (!result ||
        mft_data_fragments == nullptr || mft_data_bytes == 0 ||
        mft_bitmap_fragments == nullptr || mft_bitmap_bytes == 0) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"Fatal error, cannot process this disk.");
        Tree::delete_tree(data.item_tree_);
        data.item_tree_ = nullptr;
        return false;
    }

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                    std::format(L"MftDataBytes = " NUM_FMT ", MftBitmapBytes = " NUM_FMT,
                                mft_data_bytes, mft_bitmap_bytes));
    return true;
}

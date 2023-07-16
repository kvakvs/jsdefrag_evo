#include "precompiled_header.h"

// Load the MFT into a list of ItemStruct records in memory
bool ScanNTFS::analyze_ntfs_volume(DefragDataStruct *data) {
    NtfsDiskInfoStruct disk_info{};
    std::unique_ptr<BYTE[]> buffer;
    OVERLAPPED g_overlapped;
    DWORD bytes_read;
    FragmentListStruct *mft_data_fragments;
    uint64_t mft_data_bytes;
    FragmentListStruct *mft_bitmap_fragments;
    uint64_t mft_bitmap_bytes;
    uint64_t max_mft_bitmap_bytes;
    std::unique_ptr<BYTE[]> mft_bitmap;
    FragmentListStruct *fragment;
    std::unique_ptr<ItemStruct *[]> inode_array;
    uint64_t max_inode;
    ItemStruct *item;
    uint64_t vcn;
    uint64_t real_vcn;
    uint64_t inode_number;
    uint64_t block_start;
    uint64_t block_end;
    BYTE bitmap_masks[8] = {1, 2, 4, 8, 16, 32, 64, 128};
    bool result;
    ULONG clusters_per_mft_record;
    __timeb64 time{};
    milli64_t start_time;
    milli64_t end_time;
    wchar_t s1[BUFSIZ];
    uint64_t u1;
    DefragGui *gui = DefragGui::get_instance();

    // Read the boot block from the disk
    buffer = std::make_unique<BYTE[]>(mftbuffersize);

    g_overlapped.Offset = 0;
    g_overlapped.OffsetHigh = 0;
    g_overlapped.hEvent = nullptr;

    result = ReadFile(data->disk_.volume_handle_, buffer.get(), (uint32_t) 512, &bytes_read, &g_overlapped);

    if (result == 0 || bytes_read != 512) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        std::format(L"Error while reading bootblock: {}", DefragLib::system_error_str(GetLastError())));
        return false;
    }

    // Test if the boot block is an NTFS boot block
    constexpr long long int NTFS_BOOT_BLOCK_PASTRY = 0x202020205346544E;

    if (*(ULONGLONG *) &buffer.get()[3] != NTFS_BOOT_BLOCK_PASTRY) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"This is not an NTFS disk (different cookie).");
        return false;
    }

    // Extract data from the bootblock
    data->disk_.type_ = DiskType::NTFS;
    disk_info.bytes_per_sector_ = *(USHORT *) &buffer[11];

    // Still to do: check for impossible values
    disk_info.sectors_per_cluster_ = buffer[13];
    disk_info.total_sectors_ = *(ULONGLONG *) &buffer[40];
    disk_info.mft_start_lcn_ = *(ULONGLONG *) &buffer[48];
    disk_info.mft2_start_lcn_ = *(ULONGLONG *) &buffer[56];
    clusters_per_mft_record = *(ULONG *) &buffer[64];

    if (clusters_per_mft_record >= 128) {
        // TODO: Bug with << 256 here
        disk_info.bytes_per_mft_record_ = (uint64_t) 1 << 256 - clusters_per_mft_record;
    } else {
        disk_info.bytes_per_mft_record_ = clusters_per_mft_record * disk_info.bytes_per_sector_ * disk_info.
                sectors_per_cluster_;
    }

    disk_info.clusters_per_index_record_ = *(ULONG *) &buffer[68];

    data->bytes_per_cluster_ = disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_;

    if (disk_info.sectors_per_cluster_ > 0) {
        data->total_clusters_ = disk_info.total_sectors_ / disk_info.sectors_per_cluster_;
    }

    gui->show_debug(DebugLevel::Fatal, nullptr, std::format(
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
            *(ULONGLONG *) &buffer[3], disk_info.bytes_per_sector_, disk_info.total_sectors_,
            disk_info.sectors_per_cluster_, *(USHORT *) &buffer[24], *(USHORT *) &buffer[26], disk_info.mft_start_lcn_,
            disk_info.mft2_start_lcn_, disk_info.bytes_per_mft_record_, disk_info.clusters_per_index_record_,
            buffer[21], *(ULONGLONG *) &buffer[72]));

    /* Calculate the size of first 16 Inodes in the MFT. The Microsoft defragmentation
    API cannot move these inodes. */
    data->disk_.mft_locked_clusters_ = disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_ / disk_info.
            bytes_per_mft_record_;

    // Read the $MFT record from disk into memory, which is always the first record in the MFT
    ULARGE_INTEGER trans;
    trans.QuadPart = disk_info.mft_start_lcn_ * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_;
    g_overlapped.Offset = trans.LowPart;
    g_overlapped.OffsetHigh = trans.HighPart;
    g_overlapped.hEvent = nullptr;
    result = ReadFile(data->disk_.volume_handle_, buffer.get(),
                      (uint32_t) disk_info.bytes_per_mft_record_, &bytes_read,
                      &g_overlapped);

    if (result == 0 || bytes_read != disk_info.bytes_per_mft_record_) {
        gui->show_debug(DebugLevel::Progress, nullptr, std::format(L"Error while reading first MFT record: {}",
                                                                   DefragLib::system_error_str(GetLastError())));
        return false;
    }

    // Fixup the raw data from disk. This will also test if it's a valid $MFT record
    if (fixup_raw_mftdata(data, &disk_info, buffer.get(), disk_info.bytes_per_mft_record_) == FALSE) {
        return false;
    }

    // Extract data from the MFT record and put into an Item struct in memory. If there was an error then exit
    mft_data_bytes = 0;
    mft_data_fragments = nullptr;
    mft_bitmap_bytes = 0;
    mft_bitmap_fragments = nullptr;

    result = interpret_mft_record(data, &disk_info, nullptr, 0, 0,
                                  &mft_data_fragments, &mft_data_bytes, &mft_bitmap_fragments, &mft_bitmap_bytes,
                                  buffer.get(), disk_info.bytes_per_mft_record_);

    if (!result ||
        mft_data_fragments == nullptr || mft_data_bytes == 0 ||
        mft_bitmap_fragments == nullptr || mft_bitmap_bytes == 0) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"Fatal error, cannot process this disk.");
        Tree::delete_tree(data->item_tree_);
        data->item_tree_ = nullptr;
        return false;
    }

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                    std::format(L"MftDataBytes = " NUM_FMT ", MftBitmapBytes = " NUM_FMT,
                                mft_data_bytes, mft_bitmap_bytes));

    /* Read the complete $MFT::$BITMAP into memory.
    Note: The allocated size of the bitmap is a multiple of the cluster size. This
    is only to make it easier to read the fragments, the extra bytes are not used. */
    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Reading $MFT::$BITMAP into memory");

    vcn = 0;
    max_mft_bitmap_bytes = 0;

    for (fragment = mft_bitmap_fragments; fragment != nullptr; fragment = fragment->next_) {
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            max_mft_bitmap_bytes = max_mft_bitmap_bytes +
                                   (fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ *
                                   disk_info.sectors_per_cluster_;
        }

        vcn = fragment->next_vcn_;
    }

    if (max_mft_bitmap_bytes < mft_bitmap_bytes) max_mft_bitmap_bytes = (size_t) mft_bitmap_bytes;

    mft_bitmap = std::make_unique<BYTE[]>(max_mft_bitmap_bytes);
    std::memset(mft_bitmap.get(), 0, (size_t) mft_bitmap_bytes);

    vcn = 0;
    real_vcn = 0;

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Reading $MFT::$BITMAP into memory");

    for (fragment = mft_bitmap_fragments; fragment != nullptr; fragment = fragment->next_) {
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"  Extent Lcn=" NUM_FMT ", RealVcn=" NUM_FMT ", Size=" NUM_FMT,
                                        fragment->lcn_, real_vcn, fragment->next_vcn_ - vcn));

            trans.QuadPart = fragment->lcn_ * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_;

            g_overlapped.Offset = trans.LowPart;
            g_overlapped.OffsetHigh = trans.HighPart;
            g_overlapped.hEvent = nullptr;

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"    Reading " NUM_FMT " clusters (" NUM_FMT " bytes) from LCN=" NUM_FMT,
                                        fragment->next_vcn_ - vcn,
                                        (fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ *
                                        disk_info.sectors_per_cluster_, fragment->lcn_));

            result = ReadFile(data->disk_.volume_handle_,
                              &mft_bitmap[real_vcn * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_],
                              (uint32_t) ((fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ * disk_info.
                                      sectors_per_cluster_),
                              &bytes_read, &g_overlapped);

            if (result == 0 || bytes_read != (fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ * disk_info.
                    sectors_per_cluster_) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                std::format(L"  {}", DefragLib::system_error_str(GetLastError())));
                Tree::delete_tree(data->item_tree_);
                data->item_tree_ = nullptr;
                return false;
            }

            real_vcn = real_vcn + fragment->next_vcn_ - vcn;
        }

        vcn = fragment->next_vcn_;
    }

    /* Construct an array of all the items in memory, indexed by Inode.
    Note: the maximum number of Inodes is primarily determined by the size of the
    bitmap. But that is rounded up to 8 Inodes, and the MFT can be shorter. */
    max_inode = mft_bitmap_bytes * 8;

    if (max_inode > mft_data_bytes / disk_info.bytes_per_mft_record_) {
        max_inode = mft_data_bytes / disk_info.bytes_per_mft_record_;
    }

    inode_array = std::make_unique<ItemStruct *[]>(max_inode);
    inode_array[0] = data->item_tree_;
    std::fill(inode_array.get() + 1, inode_array.get() + max_inode, nullptr);

    // Read and process all the records in the MFT. The records are read into a
    // buffer and then given one by one to the interpret_mft_record() subroutine.
    fragment = mft_data_fragments;
    block_end = 0;
    vcn = 0;
    real_vcn = 0;

    data->phase_done_ = 0;
    data->phase_todo_ = 0;

    _ftime64_s(&time);

    start_time = std::chrono::milliseconds(time.time * 1000 + time.millitm);

    for (inode_number = 1; inode_number < max_inode; inode_number++) {
        if ((mft_bitmap[inode_number >> 3] & bitmap_masks[inode_number % 8]) == 0) continue;

        data->phase_todo_ = data->phase_todo_ + 1;
    }

    for (inode_number = 1; inode_number < max_inode; inode_number++) {
        if (*data->running_ != RunningState::RUNNING) break;

        // Ignore the Inode if the bitmap says it's not in use
        if ((mft_bitmap[inode_number >> 3] & bitmap_masks[inode_number % 8]) == 0) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"Inode " NUM_FMT " is not in use.", inode_number));
            continue;
        }

        // Update the progress counter
        data->phase_done_ = data->phase_done_ + 1;

        // Read a block of inode's into memory
        if (inode_number >= block_end) {
            // Slow the program down to the percentage that was specified on the command line
            DefragLib::slow_down(data);

            block_start = inode_number;
            block_end = block_start + mftbuffersize / disk_info.bytes_per_mft_record_;

            if (block_end > mft_bitmap_bytes * 8) block_end = mft_bitmap_bytes * 8;

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

            trans.QuadPart = (fragment->lcn_ - real_vcn) * disk_info.bytes_per_sector_ *
                             disk_info.sectors_per_cluster_ + block_start * disk_info.bytes_per_mft_record_;

            g_overlapped.Offset = trans.LowPart;
            g_overlapped.OffsetHigh = trans.HighPart;
            g_overlapped.hEvent = nullptr;

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(
                                    L"Reading block of " NUM_FMT " Inodes from MFT into memory, " NUM_FMT " bytes from LCN=" NUM_FMT,
                                    block_end - block_start,
                                    (block_end - block_start) * disk_info.bytes_per_mft_record_,
                                    trans.QuadPart / (disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_)));

            result = ReadFile(data->disk_.volume_handle_, buffer.get(),
                              (uint32_t) ((block_end - block_start) * disk_info.bytes_per_mft_record_), &bytes_read,
                              &g_overlapped);

            if (result == 0 || bytes_read != (block_end - block_start) * disk_info.bytes_per_mft_record_) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                std::format(L"Error while reading Inodes " NUM_FMT " to " NUM_FMT ": reason {}",
                                            inode_number, block_end - 1, DefragLib::system_error_str(GetLastError())));

                Tree::delete_tree(data->item_tree_);
                data->item_tree_ = nullptr;
                return FALSE;
            }
        }

        // Fixup the raw data of this Inode
        if (fixup_raw_mftdata(data, &disk_info, &buffer[(inode_number - block_start) * disk_info.bytes_per_mft_record_],
                              disk_info.bytes_per_mft_record_) == FALSE) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"The error occurred while processing Inode %I64u (max " NUM_FMT ")",
                                        inode_number, max_inode));

            continue;
        }

        // Interpret the Inode's attributes
        result = interpret_mft_record(data, &disk_info, inode_array.get(), inode_number, max_inode,
                                      &mft_data_fragments, &mft_data_bytes, &mft_bitmap_fragments, &mft_bitmap_bytes,
                                      &buffer[(inode_number - block_start) * disk_info.bytes_per_mft_record_],
                                      disk_info.bytes_per_mft_record_);
    }

    _ftime64_s(&time);
    end_time = std::chrono::milliseconds(time.time * 1000 + time.millitm);

    if (end_time > start_time) {
        gui->show_debug(DebugLevel::Progress, nullptr, std::format(L"  Analysis speed: " NUM_FMT " items per second",
                                                                   max_inode * 1000 / (end_time - start_time).count()));
    }

    if (*data->running_ != RunningState::RUNNING) {
        Tree::delete_tree(data->item_tree_);
        data->item_tree_ = nullptr;
        return false;
    }

    // Setup the ParentDirectory in all the items with the info in the InodeArray
    for (item = Tree::smallest(data->item_tree_); item != nullptr; item = Tree::next(item)) {
        item->parent_directory_ = inode_array[item->parent_inode_];

        if (item->parent_inode_ == 5) item->parent_directory_ = nullptr;
    }

    return true;
}
#include "precompiled_header.h"

#include <time_util.h>

/*
Calculate the begin of the 3 zones.
Unmovable files pose an interesting problem. Suppose an unmovable file is in
zone 1, then the calculation for the beginning of zone 2 must count that file.
But that changes the beginning of zone 2. Some unmovable files may now suddenly
be in another zone. So we have to recalculate, which causes another border
change, and again, and again....
Note: the program only knows if a file is unmovable after it has tried to move a
file. So we have to recalculate the beginning of the zones every time we encounter
an unmovable file.
*/
void DefragLib::calculate_zones(DefragDataStruct *data) {
    ItemStruct *item;
    uint64_t size_of_movable_files[3];
    uint64_t size_of_unmovable_fragments[3];
    uint64_t zone_end[3];
    uint64_t old_zone_end[3];
    int zone;
    int i;
    DefragGui *gui = DefragGui::get_instance();

    /* Calculate the number of clusters in movable items for every zone. */
    for (zone = 0; zone <= 2; zone++) size_of_movable_files[zone] = 0;

    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;
        if (item->is_dir_ && data->cannot_move_dirs_ > 20) continue;

        zone = 1;

        if (item->is_hog_) zone = 2;
        if (item->is_dir_) zone = 0;

        size_of_movable_files[zone] = size_of_movable_files[zone] + item->clusters_count_;
    }

    /* Iterate until the calculation does not change anymore, max 10 times. */
    for (zone = 0; zone <= 2; zone++) size_of_unmovable_fragments[zone] = 0;

    for (zone = 0; zone <= 2; zone++) old_zone_end[zone] = 0;

    for (int iterate = 1; iterate <= 10; iterate++) {
        // Calculate the end of the zones
        zone_end[0] = size_of_movable_files[0] + size_of_unmovable_fragments[0] +
                      (uint64_t) (data->total_clusters_ * data->free_space_ / 100.0);

        zone_end[1] = zone_end[0] + size_of_movable_files[1] + size_of_unmovable_fragments[1] +
                      (uint64_t) (data->total_clusters_ * data->free_space_ / 100.0);

        zone_end[2] = zone_end[1] + size_of_movable_files[2] + size_of_unmovable_fragments[2];

        /* Exit if there was no change. */
        if (old_zone_end[0] == zone_end[0] &&
            old_zone_end[1] == zone_end[1] &&
            old_zone_end[2] == zone_end[2]) {
            break;
        }

        for (zone = 0; zone <= 2; zone++) old_zone_end[zone] = zone_end[zone];

        /* Show debug info. */
        gui->show_debug(DebugLevel::DetailedFileInfo, nullptr,
                        std::format(
                                L"Zone calculation, iteration {}: 0 - " NUM_FMT " - " NUM_FMT " - " NUM_FMT,
                                iterate, zone_end[0], zone_end[1], zone_end[2]));

        /* Reset the SizeOfUnmovableFragments array. We are going to (re)calculate these numbers
        based on the just calculates ZoneEnd's. */
        for (zone = 0; zone <= 2; zone++) size_of_unmovable_fragments[zone] = 0;

        /* The MFT reserved areas are counted as unmovable data. */
        for (i = 0; i < 3; i++) {
            if (data->mft_excludes_[i].start_ < zone_end[0]) {
                size_of_unmovable_fragments[0] = size_of_unmovable_fragments[0]
                                                 + data->mft_excludes_[i].end_ - data->mft_excludes_[i].start_;
            } else if (data->mft_excludes_[i].start_ < zone_end[1]) {
                size_of_unmovable_fragments[1] = size_of_unmovable_fragments[1]
                                                 + data->mft_excludes_[i].end_ - data->mft_excludes_[i].start_;
            } else if (data->mft_excludes_[i].start_ < zone_end[2]) {
                size_of_unmovable_fragments[2] = size_of_unmovable_fragments[2]
                                                 + data->mft_excludes_[i].end_ - data->mft_excludes_[i].start_;
            }
        }

        /* Walk through all items and count the unmovable fragments. Ignore unmovable fragments
        in the MFT zones, we have already counted the zones. */
        for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
            if (!item->is_unmovable_ &&
                !item->is_excluded_ &&
                (!item->is_dir_ || data->cannot_move_dirs_ <= 20))
                continue;

            uint64_t vcn = 0;
            uint64_t real_vcn = 0;

            for (const FragmentListStruct *Fragment = item->fragments_; Fragment != nullptr; Fragment = Fragment->
                    next_) {
                if (Fragment->lcn_ != VIRTUALFRAGMENT) {
                    if ((Fragment->lcn_ < data->mft_excludes_[0].start_ || Fragment->lcn_ >= data->mft_excludes_[0].
                            end_)
                        &&
                        (Fragment->lcn_ < data->mft_excludes_[1].start_ || Fragment->lcn_ >= data->mft_excludes_[1].
                                end_)
                        &&
                        (Fragment->lcn_ < data->mft_excludes_[2].start_ || Fragment->lcn_ >= data->mft_excludes_[2].
                                end_)) {
                        if (Fragment->lcn_ < zone_end[0]) {
                            size_of_unmovable_fragments[0] = size_of_unmovable_fragments[0] + Fragment->next_vcn_ - vcn;
                        } else if (Fragment->lcn_ < zone_end[1]) {
                            size_of_unmovable_fragments[1] = size_of_unmovable_fragments[1] + Fragment->next_vcn_ - vcn;
                        } else if (Fragment->lcn_ < zone_end[2]) {
                            size_of_unmovable_fragments[2] = size_of_unmovable_fragments[2] + Fragment->next_vcn_ - vcn;
                        }
                    }

                    real_vcn = real_vcn + Fragment->next_vcn_ - vcn;
                }

                vcn = Fragment->next_vcn_;
            }
        }
    }

    /* Calculated the begin of the zones. */
    data->zones_[0] = 0;

    for (i = 1; i <= 3; i++) data->zones_[i] = zone_end[i - 1];
}

/* For debugging only: compare the data with the output from the
FSCTL_GET_RETRIEVAL_POINTERS function call.
Note: Reparse points will usually be flagged as different. A reparse point is
a symbolic link. The CreateFile call will resolve the symbolic link and retrieve
the info from the real item, but the MFT contains the info from the symbolic
link. */
[[maybe_unused]] void DefragLib::compare_items([[maybe_unused]] DefragDataStruct *data, const ItemStruct *item) const {
    HANDLE file_handle;
    uint64_t clusters; /* Total number of clusters. */
    STARTING_VCN_INPUT_BUFFER retrieve_param;

    struct {
        uint32_t extent_count_;
        uint64_t starting_vcn_;

        struct {
            uint64_t next_vcn_;
            uint64_t lcn_;
        } extents_[1000];
    } extent_data{};

    BY_HANDLE_FILE_INFORMATION file_information;
    uint64_t vcn;
    FragmentListStruct *fragment;
    FragmentListStruct *last_fragment;
    uint32_t error_code;
    wchar_t error_string[BUFSIZ];
    int max_loop;
    ULARGE_INTEGER u;
    uint32_t i;
    DWORD w;
    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::Fatal, nullptr,
                    std::format(NUM_FMT L" {}", get_item_lcn(item), item->get_long_fn()));

    if (!item->is_dir_) {
        file_handle = CreateFileW(item->get_long_path(), FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
    } else {
        file_handle = CreateFileW(item->get_long_path(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    }

    if (file_handle == INVALID_HANDLE_VALUE) {
        system_error_str(GetLastError(), error_string, BUFSIZ);

        gui->show_debug(DebugLevel::Fatal, nullptr, std::format(L"  Could not open: {}", error_string));

        return;
    }

    /* Fetch the date/times of the file. */
    if (GetFileInformationByHandle(file_handle, &file_information) != 0) {
        u.LowPart = file_information.ftCreationTime.dwLowDateTime;
        u.HighPart = file_information.ftCreationTime.dwHighDateTime;

        if (item->creation_time_ != std::chrono::microseconds(u.QuadPart)) {
            auto diff = item->creation_time_.count() - u.QuadPart;
            gui->show_debug(
                    DebugLevel::Fatal, nullptr,
                    std::format(L"  Different CreationTime " NUM_FMT " <> " NUM_FMT " = " NUM_FMT,
                                item->creation_time_.count(), u.QuadPart, diff));
        }

        u.LowPart = file_information.ftLastAccessTime.dwLowDateTime;
        u.HighPart = file_information.ftLastAccessTime.dwHighDateTime;

        if (item->last_access_time_ != std::chrono::microseconds(u.QuadPart)) {
            auto diff = item->last_access_time_.count() - u.QuadPart;
            gui->show_debug(
                    DebugLevel::Fatal, nullptr,
                    std::format(L"  Different LastAccessTime " NUM_FMT " <> " NUM_FMT " = " NUM_FMT,
                                item->last_access_time_.count(), u.QuadPart, diff));
        }
    }

#ifdef jk
    Vcn = 0;
    for (fragment = Item->Fragments; fragment != nullptr; fragment = fragment->Next) {
        if (fragment->Lcn != VIRTUALFRAGMENT) {
            Data->ShowDebug(DebugLevel::Fatal,nullptr,L"  Extent 1: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u",
                fragment->Lcn,Vcn,fragment->NextVcn);
        } else {
            Data->ShowDebug(DebugLevel::Fatal,nullptr,L"  Extent 1 (virtual): Vcn=%I64u, NextVcn=%I64u",
                Vcn,fragment->NextVcn);
        }
        Vcn = fragment->NextVcn;
    }
#endif

    /* Ask Windows for the clustermap of the item and save it in memory.
    The buffer that is used to ask Windows for the clustermap has a
    fixed size, so we may have to loop a couple of times. */
    fragment = item->fragments_;
    clusters = 0;
    vcn = 0;
    max_loop = 1000;
    last_fragment = nullptr;

    do {
        /* I strongly suspect that the FSCTL_GET_RETRIEVAL_POINTERS system call
        can sometimes return an empty bitmap and ERROR_MORE_DATA. That's not
        very nice of Microsoft, because it causes an infinite loop. I've
        therefore added a loop counter that will limit the loop to 1000
        iterations. This means the defragger cannot handle files with more
        than 100000 fragments, though. */
        if (max_loop <= 0) {
            gui->show_debug(DebugLevel::Fatal, nullptr, L"  FSCTL_GET_RETRIEVAL_POINTERS error: Infinite loop");

            return;
        }

        max_loop = max_loop - 1;

        /* Ask Windows for the (next segment of the) clustermap of this file. If error
        then leave the loop. */
        retrieve_param.StartingVcn.QuadPart = vcn;

        error_code = DeviceIoControl(file_handle, FSCTL_GET_RETRIEVAL_POINTERS,
                                     &retrieve_param, sizeof retrieve_param, &extent_data, sizeof extent_data, &w,
                                     nullptr);

        if (error_code != 0) {
            error_code = NO_ERROR;
        } else {
            error_code = GetLastError();
        }

        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) break;

        /* Walk through the clustermap, count the total number of clusters, and
        save all fragments in memory. */
        for (i = 0; i < extent_data.extent_count_; i++) {
            /* Show debug message. */
#ifdef jk
            if (ExtentData.Extents[i].Lcn != VIRTUALFRAGMENT) {
                Data->ShowDebug(DebugLevel::Fatal,nullptr,L"  Extent 2: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u",
                    ExtentData.Extents[i].Lcn,Vcn,ExtentData.Extents[i].NextVcn);
            } else {
                Data->ShowDebug(DebugLevel::Fatal,nullptr,L"  Extent 2 (virtual): Vcn=%I64u, NextVcn=%I64u",
                    Vcn,ExtentData.Extents[i].NextVcn);
            }
#endif

            /* Add the size of the fragment to the total number of clusters.
            There are two kinds of fragments: real and virtual. The latter do not
            occupy clusters on disk, but are information used by compressed
            and sparse files. */
            if (extent_data.extents_[i].lcn_ != VIRTUALFRAGMENT) {
                clusters = clusters + extent_data.extents_[i].next_vcn_ - vcn;
            }

            /* Compare the fragment. */
            if (fragment == nullptr) {
                gui->show_debug(DebugLevel::Fatal, nullptr, L"  Extra fragment in FSCTL_GET_RETRIEVAL_POINTERS");
            } else {
                if (fragment->lcn_ != extent_data.extents_[i].lcn_) {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    std::format(L"  Different LCN in fragment: " NUM_FMT " <> " NUM_FMT,
                                                fragment->lcn_, extent_data.extents_[i].lcn_));
                }

                if (fragment->next_vcn_ != extent_data.extents_[i].next_vcn_) {
                    gui->show_debug(DebugLevel::Fatal, nullptr,
                                    std::format(L"  Different NextVcn in fragment: " NUM_FMT " <> " NUM_FMT,
                                                fragment->next_vcn_, extent_data.extents_[i].next_vcn_));
                }

                fragment = fragment->next_;
            }

            /* The Vcn of the next fragment is the NextVcn field in this record. */
            vcn = extent_data.extents_[i].next_vcn_;
        }

        /* Loop until we have processed the entire clustermap of the file. */
    } while (error_code == ERROR_MORE_DATA);

    /* If there was an error while reading the clustermap then return false. */
    if (error_code != NO_ERROR && error_code != ERROR_HANDLE_EOF) {
        system_error_str(error_code, error_string, BUFSIZ);
        gui->show_debug(DebugLevel::Fatal, item, std::format(L"  Error while processing clustermap: {}", error_string));

        return;
    }

    if (fragment != nullptr) {
        gui->show_debug(DebugLevel::Fatal, nullptr, L"  Extra fragment from MFT");
    }

    if (item->clusters_count_ != clusters) {
        gui->show_debug(DebugLevel::Fatal, nullptr,
                        std::format(L"  Different cluster count: " NUM_FMT " <> " NUM_FMT,
                                    item->clusters_count_, clusters));
    }
}

/* Compare two items.
sort_field=0    Filename
sort_field=1    Filesize, smallest first
sort_field=2    Date/Time LastAccess, oldest first
sort_field=3    Date/Time LastChange, oldest first
sort_field=4    Date/Time Creation, oldest first
Return values:
-1   item_1 is smaller than item_2
0    Equal
1    item_1 is bigger than item_2
*/
int DefragLib::compare_items(ItemStruct *item_1, ItemStruct *item_2, int sort_field) {
    int result;

    /* If one of the items is nullptr then the other item is bigger. */
    if (item_1 == nullptr) return -1;
    if (item_2 == nullptr) return 1;

    /* Return zero if the items are exactly the same. */
    if (item_1 == item_2) return 0;

    /* Compare the sort_field of the items and return 1 or -1 if they are not equal. */
    if (sort_field == 0) {
        result = _wcsicmp(item_1->get_long_path(), item_2->get_long_path());
        if (result != 0) return result;
    }

    if (sort_field == 1) {
        if (item_1->bytes_ < item_2->bytes_) return -1;
        if (item_1->bytes_ > item_2->bytes_) return 1;
    }

    if (sort_field == 2) {
        if (item_1->last_access_time_ > item_2->last_access_time_) return -1;
        if (item_1->last_access_time_ < item_2->last_access_time_) return 1;
    }

    if (sort_field == 3) {
        if (item_1->mft_change_time_ < item_2->mft_change_time_) return -1;
        if (item_1->mft_change_time_ > item_2->mft_change_time_) return 1;
    }

    if (sort_field == 4) {
        if (item_1->creation_time_ < item_2->creation_time_) return -1;
        if (item_1->creation_time_ > item_2->creation_time_) return 1;
    }

    /* The sort_field of the items is equal, so we must compare all the other fields
    to see if they are really equal. */
    if (item_1->have_long_path() && item_2->have_long_path()) {
        result = _wcsicmp(item_1->get_long_path(), item_2->get_long_path());

        if (result != 0) return result;
    }

    if (item_1->bytes_ < item_2->bytes_) return -1;
    if (item_1->bytes_ > item_2->bytes_) return 1;
    if (item_1->last_access_time_ < item_2->last_access_time_) return -1;
    if (item_1->last_access_time_ > item_2->last_access_time_) return 1;
    if (item_1->mft_change_time_ < item_2->mft_change_time_) return -1;
    if (item_1->mft_change_time_ > item_2->mft_change_time_) return 1;
    if (item_1->creation_time_ < item_2->creation_time_) return -1;
    if (item_1->creation_time_ > item_2->creation_time_) return 1;

    /* As a last resort compare the location on harddisk. */
    if (get_item_lcn(item_1) < get_item_lcn(item_2)) return -1;
    if (get_item_lcn(item_1) > get_item_lcn(item_2)) return 1;

    return 0;
}

/* Scan all files in a directory and all it's subdirectories (recursive)
and store the information in a tree in memory for later use by the
optimizer. */
void DefragLib::scan_dir(DefragDataStruct *data, const wchar_t *mask, ItemStruct *parent_directory) {
    ItemStruct *item;
    FragmentListStruct *fragment;
    HANDLE find_handle;
    WIN32_FIND_DATAW find_file_data;
    wchar_t *root_path;
    wchar_t *temp_path;
    HANDLE file_handle;
    uint64_t SystemTime;
    SYSTEMTIME time_1;
    FILETIME time_2;
    ULARGE_INTEGER time_3;
    int result;
    size_t length;
    wchar_t *p1;
    DefragGui *gui = DefragGui::get_instance();

    /* Slow the program down to the percentage that was specified on the
    command line. */
    slow_down(data);

    /* Determine the rootpath (base path of the directory) by stripping
    everything after the last backslash in the mask. The FindFirstFile()
    system call only processes wildcards in the last section (i.e. after
    the last backslash). */
    root_path = _wcsdup(mask);

    if (root_path == nullptr) return;

    p1 = wcsrchr(root_path, '\\');

    if (p1 != nullptr) *p1 = 0;

    // Show debug message: "Analyzing: %s"
    gui->show_debug(DebugLevel::DetailedProgress, nullptr, std::format(L"Analyzing: %s", mask));

    /* Fetch the current time in the uint64_t format (1 second = 10000000). */
    GetSystemTime(&time_1);

    if (SystemTimeToFileTime(&time_1, &time_2) == FALSE) {
        SystemTime = 0;
    } else {
        time_3.LowPart = time_2.dwLowDateTime;
        time_3.HighPart = time_2.dwHighDateTime;

        SystemTime = time_3.QuadPart;
    }

    /* Walk through all the files. If nothing found then exit.
    Note: I am using FindFirstFileW() instead of _findfirst() because the latter
    will crash (exit program) on files with badly formed dates. */
    find_handle = FindFirstFileW(mask, &find_file_data);

    if (find_handle == INVALID_HANDLE_VALUE) {
        delete root_path;
        return;
    }

    item = nullptr;

    wchar_t path_buf1[MAX_PATH];
    wchar_t path_buf2[MAX_PATH];

    do {
        if (*data->running_ != RunningState::RUNNING) break;

        if (wcscmp(find_file_data.cFileName, L".") == 0) continue;
        if (wcscmp(find_file_data.cFileName, L"..") == 0) continue;

        /* Ignore reparse-points, a directory where a volume is mounted
        with the MOUNTVOL command. */
        if ((find_file_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            continue;
        }

        /* Cleanup old item. */
        if (item != nullptr) {
            while (item->fragments_ != nullptr) {
                fragment = item->fragments_->next_;
                delete item->fragments_;
                item->fragments_ = fragment;
            }

            delete item;
        }

        /* Create new item. */
        item = new ItemStruct();
        item->fragments_ = nullptr;

        length = wcslen(root_path) + wcslen(find_file_data.cFileName) + 2;
        _ASSERT(MAX_PATH > length);
        swprintf_s(path_buf1, length, L"%s\\%s", root_path, find_file_data.cFileName);
//        item->long_path_ = path_buf1;
//        item->long_filename_ = find_file_data.cFileName;

        length = wcslen(root_path) + wcslen(find_file_data.cAlternateFileName) + 2;
        _ASSERT(MAX_PATH > length);
        swprintf_s(path_buf2, length, L"%s\\%s", root_path, find_file_data.cAlternateFileName);
//        item->short_path_ = path_buf2;
//        item->short_filename_ = find_file_data.cAlternateFileName;

        item->set_names(path_buf1, find_file_data.cFileName, path_buf2, find_file_data.cAlternateFileName);

        item->bytes_ = find_file_data.nFileSizeHigh * ((uint64_t) MAXDWORD + 1) +
                       find_file_data.nFileSizeLow;

        item->clusters_count_ = 0;
        item->creation_time_ = {};
        item->last_access_time_ = {};
        item->mft_change_time_ = {};
        item->parent_directory_ = parent_directory;
        item->is_dir_ = false;

        if ((find_file_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            item->is_dir_ = true;
        }
        item->is_unmovable_ = false;
        item->is_excluded_ = false;
        item->is_hog_ = false;

        /* Analyze the item: Clusters and Fragments, and the CreationTime, LastAccessTime,
        and MftChangeTime. If the item could not be opened then ignore the item. */
        file_handle = open_item_handle(data, item);

        if (file_handle == nullptr) continue;

        result = get_fragments(data, item, file_handle);

        CloseHandle(file_handle);

        if (result == false) continue;

        /* Increment counters. */
        data->count_all_files_ = data->count_all_files_ + 1;
        data->count_all_bytes_ = data->count_all_bytes_ + item->bytes_;
        data->count_all_clusters_ = data->count_all_clusters_ + item->clusters_count_;

        if (is_fragmented(item, 0, item->clusters_count_)) {
            data->count_fragmented_items_ = data->count_fragmented_items_ + 1;
            data->count_fragmented_bytes_ = data->count_fragmented_bytes_ + item->bytes_;
            data->count_fragmented_clusters_ = data->count_fragmented_clusters_ + item->clusters_count_;
        }

        data->phase_done_ = data->phase_done_ + item->clusters_count_;

        /* Show progress message. */
        gui->show_analyze(data, item);

        /* If it's a directory then iterate subdirectories. */
        if (item->is_dir_) {
            data->count_directories_ = data->count_directories_ + 1;

            length = wcslen(root_path) + wcslen(find_file_data.cFileName) + 4;

            temp_path = new wchar_t[length];

            if (temp_path != nullptr) {
                swprintf_s(temp_path, length, L"%s\\%s\\*", root_path, find_file_data.cFileName);
                scan_dir(data, temp_path, item);
                delete temp_path;
            }
        }

        /* Ignore the item if it has no clusters or no LCN. Very small
        files are stored in the MFT and are reported by Windows as
        having zero clusters and no fragments. */
        if (item->clusters_count_ == 0 || item->fragments_ == nullptr) continue;

        // Draw the item on the screen
        colorize_item(data, item, 0, 0, false);

        // Show debug info about the file.
        // Show debug message: "%I64d clusters at %I64d, %I64d bytes"
        gui->show_debug(DebugLevel::DetailedFileInfo, item,
                        std::format(L"%I64d clusters at " NUM_FMT ", " NUM_FMT " bytes",
                                    item->clusters_count_, get_item_lcn(item), item->bytes_));

        if ((find_file_data.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0) {
            // Show debug message: "Special file attribute: Compressed"
            gui->show_debug(DebugLevel::DetailedFileInfo, item, L"Special file attribute: Compressed");
        }

        if ((find_file_data.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) != 0) {
            // Show debug message: "Special file attribute: Encrypted"
            gui->show_debug(DebugLevel::DetailedFileInfo, item, L"Special file attribute: Encrypted");
        }

        if ((find_file_data.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) != 0) {
            // Show debug message: "Special file attribute: Offline"
            gui->show_debug(DebugLevel::DetailedFileInfo, item, L"Special file attribute: Offline");
        }

        if ((find_file_data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
            // Show debug message: "Special file attribute: Read-only"
            gui->show_debug(DebugLevel::DetailedFileInfo, item, L"Special file attribute: Read-only");
        }

        if ((find_file_data.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0) {
            // Show debug message: "Special file attribute: Sparse-file"
            gui->show_debug(DebugLevel::DetailedFileInfo, item, L"Special file attribute: Sparse-file");
        }

        if ((find_file_data.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0) {
            // Show debug message: "Special file attribute: Temporary"
            gui->show_debug(DebugLevel::DetailedFileInfo, item, L"Special file attribute: Temporary");
        }

        /* Add the item to the ItemTree in memory. */
        tree_insert(data, item);
        item = nullptr;
    } while (FindNextFileW(find_handle, &find_file_data) != 0);

    FindClose(find_handle);

    /* Cleanup. */
    delete root_path;

    if (item != nullptr) {
        while (item->fragments_ != nullptr) {
            fragment = item->fragments_->next_;

            delete item->fragments_;

            item->fragments_ = fragment;
        }

        delete item;
    }
}

/* Scan all files in a volume and store the information in a tree in
memory for later use by the optimizer. */
void DefragLib::analyze_volume(DefragDataStruct *data) {
    ItemStruct *item;
    micro64_t system_time;
    SYSTEMTIME time1;
    FILETIME time2;
    int i;

    DefragGui *gui = DefragGui::get_instance();
    ScanFAT *scan_fat = ScanFAT::get_instance();
    ScanNTFS *scan_ntfs = ScanNTFS::get_instance();

    call_show_status(data, 1, -1); /* "Phase 1: Analyze" */

    /* Fetch the current time in the uint64_t format (1 second = 10000000). */
    GetSystemTime(&time1);

    if (SystemTimeToFileTime(&time1, &time2) == FALSE) {
        system_time = {};
    } else {
        ULARGE_INTEGER time3;
        time3.LowPart = time2.dwLowDateTime;
        time3.HighPart = time2.dwHighDateTime;
        system_time = std::chrono::microseconds(time3.QuadPart);
    }

    /* Scan NTFS disks. */
    bool result = scan_ntfs->analyze_ntfs_volume(data);

    /* Scan FAT disks. */
    if (result == FALSE && *data->running_ == RunningState::RUNNING) result = scan_fat->analyze_fat_volume(data);

    /* Scan all other filesystems. */
    if (result == FALSE && *data->running_ == RunningState::RUNNING) {
        gui->show_debug(DebugLevel::Fatal, nullptr, L"This is not a FAT or NTFS disk, using the slow scanner.");

        /* Setup the width of the progress bar. */
        data->phase_todo_ = data->total_clusters_ - data->count_free_clusters_;

        for (i = 0; i < 3; i++) {
            data->phase_todo_ = data->phase_todo_ - (data->mft_excludes_[i].end_ - data->mft_excludes_[i].start_);
        }

        /* Scan all the files. */
        scan_dir(data, data->include_mask_, nullptr);
    }

    /* Update the diskmap with the colors. */
    data->phase_done_ = data->phase_todo_;
    gui->draw_cluster(data, 0, 0, DrawColor::Empty);

    /* Setup the progress counter and the file/dir counters. */
    data->phase_done_ = 0;
    data->phase_todo_ = 0;

    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        data->phase_todo_ = data->phase_todo_ + 1;
    }

    gui->show_analyze(nullptr, nullptr);

    /* Walk through all the items one by one. */
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        if (*data->running_ != RunningState::RUNNING) break;

        /* If requested then redraw the diskmap. */
        //		if (*Data->RedrawScreen == 1) m_jkGui->show_diskmap(Data);

        /* Construct the full path's of the item. The MFT contains only the filename, plus
        a pointer to the directory. We have to construct the full paths's by joining
        all the names of the directories, and the name of the file. */
        if (!item->have_long_path()) item->set_long_path(get_long_path(data, item).c_str());
        if (!item->have_short_path()) item->set_short_path(get_short_path(data, item).c_str());

        // Apply the Mask and set the Exclude flag of all items that do not match
        if (!match_mask(item->get_long_path(), data->include_mask_) &&
            !match_mask(item->get_short_path(), data->include_mask_)) {
            item->is_excluded_ = true;
            colorize_item(data, item, 0, 0, false);
        }

        /* Determine if the item is to be excluded by comparing it's name with the
        Exclude masks. */
        if (!item->is_excluded_) {
            for (auto &s: data->excludes_) {
                if (match_mask(item->get_long_path(), s.c_str()) ||
                    match_mask(item->get_short_path(), s.c_str())) {
                    item->is_excluded_ = true;

                    colorize_item(data, item, 0, 0, false);

                    break;
                }
            }
        }

        /* Exclude my own logfile. */
        if (!item->is_excluded_ &&
            (_wcsicmp(item->get_long_fn(), L"jkdefrag.exe.log") == 0 ||
             _wcsicmp(item->get_long_fn(), L"jkdefragcmd.log") == 0 ||
             _wcsicmp(item->get_long_fn(), L"jkdefragscreensaver.log") == 0)) {
            item->is_excluded_ = true;

            colorize_item(data, item, 0, 0, false);
        }

        /* The item is a SpaceHog if it's larger than 50 megabytes, or last access time
        is more than 30 days ago, or if it's filename matches a SpaceHog mask. */
        if (!item->is_excluded_ && !item->is_dir_) {
            if (data->use_default_space_hogs_ && item->bytes_ > 50 * 1024 * 1024) {
                item->is_hog_ = true;
            } else if (data->use_default_space_hogs_ &&
                       data->use_last_access_time_ == TRUE &&
                       item->last_access_time_ + std::chrono::microseconds(30UL * 24UL * 3600UL) < system_time) {
                item->is_hog_ = true;
            } else {
                for (const auto &s: data->space_hogs_) {
                    if (match_mask(item->get_long_path(), s.c_str()) ||
                        match_mask(item->get_short_path(), s.c_str())) {
                        item->is_hog_ = true;
                        break;
                    }
                }
            }

            if (item->is_hog_) colorize_item(data, item, 0, 0, false);
        }

        // Special exception for "http://www.safeboot.com/"
        if (match_mask(item->get_long_path(), L"*\\safeboot.fs")) item->is_unmovable_ = true;

        // Special exception for Acronis OS Selector
        if (match_mask(item->get_long_path(), L"?:\\bootwiz.sys")) item->is_unmovable_ = true;
        if (match_mask(item->get_long_path(), L"*\\BOOTWIZ\\*")) item->is_unmovable_ = true;

        // Special exception for DriveCrypt by "http://www.securstar.com/"
        if (match_mask(item->get_long_path(), L"?:\\BootAuth?.sys")) item->is_unmovable_ = true;

        // Special exception for Symantec GoBack
        if (match_mask(item->get_long_path(), L"*\\Gobackio.bin")) item->is_unmovable_ = true;

        // The $BadClus file maps the entire disk and is always unmovable
        if (item->get_long_fn() != nullptr &&
            (_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
             _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0)) {
            item->is_unmovable_ = true;
        }

        // Update the progress percentage
        data->phase_done_ = data->phase_done_ + 1;

        if (data->phase_done_ % 10000 == 0) gui->draw_cluster(data, 0, 0, DrawColor::Empty);
    }

    /* Force the percentage to 100%. */
    data->phase_done_ = data->phase_todo_;
    gui->draw_cluster(data, 0, 0, DrawColor::Empty);

    /* Calculate the begin of the zone's. */
    calculate_zones(data);

    /* Call the ShowAnalyze() callback one last time. */
    gui->show_analyze(data, nullptr);
}

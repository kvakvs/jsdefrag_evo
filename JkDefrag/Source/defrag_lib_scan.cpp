#include "std_afx.h"

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
        /* Calculate the end of the zones. */
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
                        L"Zone calculation, iteration %u: 0 - %I64d - %I64d - %I64d", iterate,
                        zone_end[0], zone_end[1], zone_end[2]);

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

    FragmentListStruct *Fragment;
    FragmentListStruct *LastFragment;

    uint32_t ErrorCode;

    wchar_t ErrorString[BUFSIZ];

    int MaxLoop;

    ULARGE_INTEGER u;

    uint32_t i;
    DWORD w;

    DefragGui *jkGui = DefragGui::get_instance();

    jkGui->show_debug(DebugLevel::Fatal, nullptr, L"%I64u %s", get_item_lcn(item), item->long_filename_);

    if (!item->is_dir_) {
        file_handle = CreateFileW(item->long_path_, FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
    } else {
        file_handle = CreateFileW(item->long_path_, GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    }

    if (file_handle == INVALID_HANDLE_VALUE) {
        system_error_str(GetLastError(), ErrorString, BUFSIZ);

        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Could not open: %s", ErrorString);

        return;
    }

    /* Fetch the date/times of the file. */
    if (GetFileInformationByHandle(file_handle, &file_information) != 0) {
        u.LowPart = file_information.ftCreationTime.dwLowDateTime;
        u.HighPart = file_information.ftCreationTime.dwHighDateTime;

        if (item->creation_time_ != u.QuadPart) {
            jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different CreationTime %I64u <> %I64u = %I64u",
                              item->creation_time_, u.QuadPart, item->creation_time_ - u.QuadPart);
        }

        u.LowPart = file_information.ftLastAccessTime.dwLowDateTime;
        u.HighPart = file_information.ftLastAccessTime.dwHighDateTime;

        if (item->last_access_time_ != u.QuadPart) {
            jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different LastAccessTime %I64u <> %I64u = %I64u",
                              item->last_access_time_, u.QuadPart, item->last_access_time_ - u.QuadPart);
        }
    }

#ifdef jk
    Vcn = 0;
    for (Fragment = Item->Fragments; Fragment != nullptr; Fragment = Fragment->Next) {
        if (Fragment->Lcn != VIRTUALFRAGMENT) {
            Data->ShowDebug(DebugLevel::Fatal,nullptr,L"  Extent 1: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u",
                Fragment->Lcn,Vcn,Fragment->NextVcn);
        } else {
            Data->ShowDebug(DebugLevel::Fatal,nullptr,L"  Extent 1 (virtual): Vcn=%I64u, NextVcn=%I64u",
                Vcn,Fragment->NextVcn);
        }
        Vcn = Fragment->NextVcn;
    }
#endif

    /* Ask Windows for the clustermap of the item and save it in memory.
    The buffer that is used to ask Windows for the clustermap has a
    fixed size, so we may have to loop a couple of times. */
    Fragment = item->fragments_;
    clusters = 0;
    vcn = 0;
    MaxLoop = 1000;
    LastFragment = nullptr;

    do {
        /* I strongly suspect that the FSCTL_GET_RETRIEVAL_POINTERS system call
        can sometimes return an empty bitmap and ERROR_MORE_DATA. That's not
        very nice of Microsoft, because it causes an infinite loop. I've
        therefore added a loop counter that will limit the loop to 1000
        iterations. This means the defragger cannot handle files with more
        than 100000 fragments, though. */
        if (MaxLoop <= 0) {
            jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  FSCTL_GET_RETRIEVAL_POINTERS error: Infinite loop");

            return;
        }

        MaxLoop = MaxLoop - 1;

        /* Ask Windows for the (next segment of the) clustermap of this file. If error
        then leave the loop. */
        retrieve_param.StartingVcn.QuadPart = vcn;

        ErrorCode = DeviceIoControl(file_handle, FSCTL_GET_RETRIEVAL_POINTERS,
                                    &retrieve_param, sizeof retrieve_param, &extent_data, sizeof extent_data, &w,
                                    nullptr);

        if (ErrorCode != 0) {
            ErrorCode = NO_ERROR;
        } else {
            ErrorCode = GetLastError();
        }

        if (ErrorCode != NO_ERROR && ErrorCode != ERROR_MORE_DATA) break;

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
            if (Fragment == nullptr) {
                jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Extra fragment in FSCTL_GET_RETRIEVAL_POINTERS");
            } else {
                if (Fragment->lcn_ != extent_data.extents_[i].lcn_) {
                    jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different LCN in fragment: %I64u <> %I64u",
                                      Fragment->lcn_, extent_data.extents_[i].lcn_);
                }

                if (Fragment->next_vcn_ != extent_data.extents_[i].next_vcn_) {
                    jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different NextVcn in fragment: %I64u <> %I64u",
                                      Fragment->next_vcn_, extent_data.extents_[i].next_vcn_);
                }

                Fragment = Fragment->next_;
            }

            /* The Vcn of the next fragment is the NextVcn field in this record. */
            vcn = extent_data.extents_[i].next_vcn_;
        }

        /* Loop until we have processed the entire clustermap of the file. */
    } while (ErrorCode == ERROR_MORE_DATA);

    /* If there was an error while reading the clustermap then return false. */
    if (ErrorCode != NO_ERROR && ErrorCode != ERROR_HANDLE_EOF) {
        system_error_str(ErrorCode, ErrorString, BUFSIZ);

        jkGui->show_debug(DebugLevel::Fatal, item, L"  Error while processing clustermap: %s", ErrorString);

        return;
    }

    if (Fragment != nullptr) {
        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Extra fragment from MFT");
    }

    if (item->clusters_count_ != clusters) {
        jkGui->show_debug(DebugLevel::Fatal, nullptr, L"  Different cluster count: %I64u <> %I64u",
                          item->clusters_count_, clusters);
    }
}

/* Compare two items.
SortField=0    Filename
SortField=1    Filesize, smallest first
SortField=2    Date/Time LastAccess, oldest first
SortField=3    Date/Time LastChange, oldest first
SortField=4    Date/Time Creation, oldest first
Return values:
-1   Item1 is smaller than Item2
0    Equal
1    Item1 is bigger than Item2
*/
int DefragLib::compare_items(ItemStruct *Item1, ItemStruct *Item2, int SortField) {
    int Result;

    /* If one of the items is nullptr then the other item is bigger. */
    if (Item1 == nullptr) return -1;
    if (Item2 == nullptr) return 1;

    /* Return zero if the items are exactly the same. */
    if (Item1 == Item2) return 0;

    /* Compare the SortField of the items and return 1 or -1 if they are not equal. */
    if (SortField == 0) {
        if (Item1->long_path_ == nullptr && Item2->long_path_ == nullptr) return 0;
        if (Item1->long_path_ == nullptr) return -1;
        if (Item2->long_path_ == nullptr) return 1;

        Result = _wcsicmp(Item1->long_path_, Item2->long_path_);

        if (Result != 0) return Result;
    }

    if (SortField == 1) {
        if (Item1->bytes_ < Item2->bytes_) return -1;
        if (Item1->bytes_ > Item2->bytes_) return 1;
    }

    if (SortField == 2) {
        if (Item1->last_access_time_ > Item2->last_access_time_) return -1;
        if (Item1->last_access_time_ < Item2->last_access_time_) return 1;
    }

    if (SortField == 3) {
        if (Item1->mft_change_time_ < Item2->mft_change_time_) return -1;
        if (Item1->mft_change_time_ > Item2->mft_change_time_) return 1;
    }

    if (SortField == 4) {
        if (Item1->creation_time_ < Item2->creation_time_) return -1;
        if (Item1->creation_time_ > Item2->creation_time_) return 1;
    }

    /* The SortField of the items is equal, so we must compare all the other fields
    to see if they are really equal. */
    if (Item1->long_path_ != nullptr && Item2->long_path_ != nullptr) {
        if (Item1->long_path_ == nullptr) return -1;
        if (Item2->long_path_ == nullptr) return 1;

        Result = _wcsicmp(Item1->long_path_, Item2->long_path_);

        if (Result != 0) return Result;
    }

    if (Item1->bytes_ < Item2->bytes_) return -1;
    if (Item1->bytes_ > Item2->bytes_) return 1;
    if (Item1->last_access_time_ < Item2->last_access_time_) return -1;
    if (Item1->last_access_time_ > Item2->last_access_time_) return 1;
    if (Item1->mft_change_time_ < Item2->mft_change_time_) return -1;
    if (Item1->mft_change_time_ > Item2->mft_change_time_) return 1;
    if (Item1->creation_time_ < Item2->creation_time_) return -1;
    if (Item1->creation_time_ > Item2->creation_time_) return 1;

    /* As a last resort compare the location on harddisk. */
    if (get_item_lcn(Item1) < get_item_lcn(Item2)) return -1;
    if (get_item_lcn(Item1) > get_item_lcn(Item2)) return 1;

    return 0;
}

/* Scan all files in a directory and all it's subdirectories (recursive)
and store the information in a tree in memory for later use by the
optimizer. */
void DefragLib::scan_dir(DefragDataStruct *Data, wchar_t *Mask, ItemStruct *ParentDirectory) {
    ItemStruct *Item;

    FragmentListStruct *Fragment;

    HANDLE FindHandle;

    WIN32_FIND_DATAW FindFileData;

    wchar_t *RootPath;
    wchar_t *TempPath;

    HANDLE FileHandle;

    uint64_t SystemTime;

    SYSTEMTIME Time1;

    FILETIME Time2;

    ULARGE_INTEGER Time3;

    int Result;

    size_t Length;

    wchar_t *p1;

    DefragGui *jkGui = DefragGui::get_instance();

    /* Slow the program down to the percentage that was specified on the
    command line. */
    slow_down(Data);

    /* Determine the rootpath (base path of the directory) by stripping
    everything after the last backslash in the Mask. The FindFirstFile()
    system call only processes wildcards in the last section (i.e. after
    the last backslash). */
    RootPath = _wcsdup(Mask);

    if (RootPath == nullptr) return;

    p1 = wcsrchr(RootPath, '\\');

    if (p1 != nullptr) *p1 = 0;

    /* Show debug message: "Analyzing: %s". */
    jkGui->show_debug(DebugLevel::DetailedProgress, nullptr, Data->debug_msg_[23].c_str(), Mask);

    /* Fetch the current time in the uint64_t format (1 second = 10000000). */
    GetSystemTime(&Time1);

    if (SystemTimeToFileTime(&Time1, &Time2) == FALSE) {
        SystemTime = 0;
    } else {
        Time3.LowPart = Time2.dwLowDateTime;
        Time3.HighPart = Time2.dwHighDateTime;

        SystemTime = Time3.QuadPart;
    }

    /* Walk through all the files. If nothing found then exit.
    Note: I am using FindFirstFileW() instead of _findfirst() because the latter
    will crash (exit program) on files with badly formed dates. */
    FindHandle = FindFirstFileW(Mask, &FindFileData);

    if (FindHandle == INVALID_HANDLE_VALUE) {
        free(RootPath);
        return;
    }

    Item = nullptr;

    do {
        if (*Data->running_ != RunningState::RUNNING) break;

        if (wcscmp(FindFileData.cFileName, L".") == 0) continue;
        if (wcscmp(FindFileData.cFileName, L"..") == 0) continue;

        /* Ignore reparse-points, a directory where a volume is mounted
        with the MOUNTVOL command. */
        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
            continue;
        }

        /* Cleanup old item. */
        if (Item != nullptr) {
            if (Item->short_path_ != nullptr) free(Item->short_path_);
            if (Item->short_filename_ != nullptr) free(Item->short_filename_);
            if (Item->long_path_ != nullptr) free(Item->long_path_);
            if (Item->long_filename_ != nullptr) free(Item->long_filename_);

            while (Item->fragments_ != nullptr) {
                Fragment = Item->fragments_->next_;

                free(Item->fragments_);

                Item->fragments_ = Fragment;
            }

            free(Item);

            Item = nullptr;
        }

        /* Create new item. */
        Item = (ItemStruct *) malloc(sizeof(ItemStruct));

        if (Item == nullptr) break;

        Item->short_path_ = nullptr;
        Item->short_filename_ = nullptr;
        Item->long_path_ = nullptr;
        Item->long_filename_ = nullptr;
        Item->fragments_ = nullptr;

        Length = wcslen(RootPath) + wcslen(FindFileData.cFileName) + 2;

        Item->long_path_ = (wchar_t *) malloc(sizeof(wchar_t) * Length);

        if (Item->long_path_ == nullptr) break;

        swprintf_s(Item->long_path_, Length, L"%s\\%s", RootPath, FindFileData.cFileName);

        Item->long_filename_ = _wcsdup(FindFileData.cFileName);

        if (Item->long_filename_ == nullptr) break;

        Length = wcslen(RootPath) + wcslen(FindFileData.cAlternateFileName) + 2;

        Item->short_path_ = (wchar_t *) malloc(sizeof(wchar_t) * Length);

        if (Item->short_path_ == nullptr) break;

        swprintf_s(Item->short_path_, Length, L"%s\\%s", RootPath, FindFileData.cAlternateFileName);

        Item->short_filename_ = _wcsdup(FindFileData.cAlternateFileName);

        if (Item->short_filename_ == nullptr) break;

        Item->bytes_ = FindFileData.nFileSizeHigh * ((uint64_t) MAXDWORD + 1) +
                       FindFileData.nFileSizeLow;

        Item->clusters_count_ = 0;
        Item->creation_time_ = 0;
        Item->last_access_time_ = 0;
        Item->mft_change_time_ = 0;
        Item->parent_directory_ = ParentDirectory;
        Item->is_dir_ = false;

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            Item->is_dir_ = true;
        }
        Item->is_unmovable_ = false;
        Item->is_excluded_ = false;
        Item->is_hog_ = false;

        /* Analyze the item: Clusters and Fragments, and the CreationTime, LastAccessTime,
        and MftChangeTime. If the item could not be opened then ignore the item. */
        FileHandle = open_item_handle(Data, Item);

        if (FileHandle == nullptr) continue;

        Result = get_fragments(Data, Item, FileHandle);

        CloseHandle(FileHandle);

        if (Result == false) continue;

        /* Increment counters. */
        Data->count_all_files_ = Data->count_all_files_ + 1;
        Data->count_all_bytes_ = Data->count_all_bytes_ + Item->bytes_;
        Data->count_all_clusters_ = Data->count_all_clusters_ + Item->clusters_count_;

        if (is_fragmented(Item, 0, Item->clusters_count_)) {
            Data->count_fragmented_items_ = Data->count_fragmented_items_ + 1;
            Data->count_fragmented_bytes_ = Data->count_fragmented_bytes_ + Item->bytes_;
            Data->count_fragmented_clusters_ = Data->count_fragmented_clusters_ + Item->clusters_count_;
        }

        Data->phase_done_ = Data->phase_done_ + Item->clusters_count_;

        /* Show progress message. */
        jkGui->show_analyze(Data, Item);

        /* If it's a directory then iterate subdirectories. */
        if (Item->is_dir_) {
            Data->count_directories_ = Data->count_directories_ + 1;

            Length = wcslen(RootPath) + wcslen(FindFileData.cFileName) + 4;

            TempPath = (wchar_t *) malloc(sizeof(wchar_t) * Length);

            if (TempPath != nullptr) {
                swprintf_s(TempPath, Length, L"%s\\%s\\*", RootPath, FindFileData.cFileName);
                scan_dir(Data, TempPath, Item);
                free(TempPath);
            }
        }

        /* Ignore the item if it has no clusters or no LCN. Very small
        files are stored in the MFT and are reported by Windows as
        having zero clusters and no fragments. */
        if (Item->clusters_count_ == 0 || Item->fragments_ == nullptr) continue;

        /* Draw the item on the screen. */
        //		if (*Data->RedrawScreen == 0) {
        colorize_item(Data, Item, 0, 0, false);
        //		} else {
        //			m_jkGui->ShowDiskmap(Data);
        //		}

        /* Show debug info about the file. */
        /* Show debug message: "%I64d clusters at %I64d, %I64d bytes" */
        jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[16].c_str(), Item->clusters_count_,
                          get_item_lcn(Item), Item->bytes_);

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_COMPRESSED) != 0) {
            /* Show debug message: "Special file attribute: Compressed" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[17].c_str());
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_ENCRYPTED) != 0) {
            /* Show debug message: "Special file attribute: Encrypted" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[18].c_str());
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_OFFLINE) != 0) {
            /* Show debug message: "Special file attribute: Offline" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[19].c_str());
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
            /* Show debug message: "Special file attribute: Read-only" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[20].c_str());
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_SPARSE_FILE) != 0) {
            /* Show debug message: "Special file attribute: Sparse-file" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[21].c_str());
        }

        if ((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_TEMPORARY) != 0) {
            /* Show debug message: "Special file attribute: Temporary" */
            jkGui->show_debug(DebugLevel::DetailedFileInfo, Item, Data->debug_msg_[22].c_str());
        }

        /* Save some memory if short and long filename are the same. */
        if (Item->long_filename_ != nullptr &&
            Item->short_filename_ != nullptr &&
            _wcsicmp(Item->long_filename_, Item->short_filename_) == 0) {
            free(Item->short_filename_);
            Item->short_filename_ = Item->long_filename_;
        }

        if (Item->long_filename_ == nullptr && Item->short_filename_ != nullptr)
            Item->long_filename_ = Item->
                    short_filename_;
        if (Item->long_filename_ != nullptr && Item->short_filename_ == nullptr)
            Item->short_filename_ = Item->
                    long_filename_;

        if (Item->long_path_ != nullptr &&
            Item->short_path_ != nullptr &&
            _wcsicmp(Item->long_path_, Item->short_path_) == 0) {
            free(Item->short_path_);
            Item->short_path_ = Item->long_path_;
        }

        if (Item->long_path_ == nullptr && Item->short_path_ != nullptr) Item->long_path_ = Item->short_path_;
        if (Item->long_path_ != nullptr && Item->short_path_ == nullptr) Item->short_path_ = Item->long_path_;

        /* Add the item to the ItemTree in memory. */
        tree_insert(Data, Item);
        Item = nullptr;
    } while (FindNextFileW(FindHandle, &FindFileData) != 0);

    FindClose(FindHandle);

    /* Cleanup. */
    free(RootPath);

    if (Item != nullptr) {
        if (Item->short_path_ != nullptr) free(Item->short_path_);
        if (Item->short_filename_ != nullptr) free(Item->short_filename_);
        if (Item->long_path_ != nullptr) free(Item->long_path_);
        if (Item->long_filename_ != nullptr) free(Item->long_filename_);

        while (Item->fragments_ != nullptr) {
            Fragment = Item->fragments_->next_;

            free(Item->fragments_);

            Item->fragments_ = Fragment;
        }

        free(Item);
    }
}

/* Scan all files in a volume and store the information in a tree in
memory for later use by the optimizer. */
void DefragLib::analyze_volume(DefragDataStruct *data) {
    ItemStruct *item;
    uint64_t system_time;
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
        system_time = 0;
    } else {
        ULARGE_INTEGER time3;
        time3.LowPart = time2.dwLowDateTime;
        time3.HighPart = time2.dwHighDateTime;

        system_time = time3.QuadPart;
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
    gui->draw_cluster(data, 0, 0, 0);

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
        //		if (*Data->RedrawScreen == 1) m_jkGui->ShowDiskmap(Data);

        /* Construct the full path's of the item. The MFT contains only the filename, plus
        a pointer to the directory. We have to construct the full paths's by joining
        all the names of the directories, and the name of the file. */
        if (item->long_path_ == nullptr) item->long_path_ = get_long_path(data, item);
        if (item->short_path_ == nullptr) item->short_path_ = get_short_path(data, item);

        /* Save some memory if the short and long paths are the same. */
        if (item->long_path_ != nullptr &&
            item->short_path_ != nullptr &&
            item->long_path_ != item->short_path_ &&
            _wcsicmp(item->long_path_, item->short_path_) == 0) {
            free(item->short_path_);
            item->short_path_ = item->long_path_;
        }

        if (item->long_path_ == nullptr && item->short_path_ != nullptr) item->long_path_ = item->short_path_;
        if (item->long_path_ != nullptr && item->short_path_ == nullptr) item->short_path_ = item->long_path_;

        /* For debugging only: compare the data with the output from the
        FSCTL_GET_RETRIEVAL_POINTERS function call. */
        /*
        CompareItems(Data,Item);
        */

        /* Apply the Mask and set the Exclude flag of all items that do not match. */
        if (!match_mask(item->long_path_, data->include_mask_) &&
            !match_mask(item->short_path_, data->include_mask_)) {
            item->is_excluded_ = true;

            colorize_item(data, item, 0, 0, false);
        }

        /* Determine if the item is to be excluded by comparing it's name with the
        Exclude masks. */
        if (!item->is_excluded_) {
            for (auto &s: data->excludes_) {
                if (match_mask(item->long_path_, s.c_str()) ||
                    match_mask(item->short_path_, s.c_str())) {
                    item->is_excluded_ = true;

                    colorize_item(data, item, 0, 0, false);

                    break;
                }
            }
        }

        /* Exclude my own logfile. */
        if (!item->is_excluded_ &&
            item->long_filename_ != nullptr &&
            (_wcsicmp(item->long_filename_, L"jkdefrag.log") == 0 ||
             _wcsicmp(item->long_filename_, L"jkdefragcmd.log") == 0 ||
             _wcsicmp(item->long_filename_, L"jkdefragscreensaver.log") == 0)) {
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
                       item->last_access_time_ + (uint64_t) (30 * 24 * 60 * 60) * 10000000 < system_time) {
                item->is_hog_ = true;
            } else {
                for (const auto &s: data->space_hogs_) {
                    if (match_mask(item->long_path_, s.c_str()) ||
                        match_mask(item->short_path_, s.c_str())) {
                        item->is_hog_ = true;
                        break;
                    }
                }
            }

            if (item->is_hog_) colorize_item(data, item, 0, 0, false);
        }

        // Special exception for "http://www.safeboot.com/"
        if (match_mask(item->long_path_, L"*\\safeboot.fs")) item->is_unmovable_ = true;

        // Special exception for Acronis OS Selector
        if (match_mask(item->long_path_, L"?:\\bootwiz.sys")) item->is_unmovable_ = true;
        if (match_mask(item->long_path_, L"*\\BOOTWIZ\\*")) item->is_unmovable_ = true;

        // Special exception for DriveCrypt by "http://www.securstar.com/"
        if (match_mask(item->long_path_, L"?:\\BootAuth?.sys")) item->is_unmovable_ = true;

        // Special exception for Symantec GoBack
        if (match_mask(item->long_path_, L"*\\Gobackio.bin")) item->is_unmovable_ = true;

        // The $BadClus file maps the entire disk and is always unmovable
        if (item->long_filename_ != nullptr &&
            (_wcsicmp(item->long_filename_, L"$BadClus") == 0 ||
             _wcsicmp(item->long_filename_, L"$BadClus:$Bad:$DATA") == 0)) {
            item->is_unmovable_ = true;
        }

        // Update the progress percentage
        data->phase_done_ = data->phase_done_ + 1;

        if (data->phase_done_ % 10000 == 0) gui->draw_cluster(data, 0, 0, 0);
    }

    /* Force the percentage to 100%. */
    data->phase_done_ = data->phase_todo_;
    gui->draw_cluster(data, 0, 0, 0);

    /* Calculate the begin of the zone's. */
    calculate_zones(data);

    /* Call the ShowAnalyze() callback one last time. */
    gui->show_analyze(data, nullptr);
}

#include "std_afx.h"

/* Move items to their zone. This will:
- Defragment all fragmented files
- Move regular files out of the directory zone.
- Move SpaceHogs out of the directory- and regular zones.
- Move items out of the MFT reserved zones
*/
void DefragLib::fixup(DefragDataStruct *data) {
    ItemStruct *item;

    uint64_t gap_begin[3];
    uint64_t gap_end[3];

    int file_zone;

    WIN32_FILE_ATTRIBUTE_DATA attributes;

    FILETIME system_time1;

    bool result;

    ULARGE_INTEGER u;

    DefragGui *gui = DefragGui::get_instance();

    call_show_status(data, 8, -1); /* "Phase 3: Fixup" */

    /* Initialize: fetch the current time. */
    GetSystemTimeAsFileTime(&system_time1);

    u.LowPart = system_time1.dwLowDateTime;
    u.HighPart = system_time1.dwHighDateTime;

    uint64_t system_time = u.QuadPart;

    /* Initialize the width of the progress bar: the total number of clusters
    of all the items. */
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;
        if (item->clusters_count_ == 0) continue;

        data->phase_todo_ = data->phase_todo_ + item->clusters_count_;
    }

    [[maybe_unused]] uint64_t last_calc_time = system_time;

    /* Exit if nothing to do. */
    if (data->phase_todo_ == 0) return;

    /* Walk through all files and move the files that need to be moved. */
    for (file_zone = 0; file_zone < 3; file_zone++) {
        gap_begin[file_zone] = 0;
        gap_end[file_zone] = 0;
    }

    auto next_item = tree_smallest(data->item_tree_);

    while (next_item != nullptr && *data->running_ == RunningState::RUNNING) {
        /* The loop will change the position of the item in the tree, so we have
        to determine the next item before executing the loop. */
        item = next_item;

        next_item = tree_next(item);

        /* Ignore items that are unmovable or excluded. */
        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;
        if (item->clusters_count_ == 0) continue;

        /* Ignore items that do not need to be moved. */
        file_zone = 1;

        if (item->is_hog_) file_zone = 2;
        if (item->is_dir_) file_zone = 0;

        const uint64_t item_lcn = get_item_lcn(item);

        int move_me = false;

        if (is_fragmented(item, 0, item->clusters_count_)) {
            /* "I am fragmented." */
            gui->show_debug(DebugLevel::DetailedFileInfo, item, data->debug_msg_[53].c_str());

            move_me = true;
        }

        if (move_me == false &&
            ((item_lcn >= data->mft_excludes_[0].start_ && item_lcn < data->mft_excludes_[0].end_) ||
             (item_lcn >= data->mft_excludes_[1].start_ && item_lcn < data->mft_excludes_[1].end_) ||
             (item_lcn >= data->mft_excludes_[2].start_ && item_lcn < data->mft_excludes_[2].end_))
            && (data->disk_.type_ != DiskType::NTFS || !match_mask(item->get_long_path(), L"?:\\$MFT"))) {
            // "I am in MFT reserved space."
            gui->show_debug(DebugLevel::DetailedFileInfo, item, data->debug_msg_[54].c_str());

            move_me = true;
        }

        if (file_zone == 1 && item_lcn < data->zones_[1] && move_me == false) {
            // "I am a regular file in zone 1."
            gui->show_debug(DebugLevel::DetailedFileInfo, item, data->debug_msg_[55].c_str());

            move_me = true;
        }

        if (file_zone == 2 && item_lcn < data->zones_[2] && move_me == false) {
            /* "I am a spacehog in zone 1 or 2." */
            gui->show_debug(DebugLevel::DetailedFileInfo, item, data->debug_msg_[56].c_str());

            move_me = true;
        }

        if (move_me == false) {
            data->phase_done_ = data->phase_done_ + item->clusters_count_;

            continue;
        }

        /* Ignore files that have been modified less than 15 minutes ago. */
        if (!item->is_dir_) {
            result = GetFileAttributesExW(item->get_long_path(), GetFileExInfoStandard, &attributes);

            if (result != 0) {
                u.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
                u.HighPart = attributes.ftLastWriteTime.dwHighDateTime;

                if (const uint64_t FileTime = u.QuadPart; FileTime + 15 * 60 * (uint64_t) 10000000 > system_time) {
                    data->phase_done_ = data->phase_done_ + item->clusters_count_;

                    continue;
                }
            }
        }

        /* If the file does not fit in the current gap then find another gap. */
        if (item->clusters_count_ > gap_end[file_zone] - gap_begin[file_zone]) {
            result = find_gap(data, data->zones_[file_zone], 0, item->clusters_count_, true, false,
                              &gap_begin[file_zone],
                              &gap_end[file_zone], FALSE);

            if (!result) {
                /* Show debug message: "Cannot move item away because no gap is big enough: %I64d[%lu]" */
                gui->show_debug(DebugLevel::Progress, item, data->debug_msg_[25].c_str(), get_item_lcn(item),
                                item->clusters_count_);

                gap_end[file_zone] = gap_begin[file_zone]; /* Force re-scan of gap. */

                data->phase_done_ = data->phase_done_ + item->clusters_count_;

                continue;
            }
        }

        /* Move the item. */
        result = move_item(data, item, gap_begin[file_zone], 0, item->clusters_count_, 0);

        if (result) {
            gap_begin[file_zone] = gap_begin[file_zone] + item->clusters_count_;
        } else {
            gap_end[file_zone] = gap_begin[file_zone]; /* Force re-scan of gap. */
        }

        /* Get new system time. */
        GetSystemTimeAsFileTime(&system_time1);

        u.LowPart = system_time1.dwLowDateTime;
        u.HighPart = system_time1.dwHighDateTime;

        system_time = u.QuadPart;
    }
}

/* Defragment all the fragmented files. */
void DefragLib::defragment(DefragDataStruct *data) {
    ItemStruct *item;
    ItemStruct *next_item;
    uint64_t gap_begin;
    uint64_t gap_end;
    uint64_t clusters_done;
    uint64_t clusters;
    FragmentListStruct *fragment;
    uint64_t vcn;
    uint64_t real_vcn;
    HANDLE file_handle;
    int file_zone;
    int result;
    DefragGui *gui = DefragGui::get_instance();

    call_show_status(data, 2, -1); /* "Phase 2: Defragment" */

    /* Setup the width of the progress bar: the number of clusters in all
    fragmented files. */
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;
        if (item->clusters_count_ == 0) continue;

        if (!is_fragmented(item, 0, item->clusters_count_)) continue;

        data->phase_todo_ = data->phase_todo_ + item->clusters_count_;
    }

    /* Exit if nothing to do. */
    if (data->phase_todo_ == 0) return;

    /* Walk through all files and defrag. */
    next_item = tree_smallest(data->item_tree_);

    while (next_item != nullptr && *data->running_ == RunningState::RUNNING) {
        /* The loop may change the position of the item in the tree, so we have
        to determine and remember the next item now. */
        item = next_item;

        next_item = tree_next(item);

        /* Ignore if the item cannot be moved, or is Excluded, or is not fragmented. */
        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;
        if (item->clusters_count_ == 0) continue;

        if (!is_fragmented(item, 0, item->clusters_count_)) continue;

        /* Find a gap that is large enough to hold the item, or the largest gap
        on the volume. If the disk is full then show a message and exit. */
        file_zone = 1;

        if (item->is_hog_) file_zone = 2;
        if (item->is_dir_) file_zone = 0;

        result = find_gap(data, data->zones_[file_zone], 0, item->clusters_count_, false, false, &gap_begin, &gap_end,
                          FALSE);

        if (result == false) {
            /* Try finding a gap again, this time including the free area. */
            result = find_gap(data, 0, 0, item->clusters_count_, false, false, &gap_begin, &gap_end, FALSE);

            if (result == false) {
                /* Show debug message: "Disk is full, cannot defragment." */
                gui->show_debug(DebugLevel::Progress, item, data->debug_msg_[44].c_str());

                return;
            }
        }

        /* If the gap is big enough to hold the entire item then move the file
        in a single go, and loop. */
        if (gap_end - gap_begin >= item->clusters_count_) {
            move_item(data, item, gap_begin, 0, item->clusters_count_, 0);

            continue;
        }

        /* Open a filehandle for the item. If error then set the Unmovable flag,
        colorize the item on the screen, and loop. */
        file_handle = open_item_handle(data, item);

        if (file_handle == nullptr) {
            item->is_unmovable_ = true;

            colorize_item(data, item, 0, 0, false);

            continue;
        }

        /* Move the file in parts, each time selecting the biggest gap
        available. */
        clusters_done = 0;

        do {
            clusters = gap_end - gap_begin;

            if (clusters > item->clusters_count_ - clusters_done) {
                clusters = item->clusters_count_ - clusters_done;
            }

            /* Make sure that the gap is bigger than the first fragment of the
            block that we're about to move. If not then the result would be
            more fragments, not less. */
            vcn = 0;
            real_vcn = 0;

            for (fragment = item->fragments_; fragment != nullptr; fragment = fragment->next_) {
                if (fragment->lcn_ != VIRTUALFRAGMENT) {
                    if (real_vcn >= clusters_done) {
                        if (clusters > fragment->next_vcn_ - vcn) break;

                        clusters_done = real_vcn + fragment->next_vcn_ - vcn;

                        data->phase_done_ = data->phase_done_ + fragment->next_vcn_ - vcn;
                    }

                    real_vcn = real_vcn + fragment->next_vcn_ - vcn;
                }

                vcn = fragment->next_vcn_;
            }

            if (clusters_done >= item->clusters_count_) break;

            /* Move the segment. */
            result = move_item4(data, item, file_handle, gap_begin, clusters_done, clusters, 0);

            /* Next segment. */
            clusters_done = clusters_done + clusters;

            /* Find a gap large enough to hold the remainder, or the largest gap
            on the volume. */
            if (clusters_done < item->clusters_count_) {
                result = find_gap(data, data->zones_[file_zone], 0, item->clusters_count_ - clusters_done,
                                  false, false, &gap_begin, &gap_end, FALSE);

                if (result == false) break;
            }
        } while (clusters_done < item->clusters_count_ && *data->running_ == RunningState::RUNNING);

        /* Close the item. */
        FlushFileBuffers(file_handle); /* Is this useful? Can't hurt. */
        CloseHandle(file_handle);
    }
}

/* Fill all the gaps at the beginning of the disk with fragments from the files above. */
void DefragLib::forced_fill(DefragDataStruct *data) {
    uint64_t gap_begin;
    uint64_t gap_end;

    // result, LCN of end of cluster
    ItemStruct *item;
    FragmentListStruct *fragment;
    ItemStruct *highest_item;
    uint64_t max_lcn;
    uint64_t highest_lcn;
    uint64_t highest_vcn;
    uint64_t highest_size;
    uint64_t clusters;
    uint64_t vcn;
    uint64_t real_vcn;
    int result;

    call_show_status(data, 3, -1); /* "Phase 3: ForcedFill" */

    /* Walk through all the gaps. */
    gap_begin = 0;
    max_lcn = data->total_clusters_;

    while (*data->running_ == RunningState::RUNNING) {
        /* Find the next gap. If there are no more gaps then exit. */
        result = find_gap(data, gap_begin, 0, 0, true, false, &gap_begin, &gap_end, FALSE);

        if (result == false) break;

        /* Find the item with the highest fragment on disk. */
        highest_item = nullptr;
        highest_lcn = 0;
        highest_vcn = 0;
        highest_size = 0;

        for (item = tree_biggest(data->item_tree_); item != nullptr; item = tree_prev(item)) {
            if (item->is_unmovable_) continue;
            if (item->is_excluded_) continue;
            if (item->clusters_count_ == 0) continue;

            vcn = 0;
            real_vcn = 0;

            for (fragment = item->fragments_; fragment != nullptr; fragment = fragment->next_) {
                if (fragment->lcn_ != VIRTUALFRAGMENT) {
                    if (fragment->lcn_ > highest_lcn && fragment->lcn_ < max_lcn) {
                        highest_item = item;
                        highest_lcn = fragment->lcn_;
                        highest_vcn = real_vcn;
                        highest_size = fragment->next_vcn_ - vcn;
                    }

                    real_vcn = real_vcn + fragment->next_vcn_ - vcn;
                }

                vcn = fragment->next_vcn_;
            }
        }

        if (highest_item == nullptr) break;

        /* If the highest fragment is before the gap then exit, we're finished. */
        if (highest_lcn <= gap_begin) break;

        /* Move as much of the item into the gap as possible. */
        clusters = gap_end - gap_begin;

        if (clusters > highest_size) clusters = highest_size;

        result = move_item(data, highest_item, gap_begin, highest_vcn + highest_size - clusters, clusters, 0);

        gap_begin = gap_begin + clusters;
        max_lcn = highest_lcn + highest_size - clusters;
    }
}

/* Vacate an area by moving files upward. If there are unmovable files at the lcn then
skip them. Then move files upward until the gap is bigger than clusters, or when we
encounter an unmovable file. */
void DefragLib::vacate(DefragDataStruct *data, uint64_t lcn, uint64_t clusters, BOOL ignore_mft_excludes) {
    uint64_t test_gap_begin;
    uint64_t test_gap_end;
    uint64_t MoveGapBegin;
    uint64_t MoveGapEnd;

    ItemStruct *Item;
    FragmentListStruct *Fragment;

    uint64_t Vcn;
    uint64_t RealVcn;

    ItemStruct *BiggerItem;

    uint64_t BiggerBegin;
    uint64_t BiggerEnd;
    uint64_t BiggerRealVcn;
    uint64_t MoveTo;
    uint64_t DoneUntil;

    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Vacating %I64u clusters starting at LCN=%I64u",
                    clusters, lcn);

    /* Sanity check. */
    if (lcn >= data->total_clusters_) {
        gui->show_debug(DebugLevel::Warning, nullptr, L"Error: trying to vacate an area beyond the end of the disk.");

        return;
    }

    /* Determine the point to above which we will be moving the data. We want at least the
    end of the zone if everything was perfectly optimized, so data will not be moved
    again and again. */
    MoveTo = lcn + clusters;

    if (data->zone_ == 0) MoveTo = data->zones_[1];
    if (data->zone_ == 1) MoveTo = data->zones_[2];

    if (data->zone_ == 2) {
        /* Zone 2: end of disk minus all the free space. */
        MoveTo = data->total_clusters_ - data->count_free_clusters_ +
                 (uint64_t) (data->total_clusters_ * 2.0 * data->free_space_ / 100.0);
    }

    if (MoveTo < lcn + clusters) MoveTo = lcn + clusters;

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"MoveTo = %I64u", MoveTo);

    /* Loop forever. */
    MoveGapBegin = 0;
    MoveGapEnd = 0;
    DoneUntil = lcn;

    while (*data->running_ == RunningState::RUNNING) {
        /* Find the first movable data fragment at or above the DoneUntil lcn. If there is nothing
        then return, we have reached the end of the disk. */
        BiggerItem = nullptr;
        BiggerBegin = 0;

        for (Item = tree_smallest(data->item_tree_); Item != nullptr; Item = tree_next(Item)) {
            if (Item->is_unmovable_ == true || Item->is_excluded_ == true || Item->clusters_count_ == 0) {
                continue;
            }

            Vcn = 0;
            RealVcn = 0;

            for (Fragment = Item->fragments_; Fragment != nullptr; Fragment = Fragment->next_) {
                if (Fragment->lcn_ != VIRTUALFRAGMENT) {
                    if (Fragment->lcn_ >= DoneUntil &&
                        (BiggerBegin > Fragment->lcn_ || BiggerItem == nullptr)) {
                        BiggerItem = Item;
                        BiggerBegin = Fragment->lcn_;
                        BiggerEnd = Fragment->lcn_ + Fragment->next_vcn_ - Vcn;
                        BiggerRealVcn = RealVcn;

                        if (BiggerBegin == lcn) break;
                    }

                    RealVcn = RealVcn + Fragment->next_vcn_ - Vcn;
                }

                Vcn = Fragment->next_vcn_;
            }

            if (BiggerBegin != 0 && BiggerBegin == lcn) break;
        }

        if (BiggerItem == nullptr) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No data found above LCN=%I64u", lcn);

            return;
        }

        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"data found at LCN=%I64u, %s", BiggerBegin,
                        BiggerItem->get_long_path());

        /* Find the first gap above the lcn. */
        bool result = find_gap(data, lcn, 0, 0, true, false, &test_gap_begin, &test_gap_end, ignore_mft_excludes);

        if (result == false) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No gaps found above LCN=%I64u", lcn);

            return;
        }

        /* Exit if the end of the first gap is below the first movable item, the gap cannot
        be enlarged. */
        if (test_gap_end < BiggerBegin) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                            L"Cannot enlarge the gap from %I64u to %I64u (%I64u clusters) any further.",
                            test_gap_begin, test_gap_end, test_gap_end - test_gap_begin);

            return;
        }

        /* Exit if the first movable item is at the end of the gap and the gap is big enough,
        no need to enlarge any further. */
        if (test_gap_end == BiggerBegin && test_gap_end - test_gap_begin >= clusters) {
            gui->show_debug(
                    DebugLevel::DetailedGapFilling, nullptr,
                    L"Finished vacating, the gap from %I64u to %I64u (%I64u clusters) is now bigger than %I64u clusters.",
                    test_gap_begin, test_gap_end, test_gap_end - test_gap_begin, clusters);

            return;
        }

        /* Exit if we have moved the item before. We don't want a worm. */
        if (lcn >= MoveTo) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Stopping vacate because of possible worm.");
            return;
        }

        /* Determine where we want to move the fragment to. Maybe the previously used
        gap is big enough, otherwise we have to locate another gap. */
        if (BiggerEnd - BiggerBegin >= MoveGapEnd - MoveGapBegin) {
            result = false;

            /* First try to find a gap above the MoveTo point. */
            if (MoveTo < data->total_clusters_ && MoveTo >= BiggerEnd) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Finding gap above MoveTo=%I64u", MoveTo);

                result = find_gap(data, MoveTo, 0, BiggerEnd - BiggerBegin, true, false, &MoveGapBegin, &MoveGapEnd,
                                  FALSE);
            }

            /* If no gap was found then try to find a gap as high on disk as possible, but
            above the item. */
            if (result == false) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                                L"Finding gap from end of disk above BiggerEnd=%I64u", BiggerEnd);

                result = find_gap(data, BiggerEnd, 0, BiggerEnd - BiggerBegin, true, true, &MoveGapBegin,
                                  &MoveGapEnd, FALSE);
            }

            /* If no gap was found then exit, we cannot move the item. */
            if (result == false) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No gap found.");

                return;
            }
        }

        /* Move the fragment to the gap. */
        result = move_item(data, BiggerItem, MoveGapBegin, BiggerRealVcn, BiggerEnd - BiggerBegin, 0);

        if (result == true) {
            if (MoveGapBegin < MoveTo) MoveTo = MoveGapBegin;

            MoveGapBegin = MoveGapBegin + BiggerEnd - BiggerBegin;
        } else {
            MoveGapEnd = MoveGapBegin; /* Force re-scan of gap. */
        }

        /* Adjust the DoneUntil lcn. We don't want an infinite loop. */
        DoneUntil = BiggerEnd;
    }
}

/* Optimize the volume by moving all the files into a sorted order.
SortField=0    Filename
SortField=1    Filesize
SortField=2    Date/Time LastAccess
SortField=3    Date/Time LastChange
SortField=4    Date/Time Creation
*/
void DefragLib::optimize_sort(DefragDataStruct *data, const int sort_field) {
    uint64_t gap_begin;
    uint64_t gap_end;

    bool result;

    DefragGui *gui = DefragGui::get_instance();

    /* Sanity check. */
    if (data->item_tree_ == nullptr) return;

    /* Process all the zones. */
    [[maybe_unused]] uint64_t vacated_until = 0;
    const uint64_t minimum_vacate = data->total_clusters_ / 200;

    for (data->zone_ = 0; data->zone_ < 3; data->zone_++) {
        call_show_status(data, 4, data->zone_); /* "Zone N: Sort" */

        /* Start at the begin of the zone and move all the items there, one by one
        in the requested sorting order, making room as we go. */
        ItemStruct *previous_item = nullptr;

        uint64_t lcn = data->zones_[data->zone_];

        gap_begin = 0;
        gap_end = 0;

        while (*data->running_ == RunningState::RUNNING) {
            /* Find the next item that we want to place. */
            ItemStruct *item = nullptr;
            uint64_t phase_temp = 0;

            for (auto temp_item = tree_smallest(data->item_tree_); temp_item != nullptr; temp_item =
                                                                                                 tree_next(temp_item)) {
                if (temp_item->is_unmovable_ == true) continue;
                if (temp_item->is_excluded_ == true) continue;
                if (temp_item->clusters_count_ == 0) continue;

                int file_zone = 1;

                if (temp_item->is_hog_ == true) file_zone = 2;
                if (temp_item->is_dir_ == true) file_zone = 0;
                if (file_zone != data->zone_) continue;

                if (previous_item != nullptr &&
                    compare_items(previous_item, temp_item, sort_field) >= 0) {
                    continue;
                }

                phase_temp = phase_temp + temp_item->clusters_count_;

                if (item != nullptr && compare_items(temp_item, item, sort_field) >= 0) continue;

                item = temp_item;
            }

            if (item == nullptr) {
                gui->show_debug(DebugLevel::Progress, nullptr, L"Finished sorting zone %u.", data->zone_ + 1);

                break;
            }

            previous_item = item;
            data->phase_todo_ = data->phase_done_ + phase_temp;

            /* If the item is already at the Lcn then skip. */
            if (get_item_lcn(item) == lcn) {
                lcn = lcn + item->clusters_count_;

                continue;
            }

            /* Move the item to the Lcn. If the gap at Lcn is not big enough then fragment
            the file into whatever gaps are available. */
            uint64_t clusters_done = 0;

            while (*data->running_ == RunningState::RUNNING &&
                   clusters_done < item->clusters_count_ &&
                   item->is_unmovable_ == false) {
                if (clusters_done > 0) {
                    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                                    L"Item partially placed, %I64u clusters more to do",
                                    item->clusters_count_ - clusters_done);
                }

                /* Call the Vacate() function to make a gap at Lcn big enough to hold the item.
                The Vacate() function may not be able to move whatever is now at the Lcn, so
                after calling it we have to locate the first gap after the Lcn. */
                if (gap_begin + item->clusters_count_ - clusters_done + 16 > gap_end) {
                    vacate(data, lcn, item->clusters_count_ - clusters_done + minimum_vacate, FALSE);

                    result = find_gap(data, lcn, 0, 0, true, false, &gap_begin, &gap_end, FALSE);

                    if (result == false) return; /* No gaps found, exit. */
                }

                /* If the gap is not big enough to hold the entire item then calculate how much
                of the item will fit in the gap. */
                uint64_t clusters = item->clusters_count_ - clusters_done;

                if (clusters > gap_end - gap_begin) {
                    clusters = gap_end - gap_begin;

                    /* It looks like a partial move only succeeds if the number of clusters is a
                    multiple of 8. */
                    clusters = clusters - clusters % 8;

                    if (clusters == 0) {
                        lcn = gap_end;
                        continue;
                    }
                }

                /* Move the item to the gap. */
                result = move_item(data, item, gap_begin, clusters_done, clusters, 0);

                if (result == true) {
                    gap_begin = gap_begin + clusters;
                } else {
                    result = find_gap(data, gap_begin, 0, 0, true, false, &gap_begin, &gap_end, FALSE);
                    if (result == false) return; /* No gaps found, exit. */
                }

                lcn = gap_begin;
                clusters_done = clusters_done + clusters;
            }
        }
    }
}

/*

Move the MFT to the beginning of the harddisk.
- The Microsoft defragmentation api only supports moving the MFT on Vista.
- What to do if there is unmovable data at the beginning of the disk? I have
chosen to wrap the MFT around that data. The fragments will be aligned, so
the performance loss is minimal, and still faster than placing the MFT
higher on the disk.

*/
void DefragLib::move_mft_to_begin_of_disk(DefragDataStruct *data) {
    ItemStruct *item;

    uint64_t lcn;
    uint64_t gap_begin;
    uint64_t gap_end;
    uint64_t clusters;
    uint64_t clusters_done;

    bool result;

    OSVERSIONINFO os_version;

    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::Progress, nullptr, L"Moving the MFT to the beginning of the volume.");

    /* Exit if this is not an NTFS disk. */
    if (data->disk_.type_ != DiskType::NTFS) {
        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                        L"Cannot move the MFT because this is not an NTFS disk.");

        return;
    }

    /* The Microsoft defragmentation api only supports moving the MFT on Vista. */
    ZeroMemory(&os_version, sizeof(OSVERSIONINFO));

    os_version.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

    if (GetVersionEx(&os_version) != 0 && os_version.dwMajorVersion < 6) {
        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                        L"Cannot move the MFT because it is not supported by this version of Windows.");

        return;
    }

    /* Locate the Item for the MFT. If not found then exit. */
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        if (match_mask(item->get_long_path(), L"?:\\$MFT")) break;
    }

    if (item == nullptr) {
        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Cannot move the MFT because I cannot find it.");

        return;
    }

    /* Exit if the MFT is at the beginning of the volume (inside zone 0) and is not
    fragmented. */
#ifdef jk
    if ((Item->Fragments != nullptr) &&
        (Item->Fragments->NextVcn == Data->Disk.MftLockedClusters) &&
        (Item->Fragments->Next != nullptr) &&
        (Item->Fragments->Next->Lcn < Data->Zones[1]) &&
        (IsFragmented(Item,Data->Disk.MftLockedClusters,Item->Clusters - Data->Disk.MftLockedClusters) == false)) {
            m_jkGui->ShowDebug(DebugLevel::DetailedGapFilling,nullptr,L"No need to move the MFT because it's already at the beginning of the volume and it's data part is not fragmented.");
            return;
    }
#endif

    lcn = 0;
    gap_begin = 0;
    gap_end = 0;
    clusters_done = data->disk_.mft_locked_clusters_;

    while (*data->running_ == RunningState::RUNNING && clusters_done < item->clusters_count_) {
        if (clusters_done > data->disk_.mft_locked_clusters_) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Partially placed, %I64u clusters more to do",
                            item->clusters_count_ - clusters_done);
        }

        /* Call the Vacate() function to make a gap at Lcn big enough to hold the MFT.
        The Vacate() function may not be able to move whatever is now at the Lcn, so
        after calling it we have to locate the first gap after the Lcn. */
        if (gap_begin + item->clusters_count_ - clusters_done + 16 > gap_end) {
            vacate(data, lcn, item->clusters_count_ - clusters_done, TRUE);

            result = find_gap(data, lcn, 0, 0, true, false, &gap_begin, &gap_end, TRUE);

            if (result == false) return; /* No gaps found, exit. */
        }

        /* If the gap is not big enough to hold the entire MFT then calculate how much
        will fit in the gap. */
        clusters = item->clusters_count_ - clusters_done;

        if (clusters > gap_end - gap_begin) {
            clusters = gap_end - gap_begin;
            /* It looks like a partial move only succeeds if the number of clusters is a
            multiple of 8. */
            clusters = clusters - clusters % 8;

            if (clusters == 0) {
                lcn = gap_end;

                continue;
            }
        }

        /* Move the MFT to the gap. */
        result = move_item(data, item, gap_begin, clusters_done, clusters, 0);

        if (result == true) {
            gap_begin = gap_begin + clusters;
        } else {
            result = find_gap(data, gap_begin, 0, 0, true, false, &gap_begin, &gap_end, TRUE);

            if (result == false) return; /* No gaps found, exit. */
        }

        lcn = gap_begin;
        clusters_done = clusters_done + clusters;
    }

    /* Make the MFT unmovable. We don't want it moved again by any other subroutine. */
    item->is_unmovable_ = true;

    colorize_item(data, item, 0, 0, false);
    calculate_zones(data);

    /* Note: The MftExcludes do not change by moving the MFT. */
}

/* Optimize the harddisk by filling gaps with files from above. */
void DefragLib::optimize_volume(DefragDataStruct *data) {
    ItemStruct *item;

    uint64_t gap_begin;
    uint64_t gap_end;

    DefragGui *gui = DefragGui::get_instance();

    /* Sanity check. */
    if (data->item_tree_ == nullptr) return;

    /* Process all the zones. */
    for (int zone = 0; zone < 3; zone++) {
        call_show_status(data, 5, zone); /* "Zone N: Fast Optimize" */

        /* Walk through all the gaps. */
        gap_begin = data->zones_[zone];
        int retry = 0;

        while (*data->running_ == RunningState::RUNNING) {
            /* Find the next gap. */
            bool result = find_gap(data, gap_begin, 0, 0, true, false, &gap_begin, &gap_end, FALSE);

            if (result == false) break;

            /* Update the progress counter: the number of clusters in all the files
            above the gap. Exit if there are no more files. */
            uint64_t phase_temp = 0;

            for (item = tree_biggest(data->item_tree_); item != nullptr; item = tree_prev(item)) {
                if (get_item_lcn(item) < gap_end) break;
                if (item->is_unmovable_ == true) continue;
                if (item->is_excluded_ == true) continue;

                int file_zone = 1;

                if (item->is_hog_ == true) file_zone = 2;
                if (item->is_dir_ == true) file_zone = 0;
                if (file_zone != zone) continue;

                phase_temp = phase_temp + item->clusters_count_;
            }

            data->phase_todo_ = data->phase_done_ + phase_temp;
            if (phase_temp == 0) break;

            /* Loop until the gap is filled. First look for combinations of files that perfectly
            fill the gap. If no combination can be found, or if there are less files than
            the gap is big, then fill with the highest file(s) that fit in the gap. */
            bool perfect_fit = true;
            if (gap_end - gap_begin > phase_temp) perfect_fit = false;

            while (gap_begin < gap_end && retry < 5 && *data->running_ == RunningState::RUNNING) {
                /* Find the Item that is the best fit for the gap. If nothing found (no files
                fit the gap) then exit the loop. */
                if (perfect_fit == true) {
                    item = find_best_item(data, gap_begin, gap_end, 1, zone);

                    if (item == nullptr) {
                        perfect_fit = false;

                        item = find_highest_item(data, gap_begin, gap_end, 1, zone);
                    }
                } else {
                    item = find_highest_item(data, gap_begin, gap_end, 1, zone);
                }

                if (item == nullptr) break;

                /* Move the item. */
                result = move_item(data, item, gap_begin, 0, item->clusters_count_, 0);

                if (result == true) {
                    gap_begin = gap_begin + item->clusters_count_;
                    retry = 0;
                } else {
                    gap_end = gap_begin; /* Force re-scan of gap. */
                    retry = retry + 1;
                }
            }

            /* If the gap could not be filled then skip. */
            if (gap_begin < gap_end) {
                /* Show debug message: "Skipping gap, cannot fill: %I64d[%I64d]" */
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, data->debug_msg_[28].c_str(), gap_begin,
                                gap_end - gap_begin);

                gap_begin = gap_end;
                retry = 0;
            }
        }
    }
}

/* Optimize the harddisk by moving the selected items up. */
void DefragLib::optimize_up(DefragDataStruct *data) {
    ItemStruct *item;

    uint64_t gap_begin;
    uint64_t gap_end;

    DefragGui *gui = DefragGui::get_instance();

    call_show_status(data, 6, -1); /* "Phase 3: Move Up" */

    /* Setup the progress counter: the total number of clusters in all files. */
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        data->phase_todo_ = data->phase_todo_ + item->clusters_count_;
    }

    /* Exit if nothing to do. */
    if (data->item_tree_ == nullptr) return;

    /* Walk through all the gaps. */
    gap_end = data->total_clusters_;
    int retry = 0;

    while (*data->running_ == RunningState::RUNNING) {
        /* Find the previous gap. */
        bool result = find_gap(data, data->zones_[1], gap_end, 0, true, true, &gap_begin, &gap_end, FALSE);

        if (result == false) break;

        /* Update the progress counter: the number of clusters in all the files
        below the gap. */
        uint64_t phase_temp = 0;

        for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
            if (item->is_unmovable_ == true) continue;
            if (item->is_excluded_ == true) continue;
            if (get_item_lcn(item) >= gap_end) break;

            phase_temp = phase_temp + item->clusters_count_;
        }

        data->phase_todo_ = data->phase_done_ + phase_temp;
        if (phase_temp == 0) break;

        /* Loop until the gap is filled. First look for combinations of files that perfectly
        fill the gap. If no combination can be found, or if there are less files than
        the gap is big, then fill with the highest file(s) that fit in the gap. */
        bool perfect_fit = true;
        if (gap_end - gap_begin > phase_temp) perfect_fit = false;

        while (gap_begin < gap_end && retry < 5 && *data->running_ == RunningState::RUNNING) {
            /* Find the Item that is the best fit for the gap. If nothing found (no files
            fit the gap) then exit the loop. */
            if (perfect_fit == true) {
                item = find_best_item(data, gap_begin, gap_end, 0, 3);

                if (item == nullptr) {
                    perfect_fit = false;
                    item = find_highest_item(data, gap_begin, gap_end, 0, 3);
                }
            } else {
                item = find_highest_item(data, gap_begin, gap_end, 0, 3);
            }

            if (item == nullptr) break;

            /* Move the item. */
            result = move_item(data, item, gap_end - item->clusters_count_, 0, item->clusters_count_, 1);

            if (result == true) {
                gap_end = gap_end - item->clusters_count_;
                retry = 0;
            } else {
                gap_begin = gap_end; /* Force re-scan of gap. */
                retry = retry + 1;
            }
        }

        /* If the gap could not be filled then skip. */
        if (gap_begin < gap_end) {
            /* Show debug message: "Skipping gap, cannot fill: %I64d[%I64d]" */
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, data->debug_msg_[28].c_str(), gap_begin,
                            gap_end - gap_begin);

            gap_end = gap_begin;
            retry = 0;
        }
    }
}

/* Run the defragmenter. Input is the name of a disk, mountpoint, directory, or file,
and may contain wildcards '*' and '?'. */
void DefragLib::defrag_one_path(DefragDataStruct *data, const wchar_t *path, OptimizeMode opt_mode) {
    HANDLE process_token_handle;
    LUID take_ownership_value;
    TOKEN_PRIVILEGES token_privileges;
    STARTING_LCN_INPUT_BUFFER bitmap_param;

    struct {
        uint64_t starting_lcn_;
        uint64_t bitmap_size_;
        BYTE buffer_[8];
    } bitmap_data{};

    NTFS_VOLUME_DATA_BUFFER ntfs_data;

    uint64_t free_bytes_to_caller;
    uint64_t total_bytes;
    uint64_t free_bytes;
    int result;
    uint32_t error_code;
    size_t length;
    __timeb64 time{};
    FILE *fin;
    wchar_t *p1;
    DWORD w;
    int i;
    DefragGui *gui = DefragGui::get_instance();

    /* Initialize the data. Some items are inherited from the caller and are not
    initialized. */
    data->phase_ = 0;
    data->disk_.volume_handle_ = nullptr;
    data->disk_.mount_point_ = nullptr;
    data->disk_.mount_point_slash_ = nullptr;
    data->disk_.volume_name_[0] = 0;
    data->disk_.volume_name_slash_[0] = 0;
    data->disk_.type_ = DiskType::UnknownType;
    data->item_tree_ = nullptr;
    data->balance_count_ = 0;
    data->mft_excludes_[0].start_ = 0;
    data->mft_excludes_[0].end_ = 0;
    data->mft_excludes_[1].start_ = 0;
    data->mft_excludes_[1].end_ = 0;
    data->mft_excludes_[2].start_ = 0;
    data->mft_excludes_[2].end_ = 0;
    data->total_clusters_ = 0;
    data->bytes_per_cluster_ = 0;

    for (i = 0; i < 3; i++) data->zones_[i] = 0;

    data->cannot_move_dirs_ = 0;
    data->count_directories_ = 0;
    data->count_all_files_ = 0;
    data->count_fragmented_items_ = 0;
    data->count_all_bytes_ = 0;
    data->count_fragmented_bytes_ = 0;
    data->count_all_clusters_ = 0;
    data->count_fragmented_clusters_ = 0;
    data->count_free_clusters_ = 0;
    data->count_gaps_ = 0;
    data->biggest_gap_ = 0;
    data->count_gaps_less16_ = 0;
    data->count_clusters_less16_ = 0;
    data->phase_todo_ = 0;
    data->phase_done_ = 0;

    _ftime64_s(&time);

    data->start_time_ = time.time * 1000 + time.millitm;
    data->last_checkpoint_ = data->start_time_;
    data->running_time_ = 0;

    /* Compare the item with the Exclude masks. If a mask matches then return,
    ignoring the item. */

    for (const auto &s: data->excludes_) {
        if (this->match_mask(path, s.c_str()) == true) break;
        if (wcschr(s.c_str(), L'*') == nullptr &&
            s.length() <= 3 &&
            lower_case(path[0]) == lower_case(data->excludes_[i][0]))
            break;
    }

    if (data->excludes_.size() >= i) {
        // Show debug message: "Ignoring volume '%s' because of exclude mask '%s'."
        gui->show_debug(DebugLevel::Fatal, nullptr, data->debug_msg_[47].c_str(), path, data->excludes_[i].c_str());
        return;
    }


    /* Clear the screen and show "Processing '%s'" message. */
    gui->clear_screen(data->debug_msg_[14].c_str(), path);

    /* Try to change our permissions so we can access special files and directories
    such as "C:\System Volume Information". If this does not succeed then quietly
    continue, we'll just have to do with whatever permissions we have.
    SE_BACKUP_NAME = Backup and Restore Privileges.
    */
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
                         &process_token_handle) != 0 &&
        LookupPrivilegeValue(nullptr, SE_BACKUP_NAME, &take_ownership_value) != 0) {
        token_privileges.PrivilegeCount = 1;
        token_privileges.Privileges[0].Luid = take_ownership_value;
        token_privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        if (AdjustTokenPrivileges(process_token_handle, FALSE, &token_privileges,
                                  sizeof(TOKEN_PRIVILEGES), nullptr, 0) == FALSE) {
            gui->show_debug(DebugLevel::DetailedProgress, nullptr, L"Info: could not elevate to SeBackupPrivilege.");
        }
    } else {
        gui->show_debug(DebugLevel::DetailedProgress, nullptr, L"Info: could not elevate to SeBackupPrivilege.");
    }

    /* Try finding the MountPoint by treating the input path as a path to
    something on the disk. If this does not succeed then use the Path as
    a literal MountPoint name. */
    data->disk_.mount_point_ = _wcsdup(path);
    if (data->disk_.mount_point_ == nullptr) return;

    result = GetVolumePathNameW(path, data->disk_.mount_point_, (uint32_t) wcslen(data->disk_.mount_point_) + 1);

    if (result == 0) wcscpy_s(data->disk_.mount_point_, wcslen(path) + 1, path);

    /* Make two versions of the MountPoint, one with a trailing backslash and one without. */
    p1 = wcschr(data->disk_.mount_point_, 0);

    if (p1 != data->disk_.mount_point_) {
        p1--;
        if (*p1 == '\\') *p1 = 0;
    }

    length = wcslen(data->disk_.mount_point_) + 2;

    data->disk_.mount_point_slash_ = (wchar_t *) malloc(sizeof(wchar_t) * length);

    if (data->disk_.mount_point_slash_ == nullptr) {
        free(data->disk_.mount_point_);
        return;
    }

    swprintf_s(data->disk_.mount_point_slash_, length, L"%s\\", data->disk_.mount_point_);

    /* Determine the name of the volume (something like
    "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}\"). */
    result = GetVolumeNameForVolumeMountPointW(data->disk_.mount_point_slash_,
                                               data->disk_.volume_name_slash_, MAX_PATH);

    if (result == 0) {
        if (wcslen(data->disk_.mount_point_slash_) > 52 - 1 - 4) {
            // "Cannot find volume name for mountpoint '%s': %s"
            wchar_t s1[BUFSIZ];
            system_error_str(GetLastError(), s1, BUFSIZ);

            gui->show_debug(DebugLevel::Fatal, nullptr, data->debug_msg_[40].c_str(), data->disk_.mount_point_slash_,
                            s1);

            free(data->disk_.mount_point_);
            free(data->disk_.mount_point_slash_);

            return;
        }

        swprintf_s(data->disk_.volume_name_slash_, 52, L"\\\\.\\%s", data->disk_.mount_point_slash_);
    }

    /* Make a copy of the VolumeName without the trailing backslash. */
    wcscpy_s(data->disk_.volume_name_, 51, data->disk_.volume_name_slash_);

    p1 = wcschr(data->disk_.volume_name_, 0);

    if (p1 != data->disk_.volume_name_) {
        p1--;
        if (*p1 == '\\') *p1 = 0;
    }

    /* Exit if the disk is hybernated (if "?/hiberfil.sys" exists and does not begin
    with 4 zero bytes). */
    length = wcslen(data->disk_.mount_point_slash_) + 14;

    p1 = (wchar_t *) malloc(sizeof(wchar_t) * length);

    if (p1 == nullptr) {
        free(data->disk_.mount_point_slash_);
        free(data->disk_.mount_point_);

        return;
    }

    swprintf_s(p1, length, L"%s\\hiberfil.sys", data->disk_.mount_point_slash_);

    result = _wfopen_s(&fin, p1, L"rb");

    if (result == 0 && fin != nullptr) {
        w = 0;

        if (fread(&w, 4, 1, fin) == 1 && w != 0) {
            gui->show_debug(DebugLevel::Fatal, nullptr, L"Will not process this disk, it contains hybernated data.");

            free(data->disk_.mount_point_);
            free(data->disk_.mount_point_slash_);
            free(p1);

            return;
        }
    }

    free(p1);

    /* Show debug message: "Opening volume '%s' at mountpoint '%s'" */
    gui->show_debug(DebugLevel::Fatal, nullptr, data->debug_msg_[29].c_str(), data->disk_.volume_name_,
                    data->disk_.mount_point_);

    /* Open the VolumeHandle. If error then leave. */
    data->disk_.volume_handle_ = CreateFileW(data->disk_.volume_name_, GENERIC_READ,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

    if (data->disk_.volume_handle_ == INVALID_HANDLE_VALUE) {
        wchar_t last_error[BUFSIZ];
        system_error_str(GetLastError(), last_error, BUFSIZ);

        gui->show_debug(DebugLevel::Warning, nullptr, L"Cannot open volume '%s' at mountpoint '%s': reason %s",
                        data->disk_.volume_name_, data->disk_.mount_point_, last_error);

        free(data->disk_.mount_point_);
        free(data->disk_.mount_point_slash_);

        return;
    }

    /* Determine the maximum LCN (maximum cluster number). A single call to
    FSCTL_GET_VOLUME_BITMAP is enough, we don't have to walk through the
    entire bitmap.
    It's a pity we have to do it in this roundabout manner, because
    there is no system call that reports the total number of clusters
    in a volume. GetDiskFreeSpace() does, but is limited to 2Gb volumes,
    GetDiskFreeSpaceEx() reports in bytes, not clusters, _getdiskfree()
    requires a drive letter so cannot be used on unmounted volumes or
    volumes that are mounted on a directory, and FSCTL_GET_NTFS_VOLUME_DATA
    only works for NTFS volumes. */
    bitmap_param.StartingLcn.QuadPart = 0;

    //	long koko = FSCTL_GET_VOLUME_BITMAP;

    error_code = DeviceIoControl(data->disk_.volume_handle_, FSCTL_GET_VOLUME_BITMAP,
                                 &bitmap_param, sizeof bitmap_param, &bitmap_data, sizeof bitmap_data, &w, nullptr);

    if (error_code != 0) {
        error_code = NO_ERROR;
    } else {
        error_code = GetLastError();
    }

    if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) {
        /* Show debug message: "Cannot defragment volume '%s' at mountpoint '%s'" */
        gui->show_debug(DebugLevel::Fatal, nullptr, data->debug_msg_[32].c_str(), data->disk_.volume_name_,
                        data->disk_.mount_point_);

        CloseHandle(data->disk_.volume_handle_);

        free(data->disk_.mount_point_);
        free(data->disk_.mount_point_slash_);

        return;
    }

    data->total_clusters_ = bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_;

    /* Determine the number of bytes per cluster.
    Again I have to do this in a roundabout manner. As far as I know there is
    no system call that returns the number of bytes per cluster, so first I have
    to get the total size of the disk and then divide by the number of clusters.
    */
    error_code = GetDiskFreeSpaceExW(path, (PULARGE_INTEGER) &free_bytes_to_caller,
                                     (PULARGE_INTEGER) &total_bytes, (PULARGE_INTEGER) &free_bytes);

    if (error_code != 0) data->bytes_per_cluster_ = total_bytes / data->total_clusters_;

    /* Setup the list of clusters that cannot be used. The Master File
    Table cannot be moved and cannot be used by files. All this is
    only necessary for NTFS volumes. */
    error_code = DeviceIoControl(data->disk_.volume_handle_, FSCTL_GET_NTFS_VOLUME_DATA,
                                 nullptr, 0, &ntfs_data, sizeof ntfs_data, &w, nullptr);

    if (error_code != 0) {
        /* Note: NtfsData.TotalClusters.QuadPart should be exactly the same
        as the Data->TotalClusters that was determined in the previous block. */

        data->bytes_per_cluster_ = ntfs_data.BytesPerCluster;

        data->mft_excludes_[0].start_ = ntfs_data.MftStartLcn.QuadPart;
        data->mft_excludes_[0].end_ = ntfs_data.MftStartLcn.QuadPart +
                                      ntfs_data.MftValidDataLength.QuadPart / ntfs_data.BytesPerCluster;
        data->mft_excludes_[1].start_ = ntfs_data.MftZoneStart.QuadPart;
        data->mft_excludes_[1].end_ = ntfs_data.MftZoneEnd.QuadPart;
        data->mft_excludes_[2].start_ = ntfs_data.Mft2StartLcn.QuadPart;
        data->mft_excludes_[2].end_ = ntfs_data.Mft2StartLcn.QuadPart +
                                      ntfs_data.MftValidDataLength.QuadPart / ntfs_data.BytesPerCluster;

        /* Show debug message: "MftStartLcn=%I64d, MftZoneStart=%I64d, MftZoneEnd=%I64d, Mft2StartLcn=%I64d, MftValidDataLength=%I64d" */
        gui->show_debug(DebugLevel::DetailedProgress, nullptr, data->debug_msg_[33].c_str(),
                        ntfs_data.MftStartLcn.QuadPart, ntfs_data.MftZoneStart.QuadPart,
                        ntfs_data.MftZoneEnd.QuadPart, ntfs_data.Mft2StartLcn.QuadPart,
                        ntfs_data.MftValidDataLength.QuadPart / ntfs_data.BytesPerCluster);

        /* Show debug message: "MftExcludes[%u].Start=%I64d, MftExcludes[%u].End=%I64d" */
        gui->show_debug(DebugLevel::DetailedProgress, nullptr, data->debug_msg_[34].c_str(), 0,
                        data->mft_excludes_[0].start_,
                        0,
                        data->mft_excludes_[0].end_);
        gui->show_debug(DebugLevel::DetailedProgress, nullptr, data->debug_msg_[34].c_str(), 1,
                        data->mft_excludes_[1].start_,
                        1,
                        data->mft_excludes_[1].end_);
        gui->show_debug(DebugLevel::DetailedProgress, nullptr, data->debug_msg_[34].c_str(), 2,
                        data->mft_excludes_[2].start_,
                        2,
                        data->mft_excludes_[2].end_);
    }

    /* Fixup the input mask.
    - If the length is 2 or 3 characters then rewrite into "c:\*".
    - If it does not contain a wildcard then append '*'.
    */
    length = wcslen(path) + 3;

    data->include_mask_ = (wchar_t *) malloc(sizeof(wchar_t) * length);

    if (data->include_mask_ == nullptr) return;

    wcscpy_s(data->include_mask_, length, path);

    if (wcslen(path) == 2 || wcslen(path) == 3) {
        swprintf_s(data->include_mask_, length, L"%c:\\*", lower_case(path[0]));
    } else if (wcschr(path, L'*') == nullptr) {
        swprintf_s(data->include_mask_, length, L"%s*", path);
    }

    gui->show_debug(DebugLevel::Fatal, nullptr, L"Input mask: %s", data->include_mask_);

    /* Defragment and optimize. */
    gui->show_diskmap(data);

    if (*data->running_ == RunningState::RUNNING) analyze_volume(data);

    if (*data->running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeFixup) {
        defragment(data);
    }

    if (*data->running_ == RunningState::RUNNING
        && (opt_mode == OptimizeMode::AnalyzeFixupFastopt
            || opt_mode == OptimizeMode::DeprecatedAnalyzeFixupFull)) {
        defragment(data);

        if (*data->running_ == RunningState::RUNNING) fixup(data);
        if (*data->running_ == RunningState::RUNNING) optimize_volume(data);
        if (*data->running_ == RunningState::RUNNING) fixup(data); /* Again, in case of new zone startpoint. */
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeGroup) {
        forced_fill(data);
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeMoveToEnd) {
        optimize_up(data);
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeSortByName) {
        optimize_sort(data, 0); /* Filename */
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeSortBySize) {
        optimize_sort(data, 1); /* Filesize */
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeSortByAccess) {
        optimize_sort(data, 2); /* Last access */
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeSortByChanged) {
        optimize_sort(data, 3); /* Last change */
    }

    if (*data->running_ == RunningState::RUNNING && opt_mode == OptimizeMode::AnalyzeSortByCreated) {
        optimize_sort(data, 4); /* Creation */
    }
    /*
    if ((*Data->Running == RUNNING) && (Mode == 11)) {
    MoveMftToBeginOfDisk(Data);
    }
    */

    call_show_status(data, 7, -1); /* "Finished." */

    /* Close the volume handles. */
    if (data->disk_.volume_handle_ != nullptr &&
        data->disk_.volume_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(data->disk_.volume_handle_);
    }

    /* Cleanup. */
    delete_item_tree(data->item_tree_);

    if (data->disk_.mount_point_ != nullptr) free(data->disk_.mount_point_);
    if (data->disk_.mount_point_slash_ != nullptr) free(data->disk_.mount_point_slash_);
}

/* Subfunction for DefragAllDisks(). It will ignore removable disks, and
will iterate for disks that are mounted on a subdirectory of another
disk (instead of being mounted on a drive). */
void DefragLib::defrag_mountpoints(DefragDataStruct *data, const wchar_t *mount_point, const OptimizeMode opt_mode) {
    wchar_t volume_name_slash[BUFSIZ];
    wchar_t volume_name[BUFSIZ];
    DWORD file_system_flags;
    HANDLE find_mountpoint_handle;
    wchar_t root_path[MAX_PATH + BUFSIZ];
    std::unique_ptr<wchar_t[]> full_root_path;
    HANDLE volume_handle;
    int result;
    size_t length;
    uint32_t error_code;
    wchar_t s1[BUFSIZ];
    wchar_t *p1;
    DWORD w;
    DefragGui *gui = DefragGui::get_instance();

    if (*data->running_ != RunningState::RUNNING) return;

    /* Clear the screen and show message "Analyzing volume '%s'" */
    gui->clear_screen(data->debug_msg_[37].c_str(), mount_point);

    /* Return if this is not a fixed disk. */

    if (const int drive_type = GetDriveTypeW(mount_point); drive_type != DRIVE_FIXED) {
        if (drive_type == DRIVE_UNKNOWN) {
            gui->clear_screen(L"Ignoring volume '%s' because the drive type cannot be determined.", mount_point);
        }

        if (drive_type == DRIVE_NO_ROOT_DIR) {
            gui->clear_screen(L"Ignoring volume '%s' because there is no volume mounted.", mount_point);
        }

        if (drive_type == DRIVE_REMOVABLE) {
            gui->clear_screen(L"Ignoring volume '%s' because it has removable media.", mount_point);
        }

        if (drive_type == DRIVE_REMOTE) {
            gui->clear_screen(L"Ignoring volume '%s' because it is a remote (network) drive.", mount_point);
        }

        if (drive_type == DRIVE_CDROM) {
            gui->clear_screen(L"Ignoring volume '%s' because it is a CD-ROM drive.", mount_point);
        }

        if (drive_type == DRIVE_RAMDISK) {
            gui->clear_screen(L"Ignoring volume '%s' because it is a RAM disk.", mount_point);
        }

        return;
    }

    /* Determine the name of the volume, something like
    "\\?\Volume{08439462-3004-11da-bbca-806d6172696f}\". */
    result = GetVolumeNameForVolumeMountPointW(mount_point, volume_name_slash, BUFSIZ);

    if (result == 0) {
        error_code = GetLastError();

        if (error_code == 3) {
            /* "Ignoring volume '%s' because it is not a harddisk." */
            gui->show_debug(DebugLevel::Fatal, nullptr, data->debug_msg_[57].c_str(), mount_point);
        } else {
            /* "Cannot find volume name for mountpoint: %s" */
            system_error_str(error_code, s1, BUFSIZ);

            gui->show_debug(DebugLevel::Fatal, nullptr, data->debug_msg_[40].c_str(), mount_point, s1);
        }

        return;
    }

    /* Return if the disk is read-only. */
    GetVolumeInformationW(volume_name_slash, nullptr, 0, nullptr, nullptr, &file_system_flags, nullptr, 0);

    if ((file_system_flags & FILE_READ_ONLY_VOLUME) != 0) {
        /* Clear the screen and show message "Ignoring disk '%s' because it is read-only." */
        gui->clear_screen(data->debug_msg_[36].c_str(), mount_point);

        return;
    }

    /* If the volume is not mounted then leave. Unmounted volumes can be
    defragmented, but the system administrator probably has unmounted
    the volume because he wants it untouched. */
    wcscpy_s(volume_name, BUFSIZ, volume_name_slash);

    p1 = wcschr(volume_name, 0);

    if (p1 != volume_name) {
        p1--;
        if (*p1 == '\\') *p1 = 0;
    }

    volume_handle = CreateFileW(volume_name, GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

    if (volume_handle == INVALID_HANDLE_VALUE) {
        system_error_str(GetLastError(), s1, BUFSIZ);

        gui->show_debug(DebugLevel::Warning, nullptr, L"Cannot open volume '%s' at mountpoint '%s': %s",
                        volume_name, mount_point, s1);

        return;
    }

    if (DeviceIoControl(volume_handle, FSCTL_IS_VOLUME_MOUNTED, nullptr, 0, nullptr, 0, &w, nullptr) == 0) {
        /* Show debug message: "Volume '%s' at mountpoint '%s' is not mounted." */
        gui->show_debug(DebugLevel::Fatal, nullptr, data->debug_msg_[31].c_str(), volume_name, mount_point);

        CloseHandle(volume_handle);

        return;
    }

    CloseHandle(volume_handle);

    /* Defrag the disk. */
    length = wcslen(mount_point) + 2;

    p1 = (wchar_t *) malloc(sizeof(wchar_t) * length);

    if (p1 != nullptr) {
        swprintf_s(p1, length, L"%s*", mount_point);

        defrag_one_path(data, p1, opt_mode);

        free(p1);
    }

    /* According to Microsoft I should check here if the disk has support for
    reparse points:
    if ((file_system_flags & FILE_SUPPORTS_REPARSE_POINTS) == 0) return;
    However, I have found this test will frequently cause a false return
    on Windows 2000. So I've removed it, everything seems to be working
    nicely without it. */

    /* Iterate for all the mountpoints on the disk. */
    find_mountpoint_handle = FindFirstVolumeMountPointW(volume_name_slash, root_path, MAX_PATH + BUFSIZ);

    if (find_mountpoint_handle == INVALID_HANDLE_VALUE) return;

    do {
        length = wcslen(mount_point) + wcslen(root_path) + 1;
        full_root_path = std::make_unique<wchar_t[]>(length);

        if (full_root_path != nullptr) {
            swprintf_s(full_root_path.get(), length, L"%s%s", mount_point, root_path);
            defrag_mountpoints(data, full_root_path.get(), opt_mode);
        }
    } while (FindNextVolumeMountPointW(find_mountpoint_handle, root_path, MAX_PATH + BUFSIZ) != 0);

    FindVolumeMountPointClose(find_mountpoint_handle);
}

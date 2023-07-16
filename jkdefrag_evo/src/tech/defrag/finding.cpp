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

/**
 * \brief Look for a gap, a block of empty clusters on the volume.
 * \param minimum_lcn Start scanning for gaps at this location. If there is a gap at this location then return it. Zero is the begin of the disk.
 * \param maximum_lcn Stop scanning for gaps at this location. Zero is the end of the disk.
 * \param minimum_size The gap must have at least this many contiguous free clusters. Zero will match any gap, so will return the first gap at or above MinimumLcn.
 * \param must_fit if true then only return a gap that is bigger/equal than the MinimumSize. If false then return a gap bigger/equal than MinimumSize,
 *      or if no such gap is found return the largest gap on the volume (above MinimumLcn).
 * \param find_highest_gap if false then return the lowest gap that is bigger/equal than the MinimumSize. If true then return the highest gap.
 * \param begin_lcn out: LCN of begin of cluster
 * \param end_lcn out: LCN of end of cluster
 * \param ignore_mft_excludes
 * \return true if succes, false if no gap was found or an error occurred. The routine asks Windows for the cluster bitmap every time. It would be
 *  faster to cache the bitmap in memory, but that would cause more fails because of stale information.
 */
bool DefragLib::find_gap(const DefragState *data, const uint64_t minimum_lcn, uint64_t maximum_lcn,
                         const uint64_t minimum_size,
                         const int must_fit, const bool find_highest_gap, uint64_t *begin_lcn, uint64_t *end_lcn,
                         const bool ignore_mft_excludes) {
    STARTING_LCN_INPUT_BUFFER bitmap_param;
    struct {
        uint64_t starting_lcn_;
        uint64_t bitmap_size_;
        BYTE buffer_[65536]; // Most efficient if binary multiple
    } bitmap_data{};

    uint32_t error_code;
    DWORD w;
    DefragGui *gui = DefragGui::get_instance();

    // Sanity check
    if (minimum_lcn >= data->total_clusters_) return false;

    // Main loop to walk through the entire clustermap
    uint64_t lcn = minimum_lcn;
    uint64_t cluster_start = 0;
    int prev_in_use = 1;
    uint64_t highest_begin_lcn = 0;
    uint64_t highest_end_lcn = 0;
    uint64_t largest_begin_lcn = 0;
    uint64_t largest_end_lcn = 0;

    do {
        // Fetch a block of cluster data. If error then return false
        bitmap_param.StartingLcn.QuadPart = lcn;
        error_code = DeviceIoControl(data->disk_.volume_handle_, FSCTL_GET_VOLUME_BITMAP,
                                     &bitmap_param, sizeof bitmap_param, &bitmap_data, sizeof bitmap_data, &w, nullptr);

        if (error_code != 0) {
            error_code = NO_ERROR;
        } else {
            error_code = GetLastError();
        }

        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) {
            // Show debug message: "ERROR: could not get volume bitmap: %s"
            auto error_string = system_error_str(GetLastError());
            gui->show_debug(DebugLevel::Warning, nullptr,
                            std::format(L"ERROR: could not get volume bitmap: {}", error_string));

            return false;
        }

        // Sanity check
        if (lcn >= bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_) return false;
        if (maximum_lcn == 0) maximum_lcn = bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_;

        // Analyze the clusterdata. We resume where the previous block left off. If a cluster is found that matches the criteria then return it's LCN (Logical Cluster Number)
        lcn = bitmap_data.starting_lcn_;
        int index = 0;
        BYTE mask = 1;

        int index_max = sizeof bitmap_data.buffer_;

        if (bitmap_data.bitmap_size_ / 8 < index_max) index_max = (int) (bitmap_data.bitmap_size_ / 8);

        while (index < index_max && lcn < maximum_lcn) {
            if (lcn >= minimum_lcn) {
                int in_use = bitmap_data.buffer_[index] & mask;

                if ((lcn >= data->mft_excludes_[0].start_ && lcn < data->mft_excludes_[0].end_) ||
                    (lcn >= data->mft_excludes_[1].start_ && lcn < data->mft_excludes_[1].end_) ||
                    (lcn >= data->mft_excludes_[2].start_ && lcn < data->mft_excludes_[2].end_)) {
                    if (!ignore_mft_excludes) in_use = 1;
                }

                if (prev_in_use == 0 && in_use != 0) {
                    // Show debug message: "Gap found: LCN=%I64d, Size=%I64d"
                    gui->show_debug(
                            DebugLevel::DetailedGapFinding, nullptr,
                            std::format(GAP_FOUND_FMT, cluster_start, lcn - cluster_start));

                    /* If the gap is bigger/equal than the mimimum size then return it,
                    or remember it, depending on the FindHighestGap parameter. */
                    if (cluster_start >= minimum_lcn &&
                        lcn - cluster_start >= minimum_size) {
                        if (!find_highest_gap) {
                            if (begin_lcn != nullptr) *begin_lcn = cluster_start;

                            if (end_lcn != nullptr) *end_lcn = lcn;

                            return true;
                        }

                        highest_begin_lcn = cluster_start;
                        highest_end_lcn = lcn;
                    }

                    // Remember the largest gap on the volume
                    if (largest_begin_lcn == 0 ||
                        largest_end_lcn - largest_begin_lcn < lcn - cluster_start) {
                        largest_begin_lcn = cluster_start;
                        largest_end_lcn = lcn;
                    }
                }

                if (prev_in_use != 0 && in_use == 0) cluster_start = lcn;

                prev_in_use = in_use;
            }

            if (mask == 128) {
                mask = 1;
                index = index + 1;
            } else {
                mask = mask << 1;
            }

            lcn = lcn + 1;
        }
    } while (error_code == ERROR_MORE_DATA &&
             lcn < bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_ &&
             lcn < maximum_lcn);

    // Process the last gap
    if (prev_in_use == 0) {
        // Show debug message: "Gap found: LCN=%I64d, Size=%I64d"
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(GAP_FOUND_FMT, cluster_start, lcn - cluster_start));

        if (cluster_start >= minimum_lcn && lcn - cluster_start >= minimum_size) {
            if (!find_highest_gap) {
                if (begin_lcn != nullptr) *begin_lcn = cluster_start;
                if (end_lcn != nullptr) *end_lcn = lcn;

                return true;
            }

            highest_begin_lcn = cluster_start;
            highest_end_lcn = lcn;
        }

        // Remember the largest gap on the volume
        if (largest_begin_lcn == 0 ||
            largest_end_lcn - largest_begin_lcn < lcn - cluster_start) {
            largest_begin_lcn = cluster_start;
            largest_end_lcn = lcn;
        }
    }

    // If the FindHighestGap flag is true then return the highest gap we have found
    if (find_highest_gap && highest_begin_lcn != 0) {
        if (begin_lcn != nullptr) *begin_lcn = highest_begin_lcn;
        if (end_lcn != nullptr) *end_lcn = highest_end_lcn;

        return true;
    }

    // If the MustFit flag is false then return the largest gap we have found
    if (must_fit == false && largest_begin_lcn != 0) {
        if (begin_lcn != nullptr) *begin_lcn = largest_begin_lcn;
        if (end_lcn != nullptr) *end_lcn = largest_end_lcn;

        return true;
    }

    // No gap found, return false
    return false;
}

/**
 * \brief Look in the ItemTree and return the highest file above the gap that fits inside the gap (cluster start - cluster end).
 * \param direction 0=Search for files below the gap, 1=above
 * \param zone 0=only directories, 1=only regular files, 2=only space hogs, 3=all
 * \return Return a pointer to the item, or nullptr if no file could be found
 */
ItemStruct *DefragLib::find_highest_item(const DefragState *data, const uint64_t cluster_start,
                                         const uint64_t cluster_end, const Tree::Direction direction, const int zone) {
    DefragGui *gui = DefragGui::get_instance();

    // "Looking for highest-fit %I64d[%I64d]"
    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                    std::format(L"Looking for highest-fit start=" NUM_FMT " [" NUM_FMT " clusters]",
                                cluster_start, cluster_end - cluster_start));

    /* Walk backwards through all the items on disk and select the first
    file that fits inside the free block. If we find an exact match then
    immediately return it. */
    const auto step_direction =
            direction == Tree::Direction::First ? Tree::StepDirection::StepForward : Tree::StepDirection::StepBack;

    for (auto item = Tree::first(data->item_tree_, direction);
         item != nullptr;
         item = Tree::next_prev(item, step_direction)) {
        const auto item_lcn = item->get_item_lcn();

        if (item_lcn == 0) continue;

        if (direction == 1) {
            if (item_lcn < cluster_end) return nullptr;
        } else {
            if (item_lcn > cluster_start) return nullptr;
        }

        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;

        if (zone != 3) {
            int file_zone = 1;

            if (item->is_hog_) file_zone = 2;
            if (item->is_dir_) file_zone = 0;
            if (zone != file_zone) continue;
        }

        if (item->clusters_count_ > cluster_end - cluster_start) continue;

        return item;
    }

    return nullptr;
}

/*

Find the highest item on disk that fits inside the gap (cluster start - cluster
end), and combined with other items will perfectly fill the gap. Return nullptr if
no perfect fit could be found. The subroutine will limit it's running time to 0.5
seconds.
Direction=0      Search for files below the gap.
Direction=1      Search for files above the gap.
Zone=0           Only search the directories.
Zone=1           Only search the regular files.
Zone=2           Only search the SpaceHogs.
Zone=3           Search all items.

*/
ItemStruct *DefragLib::find_best_item(const DefragState *data, const uint64_t cluster_start,
                                      const uint64_t cluster_end, const Tree::Direction direction, const int zone) {
    __timeb64 time{};
    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                    std::format(L"Looking for perfect fit start=" NUM_FMT " [" NUM_FMT " clusters]",
                                cluster_start, cluster_end - cluster_start));

    /* Walk backwards through all the items on disk and select the first item that
    fits inside the free block, and combined with other items will fill the gap
    perfectly. If we find an exact match then immediately return it. */

    _ftime64_s(&time);

    const int64_t MaxTime = time.time * 1000 + time.millitm + 500;
    ItemStruct *first_item = nullptr;
    uint64_t gap_size = cluster_end - cluster_start;
    uint64_t total_items_size = 0;

    const auto step_direction =
            direction == Tree::Direction::First ? Tree::StepDirection::StepForward : Tree::StepDirection::StepBack;

    for (auto item = Tree::first(data->item_tree_, direction);
         item != nullptr;
         item = Tree::next_prev(item, step_direction)) {
        // If we have passed the top of the gap then...
        const auto item_lcn = item->get_item_lcn();

        if (item_lcn == 0) continue;

        if ((direction == 1 && item_lcn < cluster_end) ||
            (direction == 0 && item_lcn > cluster_end)) {
            // If we did not find an item that fits inside the gap then exit
            if (first_item == nullptr) break;

            /* Exit if the total size of all the items is less than the size of the gap.
            We know that we can never find a perfect fit. */
            if (total_items_size < cluster_end - cluster_start) {
                gui->show_debug(
                        DebugLevel::DetailedGapFilling, nullptr,
                        L"No perfect fit found, the total size of all the items above the gap is less than the size of the gap.");

                return nullptr;
            }

            // Exit if the running time is more than 0.5 seconds
            _ftime64_s(&time);

            if (time.time * 1000 + time.millitm > MaxTime) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No perfect fit found, out of time.");

                return nullptr;
            }

            /* Rewind and try again. The item that we have found previously fits in the
            gap, but it does not combine with other items to perfectly fill the gap. */
            item = first_item;
            first_item = nullptr;
            gap_size = cluster_end - cluster_start;
            total_items_size = 0;

            continue;
        }

        // Ignore all unsuitable items
        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;

        if (zone != 3) {
            int file_zone = 1;

            if (item->is_hog_) file_zone = 2;
            if (item->is_dir_) file_zone = 0;
            if (zone != file_zone) continue;
        }

        if (item->clusters_count_ < cluster_end - cluster_start) {
            total_items_size = total_items_size + item->clusters_count_;
        }

        if (item->clusters_count_ > gap_size) continue;

        /* Exit if this item perfectly fills the gap, or if we have found a combination
        with a previous item that perfectly fills the gap. */
        if (item->clusters_count_ == gap_size) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Perfect fit found.");

            if (first_item != nullptr) return first_item;

            return item;
        }

        /* We have found an item that fit's inside the gap, but does not perfectly fill
        the gap. We are now looking to fill a smaller gap. */
        gap_size = gap_size - item->clusters_count_;

        // Remember the first item that fits inside the gap
        if (first_item == nullptr) first_item = item;
    }

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                    L"No perfect fit found, all items above the gap are bigger than the gap.");

    return nullptr;
}

/*
Return the LCN of the fragment that contains a cluster at the LCN. If the
item has no fragment that occupies the LCN then return zero.
*/
uint64_t DefragLib::find_fragment_begin(const ItemStruct *item, const uint64_t lcn) {
    // Sanity check
    if (item == nullptr || lcn == 0) return 0;

    /* Walk through all the fragments of the item. If a fragment is found
    that contains the LCN then return the begin of that fragment. */
    uint64_t vcn = 0;
    for (const FragmentListStruct *fragment = item->fragments_; fragment != nullptr; fragment = fragment->next_) {
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            if (lcn >= fragment->lcn_ &&
                lcn < fragment->lcn_ + fragment->next_vcn_ - vcn) {
                return fragment->lcn_;
            }
        }

        vcn = fragment->next_vcn_;
    }

    // Not found: return zero
    return 0;
}

/*

Search the list for the item that occupies the cluster at the LCN. Return a
pointer to the item. If not found then return nullptr.

*/
[[maybe_unused]] ItemStruct *DefragLib::find_item_at_lcn(const DefragState *data, const uint64_t lcn) {
    /* Locate the item by descending the sorted tree in memory. If found then
    return the item. */
    ItemStruct *item = data->item_tree_;

    while (item != nullptr) {
        const auto item_lcn = item->get_item_lcn();

        if (item_lcn == lcn) return item;

        if (lcn < item_lcn) {
            item = item->smaller_;
        } else {
            item = item->bigger_;
        }
    }

    /* Walk through all the fragments of all the items in the sorted tree. If a
    fragment is found that occupies the LCN then return a pointer to the item. */
    for (item = Tree::smallest(data->item_tree_); item != nullptr; item = Tree::next(item)) {
        if (find_fragment_begin(item, lcn) != 0) return item;
    }

    // LCN not found, return nullptr
    return nullptr;
}

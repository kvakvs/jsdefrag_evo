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
//
// TODO: Very slow, improve search algorithm
//
bool
DefragRunner::find_gap(const DefragState &data, const Clusters64 minimum_lcn, Clusters64 maximum_lcn,
                       const Clusters64 minimum_size, const bool must_fit, const bool find_highest_gap,
                       PARAM_OUT Clusters64 &begin_lcn, PARAM_OUT Clusters64 &end_lcn,
                       const bool ignore_mft_excludes) {
    StopWatch clock_fg(L"find_gap", true);
    STARTING_LCN_INPUT_BUFFER bitmap_param;
    struct {
        Clusters64 starting_lcn_;
        Clusters64 bitmap_size_;
        BYTE buffer_[65536]; // Most efficient if binary multiple
    } bitmap_data{};

    uint32_t error_code;
    DWORD w;
    DefragGui *gui = DefragGui::get_instance();

    // Sanity check
    if (minimum_lcn >= data.total_clusters_) return false;

    // Main loop to walk through the entire clustermap
    Clusters64 lcn = minimum_lcn;
    Clusters64 cluster_start;
    int prev_in_use = 1;
    Clusters64 highest_begin_lcn;
    Clusters64 highest_end_lcn;
    Clusters64 largest_begin_lcn;
    Clusters64 largest_end_lcn;

    do {
        // Fetch a block of cluster data. If error then return false
        bitmap_param.StartingLcn.QuadPart = lcn.as<LONGLONG>();
        error_code = DeviceIoControl(data.disk_.volume_handle_, FSCTL_GET_VOLUME_BITMAP,
                                     &bitmap_param, sizeof bitmap_param, &bitmap_data,
                                     sizeof bitmap_data, &w, nullptr);

        if (error_code != 0) {
            error_code = NO_ERROR;
        } else {
            error_code = GetLastError();
        }

        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) {
            // Show debug message: "ERROR: could not get volume bitmap: %s"
            auto error_string = Str::system_error(GetLastError());
            gui->show_debug(DebugLevel::Warning, nullptr,
                            std::format(L"ERROR: could not get volume bitmap: {}", error_string));

            return false;
        }

        // Sanity check
        if (lcn >= bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_) return false;
        if (maximum_lcn.is_zero()) maximum_lcn = bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_;

        // Analyze the clusterdata. We resume where the previous block left off. If a cluster is found that matches the
        //  criteria, then return its LCN (Logical Cluster Number)
        lcn = bitmap_data.starting_lcn_;
        uint8_t mask = 1;
        size_t index = 0;
        size_t index_max = clamp_above(sizeof bitmap_data.buffer_, bitmap_data.bitmap_size_.value() / 8);

        while (index < index_max && lcn < maximum_lcn) {
            if (lcn >= minimum_lcn) {
                int in_use = bitmap_data.buffer_[index] & mask;

                if ((lcn >= data.mft_excludes_[0].start_ && lcn < data.mft_excludes_[0].end_) ||
                    (lcn >= data.mft_excludes_[1].start_ && lcn < data.mft_excludes_[1].end_) ||
                    (lcn >= data.mft_excludes_[2].start_ && lcn < data.mft_excludes_[2].end_)) {
                    if (!ignore_mft_excludes) in_use = 1;
                }

                if (prev_in_use == 0 && in_use != 0) {
                    // Show debug message: "Gap found: LCN=%I64d, Size=%I64d"
                    gui->show_debug(
                            DebugLevel::DetailedGapFinding, nullptr,
                            std::format(GAP_FOUND_FMT, cluster_start.value(), (lcn - cluster_start).value())
                    );

                    // If the gap is bigger/equal than the mimimum size then return it,
                    // or remember it, depending on the FindHighestGap parameter. */
                    if (cluster_start >= minimum_lcn &&
                        lcn - cluster_start >= minimum_size) {
                        if (!find_highest_gap) {
                            PARAM_OUT begin_lcn = cluster_start;
                            PARAM_OUT end_lcn = lcn;
                            return true;
                        }

                        highest_begin_lcn = cluster_start;
                        highest_end_lcn = lcn;
                    }

                    // Remember the largest gap on the volume
                    if (largest_begin_lcn.is_zero() ||
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

            lcn++;
        }
    } while (error_code == ERROR_MORE_DATA &&
             lcn < bitmap_data.starting_lcn_ + bitmap_data.bitmap_size_ &&
             lcn < maximum_lcn);

    // Process the last gap
    if (prev_in_use == 0) {
        // Show debug message: "Gap found: LCN=%I64d, Size=%I64d"
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(GAP_FOUND_FMT, cluster_start.value(), (lcn - cluster_start).value()));

        if (cluster_start >= minimum_lcn && lcn - cluster_start >= minimum_size) {
            if (!find_highest_gap) {
                PARAM_OUT begin_lcn = cluster_start;
                PARAM_OUT end_lcn = lcn;
                return true;
            }

            highest_begin_lcn = cluster_start;
            highest_end_lcn = lcn;
        }

        // Remember the largest gap on the volume
        if (largest_begin_lcn.is_zero() ||
            largest_end_lcn - largest_begin_lcn < lcn - cluster_start) {
            largest_begin_lcn = cluster_start;
            largest_end_lcn = lcn;
        }
    }

    // If the FindHighestGap flag is true then return the highest gap we have found
    if (find_highest_gap && highest_begin_lcn) {
        PARAM_OUT begin_lcn = highest_begin_lcn;
        PARAM_OUT end_lcn = highest_end_lcn;
        return true;
    }

    // If the MustFit flag is false then return the largest gap we have found
    if (!must_fit && largest_begin_lcn) {
        PARAM_OUT begin_lcn = largest_begin_lcn;
        PARAM_OUT end_lcn = largest_end_lcn;
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
FileNode *DefragRunner::find_highest_item(const DefragState &data, const Clusters64 cluster_start,
                                          const Clusters64 cluster_end, const Tree::Direction direction,
                                          const Zone zone) {
    DefragGui *gui = DefragGui::get_instance();

    // "Looking for highest-fit %I64d[%I64d]"
    gui->show_debug(
            DebugLevel::DetailedGapFilling, nullptr,
            std::format(L"Looking for highest-fit start=" NUM_FMT " [" NUM_FMT " clusters]",
                        cluster_start.value(), (cluster_end - cluster_start).value())
    );

    // Walk backwards through all the items on the disk and select the first file that fits inside the free block.
    // If we find an exact match, then immediately return it.
    const auto step_direction =
            direction == Tree::Direction::First ? Tree::StepDirection::StepForward : Tree::StepDirection::StepBack;

    for (auto item = Tree::first(data.item_tree_, direction);
         item != nullptr;
         item = Tree::next_prev(item, step_direction)) {
        const auto item_lcn = item->get_item_lcn();

        if (item_lcn.is_zero()) continue;

        if (direction == 1) {
            if (item_lcn < cluster_end) return nullptr;
        } else {
            if (item_lcn > cluster_start) return nullptr;
        }

        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;

        if (zone != Zone::All_MaxValue) {
            auto preferred_zone = item->get_preferred_zone();
            if (zone != preferred_zone) continue;
        }

        if (item->clusters_count_ > cluster_end - cluster_start) continue;

        return item;
    }

    return nullptr;
}

// Find the highest item on disk that fits inside the gap (cluster start - cluster end), and combined with other items
// will perfectly fill the gap. Return nullptr if no perfect fit could be found. The subroutine will limit it's running
// time to 0.5 seconds.
// Direction=0 - Search for files below the gap.
// Direction=1 - Search for files above the gap.
FileNode *DefragRunner::find_best_item(const DefragState &data, const Clusters64 cluster_start,
                                       const Clusters64 cluster_end, const Tree::Direction direction, const Zone zone) {
    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                    std::format(L"Looking for perfect fit start=" NUM_FMT " [" NUM_FMT " clusters]",
                                cluster_start.value(), (cluster_end - cluster_start).value()));

    // Walk backwards through all the items on disk and select the first item that
    // fits inside the free block, and combined with other items will fill the gap
    // perfectly. If we find an exact match then immediately return it.
    const Clock::time_point max_time = Clock::now() + std::chrono::milliseconds(500);
    FileNode *first_item = nullptr;
    Clusters64 gap_size = cluster_end - cluster_start;
    Clusters64 total_items_size = {};

    const auto step_direction =
            direction == Tree::Direction::First ? Tree::StepDirection::StepForward : Tree::StepDirection::StepBack;

    for (auto item = Tree::first(data.item_tree_, direction);
         item != nullptr;
         item = Tree::next_prev(item, step_direction)) {
        // If we have passed the top of the gap then...
        const auto item_lcn = item->get_item_lcn();

        if (item_lcn.is_zero()) continue;

        if ((direction == 1 && item_lcn < cluster_end) ||
            (direction == 0 && item_lcn > cluster_end)) {
            // If we did not find an item that fits inside the gap then exit
            if (first_item == nullptr) break;

            // Exit if the total size of all the items is less than the size of the gap. We know that we can never
            // find a perfect fit
            if (total_items_size < cluster_end - cluster_start) {
                gui->show_debug(
                        DebugLevel::DetailedGapFilling, nullptr,
                        L"No perfect fit found, the total size of all the items above the gap is less than the size of the gap.");

                return nullptr;
            }

            // Exit if the running time is more than 0.5 seconds
            if (Clock::now() > max_time) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No perfect fit found, out of time.");

                return nullptr;
            }

            // Rewind and try again. The item that we have found previously fits in the
            // gap, but it does not combine with other items to perfectly fill the gap.
            item = first_item;
            first_item = nullptr;
            gap_size = cluster_end - cluster_start;
            total_items_size = {};

            continue;
        }

        // Ignore all unsuitable items
        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;

        if (zone != Zone::All_MaxValue) {
            auto preferred_zone = item->get_preferred_zone();
            if (zone != preferred_zone) continue;
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

// Return the LCN of the fragment that contains a cluster at the LCN. If the
// item has no fragment that occupies the LCN then return zero.
Clusters64 DefragRunner::find_fragment_begin(const FileNode *item, const Clusters64 lcn) {
    // Sanity check
    if (item == nullptr || lcn.is_zero()) return Clusters64(0);

    // Walk through all the fragments of the item. If a fragment is found that contains the LCN then return
    // the begin of that fragment
    Clusters64 vcn = {};

    for (const auto &fragment: item->fragments_) {
        if (!fragment.is_virtual()) {
            if (lcn >= fragment.lcn_ &&
                lcn < fragment.lcn_ + fragment.next_vcn_ - vcn) {
                return fragment.lcn_;
            }
        }

        vcn = fragment.next_vcn_;
    }

    // Not found: return zero
    return Clusters64(0);
}

// Search the list for the item that occupies the cluster at the LCN. Return a
// pointer to the item. If not found then return nullptr.
[[maybe_unused]] FileNode *DefragRunner::find_item_at_lcn(const DefragState &data, const Clusters64 lcn) {
    /* Locate the item by descending the sorted tree in memory. If found then
    return the item. */
    FileNode *item = data.item_tree_;

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
    for (item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
        if (find_fragment_begin(item, lcn)) return item;
    }

    // LCN not found, return nullptr
    return nullptr;
}

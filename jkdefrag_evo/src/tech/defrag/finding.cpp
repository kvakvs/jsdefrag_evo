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
#include "volume_bitmap.h"
#include <algorithm>

#undef min

// TODO: Very slow, improve search algorithm
std::optional<lcn_extent_t> DefragRunner::find_gap(DefragState &defrag_state,
                                                   const lcn64_t minimum_lcn, lcn64_t maximum_lcn,
                                                   const cluster_count64_t minimum_size,
                                                   const int must_fit, const bool find_highest_gap,
                                                   const bool ignore_mft_excludes) {
    StopWatch clock_fg(L"find_gap", true);
    //    VolumeBitmapFragment bitmap_fragment;
    DefragGui *gui = DefragGui::get_instance();

    // Sanity check
    if (minimum_lcn >= defrag_state.total_clusters()) return std::nullopt;

    // Main loop to walk through the entire clustermap
    lcn64_t lcn = minimum_lcn;
    lcn64_t cluster_start = 0;
    int prev_in_use = 1;
    lcn64_t highest_begin_lcn = 0;
    lcn64_t highest_end_lcn = 0;
    lcn64_t largest_begin_lcn = 0;
    lcn64_t largest_end_lcn = 0;
    DWORD error_code;
    auto max_volume_lcn = defrag_state.bitmap_.volume_end_lcn();

    do {
        // Fetch a block of cluster data. If error then return false
        // error_code = bitmap_fragment.read(defrag_state.disk_.volume_handle_, lcn);
        error_code = defrag_state.bitmap_.ensure_lcn_loaded(defrag_state.disk_.volume_handle_, lcn);

        if (error_code != NO_ERROR && error_code != ERROR_MORE_DATA) {
            // Show debug message: "ERROR: could not get volume bitmap: %s"
            auto error_string = Str::system_error(GetLastError());
            gui->show_debug(DebugLevel::Warning, nullptr,
                            std::format(L"ERROR: could not get volume bitmap: {}", error_string));

            return std::nullopt;
        }

        // Analyze the clusterdata. We resume where the previous block left off. If a cluster is found that matches the
        // criteria then return it's LCN (Logical Cluster Number)
        auto max_fragment_lcn = std::min(max_volume_lcn, ClusterMap::get_next_fragment_start(lcn));

        // Loop inside the current loaded fragment of the bitmap. After this loop try load the next one or
        // stop when we reach the end of the volume
        while (lcn < max_fragment_lcn && lcn < maximum_lcn) {
            auto in_use = defrag_state.bitmap_.in_use(lcn);

            if (std::any_of(std::begin(defrag_state.mft_excludes_),
                            std::end(defrag_state.mft_excludes_),
                            [=](const lcn_extent_t &ex) -> bool { return ex.contains(lcn); })) {
                if (!ignore_mft_excludes) in_use = 1;
            }

            if (prev_in_use == 0 && in_use != 0) {
                // Show debug message: "Gap found: LCN=%I64d, Size=%I64d"
                gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                                std::format(GAP_FOUND_FMT, cluster_start, lcn - cluster_start));

                // If the gap is bigger/equal than the mimimum size then return it,
                // or remember it, depending on the FindHighestGap parameter. */
                if (cluster_start >= minimum_lcn && lcn - cluster_start >= minimum_size) {
                    if (!find_highest_gap) { return lcn_extent_t(cluster_start, lcn); }

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

        lcn++;
    } while (lcn < maximum_lcn);

    // Process the last gap
    if (prev_in_use == 0) {
        // Show debug message: "Gap found: LCN=%I64d, Size=%I64d"
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(GAP_FOUND_FMT, cluster_start, lcn - cluster_start));

        if (cluster_start >= minimum_lcn && lcn - cluster_start >= minimum_size) {
            if (!find_highest_gap) { return lcn_extent_t(cluster_start, lcn); }

            highest_begin_lcn = cluster_start;
            highest_end_lcn = lcn;
        }

        // Remember the largest gap on the volume
        if (largest_begin_lcn == 0 || largest_end_lcn - largest_begin_lcn < lcn - cluster_start) {
            largest_begin_lcn = cluster_start;
            largest_end_lcn = lcn;
        }
    }

    // If the FindHighestGap flag is true then return the highest gap we have found
    if (find_highest_gap && highest_begin_lcn != 0) {
        return lcn_extent_t(highest_begin_lcn, highest_end_lcn);
    }

    // If the MustFit flag is false then return the largest gap we have found
    if (must_fit == false && largest_begin_lcn != 0) {
        return lcn_extent_t(largest_begin_lcn, largest_end_lcn);
    }

    // No gap found, return nothing
    return std::nullopt;
}

/**
 * \brief Look in the ItemTree and return the highest file above the gap that fits inside the gap (cluster start - cluster set_end).
 * \param direction 0=Search for files below the gap, 1=above
 * \param zone 0=only directories, 1=only regular files, 2=only space hogs, 3=all
 * \return Return a pointer to the item, or nullptr if no file could be found
 */
FileNode *DefragRunner::find_highest_item(const DefragState &data, lcn_extent_t gap,
                                          Tree::Direction direction, Zone zone) {
    DefragGui *gui = DefragGui::get_instance();

    // "Looking for highest-fit %I64d[%I64d]"
    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                    std::format(L"Looking for highest-fit start=" NUM_FMT " [" NUM_FMT " clusters]",
                                gap.begin(), gap.length()));

    /* Walk backwards through all the items on disk and select the first
    file that fits inside the free block. If we find an exact match then
    immediately return it. */
    const auto step_direction = direction == Tree::Direction::First
                                        ? Tree::StepDirection::StepForward
                                        : Tree::StepDirection::StepBack;

    for (auto item = Tree::first(data.item_tree_, direction); item != nullptr;
         item = Tree::next_prev(item, step_direction)) {
        const auto item_lcn = item->get_item_lcn();

        if (item_lcn == 0) continue;

        if (direction == 1) {
            if (item_lcn < gap.end()) return nullptr;
        } else {
            if (item_lcn > gap.begin()) return nullptr;
        }

        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;

        if (zone != Zone::ZoneAll_MaxValue) {
            auto preferred_zone = item->get_preferred_zone();
            if (zone != preferred_zone) continue;
        }

        if (item->clusters_count_ > gap.length()) continue;

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
FileNode *DefragRunner::find_best_item(const DefragState &data, lcn_extent_t gap,
                                       Tree::Direction direction, Zone zone) {
    __timeb64 time{};
    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                    std::format(L"Looking for perfect fit start=" NUM_FMT " [" NUM_FMT " clusters]",
                                gap.begin(), gap.length()));

    // Walk backwards through all the items on disk and select the first item that
    // fits inside the free block, and combined with other items will fill the gap
    // perfectly. If we find an exact match then immediately return it.
    const Clock::time_point max_time = Clock::now() + std::chrono::milliseconds(500);
    FileNode *first_item = nullptr;
    auto gap_size = gap.length();
    uint64_t total_items_size = 0;

    const auto step_direction = direction == Tree::Direction::First
                                        ? Tree::StepDirection::StepForward
                                        : Tree::StepDirection::StepBack;

    for (auto item = Tree::first(data.item_tree_, direction); item != nullptr;
         item = Tree::next_prev(item, step_direction)) {
        // If we have passed the top of the gap then...
        const auto item_lcn = item->get_item_lcn();

        if (item_lcn == 0) continue;

        if ((direction == 1 && item_lcn < gap.end()) || (direction == 0 && item_lcn > gap.end())) {
            // If we did not find an item that fits inside the gap then exit
            if (first_item == nullptr) break;

            /* Exit if the total size of all the items is less than the size of the gap.
            We know that we can never find a perfect fit. */
            if (total_items_size < gap.length()) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                                L"No perfect fit found, the total size of all the items above the "
                                L"gap is less than the size of the gap.");

                return nullptr;
            }

            // Exit if the running time is more than 0.5 seconds
            if (Clock::now() > max_time) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                                L"No perfect fit found, out of time.");

                return nullptr;
            }

            // Rewind and try again. The item that we have found previously fits in the
            // gap, but it does not combine with other items to perfectly fill the gap.
            item = first_item;
            first_item = nullptr;
            gap_size = gap.length();
            total_items_size = 0;

            continue;
        }

        // Ignore all unsuitable items
        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;

        if (zone != Zone::ZoneAll_MaxValue) {
            auto preferred_zone = item->get_preferred_zone();
            if (zone != preferred_zone) continue;
        }

        if (item->clusters_count_ < gap.length()) {
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
uint64_t DefragRunner::find_fragment_begin(const FileNode *item, const uint64_t lcn) {
    // Sanity check
    if (item == nullptr || lcn == 0) return 0;

    // Walk through all the fragments of the item. If a fragment is found that contains the LCN then return
    // the begin of that fragment
    uint64_t vcn = 0;
    for (const auto &fragment: item->fragments_) {
        if (!fragment.is_virtual()) {
            if (lcn >= fragment.lcn_ && lcn < fragment.lcn_ + fragment.next_vcn_ - vcn) {
                return fragment.lcn_;
            }
        }

        vcn = fragment.next_vcn_;
    }

    // Not found: return zero
    return 0;
}

/*

Search the list for the item that occupies the cluster at the LCN. Return a
pointer to the item. If not found then return nullptr.

*/
[[maybe_unused]] FileNode *DefragRunner::find_item_at_lcn(const DefragState &data,
                                                          const uint64_t lcn) {
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
        if (find_fragment_begin(item, lcn) != 0) return item;
    }

    // LCN not found, return nullptr
    return nullptr;
}

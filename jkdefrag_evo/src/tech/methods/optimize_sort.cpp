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

// Optimize the volume by moving all the files into a sorted order.
// SortField=0    Filename
// SortField=1    Filesize
// SortField=2    Date/Time LastAccess
// SortField=3    Date/Time LastChange
// SortField=4    Date/Time Creation
void DefragRunner::optimize_sort(DefragState &defrag_state, const int sort_field) {
    lcn_extent_t gap;
    DefragGui *gui = DefragGui::get_instance();

    // Sanity check
    if (defrag_state.item_tree_ == nullptr) return;

    // Process all the zones
    [[maybe_unused]] uint64_t vacated_until = 0;
    const uint64_t minimum_vacate = defrag_state.total_clusters() / 200;

    for (defrag_state.zone_ = Zone::ZoneFirst;
         defrag_state.zone_ < Zone::ZoneAll_MaxValue; defrag_state.zone_ = (Zone) ((int) defrag_state.zone_ + 1)) {
        call_show_status(defrag_state, DefragPhase::ZoneSort, defrag_state.zone_); // "Zone N: Sort"

        // Start at the begin of the zone and move all the items there, one by one in the requested sorting order, making room as we go.
        FileNode *previous_item = nullptr;

        uint64_t lcn = defrag_state.zones_[(size_t) defrag_state.zone_];

        while (defrag_state.is_still_running()) {
            // Find the next item that we want to place
            FileNode *item = nullptr;
            uint64_t phase_temp = 0;

            for (auto temp_item = Tree::smallest(defrag_state.item_tree_);
                 temp_item != nullptr;
                 temp_item = Tree::next(temp_item)) {
                if (temp_item->is_unmovable_) continue;
                if (temp_item->is_excluded_) continue;
                if (temp_item->clusters_count_ == 0) continue;

                auto preferred_zone = temp_item->get_preferred_zone();
                if (preferred_zone != defrag_state.zone_) continue;

                if (previous_item != nullptr &&
                    compare_items(previous_item, temp_item, sort_field) >= 0) {
                    continue;
                }

                phase_temp = phase_temp + temp_item->clusters_count_;

                if (item != nullptr && compare_items(temp_item, item, sort_field) >= 0) continue;

                item = temp_item;
            }

            if (item == nullptr) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                std::format(L"Finished sorting zone {}.", zone_to_str(defrag_state.zone_)));

                break;
            }

            previous_item = item;
            defrag_state.phase_todo_ = defrag_state.clusters_done_ + phase_temp;

            // If the item is already at the Lcn then skip
            if (item->get_item_lcn() == lcn) {
                lcn = lcn + item->clusters_count_;

                continue;
            }

            // Move the item to the Lcn. If the gap at Lcn is not big enough then fragment
            // the file into whatever gaps are available.
            cluster_count64_t clusters_done = 0;

            while (defrag_state.is_still_running() &&
                   clusters_done < item->clusters_count_ &&
                   !item->is_unmovable_) {
                if (clusters_done > 0) {
                    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                                    std::format(L"Item partially placed, " NUM_FMT " clusters more to do",
                                                item->clusters_count_ - clusters_done));
                }

                // Call the Vacate() function to make a gap at Lcn big enough to hold the item.
                // The Vacate() function may not be able to move whatever is now at the Lcn, so
                // after calling it we have to locate the first gap after the Lcn.
                if (gap.begin() + item->clusters_count_ - clusters_done + 16 > gap.end()) {
                    vacate(defrag_state, lcn_extent_t::with_length(lcn, item->clusters_count_ - clusters_done + minimum_vacate),
                           false);

                    auto result = find_gap(defrag_state, lcn, 0, 0, true, false, false);
                    if (result.has_value()) {
                        gap = result.value();
                    } else {
                        return; // No gaps found, exit
                    }
                }

                // If the gap is not big enough to hold the entire item then calculate how much of the item will fit in the gap.
                cluster_count64_t clusters = item->clusters_count_ - clusters_done;

                if (clusters > gap.length()) {
                    clusters = gap.length();

                    // It looks like a partial move only succeeds if the number of clusters is a multiple of 8.
                    clusters = clusters - clusters % 8;

                    if (clusters == 0) {
                        lcn = gap.end();
                        continue;
                    }
                }

                // Move the item to the gap
                MoveTask task = {
                        .vcn_from_ = clusters_done,
                        .lcn_to_ = gap.begin(),
                        .count_ = clusters,
                        .file_ = item,
                };
                auto result = move_item(defrag_state, task, MoveDirection::Up);

                if (result) {
                    gap.shift_begin(clusters);
                } else {
                    auto result2 = find_gap(defrag_state, gap.begin(), 0, 0, true, false, false);
                    if (result2.has_value()) {
                        gap = result2.value();
                    } else {
                        return; // No gaps found, exit.
                    }
                }

                lcn = gap.begin();
                clusters_done = clusters_done + clusters;
            }
        }
    }
}

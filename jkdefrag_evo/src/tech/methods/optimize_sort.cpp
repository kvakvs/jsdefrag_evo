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
void DefragRunner::optimize_sort(DefragState &data, const SortField sort_field) {
    DefragGui *gui = DefragGui::get_instance();

    // Sanity check
    if (data.item_tree_ == nullptr) return;

    // Process all the zones
    // [[maybe_unused]] uint64_t vacated_until = 0;
    const Clusters64 minimum_vacate = data.total_clusters_ / 200uLL;

    for (data.zone_ = Zone::Directories;
         data.zone_ < Zone::All_MaxValue; data.zone_ = (Zone) ((int) data.zone_ + 1)) {
        call_show_status(data, DefragPhase::ZoneSort, data.zone_); // "Zone N: Sort"

        // Start at the beginning of the zone and move all the items there, one by one in the requested sorting order,
        // making room as we go.
        FileNode *previous_item = nullptr;

        Clusters64 lcn = data.zones_[(size_t) data.zone_];
        Clusters64 gap_begin = {};
        Clusters64 gap_end = {};

        while (data.is_still_running()) {
            // Find the next item that we want to place
            FileNode *item = nullptr;
            Clusters64 phase_temp = {};

            for (auto temp_item = Tree::smallest(data.item_tree_);
                 temp_item != nullptr;
                 temp_item = Tree::next(temp_item)) {
                if (temp_item->is_unmovable_) continue;
                if (temp_item->is_excluded_) continue;
                if (temp_item->clusters_count_.is_zero()) continue;

                auto preferred_zone = temp_item->get_preferred_zone();
                if (preferred_zone != data.zone_) continue;

                if (previous_item != nullptr &&
                    compare_items(previous_item, temp_item, sort_field) != CompareResult::FirstIsSmaller) {
                    continue;
                }

                phase_temp += temp_item->clusters_count_;

                if (item != nullptr &&
                    compare_items(temp_item, item, sort_field) != CompareResult::FirstIsSmaller) {
                    continue;
                }

                item = temp_item;
            }

            if (item == nullptr) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                std::format(L"Finished sorting zone {}.", zone_to_str(data.zone_)));

                break;
            }

            previous_item = item;
            data.phase_todo_ = data.clusters_done_ + phase_temp;

            // If the item is already at the Lcn then skip
            if (item->get_item_lcn() == lcn) {
                lcn = lcn + item->clusters_count_;

                continue;
            }

            // Move the item to the Lcn. If the gap at Lcn is not big enough then fragment
            // the file into whatever gaps are available.
            Clusters64 clusters_done = {};

            while (data.is_still_running() &&
                   clusters_done < item->clusters_count_ &&
                   !item->is_unmovable_) {
                if (clusters_done) {
                    gui->show_debug(
                            DebugLevel::DetailedGapFilling, nullptr,
                            std::format(L"Item partially placed, " NUM_FMT " clusters more to do",
                                        (item->clusters_count_ - clusters_done).value())
                    );
                }

                // Call the Vacate() function to make a gap at Lcn big enough to hold the item. The Vacate() function
                // may not be able to move whatever is now at the Lcn, so after calling it we have to locate the first
                // gap after the Lcn.
                if (gap_begin + item->clusters_count_ - clusters_done + Clusters64(16) > gap_end) {
                    vacate(data, lcn, item->clusters_count_ - clusters_done + minimum_vacate, FALSE);

                    auto result = find_gap(data, lcn, Clusters64(0), Clusters64(0),
                                           true, false, PARAM_OUT gap_begin, PARAM_OUT gap_end,
                                           false);

                    if (!result) return; // No gaps found, exit
                }

                // If the gap is not big enough to hold the entire item then calculate how much
                // of the item will fit in the gap.
                Clusters64 clusters = item->clusters_count_ - clusters_done;

                if (clusters > gap_end - gap_begin) {
                    clusters = gap_end - gap_begin;

                    // It looks like a partial move only succeeds if the number of clusters is a multiple of 8
                    clusters -= clusters % Clusters64(8);

                    if (clusters.is_zero()) {
                        lcn = gap_end;
                        continue;
                    }
                }

                // Move the item to the gap
                auto result = move_item(data, item, gap_begin, clusters_done, clusters, MoveDirection::Up);

                if (result) {
                    gap_begin = gap_begin + clusters;
                } else {
                    result = find_gap(data, gap_begin, Clusters64(0), Clusters64(0),
                                      true, false, PARAM_OUT gap_begin, PARAM_OUT gap_end,
                                      false);
                    if (!result) return; // No gaps found, exit.
                }

                lcn = gap_begin;
                clusters_done = clusters_done + clusters;
            }
        }
    }
}

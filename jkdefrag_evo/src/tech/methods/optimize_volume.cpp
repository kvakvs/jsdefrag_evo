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

// Optimize the harddisk by filling gaps with files from above
void DefragRunner::optimize_volume(DefragState &defrag_state) {
    FileNode *item;
    lcn_extent_t gap;
    DefragGui *gui = DefragGui::get_instance();

    // Sanity check
    if (defrag_state.item_tree_ == nullptr) return;

    // Process all the zones
    for (int zone_i = 0; zone_i < (int) Zone::ZoneAll_MaxValue; zone_i++) {
        auto zone = (Zone) zone_i;

        call_show_status(defrag_state, DefragPhase::ZoneFastOpt, zone); // "Zone N: Fast Optimize"

        // Walk through all the gaps
        gap.set_begin(defrag_state.zones_[(size_t) zone]);
        int retry = 0;

        while (defrag_state.is_still_running()) {
            // Find the next gap
            auto result = find_gap(defrag_state, gap.begin(), 0, 0, true, false, false);
            if (result.has_value()) {
                gap = result.value();
            } else {
                break;
            }

            // Update the progress counter: the number of clusters in all the files
            // above the gap. Exit if there are no more files
            uint64_t phase_temp = 0;

            for (item = Tree::biggest(defrag_state.item_tree_); item != nullptr; item = Tree::prev(item)) {
                if (item->get_item_lcn() < gap.end()) break;
                if (item->is_unmovable_) continue;
                if (item->is_excluded_) continue;

                auto preferred_zone = item->get_preferred_zone();
                if (preferred_zone != zone) continue;

                phase_temp = phase_temp + item->clusters_count_;
            }

            defrag_state.phase_todo_ += phase_temp;
            if (phase_temp == 0) break;

            // Loop until the gap is filled. First look for combinations of files that perfectly
            // fill the gap. If no combination can be found, or if there are fewer files than
            // the gap is big, then fill with the highest file(s) that fit in the gap
            bool perfect_fit = true;
            if (gap.length() > phase_temp) perfect_fit = false;

            while (gap.begin() < gap.end() && retry < 5 && defrag_state.is_still_running()) {
                // Find the Item that is the best fit for the gap. If nothing found (no files
                // fit the gap) then exit the loop
                if (perfect_fit) {
                    item = find_best_item(defrag_state, gap, Tree::Direction::Last, zone);

                    if (item == nullptr) {
                        perfect_fit = false;

                        item = find_highest_item(defrag_state, gap, Tree::Direction::Last, zone);
                    }
                } else {
                    item = find_highest_item(defrag_state, gap, Tree::Direction::Last, zone);
                }

                if (item == nullptr) break;

                // Move the item
                MoveTask task = {
                        .vcn_from_ = 0,
                        .lcn_to_ = gap.begin(),
                        .count_ = item->clusters_count_,
                        .file_ = item,
                };
                auto result2 = move_item(defrag_state, task, MoveDirection::Up);

                if (result2) {
                    gap.shift_begin(item->clusters_count_);
                    retry = 0;
                } else {
                    gap.set_length(0); // Force re-scan of gap
                    retry = retry + 1;
                }
            }

            // If the gap could not be filled then skip
            if (gap.begin() < gap.end()) {
                // Show debug message: "Skipping gap, cannot fill: %I64d[%I64d]"
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                                std::format(SKIPPING_GAP_FMT, gap.begin(), gap.length()));
                gap.set_begin(gap.end());
                retry = 0;
            }
        }
    }
}

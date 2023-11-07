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

// Optimize the harddisk by moving the selected items up
void DefragRunner::optimize_up(DefragState &defrag_state) {
    lcn_extent_t gap;
    DefragGui *gui = DefragGui::get_instance();

    call_show_status(defrag_state, DefragPhase::MoveUp, Zone::None); // "Phase 3: Move Up"

    // Setup the progress counter: the total number of clusters in all files
    FileNode *item;
    for (item = Tree::smallest(defrag_state.item_tree_); item != nullptr; item = Tree::next(item)) {
        defrag_state.phase_todo_ += item->clusters_count_;
    }

    // Exit if nothing to do
    if (defrag_state.item_tree_ == nullptr) return;

    // Walk through all the gaps
    gap.set_end(defrag_state.total_clusters());

    int retry = 0;

    while (defrag_state.is_still_running()) {
        // Find the previous gap
        auto result = find_gap(defrag_state, defrag_state.zones_[1], gap.end(), 0, true, true, false);

        if (result.has_value()) {
            gap = result.value();
        } else {
            break;
        }

        /* Update the progress counter: the number of clusters in all the files
        below the gap. */
        uint64_t phase_temp = 0;

        for (item = Tree::smallest(defrag_state.item_tree_); item != nullptr; item = Tree::next(item)) {
            if (item->is_unmovable_) continue;
            if (item->is_excluded_) continue;
            if (item->get_item_lcn() >= gap.end()) break;

            phase_temp = phase_temp + item->clusters_count_;
        }

        defrag_state.phase_todo_ += phase_temp;
        if (phase_temp == 0) break;

        // Loop until the gap is filled. First look for combinations of files that perfectly
        // fill the gap. If no combination can be found, or if there are less files than
        // the gap is big, then fill with the highest file(s) that fit in the gap.
        bool perfect_fit = gap.length() <= phase_temp;

        while (gap.length()
               && retry < 5
               && defrag_state.is_still_running()) {

            // Find the Item that is the best fit for the gap. If nothing found (no files
            // fit the gap) then exit the loop.
            if (perfect_fit) {
                item = find_best_item(defrag_state, gap, Tree::Direction::First, Zone::ZoneAll_MaxValue);

                if (item == nullptr) {
                    perfect_fit = false;
                    item = find_highest_item(defrag_state, gap, Tree::Direction::First, Zone::ZoneAll_MaxValue);
                }
            } else {
                item = find_highest_item(defrag_state, gap, Tree::Direction::First, Zone::ZoneAll_MaxValue);
            }

            if (item == nullptr) break;

            // Move the item
            auto result2 = move_item(defrag_state, item, gap.end() - item->clusters_count_, 0, item->clusters_count_,
                                     MoveDirection::Down);

            if (result2) {
                gap.shift_end(-item->clusters_count_);
                retry = 0;
            } else {
                gap.set_begin(gap.end()); // Force re-scan of gap
                retry = retry + 1;
            }
        }

        // If the gap could not be filled then skip
        if (gap.begin() < gap.end()) {
            // Show debug message: "Skipping gap, cannot fill: %I64d[%I64d]"
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                            std::format(SKIPPING_GAP_FMT, gap.begin(), gap.length()));

            gap.set_length(0);
            retry = 0;
        }
    }
}

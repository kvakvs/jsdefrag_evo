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
void DefragRunner::optimize_up(DefragState &data) {
    FileNode *item;
    DefragGui *gui = DefragGui::get_instance();

    call_show_status(data, DefragPhase::MoveUp, Zone::None); // "Phase 3: Move Up"

    // Setup the progress counter: the total number of clusters in all files
    for (item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
        data.phase_todo_ += item->clusters_count_;
    }

    // Exit if nothing to do
    if (data.item_tree_ == nullptr) return;

    // Walk through all the gaps
    Clusters64 gap_end = data.total_clusters_;
    int retry = 0;

    while (data.is_still_running()) {
        // Find the previous gap
        bool result;

        Clusters64 gap_begin;
        result = find_gap(data, data.zones_[1], gap_end, Clusters64(0), true,
                          true, PARAM_OUT gap_begin, PARAM_OUT gap_end, false);

        if (!result) break;

        // Update the progress counter: the number of clusters in all the files below the gap
        Clusters64 phase_temp = {};

        for (item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
            if (item->is_unmovable_) continue;
            if (item->is_excluded_) continue;
            if (item->get_item_lcn() >= gap_end) break;

            phase_temp = phase_temp + item->clusters_count_;
        }

        data.phase_todo_ += phase_temp;
        if (phase_temp.is_zero()) break;

        // Loop until the gap is filled. First look for combinations of files that perfectly fill the gap.
        // If no combination can be found, or if there are fewer files than the gap is big, then fill with the
        // highest file(s) that fit in the gap
        bool perfect_fit = true;
        if (gap_end - gap_begin > phase_temp) perfect_fit = false;

        while (gap_begin < gap_end && retry < 5 && data.is_still_running()) {
            // Find the Item that is the best fit for the gap. If nothing found (no files fit the gap) then exit the loop
            if (perfect_fit) {
                item = find_best_item(data, gap_begin, gap_end, Tree::Direction::First, Zone::All_MaxValue);

                if (item == nullptr) {
                    perfect_fit = false;
                    item = find_highest_item(data, gap_begin, gap_end, Tree::Direction::First, Zone::All_MaxValue);
                }
            } else {
                item = find_highest_item(data, gap_begin, gap_end, Tree::Direction::First, Zone::All_MaxValue);
            }

            if (item == nullptr) break;

            // Move the item
            result = move_item(data, item, gap_end - item->clusters_count_, Clusters64(0), item->clusters_count_,
                               MoveDirection::Down);

            if (result) {
                gap_end = gap_end - item->clusters_count_;
                retry = 0;
            } else {
                gap_begin = gap_end; // Force re-scan of gap
                retry = retry + 1;
            }
        }

        // If the gap could not be filled then skip
        if (gap_begin < gap_end) {
            // Show debug message: "Skipping gap, cannot fill: %I64d[%I64d]"
            gui->show_debug(
                    DebugLevel::DetailedGapFilling, nullptr,
                    std::format(SKIPPING_GAP_FMT, gap_begin.value(), (gap_end - gap_begin).value())
            );

            gap_end = gap_begin;
            retry = 0;
        }
    }
}

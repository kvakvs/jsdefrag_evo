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

// Fill all the gaps at the beginning of the disk with fragments from the files above
void DefragRunner::forced_fill(DefragState &defrag_state) {
    call_show_status(defrag_state, DefragPhase::ForcedFill, Zone::None); // "Phase 3: ForcedFill"

    // Walk through all the gaps
    lcn_extent_t gap;
    lcn64_t max_lcn = defrag_state.total_clusters();

    while (defrag_state.is_still_running()) {
        // Find the next gap. If there are no more gaps then exit
        auto result = find_gap(defrag_state, gap.begin(), 0, 0, true, false, false);

        if (result.has_value()) {
            gap = result.value();
        } else {
            break;
        }

        // Find the item with the highest fragment on disk
        FileNode *highest_item = nullptr;
        lcn64_t highest_lcn = 0;
        vcn64_t highest_vcn = 0;
        cluster_count64_t highest_size = 0;

        for (auto item = Tree::biggest(defrag_state.item_tree_); item != nullptr; item = Tree::prev(item)) {
            if (item->is_unmovable_) continue;
            if (item->is_excluded_) continue;
            if (item->clusters_count_ == 0) continue;

            vcn64_t vcn = 0;
            vcn64_t real_vcn = 0;

            for (auto &fragment: item->fragments_) {
                if (!fragment.is_virtual()) {
                    if (fragment.lcn_ > highest_lcn && fragment.lcn_ < max_lcn) {
                        highest_item = item;
                        highest_lcn = fragment.lcn_;
                        highest_vcn = real_vcn;
                        highest_size = fragment.next_vcn_ - vcn;
                    }

                    real_vcn = real_vcn + fragment.next_vcn_ - vcn;
                }

                vcn = fragment.next_vcn_;
            }
        }

        if (highest_item == nullptr) break;

        // If the highest fragment is before the gap then exit, we're finished
        if (highest_lcn <= gap.begin()) break;

        // Move as much of the item into the gap as possible
        cluster_count64_t clusters = gap.length();

        if (clusters > highest_size) clusters = highest_size;

        // TODO: return value is ignored
        MoveTask task = {
                .vcn_from_ = highest_vcn + highest_size - clusters,
                .lcn_to_ = gap.begin(),
                .count_ = clusters,
                .file_ = highest_item,
        };
        move_item(defrag_state, task, MoveDirection::Up);

        gap.shift_begin(clusters);
        max_lcn = highest_lcn + highest_size - clusters;
    }
}

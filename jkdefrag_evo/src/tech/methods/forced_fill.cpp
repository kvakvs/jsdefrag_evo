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
void DefragRunner::forced_fill(DefragState &data) {
    call_show_status(data, DefragPhase::ForcedFill, Zone::None); // "Phase 3: ForcedFill"

    // Walk through all the gaps
    lcn64_t gap_begin = 0;
    lcn64_t max_lcn = data.total_clusters_;

    while (data.is_still_running()) {
        // Find the next gap. If there are no more gaps then exit
        lcn64_t gap_end;
        auto result = find_gap(data, gap_begin, 0, 0, true, false,
                               &gap_begin, &gap_end, false);

        if (!result) break;

        // Find the item with the highest fragment on disk
        FileNode *highest_item = nullptr;
        lcn64_t highest_lcn = 0;
        vcn64_t highest_vcn = 0;
        count64_t highest_size = 0;

        FileNode *item;
        for (item = Tree::biggest(data.item_tree_); item != nullptr; item = Tree::prev(item)) {
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
        if (highest_lcn <= gap_begin) break;

        // Move as much of the item into the gap as possible
        uint64_t clusters = gap_end - gap_begin;

        if (clusters > highest_size) clusters = highest_size;

        // TODO: return value is ignored
        move_item(data, highest_item, gap_begin, highest_vcn + highest_size - clusters, clusters,
                  MoveDirection::Up);

        gap_begin = gap_begin + clusters;
        max_lcn = highest_lcn + highest_size - clusters;
    }
}

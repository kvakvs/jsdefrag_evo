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

// Defragment all the fragmented files
void DefragRunner::defragment(DefragState &data) {
    DefragGui *gui = DefragGui::get_instance();

    call_show_status(data, DefragPhase::Defragment, Zone::None); // "Phase 2: Defragment"

    // Setup the width of the progress bar: the number of clusters in all fragmented files
    FileNode *item;
    for (item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;
        if (item->clusters_count_ == 0) continue;

        if (!is_fragmented(item, 0, item->clusters_count_)) continue;

        data.phase_todo_ += item->clusters_count_;
    }

    // Exit if nothing to do
    if (data.phase_todo_ == 0) return;

    // Walk through all files and defrag
    FileNode *next_item = Tree::smallest(data.item_tree_);

    while (next_item != nullptr && data.is_still_running()) {
        /* The loop may change the position of the item in the tree, so we have
        to determine and remember the next item now. */
        item = next_item;

        next_item = Tree::next(item);

        // Ignore if the item cannot be moved, or is Excluded, or is not fragmented
        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;
        if (item->clusters_count_ == 0) continue;

        if (!is_fragmented(item, 0, item->clusters_count_)) continue;

        // Find a gap that is large enough to hold the item, or the largest gap on the volume.
        // If the disk is full then show a message and exit.
        auto file_zone = item->get_preferred_zone();

        lcn_extent_t gap;
        auto result = find_gap(data, data.zones_[(size_t) file_zone], 0, item->clusters_count_, false, false, false);

        if (result.has_value()) {
            gap = result.value();
        } else {
            // Try finding a gap again, this time including the free area
            result = find_gap(data, 0, 0, item->clusters_count_, false, false, false);

            if (result.has_value()) {
                gap = result.value();
            } else {
                gui->show_debug(DebugLevel::Progress, item, L"Disk is full, cannot defragment.");
                return;
            }
        }

        // If the gap is big enough to hold the entire item then move the file in a single go, and loop.
        if (gap.length() >= item->clusters_count_) {
            MoveTask task {
                    .vcn_from_ = 0,
                    .lcn_to_ = gap.begin(),
                    .count_ = item->clusters_count_,
                    .file_ = item,
            };
            move_item(data, task, MoveDirection::Up);
            continue;
        }

        // Open a filehandle for the item. If error then set the Unmovable flag, colorize the item on the screen, and loop.
        HANDLE file_handle = open_item_handle(data, item);

        if (file_handle == nullptr) {
            item->is_unmovable_ = true;

            colorize_disk_item(data, item, 0, 0, false);

            continue;
        }

        // Move the file in parts, each time selecting the biggest gap available
        cluster_count64_t clusters_done = 0;
        cluster_count64_t clusters;

        do {
            clusters = gap.length();

            if (clusters > item->clusters_count_ - clusters_done) {
                clusters = item->clusters_count_ - clusters_done;
            }

            // Make sure that the gap is bigger than the first fragment of the block that we're about to move.
            // If not, then the result would be more fragments, not less.
            uint64_t vcn = 0;
            uint64_t real_vcn = 0;

            for (auto &fragment: item->fragments_) {
                if (!fragment.is_virtual()) {
                    if (real_vcn >= clusters_done) {
                        if (clusters > fragment.next_vcn_ - vcn) break;

                        clusters_done = real_vcn + fragment.next_vcn_ - vcn;

                        data.clusters_done_ += fragment.next_vcn_ - vcn;
                    }

                    real_vcn = real_vcn + fragment.next_vcn_ - vcn;
                }

                vcn = fragment.next_vcn_;
            }

            if (clusters_done >= item->clusters_count_) break;

            // Move the segment
            // result =
            MoveTask task = {
                    .vcn_from_ = clusters_done,
                    .lcn_to_ = gap.begin(),
                    .count_ = clusters,
                    .file_ = item,
            };
            move_item_try_strategies(data, task, MoveDirection::Up);

            // Next segment
            clusters_done = clusters_done + clusters;

            // Find a gap large enough to hold the remainder, or the largest gap on the volume
            if (clusters_done < item->clusters_count_) {
                result = find_gap(data, data.zones_[(size_t) file_zone], 0,
                                  item->clusters_count_ - clusters_done, false, false, false);

                if (result.has_value()) {
                    gap = result.value();
                } else {
                    break;
                }
            }
        } while (clusters_done < item->clusters_count_ && data.is_still_running());

        // Close the item
        FlushFileBuffers(file_handle); // Is this useful? Can't hurt
        CloseHandle(file_handle);
    }
}

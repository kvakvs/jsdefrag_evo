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

/* Move items to their zone. This will:
- Defragment all fragmented files
- Move regular files out of the directory zone.
- Move SpaceHogs out of the directory- and regular zones.
- Move items out of the MFT reserved zones
*/
void DefragRunner::fixup(DefragState &data) {
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    DefragGui *gui = DefragGui::get_instance();

    call_show_status(data, DefragPhase::Fixup, Zone::None); // "Phase 3: Fixup"

    // Initialize: fetch the current time
    filetime64_t system_time = from_system_time();

    // Initialize the width of the progress bar: the total number of clusters of all the items
    FileNode *item;
    for (item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;
        if (item->clusters_count_ == 0) continue;

        data.phase_todo_ += item->clusters_count_;
    }

    // [[maybe_unused]] micro64_t last_calc_time = system_time;

    // Exit if nothing to do
    if (data.phase_todo_ == 0) return;

    // Walk through all files and move the files that need to be moved.
    uint64_t gap_begin[3];
    uint64_t gap_end[3];

    int file_zone;
    for (file_zone = 0; file_zone < 3; file_zone++) {
        gap_begin[file_zone] = 0;
        gap_end[file_zone] = 0;
    }

    auto next_item = Tree::smallest(data.item_tree_);

    while (next_item != nullptr && data.is_still_running()) {
        // The loop will change the position of the item in the tree, so we have to determine the next item before executing the loop.
        item = next_item;

        next_item = Tree::next(item);

        // Ignore items that are unmovable or excluded
        if (item->is_unmovable_) continue;
        if (item->is_excluded_) continue;
        if (item->clusters_count_ == 0) continue;

        // Ignore items that do not need to be moved
        file_zone = 1;

        if (item->is_hog_) file_zone = 2;
        if (item->is_dir_) file_zone = 0;

        const uint64_t item_lcn = item->get_item_lcn();

        int move_me = false;

        if (is_fragmented(item, 0, item->clusters_count_)) {
            // "I am fragmented."
            gui->show_debug(DebugLevel::DetailedFileInfo, item, L"I am fragmented.");

            move_me = true;
        }

        if (move_me == false &&
            ((item_lcn >= data.mft_excludes_[0].start_ && item_lcn < data.mft_excludes_[0].end_) ||
             (item_lcn >= data.mft_excludes_[1].start_ && item_lcn < data.mft_excludes_[1].end_) ||
             (item_lcn >= data.mft_excludes_[2].start_ && item_lcn < data.mft_excludes_[2].end_))
            && (data.disk_.type_ != DiskType::NTFS
                || !Str::match_mask(item->get_long_path(), L"?:\\$MFT"))) {
            // "I am in MFT reserved space."
            gui->show_debug(DebugLevel::DetailedFileInfo, item, L"I am in MFT reserved space.");
            move_me = true;
        }

        if (file_zone == 1 && item_lcn < data.zones_[1] && move_me == false) {
            // "I am a regular file in zone 1."
            gui->show_debug(DebugLevel::DetailedFileInfo, item, L"I am a regular file in zone 1.");
            move_me = true;
        }

        if (file_zone == 2 && item_lcn < data.zones_[2] && move_me == false) {
            // "I am a spacehog in zone 1 or 2."
            gui->show_debug(DebugLevel::DetailedFileInfo, item, L"I am a spacehog in zone 1 or 2.");
            move_me = true;
        }

        if (move_me == false) {
            data.clusters_done_ += item->clusters_count_;
            continue;
        }

        // Ignore files that have been modified less than 15 minutes ago
        bool result;

        if (!item->is_dir_) {
            result = GetFileAttributesExW(item->get_long_path(), GetFileExInfoStandard, &attributes);

            if (result != 0) {
                const filetime64_t file_time = from_FILETIME(attributes.ftLastWriteTime);

                if (file_time + std::chrono::minutes(15) > system_time) {
                    data.clusters_done_ += item->clusters_count_;
                    continue;
                }
            }
        }

        // If the file does not fit in the current gap then find another gap
        if (item->clusters_count_ > gap_end[file_zone] - gap_begin[file_zone]) {
            result = find_gap(data, data.zones_[file_zone], 0, item->clusters_count_, true, false,
                              &gap_begin[file_zone],
                              &gap_end[file_zone], FALSE);

            if (!result) {
                // Show debug message: "Cannot move item away because no gap is big enough: %I64d[%lu]"
                gui->show_debug(
                        DebugLevel::Progress, item,
                        std::format(
                                L"Cannot move file away because no gap is big enough: lcn=" NUM_FMT "[" NUM_FMT " clusters]",
                                item->get_item_lcn(), item->clusters_count_));

                gap_end[file_zone] = gap_begin[file_zone]; // Force re-scan of gap

                data.clusters_done_ += item->clusters_count_;
                continue;
            }
        }

        // Move the item.
        result = move_item(data, item, gap_begin[file_zone], 0, item->clusters_count_, MoveDirection::Up);

        if (result) {
            gap_begin[file_zone] = gap_begin[file_zone] + item->clusters_count_;
        } else {
            gap_end[file_zone] = gap_begin[file_zone]; // Force re-scan of gap
        }

        // Get new system time
        system_time = from_system_time();
    }
}

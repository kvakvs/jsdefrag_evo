#include "precompiled_header.h"

// Optimize the harddisk by filling gaps with files from above
void DefragLib::optimize_volume(DefragDataStruct *data) {
    ItemStruct *item;

    uint64_t gap_begin;
    uint64_t gap_end;

    DefragGui *gui = DefragGui::get_instance();

    // Sanity check
    if (data->item_tree_ == nullptr) return;

    // Process all the zones
    for (int zone = 0; zone < 3; zone++) {
        call_show_status(data, 5, zone); /* "Zone N: Fast Optimize" */

        // Walk through all the gaps
        gap_begin = data->zones_[zone];
        int retry = 0;

        while (*data->running_ == RunningState::RUNNING) {
            // Find the next gap
            bool result = find_gap(data, gap_begin, 0, 0, true, false, &gap_begin, &gap_end, FALSE);

            if (result == false) break;

            /* Update the progress counter: the number of clusters in all the files
            above the gap. Exit if there are no more files. */
            uint64_t phase_temp = 0;

            for (item = Tree::biggest(data->item_tree_); item != nullptr; item = Tree::prev(item)) {
                if (item->get_item_lcn() < gap_end) break;
                if (item->is_unmovable_) continue;
                if (item->is_excluded_) continue;

                int file_zone = 1;

                if (item->is_hog_) file_zone = 2;
                if (item->is_dir_) file_zone = 0;
                if (file_zone != zone) continue;

                phase_temp = phase_temp + item->clusters_count_;
            }

            data->phase_todo_ = data->phase_done_ + phase_temp;
            if (phase_temp == 0) break;

            /* Loop until the gap is filled. First look for combinations of files that perfectly
            fill the gap. If no combination can be found, or if there are less files than
            the gap is big, then fill with the highest file(s) that fit in the gap. */
            bool perfect_fit = true;
            if (gap_end - gap_begin > phase_temp) perfect_fit = false;

            while (gap_begin < gap_end && retry < 5 && *data->running_ == RunningState::RUNNING) {
                /* Find the Item that is the best fit for the gap. If nothing found (no files
                fit the gap) then exit the loop. */
                if (perfect_fit) {
                    item = find_best_item(data, gap_begin, gap_end, Tree::Direction::Last, zone);

                    if (item == nullptr) {
                        perfect_fit = false;

                        item = find_highest_item(data, gap_begin, gap_end, Tree::Direction::Last, zone);
                    }
                } else {
                    item = find_highest_item(data, gap_begin, gap_end, Tree::Direction::Last, zone);
                }

                if (item == nullptr) break;

                // Move the item
                result = move_item(data, item, gap_begin, 0, item->clusters_count_, MoveDirection::Up);

                if (result == true) {
                    gap_begin = gap_begin + item->clusters_count_;
                    retry = 0;
                } else {
                    gap_end = gap_begin; // Force re-scan of gap
                    retry = retry + 1;
                }
            }

            // If the gap could not be filled then skip
            if (gap_begin < gap_end) {
                /* Show debug message: "Skipping gap, cannot fill: %I64d[%I64d]" */
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                                std::format(SKIPPING_GAP_FMT, gap_begin,
                                            gap_end - gap_begin));

                gap_begin = gap_end;
                retry = 0;
            }
        }
    }
}

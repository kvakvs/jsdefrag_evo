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

// Vacate an area by moving files upward. If there are unmovable files at the lcn then
// skip them. Then move files upward until the gap is bigger than clusters, or when we
// encounter an unmovable file.
void DefragLib::vacate(DefragState *data, uint64_t lcn, uint64_t clusters, BOOL ignore_mft_excludes) {
    uint64_t test_gap_begin;
    uint64_t test_gap_end;
    uint64_t move_gap_begin;
    uint64_t move_gap_end;
    ItemStruct *item;
    FragmentListStruct *fragment;
    uint64_t vcn;
    uint64_t real_vcn;
    ItemStruct *bigger_item;
    uint64_t bigger_begin;
    uint64_t bigger_end;
    uint64_t bigger_real_vcn;
    uint64_t move_to;
    uint64_t done_until;

    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                    std::format(L"Vacating " NUM_FMT " clusters starting at LCN=" NUM_FMT, clusters, lcn));

    // Sanity check
    if (lcn >= data->total_clusters_) {
        gui->show_debug(DebugLevel::Warning, nullptr, L"Error: trying to vacate an area beyond the end of the disk.");

        return;
    }

    /* Determine the point to above which we will be moving the data. We want at least the
    end of the zone if everything was perfectly optimized, so data will not be moved
    again and again. */
    move_to = lcn + clusters;

    switch (data->zone_) {
        case Zone::Zone0:
            move_to = data->zones_[1];
            break;
        case Zone::Zone1:
            move_to = data->zones_[2];
            break;
        case Zone::Zone2:
            // Zone 2: end of disk minus all the free space
            move_to = data->total_clusters_ - data->count_free_clusters_ +
                      (uint64_t) (data->total_clusters_ * 2.0 * data->free_space_ / 100.0);
            break;
        default:
            break;
    }

    if (move_to < lcn + clusters) move_to = lcn + clusters;

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, std::format(L"move_to = " NUM_FMT, move_to));

    // Loop forever
    move_gap_begin = 0;
    move_gap_end = 0;
    done_until = lcn;

    while (*data->running_ == RunningState::RUNNING) {
        /* Find the first movable data fragment at or above the done_until lcn. If there is nothing
        then return, we have reached the end of the disk. */
        bigger_item = nullptr;
        bigger_begin = 0;

        for (item = Tree::smallest(data->item_tree_); item != nullptr; item = Tree::next(item)) {
            if (item->is_unmovable_ || item->is_excluded_ || item->clusters_count_ == 0) {
                continue;
            }

            vcn = 0;
            real_vcn = 0;

            for (fragment = item->fragments_; fragment != nullptr; fragment = fragment->next_) {
                if (fragment->lcn_ != VIRTUALFRAGMENT) {
                    if (fragment->lcn_ >= done_until &&
                        (bigger_begin > fragment->lcn_ || bigger_item == nullptr)) {
                        bigger_item = item;
                        bigger_begin = fragment->lcn_;
                        bigger_end = fragment->lcn_ + fragment->next_vcn_ - vcn;
                        bigger_real_vcn = real_vcn;

                        if (bigger_begin == lcn) break;
                    }

                    real_vcn = real_vcn + fragment->next_vcn_ - vcn;
                }

                vcn = fragment->next_vcn_;
            }

            if (bigger_begin != 0 && bigger_begin == lcn) break;
        }

        if (bigger_item == nullptr) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                            std::format(L"No data found above LCN=" NUM_FMT, lcn));

            return;
        }

        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                        std::format(L"Data found at LCN=" NUM_FMT ", {}", bigger_begin, bigger_item->get_long_path()));

        // Find the first gap above the lcn
        bool result = find_gap(data, lcn, 0, 0, true, false,
                               &test_gap_begin, &test_gap_end, ignore_mft_excludes);

        if (!result) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                            std::format(L"No gaps found above LCN=" NUM_FMT, lcn));
            return;
        }

        // Exit if the end of the first gap is below the first movable item, the gap cannot be expanded.
        if (test_gap_end < bigger_begin) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                            std::format(
                                    L"Cannot expand the gap from " NUM_FMT " to " NUM_FMT " (" NUM_FMT " clusters) any further.",
                                    test_gap_begin, test_gap_end, test_gap_end - test_gap_begin));
            return;
        }

        /* Exit if the first movable item is at the end of the gap and the gap is big enough,
        no need to enlarge any further. */
        if (test_gap_end == bigger_begin && test_gap_end - test_gap_begin >= clusters) {
            gui->show_debug(
                    DebugLevel::DetailedGapFilling, nullptr,
                    std::format(
                            L"Finished vacating, the gap from " NUM_FMT " to " NUM_FMT " (" NUM_FMT " clusters) is now bigger than " NUM_FMT " clusters.",
                            test_gap_begin, test_gap_end, test_gap_end - test_gap_begin, clusters));

            return;
        }

        // Exit if we have moved the item before. We don't want a worm
        if (lcn >= move_to) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Stopping vacate because of possible worm.");
            return;
        }

        /* Determine where we want to move the fragment to. Maybe the previously used
        gap is big enough, otherwise we have to locate another gap. */
        if (bigger_end - bigger_begin >= move_gap_end - move_gap_begin) {
            result = false;

            // First try to find a gap above the move_to point
            if (move_to < data->total_clusters_ && move_to >= bigger_end) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                                std::format(L"Finding gap above move_to=" NUM_FMT, move_to));

                result = find_gap(data, move_to, 0, bigger_end - bigger_begin, true, false, &move_gap_begin,
                                  &move_gap_end,
                                  FALSE);
            }

            // If no gap was found then try to find a gap as high on disk as possible, but above the item.
            if (!result) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                                std::format(L"Finding gap from end of disk above bigger_end=" NUM_FMT, bigger_end));

                result = find_gap(data, bigger_end, 0, bigger_end - bigger_begin, true, true, &move_gap_begin,
                                  &move_gap_end, FALSE);
            }

            // If no gap was found then exit, we cannot move the item.
            if (!result) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No gap found.");

                return;
            }
        }

        // Move the fragment to the gap.
        result = move_item(data, bigger_item, move_gap_begin, bigger_real_vcn, bigger_end - bigger_begin,
                           MoveDirection::Up);

        if (result) {
            if (move_gap_begin < move_to) move_to = move_gap_begin;

            move_gap_begin = move_gap_begin + bigger_end - bigger_begin;
        } else {
            move_gap_end = move_gap_begin; // Force re-scan of gap
        }

        // Adjust the done_until lcn. We don't want an infinite loop
        done_until = bigger_end;
    }
}

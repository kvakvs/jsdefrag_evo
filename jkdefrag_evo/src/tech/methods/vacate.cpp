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

#include <runner.h>

#include "precompiled_header.h"

// Vacate an area by moving files upward. If there are unmovable files at the lcn then
// skip them. Then move files upward until the gap is bigger than clusters, or when we
// encounter an unmovable file.
void DefragRunner::vacate(DefragState &defrag_state, lcn_extent_t gap, bool ignore_mft_excludes) {
    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                    std::format(L"Vacating " NUM_FMT " clusters starting at LCN=" NUM_FMT,
                                gap.length(), gap.begin()));

    // Sanity check
    if (gap.begin() >= defrag_state.total_clusters()) {
        gui->show_debug(DebugLevel::Warning, nullptr,
                        L"Error: trying to vacate an area beyond the set_end of the disk.");
        return;
    }

    // Determine the point to above which we will be moving the data. We want at least the
    // end of the zone if everything was perfectly optimized, so data will not be moved
    // again and again.
    lcn64_t move_to = gap.end();

    switch (defrag_state.zone_) {
        case Zone::ZoneFirst:
            move_to = defrag_state.zones_[1];
            break;
        case Zone::ZoneCommon:
            move_to = defrag_state.zones_[2];
            break;
        case Zone::ZoneLast:
            // Zone 2: end of disk minus all the free space
            move_to = defrag_state.total_clusters() - defrag_state.count_free_clusters_ +
                      (uint64_t) (defrag_state.total_clusters() * 2.0 * defrag_state.free_space_ /
                                  100.0);
            break;
        default:
            break;
    }

    if (move_to < gap.end()) move_to = gap.end();

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                    std::format(L"move_to = " NUM_FMT, move_to));

    // Loop forever
    lcn_extent_t move_gap;
    auto done_until = gap.begin();

    while (defrag_state.is_still_running()) {
        // Find the first movable data fragment at or above the done_until lcn. If there is nothing
        // then return, we have reached the end of the disk.
        FileNode *bigger_item = nullptr;
        lcn_extent_t bigger;
        vcn64_t bigger_real_vcn;

        for (auto item = Tree::smallest(defrag_state.item_tree_); item != nullptr;
             item = Tree::next(item)) {
            if (item->is_unmovable_ || item->is_excluded_ || item->clusters_count_ == 0) {
                continue;
            }

            vcn64_t vcn = 0;
            vcn64_t real_vcn = 0;

            for (auto &fragment: item->fragments_) {
                if (!fragment.is_virtual()) {
                    if (fragment.lcn_ >= done_until &&
                        (bigger.begin() > fragment.lcn_ || bigger_item == nullptr)) {
                        bigger_item = item;
                        bigger.set_begin(fragment.lcn_);
                        bigger.set_end(fragment.lcn_ + fragment.next_vcn_ - vcn);
                        bigger_real_vcn = real_vcn;

                        if (bigger.begin() == gap.begin()) break;
                    }

                    real_vcn = real_vcn + fragment.next_vcn_ - vcn;
                }

                vcn = fragment.next_vcn_;
            }

            if (bigger.begin() != 0 && bigger.begin() == gap.begin()) break;
        }

        if (bigger_item == nullptr) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                            std::format(L"No data found above LCN=" NUM_FMT, gap.begin()));

            return;
        }

        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                        std::format(L"Data found at LCN=" NUM_FMT ", {}", bigger.begin(),
                                    bigger_item->get_long_path()));

        // Find the first gap above the lcn
        lcn_extent_t test_gap;
        auto result = find_gap(defrag_state, gap.begin(), 0, 0, true, false, ignore_mft_excludes);
        if (result.has_value()) {
            test_gap = result.value();
        } else {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                            std::format(L"No gaps found above LCN=" NUM_FMT, gap.begin()));
            return;
        }

        // Exit if the end of the first gap is below the first movable item, the gap cannot be expanded.
        if (test_gap.end() < bigger.begin()) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                            std::format(L"Cannot expand the gap from " NUM_FMT " to " NUM_FMT
                                        " (" NUM_FMT " clusters) any further.",
                                        test_gap.begin(), test_gap.end(), test_gap.length()));
            return;
        }

        /* Exit if the first movable item is at the end of the gap and the gap is big enough,
        no need to enlarge any further. */
        if (test_gap.end() == bigger.begin() && test_gap.length() >= gap.length()) {
            gui->show_debug(
                    DebugLevel::DetailedGapFilling, nullptr,
                    std::format(L"Finished vacating, the gap from " NUM_FMT " to " NUM_FMT
                                " (" NUM_FMT " clusters) is now bigger than " NUM_FMT " clusters.",
                                test_gap.begin(), test_gap.end(), test_gap.length(), gap.length()));

            return;
        }

        // Exit if we have moved the item before. We don't want a worm
        if (gap.begin() >= move_to) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                            L"Stopping vacate because of possible worm.");
            return;
        }

        // Determine where we want to move the fragment to. Maybe the previously used
        // gap is big enough, otherwise we have to locate another gap. */
        if (bigger.length() >= move_gap.length()) {
            auto local_success = false;

            // First try to find a gap above the move_to point
            if (move_to < defrag_state.total_clusters() && move_to >= bigger.end()) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                                std::format(L"Finding gap above move_to=" NUM_FMT, move_to));

                auto result2 =
                        find_gap(defrag_state, move_to, 0, bigger.length(), true, false, false);
                if (result2.has_value()) {
                    move_gap = result2.value();
                    local_success = true;
                }
            }

            // If no gap was found then try to find a gap as high on disk as possible, but above the item.
            if (!local_success) {
                gui->show_debug(
                        DebugLevel::DetailedGapFilling, nullptr,
                        std::format(L"Finding gap from set_end of disk above bigger_end=" NUM_FMT,
                                    bigger.end()));

                auto result3 =
                        find_gap(defrag_state, bigger.end(), 0, bigger.length(), true, true, false);
                if (result3.has_value()) {
                    move_gap = result3.value();
                    local_success = true;
                }
            }

            // If no gap was found then exit, we cannot move the item.
            if (!local_success) {
                gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"No gap found.");
                return;
            }
        }

        // Move the fragment to the gap.
        MoveTask task = {
                .vcn_from_ = bigger_real_vcn,
                .lcn_to_ = move_gap.begin(),
                .count_ = bigger.length(),
                .file_ = bigger_item,
        };
        auto result4 = move_item(defrag_state, task, MoveDirection::Up);

        if (result4) {
            if (move_gap.begin() < move_to) move_to = move_gap.begin();
            move_gap.shift_begin(bigger.length());
        } else {
            move_gap.set_length(0);// Force re-scan of gap
        }

        // Adjust the done_until lcn. We don't want an infinite loop
        done_until = bigger.end();
    }
}

// Set up the list of clusters that cannot be used.
// The Master File Table cannot be moved and cannot be used by files. All this is only necessary for NTFS volumes.
void DefragRunner::set_up_unusable_cluster_list(DefragState &data) {
    DefragGui *gui = DefragGui::get_instance();
    NTFS_VOLUME_DATA_BUFFER ntfs_data;
    DWORD w2;
    auto error_code = DeviceIoControl(data.disk_.volume_handle_, FSCTL_GET_NTFS_VOLUME_DATA,
                                      nullptr, 0, &ntfs_data, sizeof ntfs_data, &w2, nullptr);

    if (error_code != 0) {
        // Note: NtfsData.TotalClusters.QuadPart should be exactly the same
        // as the Data->TotalClusters that was determined in the previous block.

        data.bytes_per_cluster_ = ntfs_data.BytesPerCluster;

        data.mft_excludes_[0] = lcn_extent_t(lcn_from(ntfs_data.MftStartLcn),
                                             lcn_from(ntfs_data.MftStartLcn) +
                                                     lcn_from(ntfs_data.MftValidDataLength) /
                                                             ntfs_data.BytesPerCluster);
        data.mft_excludes_[1] =
                lcn_extent_t(lcn_from(ntfs_data.MftZoneStart), lcn_from(ntfs_data.MftZoneEnd));
        data.mft_excludes_[2] = lcn_extent_t(lcn_from(ntfs_data.Mft2StartLcn),
                                             lcn_from(ntfs_data.Mft2StartLcn) +
                                                     lcn_from(ntfs_data.MftValidDataLength) /
                                                             ntfs_data.BytesPerCluster);

        // Show debug message: "MftStartLcn=%I64d, MftZoneStart=%I64d, MftZoneEnd=%I64d, Mft2StartLcn=%I64d, MftValidDataLength=%I64d"
        gui->show_debug(
                DebugLevel::DetailedProgress, nullptr,
                std::format(L"MftStartLcn=" NUM_FMT ", MftZoneStart=" NUM_FMT
                            ", MftZoneEnd=" NUM_FMT ", Mft2StartLcn=" NUM_FMT
                            ", MftValidDataLength=" NUM_FMT,
                            lcn_from(ntfs_data.MftStartLcn), lcn_from(ntfs_data.MftZoneStart),
                            lcn_from(ntfs_data.MftZoneEnd), lcn_from(ntfs_data.Mft2StartLcn),
                            lcn_from(ntfs_data.MftValidDataLength) / ntfs_data.BytesPerCluster));

        // Show debug message: "MftExcludes[%u].Start=%I64d, MftExcludes[%u].End=%I64d"
        gui->show_debug(DebugLevel::DetailedProgress, nullptr,
                        std::format(MFT_EXCL_FMT, 0, data.mft_excludes_[0].begin(), 0,
                                    data.mft_excludes_[0].end()));
        gui->show_debug(DebugLevel::DetailedProgress, nullptr,
                        std::format(MFT_EXCL_FMT, 1, data.mft_excludes_[1].begin(), 1,
                                    data.mft_excludes_[1].end()));
        gui->show_debug(DebugLevel::DetailedProgress, nullptr,
                        std::format(MFT_EXCL_FMT, 2, data.mft_excludes_[2].begin(), 2,
                                    data.mft_excludes_[2].end()));
    }
}

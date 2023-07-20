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
void DefragRunner::vacate(DefragState &data, uint64_t lcn, uint64_t clusters, BOOL ignore_mft_excludes) {
    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                    std::format(L"Vacating " NUM_FMT " clusters starting at LCN=" NUM_FMT, clusters, lcn));

    // Sanity check
    if (lcn >= data.total_clusters_) {
        gui->show_debug(DebugLevel::Warning, nullptr, L"Error: trying to vacate an area beyond the end of the disk.");

        return;
    }

    // Determine the point to above which we will be moving the data. We want at least the
    // end of the zone if everything was perfectly optimized, so data will not be moved
    // again and again.
    uint64_t move_to = lcn + clusters;

    switch (data.zone_) {
        case Zone::ZoneFirst:
            move_to = data.zones_[1];
            break;
        case Zone::ZoneCommon:
            move_to = data.zones_[2];
            break;
        case Zone::ZoneLast:
            // Zone 2: end of disk minus all the free space
            move_to = data.total_clusters_ - data.count_free_clusters_ +
                      (uint64_t) (data.total_clusters_ * 2.0 * data.free_space_ / 100.0);
            break;
        default:
            break;
    }

    if (move_to < lcn + clusters) move_to = lcn + clusters;

    gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, std::format(L"move_to = " NUM_FMT, move_to));

    // Loop forever
    uint64_t move_gap_begin = 0;
    uint64_t move_gap_end = 0;
    uint64_t done_until = lcn;

    while (data.is_still_running()) {
        // Find the first movable data fragment at or above the done_until lcn. If there is nothing
        // then return, we have reached the end of the disk.
        FileNode *bigger_item = nullptr;
        uint64_t bigger_begin = 0;
        uint64_t bigger_end;
        uint64_t bigger_real_vcn;

        for (auto item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
            if (item->is_unmovable_ || item->is_excluded_ || item->clusters_count_ == 0) {
                continue;
            }

            uint64_t vcn = 0;
            uint64_t real_vcn = 0;

            for (auto &fragment: item->fragments_) {
                if (!fragment.is_virtual()) {
                    if (fragment.lcn_ >= done_until &&
                        (bigger_begin > fragment.lcn_ || bigger_item == nullptr)) {
                        bigger_item = item;
                        bigger_begin = fragment.lcn_;
                        bigger_end = fragment.lcn_ + fragment.next_vcn_ - vcn;
                        bigger_real_vcn = real_vcn;

                        if (bigger_begin == lcn) break;
                    }

                    real_vcn = real_vcn + fragment.next_vcn_ - vcn;
                }

                vcn = fragment.next_vcn_;
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
        uint64_t test_gap_begin;
        uint64_t test_gap_end;
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

        // Determine where we want to move the fragment to. Maybe the previously used
        // gap is big enough, otherwise we have to locate another gap. */
        if (bigger_end - bigger_begin >= move_gap_end - move_gap_begin) {
            result = false;

            // First try to find a gap above the move_to point
            if (move_to < data.total_clusters_ && move_to >= bigger_end) {
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

// Set up the list of clusters that cannot be used.
// The Master File Table cannot be moved and cannot be used by files. All this is only necessary for NTFS volumes.
void DefragRunner::set_up_unusable_cluster_list(DefragState &data) {
    DefragGui *gui = DefragGui::get_instance();
    NTFS_VOLUME_DATA_BUFFER ntfs_data;
    DWORD w2;
    auto error_code = DeviceIoControl(data.disk_.volume_handle_, FSCTL_GET_NTFS_VOLUME_DATA,
                                      nullptr, 0, &ntfs_data, sizeof ntfs_data,
                                      &w2, nullptr);

    if (error_code != 0) {
        // Note: NtfsData.TotalClusters.QuadPart should be exactly the same
        // as the Data->TotalClusters that was determined in the previous block.

        data.bytes_per_cluster_ = ntfs_data.BytesPerCluster;

        data.mft_excludes_[0].start_ = ntfs_data.MftStartLcn.QuadPart;
        data.mft_excludes_[0].end_ = ntfs_data.MftStartLcn.QuadPart +
                                     ntfs_data.MftValidDataLength.QuadPart / ntfs_data.BytesPerCluster;
        data.mft_excludes_[1].start_ = ntfs_data.MftZoneStart.QuadPart;
        data.mft_excludes_[1].end_ = ntfs_data.MftZoneEnd.QuadPart;
        data.mft_excludes_[2].start_ = ntfs_data.Mft2StartLcn.QuadPart;
        data.mft_excludes_[2].end_ = ntfs_data.Mft2StartLcn.QuadPart +
                                     ntfs_data.MftValidDataLength.QuadPart / ntfs_data.BytesPerCluster;

        // Show debug message: "MftStartLcn=%I64d, MftZoneStart=%I64d, MftZoneEnd=%I64d, Mft2StartLcn=%I64d, MftValidDataLength=%I64d"
        gui->show_debug(DebugLevel::DetailedProgress, nullptr,
                        std::format(
                                L"MftStartLcn=" NUM_FMT ", MftZoneStart=" NUM_FMT ", MftZoneEnd=" NUM_FMT ", Mft2StartLcn=" NUM_FMT ", MftValidDataLength=" NUM_FMT,
                                ntfs_data.MftStartLcn.QuadPart, ntfs_data.MftZoneStart.QuadPart,
                                ntfs_data.MftZoneEnd.QuadPart, ntfs_data.Mft2StartLcn.QuadPart,
                                ntfs_data.MftValidDataLength.QuadPart / ntfs_data.BytesPerCluster));

        // Show debug message: "MftExcludes[%u].Start=%I64d, MftExcludes[%u].End=%I64d"
        gui->show_debug(DebugLevel::DetailedProgress, nullptr,
                        std::format(MFT_EXCL_FMT, 0, data.mft_excludes_[0].start_, 0, data.mft_excludes_[0].end_));
        gui->show_debug(DebugLevel::DetailedProgress, nullptr,
                        std::format(MFT_EXCL_FMT, 1, data.mft_excludes_[1].start_, 1, data.mft_excludes_[1].end_));
        gui->show_debug(DebugLevel::DetailedProgress, nullptr,
                        std::format(MFT_EXCL_FMT, 2, data.mft_excludes_[2].start_, 2, data.mft_excludes_[2].end_));
    }
}

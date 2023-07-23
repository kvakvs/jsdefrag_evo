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

void DefragGui::write_stats(const DefragState &data) {
    FileNode *largest_items[25];
    Clusters64 total_clusters;
    Bytes64 total_bytes;
    uint64_t total_fragments;
    size_t fragments;
    FileNode *item;
    Log::log_always(
            std::format(L"- Total disk space: " NUM_FMT " bytes ({:.1f} Gb), " NUM_FMT " clusters",
                        (data.bytes_per_cluster_ * data.total_clusters_).value(),
                        (data.bytes_per_cluster_ * data.total_clusters_).as<double>() / gigabytes(1).as<double>(),
                        data.total_clusters_.value())
    );
    Log::log_always(
            std::format(L"- Bytes per cluster: " NUM_FMT " bytes", data.bytes_per_cluster_.value()));
    Log::log_always(
            std::format(L"- Number of files: " NUM_FMT, data.count_all_files_));
    Log::log_always(
            std::format(L"- Number of directories: " NUM_FMT, data.count_directories_));
    Log::log_always(
            std::format(L"- Total size of analyzed items: " NUM_FMT " bytes ({:.1f} Gb), " NUM_FMT " clusters",
                        (data.count_all_clusters_ * data.bytes_per_cluster_).value(),
                        (data.count_all_clusters_ * data.bytes_per_cluster_).as<double>() / gigabytes(1).value(),
                        data.count_all_clusters_.value()));

    if (data.count_all_files_ + data.count_directories_ > 0) {
        Log::log_always(
                std::format(L"- Number of fragmented items: " NUM_FMT " (" FLT4_FMT "% of all items)",
                            data.count_fragmented_items_,
                            (double) (data.count_fragmented_items_ * 100) /
                            (data.count_all_files_ + data.count_directories_)));
    } else {
        Log::log_always(
                std::format(L"- Number of fragmented items: " NUM_FMT, data.count_fragmented_items_));
    }

    if (data.count_all_clusters_ && data.total_clusters_) {
        Log::log_always(std::format(
                L"- Total size of fragmented items: " NUM_FMT " bytes, " NUM_FMT " clusters, " FLT4_FMT "% of all items, " FLT4_FMT "% of disk",
                (data.count_fragmented_clusters_ * data.bytes_per_cluster_).value(),
                data.count_fragmented_clusters_.value(),
                data.count_fragmented_clusters_.as<double>() * 100.0 / data.count_all_clusters_.as<double>(),
                data.count_fragmented_clusters_.as<double>() * 100.0 / data.total_clusters_.as<double>()
        ));
    } else {
        Log::log_always(std::format(
                L"- Total size of fragmented items: " NUM_FMT " bytes, " NUM_FMT " clusters",
                (data.count_fragmented_clusters_ * data.bytes_per_cluster_).value(),
                data.count_fragmented_clusters_.value()
        ));
    }

    if (data.total_clusters_) {
        Log::log_always(std::format(
                L"- Free disk space: " NUM_FMT " bytes, " NUM_FMT " clusters, " FLT4_FMT "% of disk",
                (data.count_free_clusters_ * data.bytes_per_cluster_).value(),
                data.count_free_clusters_.value(),
                data.count_free_clusters_.as<double>() * 100.0 / data.total_clusters_.as<double>()
        ));
    } else {
        Log::log_always(std::format(
                L"- Free disk space: " NUM_FMT " bytes, " NUM_FMT " clusters",
                (data.count_free_clusters_ * data.bytes_per_cluster_).value(),
                data.count_free_clusters_.value()
        ));
    }

    Log::log_always(std::format(L"- Number of gaps: " NUM_FMT, data.count_gaps_));

    if (data.count_gaps_ > 0) {
        Log::log_always(std::format(L"- Number of small gaps: " NUM_FMT " (" FLT4_FMT "% of all gaps)",
                                    data.count_gaps_less16_,
                                    (double) (data.count_gaps_less16_ * 100) / data.count_gaps_));
    } else {
        Log::log_always(std::format(L"- Number of small gaps: " NUM_FMT, data.count_gaps_less16_));
    }

    if (data.count_free_clusters_) {
        Log::log_always(std::format(
                L"- Size of small gaps: " NUM_FMT " bytes, " NUM_FMT " clusters, " FLT4_FMT "% of free disk space",
                (data.count_clusters_less16_ * data.bytes_per_cluster_).value(),
                data.count_clusters_less16_.value(),
                data.count_clusters_less16_.as<double>() * 100.0 / data.count_free_clusters_.as<double>()
        ));
    } else {
        Log::log_always(std::format(
                L"- Size of small gaps: " NUM_FMT " bytes, " NUM_FMT " clusters",
                (data.count_clusters_less16_ * data.bytes_per_cluster_).value(),
                data.count_clusters_less16_.value()
        ));
    }

    if (data.count_gaps_ > 0) {
        Log::log_always(
                std::format(L"- Number of big gaps: " NUM_FMT " (" FLT4_FMT "% of all gaps)",
                            data.count_gaps_ - data.count_gaps_less16_,
                            (double) ((data.count_gaps_ - data.count_gaps_less16_) * 100) /
                            data.count_gaps_));
    } else {
        Log::log_always(
                std::format(L"- Number of big gaps: " NUM_FMT, data.count_gaps_ - data.count_gaps_less16_));
    }

    if (data.count_free_clusters_) {
        Log::log_always(std::format(
                L"- Size of big gaps: " NUM_FMT " bytes, " NUM_FMT " clusters, " FLT4_FMT "% of free disk space",
                ((data.count_free_clusters_ - data.count_clusters_less16_) * data.bytes_per_cluster_).value(),
                (data.count_free_clusters_ - data.count_clusters_less16_).value(),
                (data.count_free_clusters_ - data.count_clusters_less16_).as<double>() * 100.0 /
                data.count_free_clusters_.as<double>()
        ));
    } else {
        Log::log_always(std::format(
                L"- Size of big gaps: " NUM_FMT " bytes, " NUM_FMT " clusters",
                ((data.count_free_clusters_ - data.count_clusters_less16_) * data.bytes_per_cluster_).value(),
                (data.count_free_clusters_ - data.count_clusters_less16_).value()
        ));
    }

    if (data.count_gaps_ > 0) {
        Log::log_always(std::format(
                L"- Average gap size: " FLT4_FMT " clusters",
                (double) (data.count_free_clusters_.value() / data.count_gaps_)
        ));
    }

    if (data.count_free_clusters_) {
        Log::log_always(std::format(
                L"- Biggest gap: " NUM_FMT " bytes, " NUM_FMT " clusters, " FLT4_FMT "% of free disk space",
                (data.biggest_gap_ * data.bytes_per_cluster_).value(),
                data.biggest_gap_.value(),
                data.biggest_gap_.as<double>() * 100.0 / data.count_free_clusters_.as<double>()
        ));
    } else {
        Log::log_always(std::format(
                L"- Biggest gap: " NUM_FMT " bytes, " NUM_FMT " clusters",
                (data.biggest_gap_ * data.bytes_per_cluster_).value(),
                data.biggest_gap_.value()
        ));
    }

    if (data.total_clusters_) {
        Log::log_always(std::format(
                L"- Average end-begin distance: " FLT0_FMT " clusters, " FLT4_FMT "% of volume size",
                data.average_distance_,
                100.0 * data.average_distance_ / data.total_clusters_.as<double>()
        ));
    } else {
        Log::log_always(std::format(
                L"- Average end-begin distance: " FLT0_FMT " clusters", data.average_distance_));
    }

    for (item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
        if (!item->is_unmovable_) continue;
        if (item->is_excluded_) continue;
        if (item->is_dir_ && data.cannot_move_dirs_ > 20) continue;
        break;
    }

    if (item != nullptr) {
        Log::log_always(L"These items could not be moved:");
        Log::log_always(L"  " SUMMARY_HEADER);

        total_fragments = 0;
        total_bytes = {};
        total_clusters = {};

        for (item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
            if (!item->is_unmovable_) continue;
            if (item->is_excluded_) continue;
            if (item->is_dir_ && data.cannot_move_dirs_ > 20) continue;
            if ((_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
                 _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0)) {
                continue;
            }

            fragments = DefragRunner::get_fragment_count(item);

            if (!item->have_long_path()) {
                Log::log_always(std::format(
                        L"  " SUMMARY_FMT " [at cluster " NUM_FMT "]",
                        fragments, item->bytes_.value(), item->clusters_count_.value(), item->get_item_lcn().value()
                ));
            } else {
                Log::log_always(std::format(
                        L"  " SUMMARY_FMT " {}", fragments, item->bytes_.value(), item->clusters_count_.value(),
                        item->get_long_path()
                ));
            }

            total_fragments += fragments;
            total_bytes += item->bytes_;
            total_clusters += item->clusters_count_;
        }

        Log::log_always(L"  " SUMMARY_DASH_LINE);
        Log::log_always(std::format(
                L"  " SUMMARY_FMT " Total",
                total_fragments, total_bytes.value(), total_clusters.value()
        ));
    }

    for (item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
        if (item->is_excluded_) continue;
        if (item->is_dir_ && data.cannot_move_dirs_ > 20) continue;

        fragments = DefragRunner::get_fragment_count(item);

        if (fragments <= 1) continue;

        break;
    }

    if (item != nullptr) {
        Log::log_always(L"These items are still fragmented:");
        Log::log_always(L"  " SUMMARY_HEADER);

        total_fragments = 0;
        total_bytes = {};
        total_clusters = {};

        for (item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
            if (item->is_excluded_) continue;
            if (item->is_dir_ && data.cannot_move_dirs_ > 20) continue;

            fragments = DefragRunner::get_fragment_count(item);

            if (fragments <= 1) continue;

            if (!item->have_long_path()) {
                Log::log_always(std::format(
                        L"  " SUMMARY_FMT " [at LCN " NUM_FMT "]",
                        fragments, item->bytes_.value(), item->clusters_count_.value(),
                        item->get_item_lcn().value()
                ));
            } else {
                Log::log_always(std::format(
                        L"  " SUMMARY_FMT " {}",
                        fragments, item->bytes_.value(), item->clusters_count_.value(),
                        item->get_long_path()
                ));
            }

            total_fragments += fragments;
            total_bytes += item->bytes_;
            total_clusters += item->clusters_count_;
        }

        Log::log_always(L"  " SUMMARY_DASH_LINE);
        Log::log_always(std::format(
                L"  " SUMMARY_FMT " Total",
                total_fragments, total_bytes.value(), total_clusters.value()
        ));
    }

    int last_largest = 0;

    for (item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
        if ((_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
             _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0)) {
            continue;
        }

        int i;
        for (i = last_largest - 1; i >= 0; i--) {
            if (item->clusters_count_ < largest_items[i]->clusters_count_) break;

            if (item->clusters_count_ == largest_items[i]->clusters_count_ &&
                item->bytes_ < largest_items[i]->bytes_) {
                break;
            }

            if (item->clusters_count_ == largest_items[i]->clusters_count_ &&
                item->bytes_ == largest_items[i]->bytes_ &&
                _wcsicmp(item->get_long_fn(), largest_items[i]->get_long_fn()) > 0) {
                break;
            }
        }

        if (i < 24) {
            if (last_largest < 25) last_largest++;

            for (int j = last_largest - 1; j > i + 1; j--) {
                largest_items[j] = largest_items[j - 1];
            }

            largest_items[i + 1] = item;
        }
    }

    if (last_largest > 0) {
        Log::log_always(L"The 25 largest items on disk:");
        Log::log_always(L"  " SUMMARY_HEADER);

        for (auto i = 0; i < last_largest; i++) {
            if (!largest_items[i]->have_long_path()) {
                Log::log_always(std::format(
                        L"  " SUMMARY_FMT " [at LCN " NUM_FMT "]",
                        DefragRunner::get_fragment_count(largest_items[i]),
                        largest_items[i]->bytes_.value(), largest_items[i]->clusters_count_.value(),
                        largest_items[i]->get_item_lcn().value()
                ));
            } else {
                Log::log_always(std::format(
                        L"  " SUMMARY_FMT " {}",
                        DefragRunner::get_fragment_count(largest_items[i]),
                        largest_items[i]->bytes_.value(), largest_items[i]->clusters_count_.value(),
                        largest_items[i]->get_long_path()
                ));
            }
        }
    }
}

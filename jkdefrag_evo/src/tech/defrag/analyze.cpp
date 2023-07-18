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

void DefragRunner::analyze_volume_read_fs(DefragState &data) {
    DefragGui *gui = DefragGui::get_instance();
    ScanNTFS *scan_ntfs = ScanNTFS::get_instance();

    // Scan NTFS disks
    // Expensive call (can reach 1 minute runtime or more)
    bool result = scan_ntfs->analyze_ntfs_volume(data);

    gui->log_detailed_progress(L"Analyzing volume: Done analyzing NTFS volume");

    // Scan FAT disks
    if (result == FALSE && *data.running_ == RunningState::RUNNING) {
        ScanFAT *scan_fat = ScanFAT::get_instance();

        result = scan_fat->analyze_fat_volume(data);
        gui->log_detailed_progress(L"Analyzing volume: Done analyzing FAT volume");
    }

    // Scan all other filesystems
    if (result == FALSE && *data.running_ == RunningState::RUNNING) {
        gui->log_detailed_progress(L"This is not a FAT or NTFS disk, using the slow scanner.");

        // Setup the width of the progress bar
        data.phase_todo_ = data.total_clusters_ - data.count_free_clusters_;

        for (auto &mft_exclude: data.mft_excludes_) {
            data.phase_todo_ -= (mft_exclude.end_ - mft_exclude.start_);
        }

        // Scan all the files
        scan_dir(data, data.include_mask_.c_str(), nullptr);
        gui->log_detailed_progress(L"Analyzing volume: Done scanning dir");
    }
}

// Scan all files in a volume and store the information in a tree in
// memory for later use by the optimizer.
void DefragRunner::analyze_volume(DefragState &data) {
    DefragGui *gui = DefragGui::get_instance();

    gui->log_detailed_progress(L"Analyzing volume: Started...");

    call_show_status(data, DefragPhase::Analyze, Zone::None); // "Phase 1: Analyze"

    // Fetch the current time in the uint64_t format (1 second = 10000000)
    filetime64_t system_time;
    SYSTEMTIME time1;
    FILETIME time2;

    GetSystemTime(&time1);

    if (SystemTimeToFileTime(&time1, &time2) == TRUE) {
        system_time = from_FILETIME(time2);
    }

    // Expensive call (can reach 1 minute runtime or more)
    analyze_volume_read_fs(data);

    // Update the diskmap with the colors
    data.clusters_done_ = data.phase_todo_;
    gui->draw_cluster(data, 0, 0, DrawColor::Empty);

    // Set up the progress counter and the file/dir counters
    data.clusters_done_ = 0;
    data.phase_todo_ = 0;

    for (auto item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
        data.phase_todo_ += 1;
    }

    gui->show_analyze(data, nullptr);

    // Walk through all the items one by one
    const auto update_every_n_clusters = std::max<uint64_t>(10000UL, data.total_clusters_ / 100000UL);

    for (auto item = Tree::smallest(data.item_tree_); item != nullptr; item = Tree::next(item)) {
        if (*data.running_ != RunningState::RUNNING) break;

        // Construct the full path's of the item. The MFT contains only the filename, plus a pointer to the directory.
        // We have to construct the full paths by joining all the names of the directories, and the name of the file.
        if (!item->have_long_path()) item->set_long_path(get_long_path(data, item).c_str());
        if (!item->have_short_path()) item->set_short_path(get_short_path(data, item).c_str());

        // Apply the Mask and set the Exclude flag of all items that do not match
        if (!Str::match_mask(item->get_long_path(), data.include_mask_.c_str()) &&
            !Str::match_mask(item->get_short_path(), data.include_mask_.c_str())) {
            item->is_excluded_ = true;
            colorize_disk_item(data, item, 0, 0, false);
        }

        // Determine if the item is to be excluded by comparing its name with the Exclude masks.
        if (!item->is_excluded_) {
            for (auto &s: data.excludes_) {
                if (Str::match_mask(item->get_long_path(), s.c_str()) ||
                    Str::match_mask(item->get_short_path(), s.c_str())) {
                    item->is_excluded_ = true;

                    colorize_disk_item(data, item, 0, 0, false);

                    break;
                }
            }
        }

        // Exclude my own logfile
        if (!item->is_excluded_ &&
            (_wcsicmp(item->get_long_fn(), L"jkdefrag.exe.log") == 0 ||
             _wcsicmp(item->get_long_fn(), L"jkdefragcmd.log") == 0 ||
             _wcsicmp(item->get_long_fn(), L"jkdefragscreensaver.log") == 0)) {
            item->is_excluded_ = true;

            colorize_disk_item(data, item, 0, 0, false);
        }

        // The item is a SpaceHog if it's larger than 50 megabytes, or last access time
        // is more than 30 days ago, or if it's filename matches a SpaceHog mask. */
        if (!item->is_excluded_ && !item->is_dir_) {
            if (data.use_default_space_hogs_ && item->bytes_ > kilobytes(50)) {
                item->is_hog_ = true;
            } else if (data.use_default_space_hogs_ &&
                       data.use_last_access_time_ &&
                       item->last_access_time_ + std::chrono::months(1) < system_time) {
                item->is_hog_ = true;
            } else {
                for (const auto &s: data.space_hogs_) {
                    if (Str::match_mask(item->get_long_path(), s.c_str()) ||
                        Str::match_mask(item->get_short_path(), s.c_str())) {
                        item->is_hog_ = true;
                        break;
                    }
                }
            }

            if (item->is_hog_) colorize_disk_item(data, item, 0, 0, false);
        }

        // Special exception for "http://www.safeboot.com/"
        if (Str::match_mask(item->get_long_path(), L"*\\safeboot.fs")) item->is_unmovable_ = true;

        // Special exception for Acronis OS Selector
        if (Str::match_mask(item->get_long_path(), L"?:\\bootwiz.sys")) item->is_unmovable_ = true;
        if (Str::match_mask(item->get_long_path(), L"*\\BOOTWIZ\\*")) item->is_unmovable_ = true;

        // Special exception for DriveCrypt by "http://www.securstar.com/"
        if (Str::match_mask(item->get_long_path(), L"?:\\BootAuth?.sys")) item->is_unmovable_ = true;

        // Special exception for Symantec GoBack
        if (Str::match_mask(item->get_long_path(), L"*\\Gobackio.bin")) item->is_unmovable_ = true;

        // The $BadClus file maps the entire disk and is always unmovable
        if (item->get_long_fn() != nullptr &&
            (_wcsicmp(item->get_long_fn(), L"$BadClus") == 0 ||
             _wcsicmp(item->get_long_fn(), L"$BadClus:$Bad:$DATA") == 0)) {
            item->is_unmovable_ = true;
        }

        // Update the progress percentage
        data.clusters_done_ += 1;

        if (data.clusters_done_ % update_every_n_clusters == 0) {
            gui->draw_cluster(data, 0, 0, DrawColor::Empty);
        }
    }
    gui->log_detailed_progress(L"Analyzing volume: Done iterating the tree");

    // Force the percentage to 100%
    data.clusters_done_ = data.phase_todo_;
    gui->draw_cluster(data, 0, 0, DrawColor::Empty);

    // Calculate the begin of the zone's
    calculate_zones(data);
    gui->log_detailed_progress(L"Analyzing volume: Done calculating zones");

    // Call the ShowAnalyze() callback one last time
    gui->show_analyze(data, nullptr);
    gui->log_detailed_progress(L"Analyzing volume: finished");
}

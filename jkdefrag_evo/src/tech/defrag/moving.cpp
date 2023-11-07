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


bool DefragRunner::move_item(DefragState &defrag_state, MoveTask &task,
                             MoveDirection direction) const {
    // If the Item is Unmovable, Excluded, or has zero size then we cannot move it
    if (!task.file_->can_move()) return false;

    // Directories cannot be moved on FAT volumes. This is a known Windows limitation
    // and not a bug in JkDefrag. But JkDefrag will still try, to allow for possible
    // circumstances where the Windows defragmentation API can move them after all.
    // To speed up things we count the number of directories that could not be moved,
    // and when it reaches 20 we ignore all directories from then on.
    if (task.file_->is_dir_ && defrag_state.cannot_move_dirs_ > 20) {
        task.file_->is_unmovable_ = true;
        colorize_disk_item(defrag_state, task.file_, 0, 0, false);
        return false;
    }

    // Open a filehandle for the item and call the subfunctions (see above) to
    // move the file. If success then return true.
    cluster_count64_t clusters_done = 0;
    bool result = true;

    while (clusters_done < task.count_ && defrag_state.is_still_running()) {
        cluster_count64_t clusters_todo = task.count_ - clusters_done;

        if (defrag_state.bytes_per_cluster_ > 0) {
            if (clusters_todo > 0x40000000LL / defrag_state.bytes_per_cluster_) {
                clusters_todo = 0x40000000LL / defrag_state.bytes_per_cluster_;
            }
        } else {
            if (clusters_todo > 262144) clusters_todo = 262144;
        }

        HANDLE file_handle = open_item_handle(defrag_state, task.file_);
        if (file_handle == nullptr) break;

        {
            auto try_task = task;
            try_task.lcn_to_ = task.lcn_to_ + clusters_done;
            try_task.vcn_from_ = task.vcn_from_ + clusters_done;
            try_task.count_ = clusters_todo;

            result = move_item_try_strategies(defrag_state, try_task, direction);

            if (!result) break;
        }

        clusters_done = clusters_done + clusters_todo;
        FlushFileBuffers(file_handle);// Is this useful? Can't hurt
        CloseHandle(file_handle);
    }

    if (result) {
        if (task.file_->is_dir_) defrag_state.cannot_move_dirs_ = 0;
        return true;
    }

    // If error then set the Unmovable flag, colorize the item on the screen, recalculate
    // the begin of the zone's, and return false.
    task.file_->is_unmovable_ = true;

    if (task.file_->is_dir_) defrag_state.cannot_move_dirs_++;

    colorize_disk_item(defrag_state, task.file_, 0, 0, false);
    calculate_zones(defrag_state);

    return false;
}

/**
 * \brief Subfunction for MoveItem(), see below. Move (part of) an item to a new location on disk.
 * The file is moved in a single FSCTL_MOVE_FILE call. If the file has fragments then Windows will join them up.
 * Note: the offset and size of the block is in absolute clusters, not virtual clusters.
 * \param new_lcn Where to move to.
 * \param offset Number of first cluster to be moved
 * \param size
 * \return NO_ERROR value or GetLastError() from DeviceIoControl()
 */
DWORD DefragRunner::move_item_whole(DefragState &data, MoveTask &task) const {
    MOVE_FILE_DATA move_params;
    uint64_t lcn;
    DWORD w;
    DefragGui *gui = DefragGui::get_instance();

    // Find the first fragment that contains clusters inside the block, so we can translate the absolute cluster number
    // of the block into the virtual cluster number used by Windows.
    vcn64_t vcn = 0;
    vcn64_t real_vcn = 0;

    auto fragment = task.file_->fragments_.begin();
    for (; fragment != task.file_->fragments_.end(); fragment++) {
        if (!fragment->is_virtual()) {
            if (real_vcn + fragment->next_vcn_ - vcn - 1 >= task.vcn_from_) break;

            real_vcn = real_vcn + fragment->next_vcn_ - vcn;
        }

        vcn = fragment->next_vcn_;
    }

    // Set up the parameters for the move
    move_params.FileHandle = task.file_handle_;
    move_params.StartingLcn.QuadPart = task.lcn_to_;
    move_params.StartingVcn.QuadPart = vcn + (task.vcn_from_ - real_vcn);
    move_params.ClusterCount = (uint32_t) task.count_;

    if (fragment == task.file_->fragments_.end()) {
        lcn = 0;
    } else {
        lcn = fragment->lcn_ + (task.vcn_from_ - real_vcn);
    }

    // Show progress message
    gui->show_move(task.file_, task.count_, lcn, task.lcn_to_, move_params.StartingVcn.QuadPart);
    data.bitmap_.mark(lcn, lcn + task.count_, ClusterMapValue::Free);
    data.bitmap_.mark(task.lcn_to_, task.lcn_to_ + task.count_, ClusterMapValue::InUse);

    // Draw the item and the destination clusters on the screen in the BUSY	color
    colorize_disk_item(data, task.file_, move_params.StartingVcn.QuadPart, move_params.ClusterCount,
                       false);

    gui->draw_cluster(data, task.lcn_to_, task.lcn_to_ + task.count_, DrawColor::Busy);

    // Call Windows to perform the move.
    DWORD result = DeviceIoControl(data.disk_.volume_handle_, FSCTL_MOVE_FILE, &move_params,
                                   sizeof move_params, nullptr, 0, &w, nullptr);

    if (result != FALSE) {
        result = NO_ERROR;
    } else {
        result = GetLastError();
    }

    // Update the PhaseDone counter for the progress bar
    data.clusters_done_ += move_params.ClusterCount;

    // Undraw the destination clusters on the screen
    gui->draw_cluster(data, task.lcn_to_, task.lcn_to_ + task.count_, DrawColor::Empty);

    return result;
}

/**
 * \brief Subfunction for MoveItem(), see below. Move (part of) an item to a new location on disk.
 * Move the item one fragment at a time, a FSCTL_MOVE_FILE call per fragment. The fragments will be lined up on disk and the defragger will treat the
 * item as unfragmented. Note: the offset and size of the block is in absolute clusters, not virtual clusters.
 * \param new_lcn Where to move to
 * \param offset Number of first cluster to be moved
 * \param size Number of clusters to be moved
 * \return NO_ERROR or GetLastError() from DeviceIoControl()
 */
DWORD DefragRunner::move_item_in_fragments(DefragState &data, MoveTask &task) const {
    MOVE_FILE_DATA move_params;
    uint64_t from_lcn;
    DWORD w;
    DefragGui *gui = DefragGui::get_instance();

    // Walk through the fragments of the item and move them one by one to the new location
    DWORD error_code = NO_ERROR;
    vcn64_t vcn = 0;
    vcn64_t real_vcn = 0;

    for (auto &fragment: task.file_->fragments_) {
        if (*data.running_ != RunningState::RUNNING) break;

        if (!fragment.is_virtual()) {
            if (real_vcn >= task.vcn_from_ + task.count_) break;

            if (real_vcn + fragment.next_vcn_ - vcn - 1 >= task.vcn_from_) {
                // Setup the parameters for the move. If the block that we want to move begins somewhere in
                // the middle of a fragment then we have to setup slightly differently than when the fragment is
                // at or after the begin of the block.
                // TODO: THis can be filled from the MoveTask (by the MoveTask class)
                move_params.FileHandle = task.file_handle_;

                if (real_vcn < task.vcn_from_) {
                    /* The fragment starts before the Offset and overlaps. Move the
                    part of the fragment from the Offset until the end of the
                    fragment or the block. */
                    move_params.StartingLcn.QuadPart = task.lcn_to_;
                    move_params.StartingVcn.QuadPart = vcn + (task.vcn_from_ - real_vcn);

                    if (task.count_ < fragment.next_vcn_ - vcn - (task.vcn_from_ - real_vcn)) {
                        move_params.ClusterCount = (uint32_t) task.count_;
                    } else {
                        move_params.ClusterCount =
                                (uint32_t) (fragment.next_vcn_ - vcn - (task.vcn_from_ - real_vcn));
                    }

                    from_lcn = fragment.lcn_ + (task.vcn_from_ - real_vcn);
                } else {
                    // The fragment starts at or after the Offset. Move the part of the fragment inside the block (up until Offset+Size).
                    move_params.StartingLcn.QuadPart = task.lcn_to_ + real_vcn - task.vcn_from_;
                    move_params.StartingVcn.QuadPart = vcn;

                    if (fragment.next_vcn_ - vcn < task.vcn_from_ + task.count_ - real_vcn) {
                        move_params.ClusterCount = (uint32_t) (fragment.next_vcn_ - vcn);
                    } else {
                        move_params.ClusterCount =
                                (uint32_t) (task.vcn_from_ + task.count_ - real_vcn);
                    }
                    from_lcn = fragment.lcn_;
                }

                // Show progress message
                gui->show_move(task.file_, move_params.ClusterCount, from_lcn,
                               move_params.StartingLcn.QuadPart, move_params.StartingVcn.QuadPart);
                data.bitmap_.mark(from_lcn, from_lcn + move_params.ClusterCount,
                                  ClusterMapValue::Free);
                data.bitmap_.mark(move_params.StartingLcn.QuadPart,
                                  move_params.StartingLcn.QuadPart + move_params.ClusterCount,
                                  ClusterMapValue::InUse);

                // Draw the item and the destination clusters on the screen in the BUSY	color.
                colorize_disk_item(data, task.file_, move_params.StartingVcn.QuadPart,
                                   move_params.ClusterCount, false);

                gui->draw_cluster(data, move_params.StartingLcn.QuadPart,
                                  move_params.StartingLcn.QuadPart + move_params.ClusterCount,
                                  DrawColor::Busy);

                // Call Windows to perform the move
                error_code =
                        DeviceIoControl(data.disk_.volume_handle_, FSCTL_MOVE_FILE, &move_params,
                                        sizeof move_params, nullptr, 0, &w, nullptr);

                if (error_code != 0) {
                    error_code = NO_ERROR;
                } else {
                    error_code = GetLastError();
                }

                // Update the PhaseDone counter for the progress bar
                data.clusters_done_ += move_params.ClusterCount;

                // Undraw the destination clusters on the screen
                gui->draw_cluster(data, move_params.StartingLcn.QuadPart,
                                  move_params.StartingLcn.QuadPart + move_params.ClusterCount,
                                  DrawColor::Empty);
                data.bitmap_.mark(move_params.StartingLcn.QuadPart,
                                  move_params.StartingLcn.QuadPart + move_params.ClusterCount,
                                  ClusterMapValue::Free);

                // If there was an error then exit
                if (error_code != NO_ERROR) return error_code;
            }

            real_vcn = real_vcn + fragment.next_vcn_ - vcn;
        }

        // Next fragment
        vcn = fragment.next_vcn_;
    }

    return error_code;
}

/**
 * \brief Subfunction for MoveItem(), see below. Move (part of) an item to a new location on disk.
 * Strategy 0: move the block in a single FSCTL_MOVE_FILE call. If the block has fragments then Windows will join them up.
 * Strategy 1: move the block one fragment at a time. The fragments will be lined up on disk and the defragger will treat them as unfragmented.
 * Note: the offset and size of the block is in absolute clusters, not virtual clusters.
 * \param new_lcn Where to move to
 * \param offset Number of first cluster to be moved
 * \param size Number of clusters to be moved
 * \param strategy move in one part, move individual fragments
 * \return True if all good
 */
bool DefragRunner::move_item_with_strat(DefragState &data, MoveTask &task,
                                        MoveStrategy strategy) const {
    DWORD error_code;
    DefragGui *gui = DefragGui::get_instance();

    // Slow the program down if so selected
    slow_down(data);

    // Move the item, either in a single block or fragment by fragment
    switch (strategy) {
        case MoveStrategy::Whole:
            error_code = move_item_whole(data, task);
            break;
        case MoveStrategy::InFragments:
            error_code = move_item_in_fragments(data, task);
    }

    // If there was an error then fetch the errormessage and save it
    std::wstring error_string;
    if (error_code != NO_ERROR) { error_string = Str::system_error(error_code); }

    // Fetch the new fragment map of the item and refresh the screen
    colorize_disk_item(data, task.file_, 0, 0, true);
    Tree::detach(data.item_tree_, task.file_);

    const bool result = get_fragments(data, task.file_, task.file_handle_);

    Tree::insert(data.item_tree_, data.balance_count_, task.file_);
    colorize_disk_item(data, task.file_, 0, 0, false);

    // if windows reported an error while moving the item then show the error message and return false
    if (error_code != NO_ERROR) {
        gui->show_debug(DebugLevel::DetailedProgress, task.file_, std::move(error_string));
        return false;
    }

    // If there was an error analyzing the item then return false
    return result;
}

bool DefragRunner::move_item_try_strategies(DefragState &data, MoveTask &task,
                                            const MoveDirection direction) const {
    lcn_extent_t cluster;

    DefragGui *gui = DefragGui::get_instance();

    // Remember the current position on disk of the item
    const auto old_lcn = task.file_->get_item_lcn();

    // Move the Item to the requested LCN. If error then return false
    {
        auto result = move_item_with_strat(data, task, MoveStrategy::Whole);
        if (!result) return false;
    }
    if (*data.running_ != RunningState::RUNNING) return false;

    // If the block is not fragmented then return true
    if (!is_fragmented(task.file_, task.vcn_from_, task.count_)) return true;

    // Show debug message: "Windows could not move the file, trying alternative method."
    gui->show_debug(DebugLevel::DetailedProgress, task.file_,
                    L"Windows could not move the file, trying alternative method.");

    // Find another gap on disk for the item
    switch (direction) {
        case MoveDirection::Up: {
            lcn64_t cluster_start = old_lcn + task.file_->clusters_count_;

            if (cluster_start + task.file_->clusters_count_ >= task.lcn_to_ &&
                cluster_start < task.lcn_to_ + task.file_->clusters_count_) {
                cluster_start = task.lcn_to_ + task.file_->clusters_count_;
            }

            auto result2 = find_gap(data, cluster_start, 0, task.count_, true, false, false);
            if (result2.has_value()) {
                cluster = result2.value();
            } else {
                return false;
            }
            break;
        }
        case MoveDirection::Down: {
            auto result3 = find_gap(data, data.zones_[1], old_lcn, task.count_, true, true, false);
            if (result3.has_value()) {
                cluster = result3.value();
            } else {
                return false;
            }
            break;
        }
    }

    // Add the size of the item to the width of the progress bar, we have discovered that we have more work to do.
    data.phase_todo_ += task.count_;

    // Move the item to the other gap using strategy InFragments.
    switch (direction) {
        case MoveDirection::Up: {
            auto up_task = task;
            up_task.lcn_to_ = cluster.begin();

            auto result = move_item_with_strat(data, up_task, MoveStrategy::InFragments);

            if (!result) return false;
            break;
        }
        case MoveDirection::Down: {
            auto down_task = task;
            down_task.lcn_to_ = cluster.end() - task.count_;

            auto result = move_item_with_strat(data, down_task, MoveStrategy::InFragments);

            if (!result) return false;
            break;
        }
    }

    // If the block is still fragmented then return false.
    if (is_fragmented(task.file_, task.vcn_from_, task.count_)) {
        // Show debug message: "Alternative method failed, leaving file where it is."
        gui->show_debug(DebugLevel::DetailedProgress, task.file_,
                        L"Alternative method failed, leaving file where it is.");
        return false;
    }

    gui->show_debug(DebugLevel::DetailedProgress, task.file_, L"");

    // Add the size of the item to the width of the progress bar, we have more work to do.
    data.phase_todo_ += task.count_;

    // Strategy 1 has helped. Move the Item again to where we want it, but this time use strategy InFragments.
    return move_item_with_strat(data, task, MoveStrategy::InFragments);
}

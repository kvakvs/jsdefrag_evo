#include "std_afx.h"

/**
 * \brief Move (part of) an item to a new location on disk. Moving the Item will automatically defragment it. If unsuccesful then set the Unmovable
 * flag of the item and return false, otherwise return true. Note: the item will move to a different location in the tree.
 * Note: the offset and size of the block is in absolute clusters, not virtual clusters.
 * \param new_lcn Where to move to
 * \param offset Number of first cluster to be moved
 * \param size Number of clusters to be moved
 * \param direction 0: move up, 1: move down
 * \return
 */
int DefragLib::move_item(DefragDataStruct *data, ItemStruct *item, const uint64_t new_lcn,
                         const uint64_t offset, const uint64_t size, const int direction) const {
    /* If the Item is Unmovable, Excluded, or has zero size then we cannot move it. */
    if (item->is_unmovable_) return false;
    if (item->is_excluded_) return false;
    if (item->clusters_count_ == 0) return false;

    /* Directories cannot be moved on FAT volumes. This is a known Windows limitation
    and not a bug in JkDefrag. But JkDefrag will still try, to allow for possible
    circumstances where the Windows defragmentation API can move them after all.
    To speed up things we count the number of directories that could not be moved,
    and when it reaches 20 we ignore all directories from then on. */
    if (item->is_dir_ && data->cannot_move_dirs_ > 20) {
        item->is_unmovable_ = true;

        colorize_item(data, item, 0, 0, false);

        return false;
    }

    /* Open a filehandle for the item and call the subfunctions (see above) to
    move the file. If success then return true. */
    uint64_t clusters_done = 0;
    bool result = true;

    while (clusters_done < size && *data->running_ == RunningState::RUNNING) {
        uint64_t clusters_todo = size - clusters_done;

        if (data->bytes_per_cluster_ > 0) {
            if (clusters_todo > 1073741824 / data->bytes_per_cluster_) {
                clusters_todo = 1073741824 / data->bytes_per_cluster_;
            }
        } else {
            if (clusters_todo > 262144) clusters_todo = 262144;
        }

        HANDLE file_handle = open_item_handle(data, item);

        result = false;

        if (file_handle == nullptr) break;

        result = move_item4(data, item, file_handle, new_lcn + clusters_done, offset + clusters_done,
                            clusters_todo, direction);

        if (!result) break;

        clusters_done = clusters_done + clusters_todo;

        FlushFileBuffers(file_handle); /* Is this useful? Can't hurt. */
        CloseHandle(file_handle);
    }

    if (result) {
        if (item->is_dir_) data->cannot_move_dirs_ = 0;
        return true;
    }

    /* If error then set the Unmovable flag, colorize the item on the screen, recalculate
    the begin of the zone's, and return false. */
    item->is_unmovable_ = true;

    if (item->is_dir_) data->cannot_move_dirs_++;

    colorize_item(data, item, 0, 0, false);
    calculate_zones(data);

    return false;
}

/**
 * \brief Subfunction for MoveItem(), see below. Move (part of) an item to a new location on disk. Return errorcode from DeviceIoControl().
 * The file is moved in a single FSCTL_MOVE_FILE call. If the file has fragments then Windows will join them up.
 * Note: the offset and size of the block is in absolute clusters, not virtual clusters.
 * \param new_lcn Where to move to.
 * \param offset Number of first cluster to be moved
 * \param size
 * \return
 */
uint32_t DefragLib::move_item1(DefragDataStruct *data, HANDLE file_handle, const ItemStruct *item,
                               const uint64_t new_lcn, const uint64_t offset,
                               const uint64_t size) const
/* Number of clusters to be moved. */
{
    MOVE_FILE_DATA move_params;
    FragmentListStruct *fragment;
    uint64_t lcn;
    DWORD w;
    DefragGui *gui = DefragGui::get_instance();

    /* Find the first fragment that contains clusters inside the block, so we
    can translate the absolute cluster number of the block into the virtual
    cluster number used by Windows. */
    uint64_t vcn = 0;
    uint64_t real_vcn = 0;

    for (fragment = item->fragments_; fragment != nullptr; fragment = fragment->next_) {
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            if (real_vcn + fragment->next_vcn_ - vcn - 1 >= offset) break;

            real_vcn = real_vcn + fragment->next_vcn_ - vcn;
        }

        vcn = fragment->next_vcn_;
    }

    /* Setup the parameters for the move. */
    move_params.FileHandle = file_handle;
    move_params.StartingLcn.QuadPart = new_lcn;
    move_params.StartingVcn.QuadPart = vcn + (offset - real_vcn);
    move_params.ClusterCount = (uint32_t) size;

    if (fragment == nullptr) {
        lcn = 0;
    } else {
        lcn = fragment->lcn_ + (offset - real_vcn);
    }

    /* Show progress message. */
    gui->show_move(item, move_params.ClusterCount, lcn, new_lcn, move_params.StartingVcn.QuadPart);

    /* Draw the item and the destination clusters on the screen in the BUSY	color. */
    colorize_item(data, item, move_params.StartingVcn.QuadPart, move_params.ClusterCount, false);

    gui->draw_cluster(data, new_lcn, new_lcn + size, DefragStruct::COLORBUSY);

    /* Call Windows to perform the move. */
    uint32_t error_code = DeviceIoControl(data->disk_.volume_handle_, FSCTL_MOVE_FILE, &move_params,
                                          sizeof move_params, nullptr, 0, &w, nullptr);

    if (error_code != 0) {
        error_code = NO_ERROR;
    } else {
        error_code = GetLastError();
    }

    /* Update the PhaseDone counter for the progress bar. */
    data->phase_done_ = data->phase_done_ + move_params.ClusterCount;

    /* Undraw the destination clusters on the screen. */
    gui->draw_cluster(data, new_lcn, new_lcn + size, DefragStruct::COLOREMPTY);

    return error_code;
}

/**
 * \brief Subfunction for MoveItem(), see below. Move (part of) an item to a new location on disk. Return errorcode from DeviceIoControl().
 * Move the item one fragment at a time, a FSCTL_MOVE_FILE call per fragment. The fragments will be lined up on disk and the defragger will treat the
 * item as unfragmented. Note: the offset and size of the block is in absolute clusters, not virtual clusters.
 * \param new_lcn Where to move to
 * \param offset Number of first cluster to be moved
 * \param size Number of clusters to be moved
 * \return
 */
uint32_t DefragLib::move_item2(DefragDataStruct *data, HANDLE file_handle, const ItemStruct *item,
                               const uint64_t new_lcn, const uint64_t offset, const uint64_t size) const {
    MOVE_FILE_DATA move_params;
    uint64_t from_lcn;
    DWORD w;
    DefragGui *gui = DefragGui::get_instance();

    /* Walk through the fragments of the item and move them one by one to the new location. */
    uint32_t error_code = NO_ERROR;
    uint64_t vcn = 0;
    uint64_t real_vcn = 0;

    for (auto fragment = item->fragments_; fragment != nullptr; fragment = fragment->next_) {
        if (*data->running_ != RunningState::RUNNING) break;

        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            if (real_vcn >= offset + size) break;

            if (real_vcn + fragment->next_vcn_ - vcn - 1 >= offset) {
                /* Setup the parameters for the move. If the block that we want to move
                begins somewhere in the middle of a fragment then we have to setup
                slightly differently than when the fragment is at or after the begin
                of the block. */
                move_params.FileHandle = file_handle;

                if (real_vcn < offset) {
                    /* The fragment starts before the Offset and overlaps. Move the
                    part of the fragment from the Offset until the end of the
                    fragment or the block. */
                    move_params.StartingLcn.QuadPart = new_lcn;
                    move_params.StartingVcn.QuadPart = vcn + (offset - real_vcn);

                    if (size < fragment->next_vcn_ - vcn - (offset - real_vcn)) {
                        move_params.ClusterCount = (uint32_t) size;
                    } else {
                        move_params.ClusterCount = (uint32_t) (fragment->next_vcn_ - vcn - (offset - real_vcn));
                    }

                    from_lcn = fragment->lcn_ + (offset - real_vcn);
                } else {
                    /* The fragment starts at or after the Offset. Move the part of
                    the fragment inside the block (up until Offset+Size). */
                    move_params.StartingLcn.QuadPart = new_lcn + real_vcn - offset;
                    move_params.StartingVcn.QuadPart = vcn;

                    if (fragment->next_vcn_ - vcn < offset + size - real_vcn) {
                        move_params.ClusterCount = (uint32_t) (fragment->next_vcn_ - vcn);
                    } else {
                        move_params.ClusterCount = (uint32_t) (offset + size - real_vcn);
                    }
                    from_lcn = fragment->lcn_;
                }

                /* Show progress message. */
                gui->show_move(item, move_params.ClusterCount, from_lcn, move_params.StartingLcn.QuadPart,
                               move_params.StartingVcn.QuadPart);

                /* Draw the item and the destination clusters on the screen in the BUSY	color. */
                //				if (*Data->RedrawScreen == 0) {
                colorize_item(data, item, move_params.StartingVcn.QuadPart, move_params.ClusterCount, false);
                //				} else {
                //					m_jkGui->show_diskmap(Data);
                //				}

                gui->draw_cluster(data, move_params.StartingLcn.QuadPart,
                                  move_params.StartingLcn.QuadPart + move_params.ClusterCount,
                                  DefragStruct::COLORBUSY);

                /* Call Windows to perform the move. */
                error_code = DeviceIoControl(data->disk_.volume_handle_, FSCTL_MOVE_FILE, &move_params,
                                             sizeof move_params, nullptr, 0, &w, nullptr);

                if (error_code != 0) {
                    error_code = NO_ERROR;
                } else {
                    error_code = GetLastError();
                }

                /* Update the PhaseDone counter for the progress bar. */
                data->phase_done_ = data->phase_done_ + move_params.ClusterCount;

                /* Undraw the destination clusters on the screen. */
                gui->draw_cluster(data, move_params.StartingLcn.QuadPart,
                                  move_params.StartingLcn.QuadPart + move_params.ClusterCount,
                                  DefragStruct::COLOREMPTY);

                /* If there was an error then exit. */
                if (error_code != NO_ERROR) return error_code;
            }

            real_vcn = real_vcn + fragment->next_vcn_ - vcn;
        }

        /* Next fragment. */
        vcn = fragment->next_vcn_;
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
 * \param strategy 0: move in one part, 1: move individual fragments
 * \return
 */
int DefragLib::move_item3(DefragDataStruct *data, ItemStruct *item, HANDLE file_handle, const uint64_t new_lcn,
                          const uint64_t offset, const uint64_t size, const int strategy) const {
    uint32_t error_code;
    wchar_t error_string[BUFSIZ];
    DefragGui *gui = DefragGui::get_instance();

    /* Slow the program down if so selected. */
    slow_down(data);

    /* Move the item, either in a single block or fragment by fragment. */
    if (strategy == 0) {
        error_code = move_item1(data, file_handle, item, new_lcn, offset, size);
    } else {
        error_code = move_item2(data, file_handle, item, new_lcn, offset, size);
    }

    /* If there was an error then fetch the errormessage and save it. */
    if (error_code != NO_ERROR) system_error_str(error_code, error_string, BUFSIZ);

    /* Fetch the new fragment map of the item and refresh the screen. */
    colorize_item(data, item, 0, 0, true);

    tree_detach(data, item);

    const int result = get_fragments(data, item, file_handle);

    tree_insert(data, item);

    //		if (*Data->RedrawScreen == 0) {
    colorize_item(data, item, 0, 0, false);
    //		} else {
    //			m_jkGui->show_diskmap(Data);
    //		}

    /* If Windows reported an error while moving the item then show the
    errormessage and return false. */
    if (error_code != NO_ERROR) {
        gui->show_debug(DebugLevel::DetailedProgress, item, error_string);

        return false;
    }

    /* If there was an error analyzing the item then return false. */
    if (result == false) return false;

    return true;
}

/**
 * \brief Subfunction for MoveItem(), see below. Move the item with strategy 0. If this results in fragmentation then try again using strategy 1.
 * Note: The Windows defragmentation API does not report an error if it only moves part of the file and has fragmented the file. This can for example
 * happen when part of the file is locked and cannot be moved, or when (part of) the gap was previously in use by another file but has not yet been
 * released by the NTFS checkpoint system. Note: the offset and size of the block is in absolute clusters, not virtual clusters.
 * \param new_lcn Where to move to
 * \param offset Number of first cluster to be moved
 * \param size Number of clusters to be moved
 * \param direction 0: move up, 1: move down
 * \return true if success, false if failed to move without fragmenting the
item
 */
int DefragLib::move_item4(DefragDataStruct *data, ItemStruct *item, HANDLE file_handle, const uint64_t new_lcn,
                          const uint64_t offset, const uint64_t size, const int direction) const {
    uint64_t cluster_start;
    uint64_t cluster_end;

    DefragGui *gui = DefragGui::get_instance();

    /* Remember the current position on disk of the item. */
    const uint64_t old_lcn = get_item_lcn(item);

    /* Move the Item to the requested LCN. If error then return false. */
    int result = move_item3(data, item, file_handle, new_lcn, offset, size, 0);

    if (result == false) return false;
    if (*data->running_ != RunningState::RUNNING) return false;

    /* If the block is not fragmented then return true. */
    if (!is_fragmented(item, offset, size)) return true;

    /* Show debug message: "Windows could not move the file, trying alternative method." */
    gui->show_debug(DebugLevel::DetailedProgress, item, data->debug_msg_[42].c_str());

    /* Find another gap on disk for the item. */
    if (direction == 0) {
        cluster_start = old_lcn + item->clusters_count_;

        if (cluster_start + item->clusters_count_ >= new_lcn &&
            cluster_start < new_lcn + item->clusters_count_) {
            cluster_start = new_lcn + item->clusters_count_;
        }

        result = find_gap(data, cluster_start, 0, size, true, false, &cluster_start, &cluster_end, FALSE);
    } else {
        result = find_gap(data, data->zones_[1], old_lcn, size, true, true, &cluster_start, &cluster_end, FALSE);
    }

    if (result == false) return false;

    /* Add the size of the item to the width of the progress bar, we have discovered
    that we have more work to do. */
    data->phase_todo_ = data->phase_todo_ + size;

    /* Move the item to the other gap using strategy 1. */
    if (direction == 0) {
        result = move_item3(data, item, file_handle, cluster_start, offset, size, 1);
    } else {
        result = move_item3(data, item, file_handle, cluster_end - size, offset, size, 1);
    }

    if (result == false) return false;

    /* If the block is still fragmented then return false. */
    if (is_fragmented(item, offset, size)) {
        /* Show debug message: "Alternative method failed, leaving file where it is." */
        gui->show_debug(DebugLevel::DetailedProgress, item, data->debug_msg_[45].c_str());

        return false;
    }

    gui->show_debug(DebugLevel::DetailedProgress, item, L"");

    /* Add the size of the item to the width of the progress bar, we have more work to do. */
    data->phase_todo_ = data->phase_todo_ + size;

    /* Strategy 1 has helped. Move the Item again to where we want it, but
    this time use strategy 1. */
    result = move_item3(data, item, file_handle, new_lcn, offset, size, 1);

    return result;
}

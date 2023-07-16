#include "precompiled_header.h"

//    Move the MFT to the beginning of the harddisk.
//    - The Microsoft defragmentation api only supports moving the MFT on Vista.
//    - What to do if there is unmovable data at the beginning of the disk? I have
//    chosen to wrap the MFT around that data. The fragments will be aligned, so
//    the performance loss is minimal, and still faster than placing the MFT
//    higher on the disk.
[[maybe_unused]] void DefragLib::move_mft_to_begin_of_disk(DefragDataStruct *data) {
    ItemStruct *item;

    uint64_t lcn;
    uint64_t gap_begin;
    uint64_t gap_end;
    uint64_t clusters;
    uint64_t clusters_done;

    bool result;

    OSVERSIONINFO os_version;

    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::Progress, nullptr, L"Moving the MFT to the beginning of the volume.");

    /* Exit if this is not an NTFS disk. */
    if (data->disk_.type_ != DiskType::NTFS) {
        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                        L"Cannot move the MFT because this is not an NTFS disk.");

        return;
    }

    /* The Microsoft defragmentation api only supports moving the MFT on Vista. */
    ZeroMemory(&os_version, sizeof(OSVERSIONINFO));

    os_version.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

    if (GetVersionEx(&os_version) != 0 && os_version.dwMajorVersion < 6) {
        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                        L"Cannot move the MFT because it is not supported by this version of Windows.");

        return;
    }

    /* Locate the Item for the MFT. If not found then exit. */
    for (item = tree_smallest(data->item_tree_); item != nullptr; item = tree_next(item)) {
        if (match_mask(item->get_long_path(), L"?:\\$MFT")) break;
    }

    if (item == nullptr) {
        gui->show_debug(DebugLevel::DetailedGapFilling, nullptr, L"Cannot move the MFT because I cannot find it.");

        return;
    }

    /* Exit if the MFT is at the beginning of the volume (inside zone 0) and is not
    fragmented. */
#ifdef jk
    if ((Item->Fragments != nullptr) &&
        (Item->Fragments->NextVcn == Data->Disk.MftLockedClusters) &&
        (Item->Fragments->Next != nullptr) &&
        (Item->Fragments->Next->Lcn < Data->Zones[1]) &&
        (IsFragmented(Item,Data->Disk.MftLockedClusters,Item->Clusters - Data->Disk.MftLockedClusters) == false)) {
            m_jkGui->ShowDebug(DebugLevel::DetailedGapFilling,nullptr,L"No need to move the MFT because it's already at the beginning of the volume and it's data part is not fragmented.");
            return;
    }
#endif

    lcn = 0;
    gap_begin = 0;
    gap_end = 0;
    clusters_done = data->disk_.mft_locked_clusters_;

    while (*data->running_ == RunningState::RUNNING && clusters_done < item->clusters_count_) {
        if (clusters_done > data->disk_.mft_locked_clusters_) {
            gui->show_debug(DebugLevel::DetailedGapFilling, nullptr,
                            std::format(L"Partially placed, " NUM_FMT " clusters more to do",
                                        item->clusters_count_ - clusters_done));
        }

        /* Call the Vacate() function to make a gap at Lcn big enough to hold the MFT.
        The Vacate() function may not be able to move whatever is now at the Lcn, so
        after calling it we have to locate the first gap after the Lcn. */
        if (gap_begin + item->clusters_count_ - clusters_done + 16 > gap_end) {
            vacate(data, lcn, item->clusters_count_ - clusters_done, TRUE);

            result = find_gap(data, lcn, 0, 0, true, false, &gap_begin, &gap_end, TRUE);

            if (result == false) return; /* No gaps found, exit. */
        }

        /* If the gap is not big enough to hold the entire MFT then calculate how much
        will fit in the gap. */
        clusters = item->clusters_count_ - clusters_done;

        if (clusters > gap_end - gap_begin) {
            clusters = gap_end - gap_begin;
            /* It looks like a partial move only succeeds if the number of clusters is a
            multiple of 8. */
            clusters = clusters - clusters % 8;

            if (clusters == 0) {
                lcn = gap_end;

                continue;
            }
        }

        // Move the MFT to the gap
        result = move_item(data, item, gap_begin, clusters_done, clusters, MoveDirection::Up);

        if (result) {
            gap_begin = gap_begin + clusters;
        } else {
            result = find_gap(data, gap_begin, 0, 0, true, false, &gap_begin, &gap_end, TRUE);

            if (!result) return; // No gaps found, exit
        }

        lcn = gap_begin;
        clusters_done = clusters_done + clusters;
    }

    /* Make the MFT unmovable. We don't want it moved again by any other subroutine. */
    item->is_unmovable_ = true;

    colorize_disk_item(data, item, 0, 0, false);
    calculate_zones(data);

    /* Note: The MftExcludes do not change by moving the MFT. */
}

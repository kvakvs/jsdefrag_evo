#include "precompiled_header.h"

// Fill all the gaps at the beginning of the disk with fragments from the files above
void DefragLib::forced_fill(DefragDataStruct *data) {
    call_show_status(data, DefragPhase::ForcedFill, -1); // "Phase 3: ForcedFill"

    // Walk through all the gaps
    uint64_t gap_begin = 0;
    uint64_t max_lcn = data->total_clusters_;

    while (*data->running_ == RunningState::RUNNING) {
        // Find the next gap. If there are no more gaps then exit
        uint64_t gap_end;
        auto result = find_gap(data, gap_begin, 0, 0, true, false,
                               &gap_begin, &gap_end, FALSE);

        if (!result) break;

        // Find the item with the highest fragment on disk
        ItemStruct *highest_item = nullptr;
        uint64_t highest_lcn = 0;
        uint64_t highest_vcn = 0;
        uint64_t highest_size = 0;

        ItemStruct *item;
        for (item = Tree::biggest(data->item_tree_); item != nullptr; item = Tree::prev(item)) {
            if (item->is_unmovable_) continue;
            if (item->is_excluded_) continue;
            if (item->clusters_count_ == 0) continue;

            uint64_t vcn = 0;
            uint64_t real_vcn = 0;

            FragmentListStruct *fragment;
            for (fragment = item->fragments_; fragment != nullptr; fragment = fragment->next_) {
                if (fragment->lcn_ != VIRTUALFRAGMENT) {
                    if (fragment->lcn_ > highest_lcn && fragment->lcn_ < max_lcn) {
                        highest_item = item;
                        highest_lcn = fragment->lcn_;
                        highest_vcn = real_vcn;
                        highest_size = fragment->next_vcn_ - vcn;
                    }

                    real_vcn = real_vcn + fragment->next_vcn_ - vcn;
                }

                vcn = fragment->next_vcn_;
            }
        }

        if (highest_item == nullptr) break;

        // If the highest fragment is before the gap then exit, we're finished
        if (highest_lcn <= gap_begin) break;

        // Move as much of the item into the gap as possible
        uint64_t clusters = gap_end - gap_begin;

        if (clusters > highest_size) clusters = highest_size;

        // TODO: return value is ignored
        move_item(data, highest_item, gap_begin, highest_vcn + highest_size - clusters, clusters,
                  MoveDirection::Up);

        gap_begin = gap_begin + clusters;
        max_lcn = highest_lcn + highest_size - clusters;
    }
}

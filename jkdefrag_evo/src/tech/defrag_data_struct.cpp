#include "precompiled_header.h"

DefragDataStruct::DefragDataStruct() {
    // Initialize the data. Some items are inherited from the caller and are not initialized
    phase_ = DefragPhase::Analyze;
    disk_.volume_handle_ = nullptr;
    disk_.mount_point_ = nullptr;
    disk_.mount_point_slash_ = nullptr;
    disk_.volume_name_[0] = 0;
    disk_.volume_name_slash_[0] = 0;
    disk_.type_ = DiskType::UnknownType;
    item_tree_ = nullptr;
    balance_count_ = 0;
    mft_excludes_[0].start_ = 0;
    mft_excludes_[0].end_ = 0;
    mft_excludes_[1].start_ = 0;
    mft_excludes_[1].end_ = 0;
    mft_excludes_[2].start_ = 0;
    mft_excludes_[2].end_ = 0;
    total_clusters_ = 0;
    bytes_per_cluster_ = 0;

    for (auto i = 0; i < 3; i++) zones_[i] = 0;

    cannot_move_dirs_ = 0;
    count_directories_ = 0;
    count_all_files_ = 0;
    count_fragmented_items_ = 0;
    count_all_bytes_ = 0;
    count_fragmented_bytes_ = 0;
    count_all_clusters_ = 0;
    count_fragmented_clusters_ = 0;
    count_free_clusters_ = 0;
    count_gaps_ = 0;
    biggest_gap_ = 0;
    count_gaps_less16_ = 0;
    count_clusters_less16_ = 0;
    phase_todo_ = 0;
    phase_done_ = 0;

    __timeb64 t{};
    _ftime64_s(&t);

    start_time_ = t.time * 1000 + t.millitm;
    last_checkpoint_ = start_time_;
    running_time_ = 0;
}

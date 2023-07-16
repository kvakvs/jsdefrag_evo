#pragma once

#include <string>
#include <vector>

#include "defrag_lib.h"

// The big data struct that holds all the defragger's variables for a single thread

struct DefragDataStruct {
    int phase_; // The current Phase (1...3)
    int zone_; // The current Zone (0..2) for Phase 3
    RunningState *running_; // If not RUNNING then stop defragging
    //	int *RedrawScreen;                     // 0:no, 1:request, 2: busy
    bool use_last_access_time_; // If TRUE then use LastAccessTime for SpaceHogs
    int cannot_move_dirs_; // If bigger than 20 then do not move dirs

    wchar_t *include_mask_; /* Example: "c:\t1\*" */
    DiskStruct disk_;

    double free_space_; // Percentage of total disk size 0..100

    // Tree in memory with information about all the files. 
    ItemStruct *item_tree_;
    int balance_count_;
    // Array with exclude masks
    Wstrings excludes_;
    // use the built-in SpaceHogs
    bool use_default_space_hogs_;
    // Array with SpaceHog masks
    std::vector<std::wstring> space_hogs_;
    uint64_t zones_[4]; // Begin (LCN) of the zones

    ExcludesStruct mft_excludes_[3]; // List of clusters reserved for the MFT

    /*
     * Counters filled before Phase 1.
     */

    // Size of the volume, in clusters. 
    uint64_t total_clusters_;
    // Number of bytes per cluster.
    uint64_t bytes_per_cluster_;

    /*
     * Counters updated before/after every Phase.
     */

    uint64_t count_free_clusters_; // Number of free clusters
    uint64_t count_gaps_; // Number of gaps
    uint64_t biggest_gap_; // Size of biggest gap, in clusters
    uint64_t count_gaps_less16_; // Number of gaps smaller than 16 clusters
    uint64_t count_clusters_less16_; // Number of clusters in gaps that are smaller than 16 clusters

    /*
     * Counters updated after every Phase, but not before Phase 1 (analyze).
     */

    uint64_t count_directories_; // Number of analysed subdirectories
    uint64_t count_all_files_; // Number of analysed files
    uint64_t count_fragmented_items_; // Number of fragmented files
    uint64_t count_all_bytes_; // Bytes in analysed files
    uint64_t count_fragmented_bytes_; // Bytes in fragmented files
    uint64_t count_all_clusters_; // Clusters in analysed files
    uint64_t count_fragmented_clusters_; // Clusters in fragmented files
    double average_distance_; // Between end and begin of files

    // Counters used to calculate the percentage of work done

    uint64_t phase_todo_; // Number of items to do in this Phase
    uint64_t phase_done_; // Number of items already done in this Phase

    // Variables used to throttle the speed
    //struct {
    int speed_; // Speed as a percentage 1..100
    int64_t start_time_;
    int64_t running_time_;
    int64_t last_checkpoint_;
    //} throttle_;

    // The array with error messages
    // Wstrings debug_msg_;
};

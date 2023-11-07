#pragma once

#include <string>
#include <vector>

#include "runner.h"
#include "extent.h"
#include "../src/tech/defrag/volume_bitmap.h"

// The big data struct that holds all the defragger's variables for a single thread
class DefragState {
public:
    DefragState();

    void add_default_space_hogs();

    [[nodiscard]] bool is_still_running() const {
        return *running_ == RunningState::RUNNING;
    }

    [[nodiscard]] auto total_clusters() const -> cluster_count64_t { return total_clusters_; }

    void set_total_clusters(cluster_count64_t n) {
        total_clusters_ = n;
        bitmap_.reset(n);
    }

public:
    /// The current Phase (1...3)
    DefragPhase phase_ = DefragPhase::Analyze;
    /// The current Zone (0..2) for Phase 3
    Zone zone_{};
    /// If not RUNNING then stop defragging
    RunningState *running_{};
    /// If TRUE then use LastAccessTime for SpaceHogs
    bool use_last_access_time_{};
    /// If bigger than 20 then do not move dirs
    int cannot_move_dirs_{};

    /// Example: "c:\t1\*"
    std::wstring include_mask_;

    DiskStruct disk_ = {
            .volume_handle_ = nullptr,
            .mount_point_ = {},
            .mount_point_slash_ = {},
            .volume_name_ = {},
            .volume_name_slash_ = {},
            .type_ = DiskType::UnknownType,
    };

    /// Percentage of total disk size 0..100
    double free_space_;

    /// Tree in memory with information about all the files.
    FileNode *item_tree_{};
    int balance_count_{};

    /// Array with exclude masks
    Wstrings excludes_{};

    /// use the built-in SpaceHogs
    bool use_default_space_hogs_{};

    /// Array with SpaceHog masks
    std::vector<std::wstring> space_hogs_{};

    /// Begin (LCN) of the zones
    lcn64_t zones_[4] = {};

    /// List of clusters reserved for the MFT
    lcn_extent_t mft_excludes_[3] = {};

    /*
     * Counters filled before Phase 1.
     */

    /// Busy/available clusters bitmap, read in section, updated as we go
    ClusterMap bitmap_;

    /// Number of bytes per cluster.
    cluster_count64_t bytes_per_cluster_{};

    //
    // Counters updated before/after every Phase.
    //

    /// Number of free clusters
    cluster_count64_t count_free_clusters_;
    /// Number of gaps
    uint64_t count_gaps_;
    /// Size of biggest gap, in clusters
    cluster_count64_t biggest_gap_;
    /// Number of gaps smaller than 16 clusters
    uint64_t count_gaps_less16_;
    /// Number of clusters in gaps that are smaller than 16 clusters
    uint64_t count_clusters_less16_;

    //
    // Counters updated after every Phase, but not before Phase 1 (analyze).
    //

    // Number of analysed subdirectories
    uint64_t count_directories_{};
    // Number of analysed files
    uint64_t count_all_files_{};
    // Number of fragmented files
    uint64_t count_fragmented_items_{};
    // Bytes in analysed files
    uint64_t count_all_bytes_{};
    // Bytes in fragmented files
    uint64_t count_fragmented_bytes_{};
    // Clusters in analysed files
    uint64_t count_all_clusters_{};
    // Clusters in fragmented files
    uint64_t count_fragmented_clusters_{};
    // Between end and begin of files
    double average_distance_ = 0.0;

    //
    // Counters used to calculate the percentage of work done
    //

    // Number of items to do in this Phase
    uint64_t phase_todo_{};
    // Number of items already done in this Phase
    uint64_t clusters_done_{};

    // Variables used to throttle the speed; Speed as a percentage 1..100
    uint64_t speed_{};

    Clock::time_point start_time_{};
    Clock::duration running_time_{};
    Clock::time_point last_checkpoint_{};

    void check_last_access_enabled();

private:
    /// Size of the volume, in clusters.
    cluster_count64_t total_clusters_{};
};

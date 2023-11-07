#pragma once

#include <Windows.h>
#include <cstdint>

enum class ClusterMapValue : uint8_t {
    Free,
    InUse,
};

/// Represents entire drive cluster bitmap
class ClusterMap {
private:
    /// Contains all bits of the volume, stored as bytes
    using BitmapStorageItem = ClusterMapValue;
    std::vector<BitmapStorageItem> cluster_map_;

    /// Set to true for each available (loaded) fragment of DRIVE_BITMAP_READ_SIZE bits
    std::vector<bool> availability_;

    lcn64_t max_lcn_;

public:
    static constexpr lcn64_t DRIVE_BITMAP_READ_SIZE = 1ULL << 16;
    static constexpr lcn64_t LCN_PER_BITMAP_FRAGMENT = DRIVE_BITMAP_READ_SIZE * 8;

    [[nodiscard]] lcn64_t volume_end_lcn() const { return max_lcn_; }

    void reset(lcn64_t max_lcn) {
        max_lcn_ = max_lcn;

        cluster_map_.clear();

        const auto round_up_max_lcn = (max_lcn + 7LL) & ~7LL;
        cluster_map_.resize(round_up_max_lcn);

        availability_.clear();
        availability_.resize(get_next_fragment_start(max_lcn) / LCN_PER_BITMAP_FRAGMENT);
    }

    /// Return true if the fragment of drive bitmap is loaded
    [[nodiscard]] inline auto has_fragment_for_lcn(lcn64_t lcn) -> bool {
        const auto fragment = lcn / LCN_PER_BITMAP_FRAGMENT;
        return availability_[fragment];
    }

    auto ensure_lcn_loaded(HANDLE handle, lcn64_t lcn) -> DWORD;

    /// Returns true if a cluster is in use (assumes the drive map was loaded)
    inline auto in_use(lcn64_t lcn) -> bool {
        _ASSERT(has_fragment_for_lcn(lcn));
        return cluster_map_[lcn] == ClusterMapValue::InUse;
    }

    static constexpr auto get_fragment_start(lcn64_t lcn) -> lcn64_t {
        return (lcn / LCN_PER_BITMAP_FRAGMENT) * LCN_PER_BITMAP_FRAGMENT;
    }

    static constexpr auto get_next_fragment_start(lcn64_t lcn) -> lcn64_t {
        return (lcn / LCN_PER_BITMAP_FRAGMENT + 1) * LCN_PER_BITMAP_FRAGMENT;
    }

    inline void mark(lcn64_t lcn, cluster_count64_t count, const ClusterMapValue value) {
        std::fill(std::begin(cluster_map_) + lcn, std::begin(cluster_map_) + lcn + count, value);
    }

private:
    auto load_lcn(HANDLE handle, lcn64_t lcn) -> DWORD;
};

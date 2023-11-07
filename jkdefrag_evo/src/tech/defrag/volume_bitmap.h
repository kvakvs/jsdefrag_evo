#pragma once

#include <Windows.h>
#include <cstdint>

/// Represents one section of volume bitmap read from disk, each bit represents a cluster busy or available.
class VolumeBitmapFragment {
public:
    static constexpr lcn64_t DRIVE_BITMAP_READ_SIZE = 1ULL << 16;
    DWORD bytes_returned_{};

private:
    struct BitmapData {
        uint64_t starting_lcn_;
        uint64_t bitmap_size_;
        BYTE buffer_[DRIVE_BITMAP_READ_SIZE]; // Most efficient if binary multiple
    };

    BitmapData bitmap_{};

public:
    VolumeBitmapFragment() = default;

    /// Fetch a block of cluster data. If error then return false
    DWORD read(HANDLE handle, lcn64_t start_lcn);

    [[nodiscard]] lcn64_t starting_lcn() const { return (lcn64_t) bitmap_.starting_lcn_; }

    [[nodiscard]] uint64_t bitmap_size() const { return (size_t) bitmap_.bitmap_size_; }

    [[nodiscard]] constexpr size_t buffer_size() const { return sizeof(bitmap_.buffer_); }

    /// Gives access to the utilization bitmap
    [[nodiscard]] decltype(auto) buffer(size_t index) const {
        return bitmap_.buffer_[index];
    }

    [[nodiscard]] auto buffer_bit(lcn64_t lcn) -> bool {
        const auto rel_lcn = lcn - starting_lcn();
        const auto mask = rel_lcn & 7;
        const auto index = rel_lcn / 8;
        return (bitmap_.buffer_[index] & mask) != 0;
    }
};

/// Represents entire drive cluster bitmap
class VolumeBitmap {
private:
    /// Contains all bits of the volume
    std::vector<bool> bitmap_;

    /// Set to true for each available (loaded) fragment of DRIVE_BITMAP_READ_SIZE bits
    std::vector<bool> availability_;

    lcn64_t max_lcn_;
public:
    static constexpr lcn64_t LCN_PER_BITMAP_FRAGMENT = VolumeBitmapFragment::DRIVE_BITMAP_READ_SIZE / 8;

    [[nodiscard]] lcn64_t volume_end_lcn() const {
        return max_lcn_;
    }

    void reset(lcn64_t max_lcn) {
        max_lcn_ = max_lcn;
        bitmap_.clear();
        bitmap_.resize(max_lcn);
        availability_.clear();
        availability_.resize(max_lcn / LCN_PER_BITMAP_FRAGMENT);
    }

    /// Return true if the fragment of drive bitmap is loaded
    [[nodiscard]] inline auto has_fragment_for_lcn(lcn64_t lcn) -> bool {
        const auto fragment = lcn / LCN_PER_BITMAP_FRAGMENT;
        return availability_[fragment];
    }

    auto ensure_lcn_loaded(HANDLE handle, lcn64_t lcn) -> DWORD;

    auto load_lcn(HANDLE handle, lcn64_t lcn) -> DWORD;

    /// Returns true if a cluster is in use (assumes the drive map was loaded)
    inline auto in_use(lcn64_t lcn) -> bool {
        _ASSERT(has_fragment_for_lcn(lcn));

        return bitmap_[lcn];
    }

    static constexpr auto get_fragment_start(lcn64_t lcn) -> lcn64_t {
        return (lcn / LCN_PER_BITMAP_FRAGMENT) * LCN_PER_BITMAP_FRAGMENT;
    }

    static constexpr auto get_next_fragment_start(lcn64_t lcn) -> lcn64_t {
        return (lcn / LCN_PER_BITMAP_FRAGMENT + 1) * LCN_PER_BITMAP_FRAGMENT;
    }

    inline void mark(lcn64_t lcn, cluster_count64_t count, const bool value) {
        std::fill(std::begin(bitmap_) + lcn, std::begin(bitmap_) + lcn + count, value);
    }
};

#pragma once

#include "types.h"

/// An extent is a run of contiguous clusters. For example, suppose a file consisting of 30 clusters is recorded in
/// two extents. The first extent might consist of five contiguous clusters, the other of the remaining 25 clusters.
struct lcn_extent_t {
private:
    lcn64_t begin_;
    lcn64_t end_;
public:
    constexpr lcn_extent_t(lcn64_t start, lcn64_t end) : begin_(start), end_(end) {
    }

    constexpr lcn_extent_t() : begin_{}, end_{} {
    }

    static constexpr lcn_extent_t with_length(lcn64_t begin, cluster_count64_t length) {
        return {begin, begin + length};
    }

    [[nodiscard]] constexpr auto begin() const -> lcn64_t { return begin_; }

    [[nodiscard]] constexpr auto end() const -> lcn64_t { return end_; }

    [[nodiscard]] constexpr auto length() const -> cluster_count64_t { return end_ - begin_; }

    /// Modify the set_begin value
    inline void set_begin(lcn64_t n) { begin_ = n; }

    /// Modify the set_end value
    inline void set_end(lcn64_t n) { end_ = n; }

    /// Modify the set_length
    inline void set_length(cluster_count64_t n) { end_ = begin_ + n; }

    inline void shift_begin(cluster_count64_t n) {
        begin_ += n;
    }

    inline void shift_end(cluster_count64_t n) {
        end_ += n;
    }

    inline void shift(cluster_count64_t n) {
        shift_begin(n);
        shift_end(n);
    }

    [[nodiscard]] auto contains(lcn64_t val) const -> bool {
        return val >= begin_ && val < end_;
    }
};

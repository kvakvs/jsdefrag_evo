#pragma once

#include "constants.h"

// Represents state of a disk cluster, used for coloring the diskmap. Dirty requires a redraw.
using ClusterRepr = struct {
    bool dirty: 1;
    bool empty: 1;
    bool allocated: 1;
    bool unfragmented: 1;
    bool unmovable: 1;
    bool fragmented: 1;
    bool busy: 1;
    bool mft: 1;
    bool spacehog: 1;
};

class DiskColorMap {
public:
    DiskColorMap(uint64_t num_clusters);

    void set_size(size_t width, size_t height);

    void update_square_colors_from_diskmap(uint64_t start_square, uint64_t end_square);

    [[nodiscard]] size_t get_width() const {
        return width_;
    }

    [[nodiscard]] size_t get_height() const {
        return height_;
    }

    [[nodiscard]] size_t get_total_count() const {
        return square_count_;
    }

    [[nodiscard]] ClusterRepr &get_cell(size_t index) {
        return cells_[index];
    }

    void set_cluster_colors(uint64_t start, uint64_t end, DrawColor color);

    [[nodiscard]] uint64_t get_cluster_count() const {
        return cluster_count_;
    }

    void set_cluster_count(uint64_t count);

private:
    [[nodiscard]] uint64_t get_square_from_cluster_index(uint64_t cluster_index) const {
        return cluster_index * square_count_ / cluster_count_;
    }

    [[nodiscard]] uint64_t get_cluster_from_square_index(uint64_t square_index) const {
        return square_index * cluster_count_ / square_count_;
    }

    // Number of disk clusters
    uint64_t cluster_count_;

    // How many squares the disk area has from left to right
    size_t width_{};

    // How many squares the disk area has from top to down
    size_t height_{};

    // Total number of squares in the disk area (cached from width * height)
    size_t square_count_;

    // (cached from cluster_count_, width_ and height_)
    double downscale_ratio_;

    // Color of each disk cluster. This can be big on large drives
    std::unique_ptr<DrawColor[]> cluster_color_;

    // Color of each square in disk area and status whether it is "dirty"
    std::vector<ClusterRepr> cells_;
};

#pragma once

#include "constants.h"
#include "diskmap_cell.h"
#include "diskmap_color.h"
#include "types.h"

using SquareUnit = struct {
};

// To be used instead of raw size_t for cluster number
using Squares = Numeral<uint64_t, SquareUnit>;

using RowUnit = struct {
};

// To be used instead of raw size_t for row number
using Rows = Numeral<size_t, RowUnit>;

enum class InvalidatedRowValue : uint8_t {
    Valid,
    SomeInvalidSquares,
    EntireRowInvalid,
};

class DiskMap {
public:
    DiskMap(Clusters64 num_clusters);

    void set_size(Squares width, Squares height);

    void update_square_colors_from_diskmap(Squares start_square, Squares end_square);

    [[nodiscard]] Squares get_width() const {
        return width_;
    }

    [[nodiscard]] Squares get_height() const {
        return height_;
    }

    [[nodiscard]] Squares get_total_count() const {
        return square_count_;
    }

    [[nodiscard]] DiskMapCell &get_cell(Squares index) {
        return squares_[index.value()];
    }

    void set_cluster_colors(Clusters64 start, Clusters64 end, DrawColor color);

    [[nodiscard]] Clusters64 get_cluster_count() const {
        return cluster_count_;
    }

    void set_cluster_count(Clusters64 count);

    void invalidate_clusters(Clusters64 start, Clusters64 end);

private:
    [[nodiscard]] Squares get_square_from_cluster_index(Clusters64 cluster_index) const {
        // 'clusters * 'squares / 'clusters = 'squares
        return Squares(cluster_index.value() * square_count_.value() / cluster_count_.value());
    }

    [[nodiscard]] Clusters64 get_cluster_from_square_index(Squares square_index) const {
        // 'squares * 'clusters / 'squares = 'clusters
        return Clusters64(square_index.value() * cluster_count_.value() / square_count_.value());
    }

    [[nodiscard]] Clusters64 get_row_step_in_clusters() const {
        return get_cluster_from_square_index(width_);
    }

    [[nodiscard]] Rows get_row_from_cluster_index(Clusters64 cluster_index) const {
        // 'clusters / 'clusters = 1
        return Rows(cluster_index.value() / get_row_step_in_clusters().value());
    }

    // Number of disk clusters
    Clusters64 cluster_count_;

    // How many squares the disk area has from left to right
    Squares width_{};

    // How many squares the disk area has from top to down
    Squares height_{};

    // Total number of squares in the disk area (cached from width * height)
    Squares square_count_{};

    // Color of each disk cluster. This scales with drive size, and can be big on large drives
    std::unique_ptr<DrawColor[]> cluster_color_;

    // Color of each square in disk area and status whether it is "dirty". This scales with screen size.
    std::vector<DiskMapCell> squares_;

    // Dirty per row map
    std::vector<InvalidatedRowValue> dirty_squares_row_;
};

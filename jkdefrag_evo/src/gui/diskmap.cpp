#include "precompiled_header.h"
#include "diskmap.h"


DiskMap::DiskMap(Clusters64 num_clusters)
        : width_(1), height_(1), square_count_(1), squares_() {
    set_cluster_count(num_clusters);
}

void DiskMap::set_size(Squares width, Squares height) {
    width_ = width;
    height_ = height;
    // 'squares * 'squares = 'squares^2 (area of squares field)
    square_count_ = width_ * height_;

    squares_.resize(square_count_.as<size_t>());
    dirty_squares_row_.resize(height_.as<size_t>());

    auto dirty_value = DiskMapCell{.dirty=true};

    std::fill(squares_.begin(), squares_.end(), dirty_value);
}

// Fill a sequence of squares with their current state bitflags, merged.
// Because the cluster map is much smaller than the physical cluster array, we have to downscale by merging values and
// combine the bits using OR operation (writing 1 into a lookup array).
void DiskMap::update_square_colors_from_diskmap(Squares start_square, Squares end_square) {
    uint8_t color_merge_table[(size_t) DrawColor::MaxValue];

    _ASSERT(end_square <= square_count_);
    _ASSERT(squares_.size() == square_count_.value());

    for (auto square_index = start_square; square_index < end_square; square_index++) {
        // 'clusters / 'clusters = 1
        Clusters64 limit = std::min<Clusters64>(
                cluster_count_, get_cluster_from_square_index(square_index + Squares(1)));

        std::fill(color_merge_table, color_merge_table + (size_t) DrawColor::MaxValue, 0);

        // Instead of having a case switch in here, use a lookup table to merge the colors
        for (auto cluster_index = get_cluster_from_square_index({square_index});
             cluster_index < limit; cluster_index++) {
            color_merge_table[(size_t) cluster_color_[cluster_index.value()]] = 1;
        }

        squares_[square_index.as<size_t>()] = {
                .dirty = true,
                .empty = (bool) color_merge_table[(size_t) DrawColor::Empty],
                .allocated = (bool) color_merge_table[(size_t) DrawColor::Allocated],
                .unfragmented = (bool) color_merge_table[(size_t) DrawColor::Unfragmented],
                .unmovable = (bool) color_merge_table[(size_t) DrawColor::Unmovable],
                .fragmented = (bool) color_merge_table[(size_t) DrawColor::Fragmented],
                .busy = (bool) color_merge_table[(size_t) DrawColor::Busy],
                .mft = (bool) color_merge_table[(size_t) DrawColor::Mft],
                .spacehog = (bool) color_merge_table[(size_t) DrawColor::SpaceHog],
        };
    }
}

void DiskMap::set_cluster_count(Clusters64 count) {
    cluster_count_ = count;
    cluster_color_ = std::make_unique<DrawColor[]>(cluster_count_.value());

    // std::fill(cluster_info_.get(), cluster_info_.get() + num_clusters_, DrawColor::Empty);
}

void DiskMap::set_cluster_colors(Clusters64 start, Clusters64 end, DrawColor color) {
    std::fill(cluster_color_.get() + start.as<isize_t>(),
              cluster_color_.get() + end.as<isize_t>(),
              color);

    invalidate_clusters(start, end);

//    const auto start_square = get_square_from_cluster_index(start);
//    const auto end_square = get_square_from_cluster_index(end);

    // update_square_colors_from_diskmap(start_square, end_square);
}

void DiskMap::invalidate_clusters(Clusters64 start, Clusters64 end) {
    const auto start_square = get_square_from_cluster_index(start);
    const auto end_square = get_square_from_cluster_index(end);

    std::fill(squares_.begin() + start_square.as<isize_t>(),
              squares_.begin() + end_square.as<isize_t>(),
              DiskMapCell{.dirty=true});

    // Update each affected row
    const auto row_step = get_row_step_in_clusters();

    for (auto cluster_index = start; cluster_index < end; cluster_index += row_step) {
        auto row_i = get_row_from_cluster_index(cluster_index);
        // TODO: Use EntireRowInvalid if the span is very long
        dirty_squares_row_[row_i.as<size_t>()] = InvalidatedRowValue::SomeInvalidSquares;
    }
}

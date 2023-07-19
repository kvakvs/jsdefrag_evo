#include "precompiled_header.h"
#include "colormap.h"


DiskColorMap::DiskColorMap(uint64_t num_clusters)
        : width_(1), height_(1), square_count_(1), cells_() {
    set_cluster_count(num_clusters);
}

void DiskColorMap::set_size(size_t width, size_t height) {
    width_ = width;
    height_ = height;
    square_count_ = width_ * height_;
    downscale_ratio_ = (double) cluster_count_ / (double) square_count_;

    cells_.resize(square_count_);

    auto dirty_value = ClusterRepr{.dirty=true};

    std::fill(cells_.begin(), cells_.end(), dirty_value);
}

// Fill a sequence of squares with their current state bitflags, merged.
// Because the cluster map is much smaller than the physical cluster array, we have to downscale by merging values and
// combine the bits using OR operation (writing 1 into a lookup array).
void DiskColorMap::update_square_colors_from_diskmap(uint64_t start_square, uint64_t end_square) {
    uint8_t color_merge_table[(size_t) DrawColor::MaxValue];

    _ASSERT(end_square <= square_count_);
    _ASSERT(cells_.size() == square_count_);

    for (auto square_index = start_square; square_index < end_square; square_index++) {
        auto limit = std::min<uint64_t>(cluster_count_, get_cluster_from_square_index(square_index + 1));

        std::fill(color_merge_table, color_merge_table + (size_t) DrawColor::MaxValue, 0);

        // Instead of having a case switch in here, use a lookup table to merge the colors
        for (uint64_t cluster_index = get_cluster_from_square_index(square_index);
             cluster_index < limit;
             cluster_index++) {
            color_merge_table[(size_t) cluster_color_[cluster_index]] = 1;
        }

        cells_[square_index] = {
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

void DiskColorMap::set_cluster_count(uint64_t count) {
    cluster_count_ = count;
    downscale_ratio_ = (double) cluster_count_ / (double) square_count_;

    cluster_color_ = std::make_unique<DrawColor[]>(cluster_count_);

    // std::fill(cluster_info_.get(), cluster_info_.get() + num_clusters_, DrawColor::Empty);
}

void DiskColorMap::set_cluster_colors(uint64_t start, uint64_t end, DrawColor color) {
    std::fill(cluster_color_.get() + start, cluster_color_.get() + end, color);

    const auto cluster_start_square_num = get_square_from_cluster_index(start);
    const auto cluster_end_square_num = get_square_from_cluster_index(end);

    update_square_colors_from_diskmap(cluster_start_square_num, cluster_end_square_num);
}

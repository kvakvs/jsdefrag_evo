#include "precompiled_header.h"
#include "colormap.h"


DiskColorMap::DiskColorMap()
        : width_(0), height_(0), total_count_(0), cells_() {
}

void DiskColorMap::set_size(size_t width, size_t height) {
    width_ = width;
    height_ = height;

    total_count_ = width_ * height_;
    cells_.resize(total_count_ + 1);

    for (int ii = 0; ii < total_count_; ii++) {
        cells_[ii].color_ = {};
        cells_[ii].dirty_ = true;
    }
}

#pragma once

struct ClusterSquareStruct {
    bool dirty_;

    using ColorBits = struct {
        bool empty: 1;
        bool allocated: 1;
        bool unfragmented: 1;
        bool unmovable: 1;
        bool fragmented: 1;
        bool busy: 1;
        bool mft: 1;
        bool spacehog: 1;
    };

    ColorBits color_;
};

class DiskColorMap {
public:
    DiskColorMap();

    void set_size(size_t width, size_t height);

    [[nodiscard]] size_t get_width() const {
        return width_;
    }

    [[nodiscard]] size_t get_height() const {
        return height_;
    }

    [[nodiscard]] size_t get_total_count() const {
        return total_count_;
    }

    [[nodiscard]] ClusterSquareStruct &get_cell(size_t index) {
        return cells_[index];
    }

private:
    // Number of squares in horizontal direction of disk area
    size_t width_{};

    // Number of squares in horizontal direction of disk area
    size_t height_{};

    // Total number of squares in disk area
    size_t total_count_;

    // Color of each square in disk area and status whether it is "dirty"
    std::vector<ClusterSquareStruct> cells_;
};

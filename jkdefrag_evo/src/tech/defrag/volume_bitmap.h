#pragma once

#include <Windows.h>
#include <cstdint>
//#include "constants.h"

static constexpr size_t DRIVE_BITMAP_READ_SIZE = 1ULL << 16;

class VolumeBitmap {
    struct BitmapData {
        uint64_t starting_lcn_;
        uint64_t bitmap_size_;
        BYTE buffer_[DRIVE_BITMAP_READ_SIZE]; // Most efficient if binary multiple
    };

    BitmapData bitmap_data{};
    DWORD bytes_returned;

public:
    VolumeBitmap() {
    }

    DWORD read(HANDLE handle, Lcn start_lcn);
    Lcn starting_lcn() const { return (Lcn)bitmap_data.starting_lcn_; }
    uint64_t bitmap_size() const { return (size_t)bitmap_data.bitmap_size_; }
    constexpr size_t buffer_size() const { return sizeof(bitmap_data.buffer_); }
    decltype(auto) buffer(size_t index) const {
        return bitmap_data.buffer_[index];
    }
};

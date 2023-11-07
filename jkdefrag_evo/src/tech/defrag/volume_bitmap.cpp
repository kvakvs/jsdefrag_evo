#include "precompiled_header.h"
#include "volume_bitmap.h"

DWORD VolumeBitmapFragment::read(HANDLE handle, lcn64_t start_lcn) {
    STARTING_LCN_INPUT_BUFFER bitmap_param = {.StartingLcn = { .QuadPart = start_lcn }};

    DWORD error_code = DeviceIoControl(
            handle, FSCTL_GET_VOLUME_BITMAP,
            &bitmap_param, sizeof bitmap_param, &bitmap_,
            sizeof bitmap_, &bytes_returned_, nullptr
    );

    if (error_code != 0) {
        error_code = NO_ERROR;
    } else {
        error_code = GetLastError();
    }

    return error_code;
}

/// Using the open volume handle, read the drive bitmap fragment for lcn and store in the overall bitmap
auto VolumeBitmap::load_lcn(HANDLE handle, lcn64_t lcn) -> DWORD {
    VolumeBitmapFragment fragment;
    const auto fragment_id = lcn / LCN_PER_BITMAP_FRAGMENT;

    const auto result_code = fragment.read(handle, get_fragment_start(lcn));
    if (result_code != NO_ERROR && result_code != ERROR_MORE_DATA) {
        return result_code;
    }

    // Copy the data into our global bitmap
    auto bitmap_size = fragment.bytes_returned_;

    for (size_t i = 0; i < bitmap_size; i++) {
        uint8_t mask = 1;
        uint8_t value = fragment.buffer(i);
        auto write_index = lcn + i * 8;

        for (auto bit = 0; bit < 8; bit++) {
            bitmap_[write_index + bit] = (value & mask) != 0;
            mask <<= 1;
        }
    }

    availability_[fragment_id] = true;
    return NO_ERROR;
}

auto VolumeBitmap::ensure_lcn_loaded(HANDLE handle, lcn64_t lcn) -> DWORD {
    _ASSERT(lcn >= 0 && lcn < max_lcn_);

    if (!has_fragment_for_lcn(lcn)) {
        return load_lcn(handle, lcn);
    }
    return NO_ERROR;
}

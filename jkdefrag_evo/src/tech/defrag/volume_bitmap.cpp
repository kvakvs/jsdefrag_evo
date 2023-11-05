#include "precompiled_header.h"
#include "volume_bitmap.h"

DWORD VolumeBitmap::read(HANDLE handle, lcn64_t start_lcn) {
    STARTING_LCN_INPUT_BUFFER bitmap_param;
    bitmap_param.StartingLcn.QuadPart = start_lcn;

    DWORD error_code = DeviceIoControl(
            handle, FSCTL_GET_VOLUME_BITMAP,
            &bitmap_param, sizeof bitmap_param, &bitmap_data,
            sizeof bitmap_data, &bytes_returned, nullptr
    );

    if (error_code != 0) {
        error_code = NO_ERROR;
    } else {
        error_code = GetLastError();
    }

    return error_code;
}

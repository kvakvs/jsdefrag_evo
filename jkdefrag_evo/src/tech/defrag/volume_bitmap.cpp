#include "volume_bitmap.h"
#include "precompiled_header.h"
#undef min
#include <algorithm>

/// The FSCTL_GET_VOLUME_BITMAP control code retrieves a data structure that describes the allocation state of
/// each cluster in the file system from the requested starting LCN to the last cluster on the volume. The bitmap
/// uses one bit to represent each cluster:
/// <ul>
/// <li>The value 1 indicates that the cluster is allocated (in use).</li>
/// <li>The value 0 indicates that the cluster is not allocated (free).</li>
/// </ul>
class ClusterMapFragment {
private:
    struct BitmapData {
        decltype(_LARGE_INTEGER::QuadPart) starting_lcn_;
        /// Count starting from the LCN requested
        decltype(_LARGE_INTEGER::QuadPart) cluster_count_from_lcn_;
        BYTE buffer_[ClusterMap::DRIVE_BITMAP_READ_SIZE]; // Most efficient if binary multiple
    };

    BitmapData bitmap_{};
    DWORD bytes_returned_{};

public:
    ClusterMapFragment() = default;

    /// Fetch a block of cluster data. If error then return false
    DWORD read(HANDLE handle, lcn64_t start_lcn) {
        STARTING_LCN_INPUT_BUFFER bitmap_param = {.StartingLcn = {.QuadPart = start_lcn}};

        DWORD error_code =
                DeviceIoControl(handle, FSCTL_GET_VOLUME_BITMAP, &bitmap_param, sizeof bitmap_param,
                                &bitmap_, sizeof bitmap_, &bytes_returned_, nullptr);

        if (error_code != 0) {
            error_code = NO_ERROR;
        } else {
            error_code = GetLastError();
        }

        return error_code;
    }

    [[nodiscard]] lcn64_t starting_lcn() const { return (lcn64_t) bitmap_.starting_lcn_; }

    [[nodiscard]] uint64_t cluster_count_from_lcn() const { return (size_t) bitmap_.cluster_count_from_lcn_; }

    [[nodiscard]] DWORD bytes_returned() const { return bytes_returned_; }

    [[nodiscard]] constexpr size_t buffer_size() const { return sizeof(bitmap_.buffer_); }

    /// Gives access to the utilization bitmap
    [[nodiscard]] decltype(auto) buffer(size_t index) const { return bitmap_.buffer_[index]; }

    [[nodiscard]] auto buffer_bit(lcn64_t lcn) -> bool {
        const auto rel_lcn = lcn - starting_lcn();
        const auto mask = rel_lcn & 7;
        const auto index = rel_lcn / 8;
        return (bitmap_.buffer_[index] & mask) != 0;
    }
};

/// Using the open volume handle, read the drive bitmap fragment for lcn and store in the overall bitmap
auto ClusterMap::load_lcn(HANDLE handle, lcn64_t lcn) -> DWORD {
    ClusterMapFragment fragment;
    const auto fragment_id = lcn / LCN_PER_BITMAP_FRAGMENT;

    lcn64_t fragment_start_lcn = get_fragment_start(lcn);
    const auto result_code = fragment.read(handle, fragment_start_lcn);
    if (result_code != NO_ERROR && result_code != ERROR_MORE_DATA) { return result_code; }
    _ASSERTE(lcn == fragment.starting_lcn());

    // Copy the data into our global bitmap
    auto input_max_index = std::min<size_t>(fragment.buffer_size(), fragment.cluster_count_from_lcn() / 8);
    auto write_index = fragment_start_lcn;

    // FIXME: input_max_index is incorrect or somethign is wrong with loading
    for (size_t i = 0; i < input_max_index; i++) {
        uint8_t mask = 1;
        uint8_t value = fragment.buffer(i);

        for (auto bit = 0; bit < 8; bit++) {
            cluster_map_[write_index] =
                    (value & mask) == 0 ? ClusterMapValue::Free : ClusterMapValue::InUse;
            mask <<= 1;
            write_index++;
        }
    }

    availability_[fragment_id] = true;
    return NO_ERROR;
}

auto ClusterMap::ensure_lcn_loaded(HANDLE handle, lcn64_t lcn) -> DWORD {
    _ASSERT(lcn >= 0 && lcn < max_lcn_);

    if (!has_fragment_for_lcn(lcn)) { return load_lcn(handle, lcn); }
    return NO_ERROR;
}

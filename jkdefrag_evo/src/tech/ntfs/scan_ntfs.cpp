#include "precompiled_header.h"
#include "defrag_data_struct.h"

#include <memory>

ScanNTFS::ScanNTFS() = default;

ScanNTFS::~ScanNTFS() = default;

ScanNTFS *ScanNTFS::get_instance() {
    if (instance_ == nullptr) {
        instance_ = std::make_unique<ScanNTFS>();
    }

    return instance_.get();
}

/**
 * \brief Read the data that is specified in a RunData list from disk into memory, skipping the first Offset bytes.
 * \param offset Bytes to skip from begin of data
 * \return Return a malloc'ed buffer with the data, or nullptr if error. Note: The caller owns the returned buffer.
 */
BYTE *ScanNTFS::read_non_resident_data(const DefragDataStruct *data, const NtfsDiskInfoStruct *disk_info,
                                       const BYTE *run_data, const uint32_t run_data_length,
                                       const uint64_t offset, uint64_t wanted_length) {
    union UlongBytes {
        struct {
            BYTE bytes_[8];
        };

        int64_t value;
    };

    UlongBytes run_offset{};
    UlongBytes run_length{};
    OVERLAPPED g_overlapped;
    DWORD bytes_read;
    int i;
    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                    std::format(L"    Reading " NUM_FMT " bytes from offset " NUM_FMT,
                                wanted_length, offset));

    // Sanity check
    if (run_data == nullptr || run_data_length == 0) return nullptr;
    if (wanted_length >= INT_MAX) {
        gui->show_debug(DebugLevel::Progress, nullptr,
                        std::format(L"    Cannot read " NUM_FMT" bytes, maximum is " NUM_FMT,
                                    wanted_length, INT_MAX));
        return nullptr;
    }

    // We have to round up the WantedLength to the nearest sector. For some reason or other Microsoft has decided
    // that raw reading from disk can only be done by whole sector, even though ReadFile() accepts it's parameters in bytes
    if (wanted_length % disk_info->bytes_per_sector_ > 0) {
        wanted_length = wanted_length + disk_info->bytes_per_sector_ - wanted_length % disk_info->bytes_per_sector_;
    }

    // Allocate the data buffer. Clear the buffer with zero's in case of sparse content
    auto buffer = std::make_unique<BYTE[]>(wanted_length);

    // Walk through the RunData and read the requested data from disk
    uint32_t index = 0;
    int64_t lcn = 0;
    int64_t vcn = 0;

    while (run_data[index] != 0) {
        // Decode the RunData and calculate the next Lcn.
        const int run_length_size = run_data[index] & 0x0F;
        const int run_offset_size = (run_data[index] & 0xF0) >> 4;

        index++;

        if (index >= run_data_length) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            L"Error: datarun is longer than buffer, the MFT may be corrupt.");

            return nullptr;
        }

        run_length.value = 0;

        for (i = 0; i < run_length_size; i++) {
            run_length.bytes_[i] = run_data[index];

            index++;

            if (index >= run_data_length) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                L"Error: datarun is longer than buffer, the MFT may be corrupt.");

                return nullptr;
            }
        }

        run_offset.value = 0;

        for (i = 0; i < run_offset_size; i++) {
            run_offset.bytes_[i] = run_data[index];

            index++;

            if (index >= run_data_length) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                L"Error: datarun is longer than buffer, the MFT may be corrupt.");

                return nullptr;
            }
        }

        if (run_offset.bytes_[i - 1] >= 0x80) while (i < 8) run_offset.bytes_[i++] = 0xFF;

        lcn = lcn + run_offset.value;
        vcn = vcn + run_length.value;

        // Ignore virtual extents
        if (run_offset.value == 0) continue;

        // I don't think the RunLength can ever be zero, but just in case
        if (run_length.value == 0) continue;

        /* Determine how many and which bytes we want to read. If we don't need
        any bytes from this extent then loop. */

        uint64_t extent_vcn = (vcn - run_length.value) * disk_info->bytes_per_sector_ * disk_info->sectors_per_cluster_;
        uint64_t extent_lcn = lcn * disk_info->bytes_per_sector_ * disk_info->sectors_per_cluster_;
        uint64_t extent_length = run_length.value * disk_info->bytes_per_sector_ * disk_info->sectors_per_cluster_;

        if (offset >= extent_vcn + extent_length) continue;

        if (offset > extent_vcn) {
            extent_lcn = extent_lcn + offset - extent_vcn;
            extent_length = extent_length - (offset - extent_vcn);
            extent_vcn = offset;
        }

        if (offset + wanted_length <= extent_vcn) continue;

        if (offset + wanted_length < extent_vcn + extent_length) {
            extent_length = offset + wanted_length - extent_vcn;
        }

        if (extent_length == 0) continue;

        // Read the data from the disk. If error then return FALSE
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"    Reading " NUM_FMT " bytes from LCN=" NUM_FMT " into offset=" NUM_FMT,
                                    extent_length,
                                    extent_lcn / (disk_info->bytes_per_sector_ * disk_info->sectors_per_cluster_),
                                    extent_vcn - offset));

        ULARGE_INTEGER trans;
        trans.QuadPart = extent_lcn;

        g_overlapped.Offset = trans.LowPart;
        g_overlapped.OffsetHigh = trans.HighPart;
        g_overlapped.hEvent = nullptr;

        if (const errno_t result = ReadFile(data->disk_.volume_handle_, &buffer[extent_vcn - offset],
                                            (uint32_t) extent_length, &bytes_read, &g_overlapped); result == 0) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"Error while reading disk: {}", DefragLib::system_error_str(GetLastError())));
            return nullptr;
        }
    }

    return buffer.release();
}

// Read the RunData list and translate into a list of fragments
bool ScanNTFS::translate_rundata_to_fragmentlist(
        const DefragDataStruct *data, InodeDataStruct *inode_data, const wchar_t *stream_name,
        ATTRIBUTE_TYPE stream_type, const BYTE *run_data, const uint32_t run_data_length, const uint64_t starting_vcn,
        const uint64_t bytes) {
    StreamStruct *stream;

    union UlongBytes {
        struct {
            BYTE bytes_[8];
        };

        int64_t value;
    };
    UlongBytes run_offset{};
    UlongBytes run_length{};

    int i;

    DefragGui *gui = DefragGui::get_instance();

    // Sanity check
    if (data == nullptr || inode_data == nullptr) return FALSE;

    // Find the stream in the list of streams. If not found then create a new stream
    for (stream = inode_data->streams_; stream != nullptr; stream = stream->next_) {
        if (stream->stream_type_ != stream_type) continue;
        if (stream_name == nullptr) break;
        if (stream_name != nullptr && stream->stream_name_ == stream_name) {
            break;
        }
    }

    if (stream == nullptr) {
        if (stream_name != nullptr) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"    Creating new stream: '{}:{}'", stream_name,
                                        stream_type_names(stream_type)));
        } else {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"    Creating new stream: ':{}'", stream_type_names(stream_type)));
        }

        stream = new StreamStruct();

        stream->next_ = inode_data->streams_;

        inode_data->streams_ = stream;

        stream->stream_name_.clear();

        if (stream_name != nullptr && wcslen(stream_name) > 0) {
            stream->stream_name_ = stream_name;
        }

        stream->stream_type_ = stream_type;
        stream->fragments_ = nullptr;
        stream->clusters_ = 0;
        stream->bytes_ = bytes;
    } else {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"    Appending rundata to existing stream: '{}:{}",
                                    stream_name ? stream_name : L"", stream_type_names(stream_type)));

        if (stream->bytes_ == 0) stream->bytes_ = bytes;
    }

    // If the stream already has a list of fragments then find the last fragment
    auto last_fragment = stream->fragments_;

    if (last_fragment != nullptr) {
        while (last_fragment->next_ != nullptr) last_fragment = last_fragment->next_;

        if (starting_vcn != last_fragment->next_vcn_) {
            gui->show_debug(
                    DebugLevel::Progress, nullptr,
                    std::format(
                            L"Error: Inode " NUM_FMT " already has a list of fragments. LastVcn=" NUM_FMT ", StartingVCN=" NUM_FMT,
                            inode_data->inode_, last_fragment->next_vcn_, starting_vcn));

            return false;
        }
    }

    // Walk through the RunData and add the extents
    uint32_t index = 0;
    int64_t lcn = 0;
    auto vcn = starting_vcn;

    if (run_data != nullptr)
        while (run_data[index] != 0) {
            // Decode the RunData and calculate the next Lcn
            const int run_length_size = run_data[index] & 0x0F;
            const int run_offset_size = (run_data[index] & 0xF0) >> 4;

            index++;

            if (index >= run_data_length) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                std::format(L"Error: datarun is longer than buffer, the MFT may be corrupt. inode={}",
                                            inode_data->inode_));
                return false;
            }

            run_length.value = 0;

            for (i = 0; i < run_length_size; i++) {
                run_length.bytes_[i] = run_data[index];

                index++;

                if (index >= run_data_length) {
                    gui->show_debug(DebugLevel::Progress, nullptr,
                                    std::format(
                                            L"Error: datarun is longer than buffer, the MFT may be corrupt. inode={}",
                                            inode_data->inode_));

                    return false;
                }
            }

            run_offset.value = 0;

            for (i = 0; i < run_offset_size; i++) {
                run_offset.bytes_[i] = run_data[index];

                index++;

                if (index >= run_data_length) {
                    gui->show_debug(DebugLevel::Progress, nullptr,
                                    std::format(
                                            L"Error: datarun is longer than buffer, the MFT may be corrupt. inode={}",
                                            inode_data->inode_));

                    return false;
                }
            }

            if (run_offset.bytes_[i - 1] >= 0x80) while (i < 8) run_offset.bytes_[i++] = 0xFF;

            lcn = lcn + run_offset.value;
            vcn = vcn + run_length.value;

            // Show debug message
            if (run_offset.value != 0) {
                gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                                std::format(L"    Extent: Lcn=" NUM_FMT ", Vcn=" NUM_FMT ", NextVcn=" NUM_FMT, lcn,
                                            vcn - run_length.value, vcn));
            } else {
                gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                                std::format(L"    Extent (virtual): Vcn=" NUM_FMT ", NextVcn=" NUM_FMT,
                                            vcn - run_length.value, vcn));
            }

            /* Add the size of the fragment to the total number of clusters.
            There are two kinds of fragments: real and virtual. The latter do not
            occupy clusters on disk, but are information used by compressed
            and sparse files. */

            if (run_offset.value != 0) {
                stream->clusters_ = stream->clusters_ + run_length.value;
            }

            // Add the extent to the Fragments
            const auto new_fragment = new FragmentListStruct();

            new_fragment->lcn_ = lcn;

            if (run_offset.value == 0) new_fragment->lcn_ = VIRTUALFRAGMENT;

            new_fragment->next_vcn_ = vcn;
            new_fragment->next_ = nullptr;

            if (stream->fragments_ == nullptr) {
                stream->fragments_ = new_fragment;
            } else {
                if (last_fragment != nullptr) last_fragment->next_ = new_fragment;
            }

            last_fragment = new_fragment;
        }

    return true;
}


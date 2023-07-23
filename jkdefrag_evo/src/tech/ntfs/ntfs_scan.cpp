/*
 JkDefrag  --  Defragment and optimize all harddisks.

 This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 Public License as published by the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even
 the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 For the full text of the license see the "License gpl.txt" file.

 Jeroen C. Kessels, Internet Engineer
 http://www.kessels.com/
 */

#include "precompiled_header.h"
#include "defrag_state.h"

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
BYTE *ScanNTFS::read_non_resident_data(const DefragState &data, const NtfsDiskInfoStruct *disk_info,
                                       const MemSlice &run_data, const Bytes64 offset, Bytes64 wanted_length) {
    union UlongBytes {
        struct {
            BYTE bytes_[8];
        };
        Clusters64 value;
    };

    UlongBytes run_offset{};
    UlongBytes run_length{};
    OVERLAPPED g_overlapped;
    DWORD bytes_read;
    int i;
    DefragGui *gui = DefragGui::get_instance();

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
            L"    Reading " NUM_FMT " bytes from offset " NUM_FMT,
            wanted_length, offset
    ));

    // Sanity check
    if (!run_data) return nullptr;
    if (wanted_length >= Bytes64(INT_MAX)) {
        gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                L"    Cannot read " NUM_FMT" bytes, maximum is " NUM_FMT " (0x{:x})",
                wanted_length, INT_MAX, INT_MAX
        ));
        return nullptr;
    }

    // We have to round up the WantedLength to the nearest sector. For some reason or other Microsoft has decided
    // that raw reading from disk can only be done by whole sector, even though ReadFile() accepts it's parameters in bytes
    auto bytes_in_sector = disk_info->bytes_per_sector_ * Sectors64(1);

    if (wanted_length % bytes_in_sector) {
        wanted_length += bytes_in_sector - wanted_length % bytes_in_sector;
    }

    // Allocate the data buffer. Clear the buffer with zero's in case of sparse content
    auto buffer = std::make_unique<BYTE[]>(wanted_length.as<size_t>());

    // Walk through the RunData and read the requested data from disk
    Bytes64 index = {};
    Clusters64 lcn = {};
    Clusters64 vcn = {};

    while (true) {
        auto next_byte = run_data.read<uint8_t>(index);
        if (!next_byte) break;

        // Decode the RunData and calculate the next Lcn.
        const int run_length_size = next_byte & 0x0F;
        const int run_offset_size = (next_byte & 0xF0) >> 4;

        index++;

        if (index >= run_data.length()) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            L"Error: datarun is longer than buffer, the MFT may be corrupt.");
            return nullptr;
        }

        run_length.value = {};

        for (i = 0; i < run_length_size; i++) {
            run_length.bytes_[i] = run_data.read<uint8_t>(index);

            index++;

            if (index >= run_data.length()) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                L"Error: datarun is longer than buffer, the MFT may be corrupt.");
                return nullptr;
            }
        }

        run_offset.value = {};

        for (i = 0; i < run_offset_size; i++) {
            run_offset.bytes_[i] = run_data.read<uint8_t>(index);

            index++;

            if (index >= run_data.length()) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                L"Error: datarun is longer than buffer, the MFT may be corrupt.");
                return nullptr;
            }
        }

        if (run_offset.bytes_[i - 1] >= 0x80) while (i < 8) run_offset.bytes_[i++] = 0xFF;

        lcn = lcn + run_offset.value;
        vcn = vcn + run_length.value;

        // Ignore virtual extents
        if (run_offset.value.is_zero()) continue;

        // I don't think the RunLength can ever be zero, but just in case
        if (run_length.value.is_zero()) continue;

        // Determine how many and which bytes we want to read. If we don't need any bytes from this extent then loop.

        auto bytes_in_cluster = disk_info->bytes_per_sector_ * disk_info->sectors_per_cluster_;

        Bytes64 extent_vcn = (vcn - run_length.value) * bytes_in_cluster;
        Bytes64 extent_lcn = lcn * bytes_in_cluster;
        Bytes64 extent_length = run_length.value * bytes_in_cluster;

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

        if (extent_length.is_zero()) continue;

        // Read the data from the disk. If error then return FALSE
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
                L"    Reading " NUM_FMT " bytes from LCN=" NUM_FMT " into offset=" NUM_FMT,
                extent_length,
                // Unit: clusters / ((bytes / sector) * (sectors / cluster)) = clusters / (bytes / cluster) = bytes
                extent_lcn / (disk_info->bytes_per_sector_ * disk_info->sectors_per_cluster_),
                extent_vcn - offset
        ));

        ULARGE_INTEGER trans;
        trans.QuadPart = extent_lcn.as<ULONGLONG>();

        g_overlapped.Offset = trans.LowPart;
        g_overlapped.OffsetHigh = trans.HighPart;
        g_overlapped.hEvent = nullptr;

        const errno_t result = ReadFile(data.disk_.volume_handle_, buffer.get() + (extent_vcn - offset).as<size_t>(),
                                        extent_length.as<DWORD>(), &bytes_read, &g_overlapped);
        if (result == 0) {
            gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                    L"Error while reading disk: {}", Str::system_error(GetLastError())));
            return nullptr;
        }
    }

    return buffer.release();
}

// Read the RunData list and translate into a list of fragments
bool ScanNTFS::translate_rundata_to_fragmentlist(
        const DefragState &data, InodeDataStruct *inode_data, const wchar_t *stream_name,
        ATTRIBUTE_TYPE stream_type, MemSlice &run_data, Clusters64 starting_vcn, Bytes64 bytes) {
    // A cluster64 value with per-byte access
    union Clusters64Special {
        struct {
            BYTE bytes_[8];
        };
        Clusters64 value_;
    };
    Clusters64Special run_offset{};
    Clusters64Special run_length{};

    int i;

    DefragGui *gui = DefragGui::get_instance();

    // Sanity check
    if (inode_data == nullptr) return false;

    // Find the stream in the list of streams. If not found, then create a new stream
    auto stream_iter = inode_data->streams_.begin();

    for (; stream_iter != inode_data->streams_.end(); stream_iter++) {
        if (stream_iter->stream_type_ != stream_type) continue;

        // Break if stream_name that we are searching for, is a null, or if the stream_name matches
        if (stream_name == nullptr || stream_iter->stream_name_ == stream_name) break;
    }

    // Stream name was not found by the previous loop
    if (stream_iter == inode_data->streams_.end()) {
        if (stream_name != nullptr) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"    Creating new stream: '{}:{}'", stream_name,
                                        stream_type_names(stream_type)));
        } else {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"    Creating new stream: ':{}'", stream_type_names(stream_type)));
        }

        StreamStruct new_stream = {
                .stream_name_ = (stream_name != nullptr && wcslen(stream_name) > 0) ? stream_name : L"",
                .stream_type_ = stream_type,
                .fragments_ = {},
                .clusters_ = {},
                .bytes_ = bytes,
        };
        inode_data->streams_.push_back(new_stream);

        // Step back one from end
        stream_iter = inode_data->streams_.end();
        stream_iter--;
    } else {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"    Appending rundata to existing stream: '{}:{}",
                                    stream_name ? stream_name : L"", stream_type_names(stream_type)));

        if (stream_iter->bytes_.is_zero()) stream_iter->bytes_ = bytes;
    }

    // If the stream already has a list of fragments then find the last fragment
    _ASSERT(stream_iter != inode_data->streams_.end());
    auto last_fragment = stream_iter->fragments_.rbegin();

    if (last_fragment != stream_iter->fragments_.rend()) {
        //while (last_fragment->next_ != nullptr) last_fragment = last_fragment->next_;

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
    Bytes64 index = {};
    Clusters64 lcn = {};
    auto vcn = starting_vcn;

    if (run_data)
        while (true) {
            const auto next_byte = run_data.read<uint8_t>(index);
            if (!next_byte) break;

            // Decode the RunData and calculate the next Lcn
            const int run_length_size = next_byte & 0x0F;
            const int run_offset_size = (next_byte & 0xF0) >> 4;

            index++;

            if (index >= run_data.length()) {
                gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                        L"Error: datarun is longer than buffer, the MFT may be corrupt. inode={}",
                        inode_data->inode_));
                return false;
            }

            run_length.value_ = {};

            for (i = 0; i < run_length_size; i++) {
                run_length.bytes_[i] = run_data.read<uint8_t>(index);

                index++;

                if (index >= run_data.length()) {
                    gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                            L"Error: datarun is longer than buffer, the MFT may be corrupt. inode={}",
                            inode_data->inode_));

                    return false;
                }
            }

            run_offset.value_ = {};

            for (i = 0; i < run_offset_size; i++) {
                run_offset.bytes_[i] = run_data.read<uint8_t>(index);

                index++;

                if (index >= run_data.length()) {
                    gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                            L"Error: datarun is longer than buffer, the MFT may be corrupt. inode={}",
                            inode_data->inode_));

                    return false;
                }
            }

            if (run_offset.bytes_[i - 1] >= 0x80) while (i < 8) run_offset.bytes_[i++] = 0xFF;

            lcn += run_offset.value_;
            vcn += run_length.value_;

            // Show a debug message
            if (run_offset.value_) {
                gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
                        L"    Extent: Lcn=" NUM_FMT ", Vcn=" NUM_FMT ", NextVcn=" NUM_FMT, lcn,
                        vcn - run_length.value_, vcn));
            } else {
                gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
                        L"    Extent (virtual): Vcn=" NUM_FMT ", NextVcn=" NUM_FMT,
                        vcn - run_length.value_, vcn));
            }

            /* Add the size of the fragment to the total number of clusters.
            There are two kinds of fragments: real and virtual. The latter do not
            occupy clusters on disk, but are information used by compressed
            and sparse files. */

            if (run_offset.value_) {
                stream_iter->clusters_ += run_length.value_;
            }

            // Add the extent to the Fragments
            FileFragment new_fragment = {
                    .lcn_ = lcn,
                    .next_vcn_ = vcn,
            };

            if (run_offset.value_.is_zero()) new_fragment.set_virtual();

            stream_iter->fragments_.push_back(new_fragment);
        }

    return true;
}


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

const wchar_t *ScanNTFS::stream_type_names(const ATTRIBUTE_TYPE stream_type) {
    switch (stream_type) {
        case ATTRIBUTE_TYPE::AttributeStandardInformation:
            return L"$STANDARD_INFORMATION";
        case ATTRIBUTE_TYPE::AttributeAttributeList:
            return L"$ATTRIBUTE_LIST";
        case ATTRIBUTE_TYPE::AttributeFileName:
            return L"$FILE_NAME";
        case ATTRIBUTE_TYPE::AttributeObjectId:
            return L"$OBJECT_ID";
        case ATTRIBUTE_TYPE::AttributeSecurityDescriptor:
            return L"$SECURITY_DESCRIPTOR";
        case ATTRIBUTE_TYPE::AttributeVolumeName:
            return L"$VOLUME_NAME";
        case ATTRIBUTE_TYPE::AttributeVolumeInformation:
            return L"$VOLUME_INFORMATION";
        case ATTRIBUTE_TYPE::AttributeData:
            return L"$DATA";
        case ATTRIBUTE_TYPE::AttributeIndexRoot:
            return L"$INDEX_ROOT";
        case ATTRIBUTE_TYPE::AttributeIndexAllocation:
            return L"$INDEX_ALLOCATION";
        case ATTRIBUTE_TYPE::AttributeBitmap:
            return L"$BITMAP";
        case ATTRIBUTE_TYPE::AttributeReparsePoint:
            return L"$REPARSE_POINT";
        case ATTRIBUTE_TYPE::AttributeEAInformation:
            return L"$EA_INFORMATION";
        case ATTRIBUTE_TYPE::AttributeEA:
            return L"$EA";
        case ATTRIBUTE_TYPE::AttributePropertySet:
            return L"$PROPERTY_SET"; /* guess, not documented */
        case ATTRIBUTE_TYPE::AttributeLoggedUtilityStream:
            return L"$LOGGED_UTILITY_STREAM";
        case ATTRIBUTE_TYPE::AttributeInvalid:
            break;
        default:;
    }
    return L"ATTRIBUTE_INVALID";
}

/*

Fixup the raw MFT data that was read from disk. Return TRUE if everything is ok,
FALSE if the MFT data is corrupt (this can also happen when we have read a
record past the end of the MFT, maybe it has shrunk while we were processing).

- To protect against disk failure, the last 2 bytes of every sector in the MFT are
not stored in the sector itself, but in the "Usa" array in the header (described
by UsaOffset and UsaCount). The last 2 bytes are copied into the array and the
Update Sequence Number is written in their place.

- The Update Sequence Number is stored in the first item (item zero) of the "Usa"
array.

- The number of bytes per sector is defined in the $Boot record.

*/

bool ScanNTFS::fixup_raw_mftdata(DefragDataStruct *data, const NtfsDiskInfoStruct *disk_info, BYTE *buffer,
                                 const uint64_t buf_length) const {
    DefragGui *gui = DefragGui::get_instance();

    // Sanity check. 
    if (buffer == nullptr) return false;
    if (buf_length < sizeof(NTFS_RECORD_HEADER)) return false;

    // If this is not a FILE record then return FALSE. 
    if (memcmp(buffer, "FILE", 4) != 0) {
        gui->show_debug(
                DebugLevel::Progress, nullptr,
                L"This is not a valid MFT record, it does not begin with FILE (maybe trying to read past the end?).");

        DefragLib::show_hex(data, buffer, buf_length);

        return false;
    }

    /* Walk through all the sectors and restore the last 2 bytes with the value
    from the Usa array. If we encounter bad sector data then return with FALSE. */
    const auto buffer_w = (WORD *) buffer;
    const auto record_header = (NTFS_RECORD_HEADER *) buffer;
    const auto update_sequence_array = (WORD *) &buffer[record_header->usa_offset_];
    const auto increment = (uint32_t) (disk_info->bytes_per_sector_ / sizeof(USHORT));
    uint32_t index = increment - 1;

    for (USHORT i = 1; i < record_header->usa_count_; i++) {
        /* Check if we are inside the buffer. */
        if (index * sizeof(WORD) >= buf_length) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            L"Warning: USA data indicates that data is missing, the MFT may be corrupt.");
        }

        /* Check if the last 2 bytes of the sector contain the Update Sequence Number.
        If not then return FALSE. */
        if (buffer_w[index] != update_sequence_array[0]) {
            gui->show_debug(
                    DebugLevel::Progress, nullptr,
                    L"Error: USA fixup word is not equal to the Update Sequence Number, the MFT may be corrupt.");

            return false;
        }

        /* Replace the last 2 bytes in the sector with the value from the Usa array. */
        buffer_w[index] = update_sequence_array[i];
        index = index + increment;
    }

    return true;
}

/**
 * \brief Read the data that is specified in a RunData list from disk into memory, skipping the first Offset bytes.
 * \param offset Bytes to skip from begin of data
 * \return Return a malloc'ed buffer with the data, or nullptr if error. Note: The caller owns the returned buffer.
 */
BYTE *ScanNTFS::read_non_resident_data(const DefragDataStruct *data, const NtfsDiskInfoStruct *disk_info,
                                       const BYTE *run_data, const uint32_t run_data_length,
                                       const uint64_t offset, uint64_t wanted_length) {
    BYTE *buffer;
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
    buffer = new BYTE[wanted_length];

    memset(buffer, 0, (size_t) wanted_length);

    /* Walk through the RunData and read the requested data from disk. */
    uint32_t index = 0;
    int64_t lcn = 0;
    int64_t vcn = 0;

    while (run_data[index] != 0) {
        ULARGE_INTEGER trans;
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

        /* Ignore virtual extents. */
        if (run_offset.value == 0) continue;

        /* I don't think the RunLength can ever be zero, but just in case. */
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

        /* Read the data from the disk. If error then return FALSE. */
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"    Reading " NUM_FMT " bytes from LCN=" NUM_FMT " into offset=" NUM_FMT,
                                    extent_length,
                                    extent_lcn / (disk_info->bytes_per_sector_ * disk_info->sectors_per_cluster_),
                                    extent_vcn - offset));

        trans.QuadPart = extent_lcn;

        g_overlapped.Offset = trans.LowPart;
        g_overlapped.OffsetHigh = trans.HighPart;
        g_overlapped.hEvent = nullptr;

        if (const errno_t result = ReadFile(data->disk_.volume_handle_, &buffer[extent_vcn - offset],
                                            (uint32_t) extent_length, &bytes_read, &g_overlapped); result == 0) {
            wchar_t s1[BUFSIZ];
            DefragLib::system_error_str(GetLastError(), s1, BUFSIZ);

            gui->show_debug(DebugLevel::Progress, nullptr, std::format(L"Error while reading disk: {}", s1));

            delete buffer;
            return nullptr;
        }
    }

    return buffer;
}

/* Read the RunData list and translate into a list of fragments. */
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

    /* If the stream already has a list of fragments then find the last fragment. */
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
            /* Decode the RunData and calculate the next Lcn. */
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

            /* Show debug message. */
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

            /* Add the extent to the Fragments. */
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

/*

Cleanup the Streams data in an inode_data struct. If CleanFragments is TRUE then
also cleanup the fragments.

*/
void ScanNTFS::cleanup_streams(InodeDataStruct *inode_data, const bool cleanup_fragments) {
    const StreamStruct *stream = inode_data->streams_;

    while (stream != nullptr) {
        if (cleanup_fragments == TRUE) {
            const FragmentListStruct *fragment = stream->fragments_;

            while (fragment != nullptr) {
                const FragmentListStruct *temp_fragment = fragment;
                fragment = fragment->next_;

                delete temp_fragment;
            }
        }

        const StreamStruct *temp_stream = stream;
        stream = stream->next_;

        delete temp_stream;
    }

    inode_data->streams_ = nullptr;
}

/* Construct the full stream name from the filename, the stream name, and the stream type. */
std::wstring
ScanNTFS::construct_stream_name(const wchar_t *file_name_1, const wchar_t *file_name_2, const StreamStruct *stream) {
    auto file_name = file_name_1;

    if (file_name == nullptr || wcslen(file_name) == 0) {
        file_name = file_name_2;
    }
    if (file_name != nullptr && wcslen(file_name) == 0) {
        file_name = nullptr;
    }

    const wchar_t *stream_name = nullptr;
    auto stream_type = ATTRIBUTE_TYPE::AttributeInvalid;

    if (stream != nullptr) {
        stream_name = stream->stream_name_.c_str();
        if (wcslen(stream_name) == 0) stream_name = nullptr;

        stream_type = stream->stream_type_;
    }

    // If the stream_name is empty and the stream_type is Data then return only the
    // file_name. The Data stream is the default stream of regular files. 
    if ((stream_name == nullptr || wcslen(stream_name) == 0) && stream_type == ATTRIBUTE_TYPE::AttributeData) {
        if (file_name == nullptr || wcslen(file_name) == 0) return {};

        return file_name;
    }

    // If the stream_name is "$I30" and the stream_type is AttributeIndexAllocation then
    // return only the file_name. This must be a directory, and the Microsoft defragmentation
    // API will automatically select this stream. 
    if (stream_name != nullptr &&
        wcscmp(stream_name, L"$I30") == 0 &&
        stream_type == ATTRIBUTE_TYPE::AttributeIndexAllocation) {
        if (file_name == nullptr || wcslen(file_name) == 0) return {};

        return file_name;
    }

    // If the stream_name is empty and the stream_type is Data then return only the
    // file_name. The Data stream is the default stream of regular files.
    if ((stream_name == nullptr || wcslen(stream_name) == 0)
        && wcslen(stream_type_names(stream_type)) == 0) {
        if (file_name == nullptr || wcslen(file_name) == 0) return {};

        return file_name;
    }

    size_t length = 3;

    if (file_name != nullptr) length = length + wcslen(file_name);
    if (stream_name != nullptr) length = length + wcslen(stream_name);

    length = length + wcslen(stream_type_names(stream_type));

    if (length == 3) return {};

    auto *p1 = new wchar_t[length];

    if (p1 == nullptr) return {};

    *p1 = 0;

    if (file_name != nullptr) wcscat_s(p1, length, file_name);

    wcscat_s(p1, length, L":");

    if (stream_name != nullptr) wcscat_s(p1, length, stream_name);

    wcscat_s(p1, length, L":");
    wcscat_s(p1, length, stream_type_names(stream_type));

    return p1;
}

/* Forward declaration for recursion. */
/*
	BOOL process_attributes(
	struct DefragDataStruct *Data,
	struct NtfsDiskInfoStruct *DiskInfo,
	struct InodeDataStruct *InodeData,
		BYTE *Buffer,
		uint64_t BufLength,
		USHORT Instance,
		int Depth);*/

/**
 * \brief Process a list of attributes and store the gathered information in the Item struct. Return FALSE if an error occurred.
 */
void
ScanNTFS::process_attribute_list(DefragDataStruct *data, NtfsDiskInfoStruct *disk_info, InodeDataStruct *inode_data,
                                 BYTE *buffer, const uint64_t buf_length, const int depth) {
    std::unique_ptr<BYTE[]> buffer_2;
    ATTRIBUTE_LIST *attribute;
    FragmentListStruct *fragment;
    OVERLAPPED g_overlapped;
    DWORD bytes_read;
    DefragGui *gui = DefragGui::get_instance();

    /* Sanity checks. */
    if (buffer == nullptr || buf_length == 0) return;

    if (depth > 1000) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"Error: infinite attribute loop, the MFT may be corrupt.");
        return;
    }

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                    std::format(L"    Processing AttributeList for Inode " NUM_FMT ", " NUM_FMT " bytes",
                                inode_data->inode_, buf_length));

    // Walk through all the attributes and gather information
    for (ULONG attribute_offset = 0;
         attribute_offset < buf_length; attribute_offset = attribute_offset + attribute->length_) {
        ULARGE_INTEGER trans;
        attribute = (ATTRIBUTE_LIST *) &buffer[attribute_offset];

        // Exit if no more attributes. AttributeLists are usually not closed by the
        // 0xFFFFFFFF endmarker. Reaching the end of the buffer is therefore normal and
        // not an error. 
        if (attribute_offset + 3 > buf_length) break;
        if (*(ULONG *) attribute == 0xFFFFFFFF) break;
        if (attribute->length_ < 3) break;
        if (attribute_offset + attribute->length_ > buf_length) break;

        // Extract the referenced Inode. If it's the same as the calling Inode then ignore
        // (if we don't ignore then the program will loop forever, because for some
        // reason the info in the calling Inode is duplicated here...). 
        const uint64_t ref_inode = (uint64_t) attribute->file_reference_number_.inode_number_low_part_ +
                                   ((uint64_t) attribute->file_reference_number_.inode_number_high_part_ << 32);

        if (ref_inode == inode_data->inode_) continue;

        /* Show debug message. */
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"    List attribute: {}", stream_type_names(attribute->attribute_type_)));
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(
                                L"      LowestVcn = " NUM_FMT ", ref_inode = " NUM_FMT ", InodeSequence = " NUM_FMT ", Instance = " NUM_FMT,
                                attribute->lowest_vcn_, ref_inode, attribute->file_reference_number_.sequence_number_,
                                attribute->instance_));

        /* Extract the streamname. I don't know why AttributeLists can have names, and
        the name is not used further down. It is only extracted for debugging purposes.
        */
        if (attribute->name_length_ > 0) {
            auto p1 = std::make_unique<wchar_t[]>(attribute->name_length_ + 1);

            wcsncpy_s(p1.get(), attribute->name_length_ + 1,
                      (wchar_t *) &buffer[attribute_offset + attribute->name_offset_], attribute->name_length_);

            p1.get()[attribute->name_length_] = 0;
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"      AttributeList name = '{}'", p1.get()));
        }

        /* Find the fragment in the MFT that contains the referenced Inode. */
        uint64_t vcn = 0;
        uint64_t real_vcn = 0;
        const uint64_t ref_inode_vcn = ref_inode * disk_info->bytes_per_mft_record_ /
                                       (disk_info->bytes_per_sector_ * disk_info->sectors_per_cluster_);

        for (fragment = inode_data->mft_data_fragments_; fragment != nullptr; fragment = fragment->next_) {
            if (fragment->lcn_ != VIRTUALFRAGMENT) {
                if (ref_inode_vcn >= real_vcn && ref_inode_vcn < real_vcn + fragment->next_vcn_ - vcn) {
                    break;
                }

                real_vcn = real_vcn + fragment->next_vcn_ - vcn;
            }

            vcn = fragment->next_vcn_;
        }

        if (fragment == nullptr) {
            gui->show_debug(
                    DebugLevel::DetailedGapFinding, nullptr,
                    std::format(
                            L"      Error: Inode " NUM_FMT " is an extension of Inode " NUM_FMT ", but does not exist (outside the MFT).",
                            ref_inode, inode_data->inode_));

            continue;
        }

        /* Fetch the record of the referenced Inode from disk. */
        buffer_2 = std::make_unique<BYTE[]>((size_t) disk_info->bytes_per_mft_record_);

        trans.QuadPart = (fragment->lcn_ - real_vcn) * disk_info->bytes_per_sector_ *
                         disk_info->sectors_per_cluster_ + ref_inode * disk_info->bytes_per_mft_record_;

        g_overlapped.Offset = trans.LowPart;
        g_overlapped.OffsetHigh = trans.HighPart;
        g_overlapped.hEvent = nullptr;

        int result = ReadFile(data->disk_.volume_handle_, buffer_2.get(), (uint32_t) disk_info->bytes_per_mft_record_,
                              &bytes_read,
                              &g_overlapped);

        if (result == 0 || bytes_read != disk_info->bytes_per_mft_record_) {
            wchar_t s1[BUFSIZ];
            DefragLib::system_error_str(GetLastError(), s1, BUFSIZ);
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"      Error while reading Inode " NUM_FMT ": reason {}", ref_inode, s1));
            return;
        }

        // Fixup the raw data
        if (fixup_raw_mftdata(data, disk_info, buffer_2.get(), disk_info->bytes_per_mft_record_) == FALSE) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"The error occurred while processing Inode " NUM_FMT, ref_inode));

            continue;
        }

        // If the Inode is not in use then skip.
        const auto file_record_header = (FILE_RECORD_HEADER *) buffer_2.get();

        if ((file_record_header->flags_ & 1) != 1) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"      Referenced Inode " NUM_FMT " is not in use.", ref_inode));
            continue;
        }

        // If the base_inode inside the Inode is not the same as the calling Inode then skip.
        const uint64_t base_inode = (uint64_t) file_record_header->base_file_record_.inode_number_low_part_ +
                                    ((uint64_t) file_record_header->base_file_record_.inode_number_high_part_ << 32);

        if (inode_data->inode_ != base_inode) {
            gui->show_debug(
                    DebugLevel::DetailedGapFinding, nullptr,
                    std::format(
                            L"      Warning: Inode " NUM_FMT " is an extension of Inode " NUM_FMT ", but thinks it's an extension of Inode " NUM_FMT,
                            ref_inode, inode_data->inode_, base_inode));
            continue;
        }

        // Process the list of attributes in the Inode, by recursively calling the process_attributes() subroutine.
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"      Processing Inode " NUM_FMT " Instance {}", ref_inode,
                                    attribute->instance_));

        result = process_attributes(data, disk_info, inode_data,
                                    &buffer_2[file_record_header->attribute_offset_],
                                    disk_info->bytes_per_mft_record_ - file_record_header->attribute_offset_,
                                    attribute->instance_, depth + 1);

        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"      Finished processing Inode " NUM_FMT " Instance {}",
                                    ref_inode, attribute->instance_));
    }
}

/* Process a list of attributes and store the gathered information in the Item
struct. Return FALSE if an error occurred. */
bool ScanNTFS::process_attributes(DefragDataStruct *data, NtfsDiskInfoStruct *disk_info,
                                  InodeDataStruct *inode_data, BYTE *buffer, const uint64_t buf_length,
                                  const USHORT instance, const int depth) {
    std::unique_ptr<BYTE[]> buffer_2;
    ULONG attribute_offset;
    ATTRIBUTE *attribute;
    RESIDENT_ATTRIBUTE *resident_attribute;
    NONRESIDENT_ATTRIBUTE *nonresident_attribute;
    DefragGui *gui = DefragGui::get_instance();

    /* Walk through all the attributes and gather information. AttributeLists are
    skipped and interpreted later. */
    for (attribute_offset = 0; attribute_offset < buf_length; attribute_offset = attribute_offset + attribute->
            length_) {
        attribute = (ATTRIBUTE *) &buffer[attribute_offset];

        /* Exit the loop if end-marker. */
        if (attribute_offset + 4 <= buf_length && *(ULONG *) attribute == 0xFFFFFFFF) break;

        /* Sanity check. */
        if (attribute_offset + 4 > buf_length ||
            attribute->length_ < 3 ||
            attribute_offset + attribute->length_ > buf_length) {
            gui->show_debug(
                    DebugLevel::Progress, nullptr,
                    std::format(
                            L"Error: attribute in Inode " NUM_FMT " is bigger than the data, the MFT may be corrupt.",
                            inode_data->inode_));
            gui->show_debug(
                    DebugLevel::Progress, nullptr,
                    std::format(L"  buf_length=" NUM_FMT ", attribute_offset=" NUM_FMT ", AttributeLength={}({:x})",
                                buf_length, attribute_offset, attribute->length_, attribute->length_));

            DefragLib::show_hex(data, buffer, buf_length);

            return FALSE;
        }

        // Skip AttributeList's for now
        if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeAttributeList) continue;

        // If the instance does not equal the AttributeNumber then ignore the attribute.
        // This is used when an AttributeList is being processed and we only want a specific instance 
        if (instance != 65535 && instance != attribute->attribute_number_) continue;

        // Show debug message
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"  attribute {}: {}", attribute->attribute_number_,
                                    stream_type_names(attribute->attribute_type_)));

        if (attribute->nonresident_ == 0) {
            resident_attribute = (RESIDENT_ATTRIBUTE *) attribute;

            // The AttributeFileName (0x30) contains the filename and the link to the parent directory
            if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeFileName) {
                const auto file_name_attribute = (FILENAME_ATTRIBUTE *) &buffer[attribute_offset +
                                                                                resident_attribute->value_offset_];

                inode_data->parent_inode_ = file_name_attribute->parent_directory_.inode_number_low_part_ +
                                            ((uint64_t) file_name_attribute->parent_directory_.inode_number_high_part_
                                                    << 32);

                if (file_name_attribute->name_length_ > 0) {
                    // Extract the filename.
                    // Lhis value is moved later to an owned pointer
                    auto p1 = std::make_unique<wchar_t[]>(file_name_attribute->name_length_ + 1);

                    wcsncpy_s(p1.get(), file_name_attribute->name_length_ + 1, file_name_attribute->name_,
                              file_name_attribute->name_length_);

                    p1[file_name_attribute->name_length_] = 0;

                    // Save the filename in either the Long or the Short filename. We only save the first filename,
                    // any additional filenames are hard links. They might be useful for an optimization algorithm
                    // that sorts by filename, but which of the hardlinked names should it sort? So we only store the
                    // first filename
                    if (file_name_attribute->name_type_ == 2) {
                        if (inode_data->short_filename_ != nullptr) {
                            p1.reset();
                        } else {
                            inode_data->short_filename_ = std::move(p1);
                            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                                            std::format(L"    Short filename = '{}'",
                                                        inode_data->short_filename_.get()));
                        }
                    } else {
                        if (inode_data->long_filename_ != nullptr) {
                            p1.reset();
                        } else {
                            inode_data->long_filename_ = std::move(p1);
                            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                                            std::format(L"    Long filename = '{}'", inode_data->long_filename_.get()));
                        }
                    }
                }
            }

            // The AttributeStandardInformation (0x10) contains the CreationTime, LastAccessTime,
            // the MftChangeTime, and the file attributes.
            if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeStandardInformation) {
                auto standard_information = (STANDARD_INFORMATION *) &buffer[attribute_offset + resident_attribute->
                        value_offset_];

                inode_data->creation_time_ = std::chrono::microseconds(standard_information->creation_time_);
                inode_data->mft_change_time_ = std::chrono::microseconds(standard_information->mft_change_time_);
                inode_data->last_access_time_ = std::chrono::microseconds(standard_information->last_access_time_);
            }

            /* The value of the AttributeData (0x80) is the actual data of the file. */
            if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeData) {
                inode_data->bytes_ = resident_attribute->value_length_;
            }
        } else {
            nonresident_attribute = (NONRESIDENT_ATTRIBUTE *) attribute;

            /* Save the length (number of bytes) of the data. */
            if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeData && inode_data->bytes_ == 0) {
                inode_data->bytes_ = nonresident_attribute->data_size_;
            }

            // Extract the streamname
            std::unique_ptr<wchar_t[]> p2;

            if (attribute->name_length_ > 0) {
                p2 = std::make_unique<wchar_t[]>(attribute->name_length_ + 1);

                wcsncpy_s(p2.get(), attribute->name_length_ + 1,
                          (wchar_t *) &buffer[attribute_offset + attribute->name_offset_],
                          attribute->name_length_);

                p2[attribute->name_length_] = 0;
            }

            // Create a new stream with a list of fragments for this data
            translate_rundata_to_fragmentlist(
                    data, inode_data, p2.get(), attribute->attribute_type_,
                    (BYTE *) &buffer[attribute_offset + nonresident_attribute->run_array_offset_],
                    attribute->length_ - nonresident_attribute->run_array_offset_,
                    nonresident_attribute->starting_vcn_, nonresident_attribute->data_size_);

            // Cleanup the streamname
            p2.reset();

            /* Special case: If this is the $MFT then save data. */
            if (inode_data->inode_ == 0) {
                if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeData
                    && inode_data->mft_data_fragments_ == nullptr) {
                    inode_data->mft_data_fragments_ = inode_data->streams_->fragments_;
                    inode_data->mft_data_bytes_ = nonresident_attribute->data_size_;
                }

                if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeBitmap
                    && inode_data->mft_bitmap_fragments_ == nullptr) {
                    inode_data->mft_bitmap_fragments_ = inode_data->streams_->fragments_;
                    inode_data->mft_bitmap_bytes_ = nonresident_attribute->data_size_;
                }
            }
        }
    }

    /* Walk through all the attributes and interpret the AttributeLists. We have to
    do this after the DATA and BITMAP attributes have been interpreted, because
    some MFT's have an AttributeList that is stored in fragments that are
    defined in the DATA attribute, and/or contain a continuation of the DATA or
    BITMAP attributes. */
    for (attribute_offset = 0; attribute_offset < buf_length; attribute_offset = attribute_offset + attribute->
            length_) {
        attribute = (ATTRIBUTE *) &buffer[attribute_offset];

        if (*(ULONG *) attribute == 0xFFFFFFFF) break;
        if (attribute->attribute_type_ != ATTRIBUTE_TYPE::AttributeAttributeList) continue;

        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"  attribute {}: {}", attribute->attribute_number_,
                                    stream_type_names(attribute->attribute_type_)));

        if (attribute->nonresident_ == 0) {
            resident_attribute = (RESIDENT_ATTRIBUTE *) attribute;

            process_attribute_list(data, disk_info, inode_data,
                                   (BYTE *) &buffer[attribute_offset + resident_attribute->value_offset_],
                                   resident_attribute->value_length_, depth);
        } else {
            nonresident_attribute = (NONRESIDENT_ATTRIBUTE *) attribute;
            const uint64_t buffer_2_length = nonresident_attribute->data_size_;

            buffer_2.reset(
                    read_non_resident_data(data, disk_info,
                                           (BYTE *) &buffer[attribute_offset +
                                                            nonresident_attribute->run_array_offset_],
                                           attribute->length_ - nonresident_attribute->run_array_offset_, 0,
                                           buffer_2_length));

            process_attribute_list(data, disk_info, inode_data, buffer_2.get(), buffer_2_length, depth);
        }
    }

    return true;
}

bool ScanNTFS::interpret_mft_record(DefragDataStruct *data, NtfsDiskInfoStruct *disk_info, ItemStruct **inode_array,
                                    const uint64_t inode_number, const uint64_t max_inode,
                                    FragmentListStruct **mft_data_fragments, uint64_t *mft_data_bytes,
                                    FragmentListStruct **mft_bitmap_fragments, uint64_t *mft_bitmap_bytes,
                                    BYTE *buffer, const uint64_t buf_length) {
    InodeDataStruct inode_data{};
    DefragGui *gui = DefragGui::get_instance();

    /* If the record is not in use then quietly exit. */
    const FILE_RECORD_HEADER *file_record_header = (FILE_RECORD_HEADER *) buffer;

    if ((file_record_header->flags_ & 1) != 1) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"Inode " NUM_FMT " is not in use.", inode_number));

        return false;
    }

    /* If the record has a BaseFileRecord then ignore it. It is used by an
    AttributeAttributeList as an extension of another Inode, it's not an
    Inode by itself. */
    const uint64_t base_inode = (uint64_t) file_record_header->base_file_record_.inode_number_low_part_ +
                                ((uint64_t) file_record_header->base_file_record_.inode_number_high_part_ << 32);

    if (base_inode != 0) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"Ignoring Inode " NUM_FMT ", it's an extension of Inode %I64u", inode_number,
                                    base_inode));

        return true;
    }

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                    std::format(L"Processing Inode " NUM_FMT "…", inode_number));

    /* Show a warning if the Flags have an unknown value. */
    if ((file_record_header->flags_ & 252) != 0) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"  Inode " NUM_FMT " has Flags = {}", inode_number, file_record_header->flags_));
    }

    /* I think the MFTRecordNumber should always be the inode_number, but it's an XP
    extension and I'm not sure about Win2K.
    Note: why is the MFTRecordNumber only 32 bit? Inode numbers are 48 bit. */
    if (file_record_header->mft_record_number_ != inode_number) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"  Warning: Inode " NUM_FMT " contains a different MFTRecordNumber " NUM_FMT,
                                    inode_number, file_record_header->mft_record_number_));
    }

    // Sanity check
    if (file_record_header->attribute_offset_ >= buf_length) {
        gui->show_debug(
                DebugLevel::Progress, nullptr,
                std::format(
                        L"Error: attributes in Inode " NUM_FMT " are outside the FILE record, the MFT may be corrupt.",
                        inode_number));

        return false;
    }

    if (file_record_header->bytes_in_use_ > buf_length) {
        gui->show_debug(
                DebugLevel::Progress, nullptr,
                std::format(
                        L"Error: in Inode " NUM_FMT " the record is bigger than the size of the buffer, the MFT may be corrupt.",
                        inode_number));

        return false;
    }

    /* Initialize the InodeData struct. */
    inode_data.inode_ = inode_number; /* The Inode number. */
    inode_data.parent_inode_ = 5; /* The Inode number of the parent directory. */
    inode_data.is_directory_ = false;

    if ((file_record_header->flags_ & 2) == 2) inode_data.is_directory_ = true;

    inode_data.long_filename_ = nullptr; /* Long filename. */
    inode_data.short_filename_ = nullptr; /* Short filename (8.3 DOS). */
    inode_data.creation_time_ = {};
    inode_data.mft_change_time_ = {};
    inode_data.last_access_time_ = {};
    inode_data.bytes_ = 0; /* Size of the $DATA stream. */
    inode_data.streams_ = nullptr; /* List of StreamStruct. */
    inode_data.mft_data_fragments_ = *mft_data_fragments;
    inode_data.mft_data_bytes_ = *mft_data_bytes;
    inode_data.mft_bitmap_fragments_ = nullptr;
    inode_data.mft_bitmap_bytes_ = 0;

    /* Make sure that directories are always created. */
    if (inode_data.is_directory_) {
        translate_rundata_to_fragmentlist(data, &inode_data, L"$I30", ATTRIBUTE_TYPE::AttributeIndexAllocation, nullptr,
                                          0,
                                          0, 0);
    }

    // Interpret the attributes
    [[maybe_unused]] int result = process_attributes(data, disk_info, &inode_data,
                                                     &buffer[file_record_header->attribute_offset_],
                                                     buf_length - file_record_header->attribute_offset_, 65535, 0);

    /* Save the mft_data_fragments, mft_data_bytes, mft_bitmap_fragments, and MftBitmapBytes. */
    if (inode_number == 0) {
        *mft_data_fragments = inode_data.mft_data_fragments_;
        *mft_data_bytes = inode_data.mft_data_bytes_;
        *mft_bitmap_fragments = inode_data.mft_bitmap_fragments_;
        *mft_bitmap_bytes = inode_data.mft_bitmap_bytes_;
    }

    /* Create an item in the data->ItemTree for every stream. */
    StreamStruct *stream = inode_data.streams_;
    do {
        /* Create and fill a new item record in memory. */
        auto item = new ItemStruct();

        auto long_fn_constructed = construct_stream_name(inode_data.long_filename_.get(),
                                                         inode_data.short_filename_.get(), stream);
        auto short_fn_constructed = construct_stream_name(inode_data.short_filename_.get(),
                                                          inode_data.long_filename_.get(),
                                                          stream);
        item->set_names(L"", long_fn_constructed.c_str(), nullptr, short_fn_constructed.c_str());

        item->bytes_ = inode_data.bytes_;

        if (stream != nullptr) item->bytes_ = stream->bytes_;

        item->clusters_count_ = 0;

        if (stream != nullptr) item->clusters_count_ = stream->clusters_;

        item->creation_time_ = inode_data.creation_time_;
        item->mft_change_time_ = inode_data.mft_change_time_;
        item->last_access_time_ = inode_data.last_access_time_;
        item->fragments_ = nullptr;

        if (stream != nullptr) item->fragments_ = stream->fragments_;

        item->parent_inode_ = inode_data.parent_inode_;
        item->is_dir_ = inode_data.is_directory_;
        item->is_unmovable_ = false;
        item->is_excluded_ = false;
        item->is_hog_ = false;

        /* Increment counters. */
        if (item->is_dir_ == true) {
            data->count_directories_ = data->count_directories_ + 1;
        }

        data->count_all_files_ = data->count_all_files_ + 1;

        if (stream != nullptr && stream->stream_type_ == ATTRIBUTE_TYPE::AttributeData) {
            data->count_all_bytes_ = data->count_all_bytes_ + inode_data.bytes_;
        }

        if (stream != nullptr) data->count_all_clusters_ = data->count_all_clusters_ + stream->clusters_;

        if (DefragLib::get_fragment_count(item) > 1) {
            data->count_fragmented_items_ = data->count_fragmented_items_ + 1;
            data->count_fragmented_bytes_ = data->count_fragmented_bytes_ + inode_data.bytes_;

            if (stream != nullptr)
                data->count_fragmented_clusters_ = data->count_fragmented_clusters_ + stream->
                        clusters_;
        }

        /* Add the item record to the sorted item tree in memory. */
        DefragLib::tree_insert(data, item);

        /* Also add the item to the array that is used to construct the full pathnames.
        Note: if the array already contains an entry, and the new item has a shorter
        filename, then the entry is replaced. This is needed to make sure that
        the shortest form of the name of directories is used. */

        if (inode_array != nullptr &&
            inode_number < max_inode &&
            (inode_array[inode_number] == nullptr ||
             (inode_array[inode_number]->have_long_fn()
              && item->have_long_fn()
              && wcscmp(inode_array[inode_number]->get_long_fn(), item->get_long_fn()) > 0))) {
            inode_array[inode_number] = item;
        }

        // Draw the item on the screen.
        gui->show_analyze(data, item);
        defrag_lib_->colorize_item(data, item, 0, 0, false);

        if (stream != nullptr) stream = stream->next_;
    } while (stream != nullptr);

    // Cleanup and return TRUE
    inode_data.long_filename_.reset();
    inode_data.short_filename_.reset();

    cleanup_streams(&inode_data, false);

    return true;
}

/* Load the MFT into a list of ItemStruct records in memory. */
bool ScanNTFS::analyze_ntfs_volume(DefragDataStruct *data) {
    NtfsDiskInfoStruct disk_info{};
    std::unique_ptr<BYTE[]> buffer;
    OVERLAPPED g_overlapped;
    ULARGE_INTEGER trans;
    DWORD bytes_read;
    FragmentListStruct *mft_data_fragments;
    uint64_t mft_data_bytes;
    FragmentListStruct *mft_bitmap_fragments;
    uint64_t mft_bitmap_bytes;
    uint64_t max_mft_bitmap_bytes;
    std::unique_ptr<BYTE[]> mft_bitmap;
    FragmentListStruct *fragment;
    std::unique_ptr<ItemStruct *[]> inode_array;
    uint64_t max_inode;
    ItemStruct *item;
    uint64_t vcn;
    uint64_t real_vcn;
    uint64_t inode_number;
    uint64_t block_start;
    uint64_t block_end;
    BYTE bitmap_masks[8] = {1, 2, 4, 8, 16, 32, 64, 128};
    bool result;
    ULONG clusters_per_mft_record;
    __timeb64 time{};
    milli64_t start_time;
    milli64_t end_time;
    wchar_t s1[BUFSIZ];
    uint64_t u1;
    DefragGui *gui = DefragGui::get_instance();

    // Read the boot block from the disk
    buffer = std::make_unique<BYTE[]>(mftbuffersize);

    g_overlapped.Offset = 0;
    g_overlapped.OffsetHigh = 0;
    g_overlapped.hEvent = nullptr;

    result = ReadFile(data->disk_.volume_handle_, buffer.get(), (uint32_t) 512, &bytes_read, &g_overlapped);

    if (result == 0 || bytes_read != 512) {
        DefragLib::system_error_str(GetLastError(), s1, BUFSIZ);
        gui->show_debug(DebugLevel::Progress, nullptr, std::format(L"Error while reading bootblock: {}", s1));
        return false;
    }

    /* Test if the boot block is an NTFS boot block. */
    if (*(ULONGLONG *) &buffer.get()[3] != 0x202020205346544E) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"This is not an NTFS disk (different cookie).");
        return false;
    }

    /* Extract data from the bootblock. */
    data->disk_.type_ = DiskType::NTFS;
    disk_info.bytes_per_sector_ = *(USHORT *) &buffer[11];

    /* Still to do: check for impossible values. */
    disk_info.sectors_per_cluster_ = buffer[13];
    disk_info.total_sectors_ = *(ULONGLONG *) &buffer[40];
    disk_info.mft_start_lcn_ = *(ULONGLONG *) &buffer[48];
    disk_info.mft2_start_lcn_ = *(ULONGLONG *) &buffer[56];
    clusters_per_mft_record = *(ULONG *) &buffer[64];

    if (clusters_per_mft_record >= 128) {
        // TODO: Bug with << 256 here
        disk_info.bytes_per_mft_record_ = (uint64_t) 1 << 256 - clusters_per_mft_record;
    } else {
        disk_info.bytes_per_mft_record_ = clusters_per_mft_record * disk_info.bytes_per_sector_ * disk_info.
                sectors_per_cluster_;
    }

    disk_info.clusters_per_index_record_ = *(ULONG *) &buffer[68];

    data->bytes_per_cluster_ = disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_;

    if (disk_info.sectors_per_cluster_ > 0) {
        data->total_clusters_ = disk_info.total_sectors_ / disk_info.sectors_per_cluster_;
    }

    gui->show_debug(DebugLevel::Fatal, nullptr, std::format(
            L"This is an NTFS disk."
            L"\n  Disk cookie: " NUM_FMT
            L"\n  BytesPerSector: " NUM_FMT
            L"\n  TotalSectors: " NUM_FMT
            L"\n  SectorsPerCluster: " NUM_FMT
            L"\n  SectorsPerTrack: " NUM_FMT
            L"\n  NumberOfHeads: " NUM_FMT
            L"\n  MftStartLcn: " NUM_FMT
            L"\n  Mft2StartLcn: " NUM_FMT
            L"\n  BytesPerMftRecord: " NUM_FMT
            L"\n  ClustersPerIndexRecord: " NUM_FMT
            L"\n  MediaType: {:x}"
            L"\n  VolumeSerialNumber: " NUM_FMT,
            *(ULONGLONG *) &buffer[3], disk_info.bytes_per_sector_, disk_info.total_sectors_,
            disk_info.sectors_per_cluster_, *(USHORT *) &buffer[24], *(USHORT *) &buffer[26], disk_info.mft_start_lcn_,
            disk_info.mft2_start_lcn_, disk_info.bytes_per_mft_record_, disk_info.clusters_per_index_record_,
            buffer[21], *(ULONGLONG *) &buffer[72]));

    /* Calculate the size of first 16 Inodes in the MFT. The Microsoft defragmentation
    API cannot move these inodes. */
    data->disk_.mft_locked_clusters_ = disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_ / disk_info.
            bytes_per_mft_record_;

    /* Read the $MFT record from disk into memory, which is always the first record in
    the MFT. */
    trans.QuadPart = disk_info.mft_start_lcn_ * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_;
    g_overlapped.Offset = trans.LowPart;
    g_overlapped.OffsetHigh = trans.HighPart;
    g_overlapped.hEvent = nullptr;
    result = ReadFile(data->disk_.volume_handle_, buffer.get(),
                      (uint32_t) disk_info.bytes_per_mft_record_, &bytes_read,
                      &g_overlapped);

    if (result == 0 || bytes_read != disk_info.bytes_per_mft_record_) {
        DefragLib::system_error_str(GetLastError(), s1, BUFSIZ);
        gui->show_debug(DebugLevel::Progress, nullptr, std::format(L"Error while reading first MFT record: {}", s1));
        return false;
    }

    /* Fixup the raw data from disk. This will also test if it's a valid $MFT record. */
    if (fixup_raw_mftdata(data, &disk_info, buffer.get(), disk_info.bytes_per_mft_record_) == FALSE) {
        return false;
    }

    /* Extract data from the MFT record and put into an Item struct in memory. If
    there was an error then exit. */
    mft_data_bytes = 0;
    mft_data_fragments = nullptr;
    mft_bitmap_bytes = 0;
    mft_bitmap_fragments = nullptr;

    result = interpret_mft_record(data, &disk_info, nullptr, 0, 0, &mft_data_fragments, &mft_data_bytes,
                                  &mft_bitmap_fragments, &mft_bitmap_bytes, buffer.get(),
                                  disk_info.bytes_per_mft_record_);

    if (!result ||
        mft_data_fragments == nullptr || mft_data_bytes == 0 ||
        mft_bitmap_fragments == nullptr || mft_bitmap_bytes == 0) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"Fatal error, cannot process this disk.");
        DefragLib::delete_item_tree(data->item_tree_);
        data->item_tree_ = nullptr;
        return false;
    }

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                    std::format(L"MftDataBytes = " NUM_FMT ", MftBitmapBytes = " NUM_FMT,
                                mft_data_bytes, mft_bitmap_bytes));

    /* Read the complete $MFT::$BITMAP into memory.
    Note: The allocated size of the bitmap is a multiple of the cluster size. This
    is only to make it easier to read the fragments, the extra bytes are not used. */
    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Reading $MFT::$BITMAP into memory");

    vcn = 0;
    max_mft_bitmap_bytes = 0;

    for (fragment = mft_bitmap_fragments; fragment != nullptr; fragment = fragment->next_) {
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            max_mft_bitmap_bytes = max_mft_bitmap_bytes +
                                   (fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ *
                                   disk_info.sectors_per_cluster_;
        }

        vcn = fragment->next_vcn_;
    }

    if (max_mft_bitmap_bytes < mft_bitmap_bytes) max_mft_bitmap_bytes = (size_t) mft_bitmap_bytes;

    mft_bitmap = std::make_unique<BYTE[]>(max_mft_bitmap_bytes);
    std::memset(mft_bitmap.get(), 0, (size_t) mft_bitmap_bytes);

    vcn = 0;
    real_vcn = 0;

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Reading $MFT::$BITMAP into memory");

    for (fragment = mft_bitmap_fragments; fragment != nullptr; fragment = fragment->next_) {
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"  Extent Lcn=" NUM_FMT ", RealVcn=" NUM_FMT ", Size=" NUM_FMT,
                                        fragment->lcn_, real_vcn, fragment->next_vcn_ - vcn));

            trans.QuadPart = fragment->lcn_ * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_;

            g_overlapped.Offset = trans.LowPart;
            g_overlapped.OffsetHigh = trans.HighPart;
            g_overlapped.hEvent = nullptr;

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"    Reading " NUM_FMT " clusters (" NUM_FMT " bytes) from LCN=" NUM_FMT,
                                        fragment->next_vcn_ - vcn,
                                        (fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ *
                                        disk_info.sectors_per_cluster_, fragment->lcn_));

            result = ReadFile(data->disk_.volume_handle_,
                              &mft_bitmap[real_vcn * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_],
                              (uint32_t) ((fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ * disk_info.
                                      sectors_per_cluster_),
                              &bytes_read, &g_overlapped);

            if (result == 0 || bytes_read != (fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ * disk_info.
                    sectors_per_cluster_) {
                DefragLib::system_error_str(GetLastError(), s1, BUFSIZ);
                gui->show_debug(DebugLevel::Progress, nullptr, std::format(L"  {}", s1));
                DefragLib::delete_item_tree(data->item_tree_);
                data->item_tree_ = nullptr;
                return false;
            }

            real_vcn = real_vcn + fragment->next_vcn_ - vcn;
        }

        vcn = fragment->next_vcn_;
    }

    /* Construct an array of all the items in memory, indexed by Inode.
    Note: the maximum number of Inodes is primarily determined by the size of the
    bitmap. But that is rounded up to 8 Inodes, and the MFT can be shorter. */
    max_inode = mft_bitmap_bytes * 8;

    if (max_inode > mft_data_bytes / disk_info.bytes_per_mft_record_) {
        max_inode = mft_data_bytes / disk_info.bytes_per_mft_record_;
    }

    inode_array = std::make_unique<ItemStruct *[]>(max_inode);
    inode_array[0] = data->item_tree_;
    std::fill(inode_array.get() + 1, inode_array.get() + max_inode, nullptr);

    // Read and process all the records in the MFT. The records are read into a
    // buffer and then given one by one to the interpret_mft_record() subroutine.
    fragment = mft_data_fragments;
    block_end = 0;
    vcn = 0;
    real_vcn = 0;

    data->phase_done_ = 0;
    data->phase_todo_ = 0;

    _ftime64_s(&time);

    start_time = std::chrono::milliseconds(time.time * 1000 + time.millitm);

    for (inode_number = 1; inode_number < max_inode; inode_number++) {
        if ((mft_bitmap[inode_number >> 3] & bitmap_masks[inode_number % 8]) == 0) continue;

        data->phase_todo_ = data->phase_todo_ + 1;
    }

    for (inode_number = 1; inode_number < max_inode; inode_number++) {
        if (*data->running_ != RunningState::RUNNING) break;

        // Ignore the Inode if the bitmap says it's not in use
        if ((mft_bitmap[inode_number >> 3] & bitmap_masks[inode_number % 8]) == 0) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"Inode " NUM_FMT " is not in use.", inode_number));
            continue;
        }

        /* Update the progress counter. */
        data->phase_done_ = data->phase_done_ + 1;

        /* Read a block of inode's into memory. */
        if (inode_number >= block_end) {
            /* Slow the program down to the percentage that was specified on the command line. */
            DefragLib::slow_down(data);

            block_start = inode_number;
            block_end = block_start + mftbuffersize / disk_info.bytes_per_mft_record_;

            if (block_end > mft_bitmap_bytes * 8) block_end = mft_bitmap_bytes * 8;

            while (fragment != nullptr) {
                /* Calculate Inode at the end of the fragment. */
                u1 = (real_vcn + fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ *
                     disk_info.sectors_per_cluster_ / disk_info.bytes_per_mft_record_;

                if (u1 > inode_number) break;

                do {
                    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Skipping to next extent");

                    if (fragment->lcn_ != VIRTUALFRAGMENT) real_vcn = real_vcn + fragment->next_vcn_ - vcn;

                    vcn = fragment->next_vcn_;
                    fragment = fragment->next_;

                    if (fragment == nullptr) break;
                } while (fragment->lcn_ == VIRTUALFRAGMENT);

                gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
                        L"  Extent Lcn=" NUM_FMT ", RealVcn=" NUM_FMT ", Size=" NUM_FMT,
                        fragment->lcn_, real_vcn, fragment->next_vcn_ - vcn));
            }
            if (fragment == nullptr) break;
            if (block_end >= u1) block_end = u1;

            trans.QuadPart = (fragment->lcn_ - real_vcn) * disk_info.bytes_per_sector_ *
                             disk_info.sectors_per_cluster_ + block_start * disk_info.bytes_per_mft_record_;

            g_overlapped.Offset = trans.LowPart;
            g_overlapped.OffsetHigh = trans.HighPart;
            g_overlapped.hEvent = nullptr;

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(
                                    L"Reading block of " NUM_FMT " Inodes from MFT into memory, " NUM_FMT " bytes from LCN=" NUM_FMT,
                                    block_end - block_start,
                                    (block_end - block_start) * disk_info.bytes_per_mft_record_,
                                    trans.QuadPart / (disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_)));

            result = ReadFile(data->disk_.volume_handle_, buffer.get(),
                              (uint32_t) ((block_end - block_start) * disk_info.bytes_per_mft_record_), &bytes_read,
                              &g_overlapped);

            if (result == 0 || bytes_read != (block_end - block_start) * disk_info.bytes_per_mft_record_) {
                DefragLib::system_error_str(GetLastError(), s1, BUFSIZ);

                gui->show_debug(DebugLevel::Progress, nullptr,
                                std::format(L"Error while reading Inodes " NUM_FMT " to " NUM_FMT ": {}",
                                            inode_number, block_end - 1, s1));

                DefragLib::delete_item_tree(data->item_tree_);
                data->item_tree_ = nullptr;
                return FALSE;
            }
        }

        /* Fixup the raw data of this Inode. */
        if (fixup_raw_mftdata(data, &disk_info, &buffer[(inode_number - block_start) * disk_info.bytes_per_mft_record_],
                              disk_info.bytes_per_mft_record_) == FALSE) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"The error occurred while processing Inode %I64u (max " NUM_FMT ")",
                                        inode_number, max_inode));

            continue;
        }

        /* Interpret the Inode's attributes. */
        result = interpret_mft_record(data, &disk_info, inode_array.get(), inode_number, max_inode,
                                      &mft_data_fragments, &mft_data_bytes, &mft_bitmap_fragments, &mft_bitmap_bytes,
                                      &buffer[(inode_number - block_start) * disk_info.bytes_per_mft_record_],
                                      disk_info.bytes_per_mft_record_);
    }

    _ftime64_s(&time);
    end_time = std::chrono::milliseconds(time.time * 1000 + time.millitm);

    if (end_time > start_time) {
        gui->show_debug(DebugLevel::Progress, nullptr, std::format(L"  Analysis speed: " NUM_FMT " items per second",
                                                                   max_inode * 1000 / (end_time - start_time).count()));
    }

    if (*data->running_ != RunningState::RUNNING) {
        DefragLib::delete_item_tree(data->item_tree_);
        data->item_tree_ = nullptr;
        return false;
    }

    // Setup the ParentDirectory in all the items with the info in the InodeArray
    for (item = DefragLib::tree_smallest(data->item_tree_); item != nullptr; item = DefragLib::tree_next(item)) {
        item->parent_directory_ = inode_array[item->parent_inode_];

        if (item->parent_inode_ == 5) item->parent_directory_ = nullptr;
    }

    return true;
}
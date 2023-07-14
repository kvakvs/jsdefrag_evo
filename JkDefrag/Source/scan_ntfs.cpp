#include "std_afx.h"
#include "defrag_data_struct.h"

/*
#include "JkDefragLib.h"
#include "JKDefragStruct.h"
#include "JKDefragLog.h"
#include "JkDefragGui.h"
#include "ScanNtfs.h"
*/

ScanNTFS::ScanNTFS() = default;

ScanNTFS::~ScanNTFS() = default;

ScanNTFS *ScanNTFS::get_instance() {
    if (instance_ == nullptr) {
        instance_.reset(new ScanNTFS());
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
    if (buffer == nullptr) return FALSE;
    if (buf_length < sizeof(NTFS_RECORD_HEADER)) return FALSE;

    // If this is not a FILE record then return FALSE. 
    if (memcmp(buffer, "FILE", 4) != 0) {
        gui->show_debug(
                DebugLevel::Progress, nullptr,
                L"This is not a valid MFT record, it does not begin with FILE (maybe trying to read past the end?).");

        defrag_lib_->show_hex(data, buffer, buf_length);

        return FALSE;
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

            return FALSE;
        }

        /* Replace the last 2 bytes in the sector with the value from the Usa array. */
        buffer_w[index] = update_sequence_array[i];
        index = index + increment;
    }

    return TRUE;
}

/*

Read the data that is specified in a RunData list from disk into memory, skipping the first Offset bytes.
Return a malloc'ed buffer with the data, or nullptr if error.
Note: The caller must free() the buffer.

*/
BYTE *ScanNTFS::read_non_resident_data(const DefragDataStruct *data, const NtfsDiskInfoStruct *disk_info,
                                       const BYTE *run_data, const uint32_t run_data_length,
                                       const uint64_t offset, /* Bytes to skip from begin of data. */
                                       uint64_t wanted_length) const {
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

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Reading %I64u bytes from offset %I64u",
                    wanted_length, offset);

    /* Sanity check. */
    if (run_data == nullptr || run_data_length == 0) return nullptr;
    if (wanted_length >= INT_MAX) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"    Cannot read %I64u bytes, maximum is %lu.", wanted_length,
                        INT_MAX);

        return nullptr;
    }

    /* We have to round up the WantedLength to the nearest sector. For some
    reason or other Microsoft has decided that raw reading from disk can
    only be done by whole sector, even though ReadFile() accepts it's
    parameters in bytes. */
    if (wanted_length % disk_info->bytes_per_sector_ > 0) {
        wanted_length = wanted_length + disk_info->bytes_per_sector_ - wanted_length % disk_info->bytes_per_sector_;
    }

    /* Allocate the data buffer. Clear the buffer with zero's in case of sparse
    content. */
    buffer = (BYTE *) malloc((size_t) wanted_length);

    if (buffer == nullptr) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

        return nullptr;
    }

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
                        L"    Reading %I64u bytes from Lcn=%I64u into offset=%I64u",
                        extent_length, extent_lcn / (disk_info->bytes_per_sector_ * disk_info->sectors_per_cluster_),
                        extent_vcn - offset);

        trans.QuadPart = extent_lcn;

        g_overlapped.Offset = trans.LowPart;
        g_overlapped.OffsetHigh = trans.HighPart;
        g_overlapped.hEvent = nullptr;

        if (const errno_t result = ReadFile(data->disk_.volume_handle_, &buffer[extent_vcn - offset],
                                            (uint32_t) extent_length, &bytes_read, &g_overlapped); result == 0) {
            wchar_t s1[BUFSIZ];
            defrag_lib_->system_error_str(GetLastError(), s1, BUFSIZ);

            gui->show_debug(DebugLevel::Progress, nullptr, L"Error while reading disk: %s", s1);

            free(buffer);

            return nullptr;
        }
    }

    return buffer;
}

/* Read the RunData list and translate into a list of fragments. */
BOOL ScanNTFS::translate_rundata_to_fragmentlist(const DefragDataStruct *data, InodeDataStruct *inode_data,
                                                 const wchar_t *stream_name,
                                                 const ATTRIBUTE_TYPE stream_type, const BYTE *run_data,
                                                 const uint32_t run_data_length,
                                                 const uint64_t starting_vcn, const uint64_t bytes) {
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

    /* Sanity check. */
    if (data == nullptr || inode_data == nullptr) return FALSE;

    /* Find the stream in the list of streams. If not found then create a new stream. */
    for (stream = inode_data->streams_; stream != nullptr; stream = stream->next_) {
        if (stream->stream_type_ != stream_type) continue;
        if (stream_name == nullptr && stream->stream_name_ == nullptr) break;
        if (stream_name != nullptr && stream->stream_name_ != nullptr &&
            wcscmp(stream->stream_name_, stream_name) == 0)
            break;
    }

    if (stream == nullptr) {
        if (stream_name != nullptr) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Creating new stream: '%s:%s'",
                            stream_name, stream_type_names(stream_type));
        } else {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Creating new stream: ':%s'",
                            stream_type_names(stream_type));
        }

        stream = (StreamStruct *) malloc(sizeof(StreamStruct));

        if (stream == nullptr) {
            gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

            return FALSE;
        }

        stream->next_ = inode_data->streams_;

        inode_data->streams_ = stream;

        stream->stream_name_ = nullptr;

        if (stream_name != nullptr && wcslen(stream_name) > 0) {
            stream->stream_name_ = _wcsdup(stream_name);
        }

        stream->stream_type_ = stream_type;
        stream->fragments_ = nullptr;
        stream->clusters_ = 0;
        stream->bytes_ = bytes;
    } else {
        if (stream_name != nullptr) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            L"    Appending rundata to existing stream: '%s:%s",
                            stream_name, stream_type_names(stream_type));
        } else {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Appending rundata to existing stream: ':%s",
                            stream_type_names(stream_type));
        }

        if (stream->bytes_ == 0) stream->bytes_ = bytes;
    }

    /* If the stream already has a list of fragments then find the last fragment. */
    auto last_fragment = stream->fragments_;

    if (last_fragment != nullptr) {
        while (last_fragment->next_ != nullptr) last_fragment = last_fragment->next_;

        if (starting_vcn != last_fragment->next_vcn_) {
            gui->show_debug(
                    DebugLevel::Progress, nullptr,
                    L"Error: Inode %I64u already has a list of fragments. LastVcn=%I64u, StartingVCN=%I64u",
                    inode_data->inode_, last_fragment->next_vcn_, starting_vcn);

            return FALSE;
        }
    }

    /* Walk through the RunData and add the extents. */
    uint32_t index = 0;

    int64_t lcn = 0;
    int64_t vcn = starting_vcn;

    if (run_data != nullptr)
        while (run_data[index] != 0) {
            /* Decode the RunData and calculate the next Lcn. */
            const int run_length_size = run_data[index] & 0x0F;
            const int run_offset_size = (run_data[index] & 0xF0) >> 4;

            index++;

            if (index >= run_data_length) {
                gui->show_debug(DebugLevel::Progress, nullptr,
                                L"Error: datarun is longer than buffer, the MFT may be corrupt.",
                                inode_data->inode_);
                return FALSE;
            }

            run_length.value = 0;

            for (i = 0; i < run_length_size; i++) {
                run_length.bytes_[i] = run_data[index];

                index++;

                if (index >= run_data_length) {
                    gui->show_debug(DebugLevel::Progress, nullptr,
                                    L"Error: datarun is longer than buffer, the MFT may be corrupt.",
                                    inode_data->inode_);

                    return FALSE;
                }
            }

            run_offset.value = 0;

            for (i = 0; i < run_offset_size; i++) {
                run_offset.bytes_[i] = run_data[index];

                index++;

                if (index >= run_data_length) {
                    gui->show_debug(DebugLevel::Progress, nullptr,
                                    L"Error: datarun is longer than buffer, the MFT may be corrupt.",
                                    inode_data->inode_);

                    return FALSE;
                }
            }

            if (run_offset.bytes_[i - 1] >= 0x80) while (i < 8) run_offset.bytes_[i++] = 0xFF;

            lcn = lcn + run_offset.value;

            vcn = vcn + run_length.value;

            /* Show debug message. */
            if (run_offset.value != 0) {
                gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                                L"    Extent: Lcn=%I64u, Vcn=%I64u, NextVcn=%I64u", lcn,
                                vcn - run_length.value, vcn);
            } else {
                gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                                L"    Extent (virtual): Vcn=%I64u, NextVcn=%I64u", vcn - run_length.value,
                                vcn);
            }

            /* Add the size of the fragment to the total number of clusters.
            There are two kinds of fragments: real and virtual. The latter do not
            occupy clusters on disk, but are information used by compressed
            and sparse files. */

            if (run_offset.value != 0) {
                stream->clusters_ = stream->clusters_ + run_length.value;
            }

            /* Add the extent to the Fragments. */
            const auto new_fragment = (FragmentListStruct *) malloc(sizeof(FragmentListStruct));

            if (new_fragment == nullptr) {
                gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

                return FALSE;
            }

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

    return TRUE;
}

/*

Cleanup the Streams data in an InodeData struct. If CleanFragments is TRUE then
also cleanup the fragments.

*/
void ScanNTFS::cleanup_streams(InodeDataStruct *InodeData, BOOL CleanupFragments) {
    StreamStruct *Stream;
    StreamStruct *TempStream;

    FragmentListStruct *Fragment;
    FragmentListStruct *TempFragment;

    Stream = InodeData->streams_;

    while (Stream != nullptr) {
        if (Stream->stream_name_ != nullptr) free(Stream->stream_name_);

        if (CleanupFragments == TRUE) {
            Fragment = Stream->fragments_;

            while (Fragment != nullptr) {
                TempFragment = Fragment;
                Fragment = Fragment->next_;

                free(TempFragment);
            }
        }

        TempStream = Stream;
        Stream = Stream->next_;

        free(TempStream);
    }

    InodeData->streams_ = nullptr;
}

/* Construct the full stream name from the filename, the stream name, and the stream type. */
wchar_t *ScanNTFS::construct_stream_name(const wchar_t *file_name_1, const wchar_t *file_name_2, StreamStruct *stream) {
    size_t length;

    auto file_name = file_name_1;

    if (file_name == nullptr || wcslen(file_name) == 0) file_name = file_name_2;
    if (file_name != nullptr && wcslen(file_name) == 0) file_name = nullptr;

    wchar_t *stream_name = nullptr;
    ATTRIBUTE_TYPE stream_type = ATTRIBUTE_TYPE::AttributeInvalid;

    if (stream != nullptr) {
        stream_name = stream->stream_name_;

        if (stream_name != nullptr && wcslen(stream_name) == 0) stream_name = nullptr;

        stream_type = stream->stream_type_;
    }

    /* If the stream_name is empty and the stream_type is Data then return only the
    file_name. The Data stream is the default stream of regular files. */
    if ((stream_name == nullptr || wcslen(stream_name) == 0) && stream_type == ATTRIBUTE_TYPE::AttributeData) {
        if (file_name == nullptr || wcslen(file_name) == 0) return nullptr;

        return _wcsdup(file_name);
    }

    /* If the stream_name is "$I30" and the stream_type is AttributeIndexAllocation then
    return only the file_name. This must be a directory, and the Microsoft defragmentation
    API will automatically select this stream. */
    if (stream_name != nullptr &&
        wcscmp(stream_name, L"$I30") == 0 &&
        stream_type == ATTRIBUTE_TYPE::AttributeIndexAllocation) {
        if (file_name == nullptr || wcslen(file_name) == 0) return nullptr;

        return _wcsdup(file_name);
    }

    // If the stream_name is empty and the stream_type is Data then return only the
    // file_name. The Data stream is the default stream of regular files.
    if ((stream_name == nullptr || wcslen(stream_name) == 0)
        && wcslen(stream_type_names(stream_type)) == 0) {
        if (file_name == nullptr || wcslen(file_name) == 0) return nullptr;

        return _wcsdup(file_name);
    }

    length = 3;

    if (file_name != nullptr) length = length + wcslen(file_name);
    if (stream_name != nullptr) length = length + wcslen(stream_name);

    length = length + wcslen(stream_type_names(stream_type));

    if (length == 3) return nullptr;

    auto *p1 = (wchar_t *) malloc(sizeof(wchar_t) * length);

    if (p1 == nullptr) return nullptr;

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

/*

Process a list of attributes and store the gathered information in the Item
struct. Return FALSE if an error occurred.

*/
void ScanNTFS::process_attribute_list(
        DefragDataStruct *Data,
        NtfsDiskInfoStruct *DiskInfo,
        InodeDataStruct *InodeData,
        BYTE *Buffer,
        uint64_t BufLength,
        int Depth) {
    BYTE *Buffer2;

    ATTRIBUTE_LIST *Attribute;

    ULONG AttributeOffset;

    FILE_RECORD_HEADER *FileRecordHeader;
    FragmentListStruct *Fragment;

    uint64_t RefInode;
    uint64_t BaseInode;
    uint64_t Vcn;
    uint64_t RealVcn;
    uint64_t RefInodeVcn;

    OVERLAPPED gOverlapped;

    ULARGE_INTEGER Trans;

    DWORD BytesRead;

    int Result;

    wchar_t *p1;
    wchar_t s1[BUFSIZ];

    DefragGui *jkGui = DefragGui::get_instance();

    /* Sanity checks. */
    if (Buffer == nullptr || BufLength == 0) return;

    if (Depth > 1000) {
        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: infinite attribute loop, the MFT may be corrupt.");

        return;
    }

    jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                      L"    Processing AttributeList for Inode %I64u, %u bytes",
                      InodeData->inode_,
                      BufLength);

    /* Walk through all the attributes and gather information. */
    for (AttributeOffset = 0; AttributeOffset < BufLength; AttributeOffset = AttributeOffset + Attribute->length_) {
        Attribute = (ATTRIBUTE_LIST *) &Buffer[AttributeOffset];

        /* Exit if no more attributes. AttributeLists are usually not closed by the
        0xFFFFFFFF endmarker. Reaching the end of the buffer is therefore normal and
        not an error. */
        if (AttributeOffset + 3 > BufLength) break;
        if (*(ULONG *) Attribute == 0xFFFFFFFF) break;
        if (Attribute->length_ < 3) break;
        if (AttributeOffset + Attribute->length_ > BufLength) break;

        /* Extract the referenced Inode. If it's the same as the calling Inode then ignore
        (if we don't ignore then the program will loop forever, because for some
        reason the info in the calling Inode is duplicated here...). */
        RefInode = (uint64_t) Attribute->file_reference_number_.inode_number_low_part_ +
                   ((uint64_t) Attribute->file_reference_number_.inode_number_high_part_ << 32);

        if (RefInode == InodeData->inode_) continue;

        /* Show debug message. */
        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    List attribute: %s",
                          stream_type_names(Attribute->attribute_type_));
        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                          L"      LowestVcn = %I64u, RefInode = %I64u, InodeSequence = %u, Instance = %u",
                          Attribute->lowest_vcn_, RefInode, Attribute->file_reference_number_.sequence_number_,
                          Attribute->instance_);

        /* Extract the streamname. I don't know why AttributeLists can have names, and
        the name is not used further down. It is only extracted for debugging purposes.
        */
        if (Attribute->name_length_ > 0) {
            p1 = (wchar_t *) malloc(sizeof(wchar_t) * (Attribute->name_length_ + 1));

            if (p1 == nullptr) {
                jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

                return;
            }

            wcsncpy_s(p1, Attribute->name_length_ + 1,
                      (wchar_t *) &Buffer[AttributeOffset + Attribute->name_offset_], Attribute->name_length_);

            p1[Attribute->name_length_] = 0;

            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"      AttributeList name = '%s'", p1);

            free(p1);
        }

        /* Find the fragment in the MFT that contains the referenced Inode. */
        Vcn = 0;
        RealVcn = 0;
        RefInodeVcn = RefInode * DiskInfo->bytes_per_mft_record_ / (DiskInfo->bytes_per_sector_ * DiskInfo->
                sectors_per_cluster_);

        for (Fragment = InodeData->mft_data_fragments_; Fragment != nullptr; Fragment = Fragment->next_) {
            if (Fragment->lcn_ != VIRTUALFRAGMENT) {
                if (RefInodeVcn >= RealVcn && RefInodeVcn < RealVcn + Fragment->next_vcn_ - Vcn) {
                    break;
                }

                RealVcn = RealVcn + Fragment->next_vcn_ - Vcn;
            }

            Vcn = Fragment->next_vcn_;
        }

        if (Fragment == nullptr) {
            jkGui->show_debug(
                    DebugLevel::DetailedGapFinding, nullptr,
                    L"      Error: Inode %I64u is an extension of Inode %I64u, but does not exist (outside the MFT).",
                    RefInode, InodeData->inode_);

            continue;
        }

        /* Fetch the record of the referenced Inode from disk. */
        Buffer2 = (BYTE *) malloc((size_t) DiskInfo->bytes_per_mft_record_);

        if (Buffer2 == nullptr) {
            jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

            return;
        }

        Trans.QuadPart = (Fragment->lcn_ - RealVcn) * DiskInfo->bytes_per_sector_ *
                         DiskInfo->sectors_per_cluster_ + RefInode * DiskInfo->bytes_per_mft_record_;

        gOverlapped.Offset = Trans.LowPart;
        gOverlapped.OffsetHigh = Trans.HighPart;
        gOverlapped.hEvent = nullptr;

        Result = ReadFile(Data->disk_.volume_handle_, Buffer2, (uint32_t) DiskInfo->bytes_per_mft_record_, &BytesRead,
                          &gOverlapped);

        if (Result == 0 || BytesRead != DiskInfo->bytes_per_mft_record_) {
            defrag_lib_->system_error_str(GetLastError(), s1, BUFSIZ);

            jkGui->show_debug(DebugLevel::Progress, nullptr, L"      Error while reading Inode %I64u: %s", RefInode,
                              s1);

            free(Buffer2);

            return;
        }

        /* Fixup the raw data. */
        if (fixup_raw_mftdata(Data, DiskInfo, Buffer2, DiskInfo->bytes_per_mft_record_) == FALSE) {
            jkGui->show_debug(DebugLevel::Progress, nullptr, L"The error occurred while processing Inode %I64u",
                              RefInode);

            free(Buffer2);

            continue;
        }

        /* If the Inode is not in use then skip. */
        FileRecordHeader = (FILE_RECORD_HEADER *) Buffer2;

        if ((FileRecordHeader->flags_ & 1) != 1) {
            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"      Referenced Inode %I64u is not in use.",
                              RefInode);

            free(Buffer2);

            continue;
        }

        /* If the BaseInode inside the Inode is not the same as the calling Inode then skip. */
        BaseInode = (uint64_t) FileRecordHeader->base_file_record_.inode_number_low_part_ +
                    ((uint64_t) FileRecordHeader->base_file_record_.inode_number_high_part_ << 32);

        if (InodeData->inode_ != BaseInode) {
            jkGui->show_debug(
                    DebugLevel::DetailedGapFinding, nullptr,
                    L"      Warning: Inode %I64u is an extension of Inode %I64u, but thinks it's an extension of Inode %I64u.",
                    RefInode, InodeData->inode_, BaseInode);

            free(Buffer2);

            continue;
        }

        /* Process the list of attributes in the Inode, by recursively calling the process_attributes() subroutine. */
        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"      Processing Inode %I64u Instance %u",
                          RefInode,
                          Attribute->instance_);

        Result = process_attributes(Data, DiskInfo, InodeData,
                                    &Buffer2[FileRecordHeader->attribute_offset_],
                                    DiskInfo->bytes_per_mft_record_ - FileRecordHeader->attribute_offset_,
                                    Attribute->instance_, Depth + 1);

        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"      Finished processing Inode %I64u Instance %u",
                          RefInode,
                          Attribute->instance_);

        free(Buffer2);
    }
}

/* Process a list of attributes and store the gathered information in the Item
struct. Return FALSE if an error occurred. */
BOOL ScanNTFS::process_attributes(
        DefragDataStruct *Data,
        NtfsDiskInfoStruct *DiskInfo,
        InodeDataStruct *InodeData,
        BYTE *Buffer,
        uint64_t BufLength,
        USHORT Instance,
        int Depth) {
    BYTE *Buffer2;

    uint64_t Buffer2Length;
    ULONG AttributeOffset;

    ATTRIBUTE *Attribute;
    RESIDENT_ATTRIBUTE *ResidentAttribute;
    NONRESIDENT_ATTRIBUTE *NonResidentAttribute;
    STANDARD_INFORMATION *StandardInformation;
    FILENAME_ATTRIBUTE *FileNameAttribute;

    wchar_t *p1;

    DefragGui *jkGui = DefragGui::get_instance();

    /* Walk through all the attributes and gather information. AttributeLists are
    skipped and interpreted later. */
    for (AttributeOffset = 0; AttributeOffset < BufLength; AttributeOffset = AttributeOffset + Attribute->length_) {
        Attribute = (ATTRIBUTE *) &Buffer[AttributeOffset];

        /* Exit the loop if end-marker. */
        if (AttributeOffset + 4 <= BufLength && *(ULONG *) Attribute == 0xFFFFFFFF) break;

        /* Sanity check. */
        if (AttributeOffset + 4 > BufLength ||
            Attribute->length_ < 3 ||
            AttributeOffset + Attribute->length_ > BufLength) {
            jkGui->show_debug(
                    DebugLevel::Progress, nullptr,
                    L"Error: attribute in Inode %I64u is bigger than the data, the MFT may be corrupt.",
                    InodeData->inode_);
            jkGui->show_debug(DebugLevel::Progress, nullptr,
                              L"  BufLength=%I64u, AttributeOffset=%lu, AttributeLength=%u(%X)",
                              BufLength, AttributeOffset, Attribute->length_, Attribute->length_);

            defrag_lib_->show_hex(Data, Buffer, BufLength);

            return FALSE;
        }

        /* Skip AttributeList's for now. */
        if (Attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeAttributeList) continue;

        /* If the Instance does not equal the AttributeNumber then ignore the attribute.
        This is used when an AttributeList is being processed and we only want a specific
        instance. */
        if (Instance != 65535 && Instance != Attribute->attribute_number_) continue;

        /* Show debug message. */
        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"  Attribute %u: %s", Attribute->attribute_number_,
                          stream_type_names(Attribute->attribute_type_));

        if (Attribute->nonresident_ == 0) {
            ResidentAttribute = (RESIDENT_ATTRIBUTE *) Attribute;

            /* The AttributeFileName (0x30) contains the filename and the link to the parent directory. */
            if (Attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeFileName) {
                FileNameAttribute = (FILENAME_ATTRIBUTE *) &Buffer[AttributeOffset + ResidentAttribute->
                        value_offset_];

                InodeData->parent_inode_ = FileNameAttribute->parent_directory_.inode_number_low_part_ +
                                           ((uint64_t) FileNameAttribute->parent_directory_.inode_number_high_part_
                                                   << 32);

                if (FileNameAttribute->name_length_ > 0) {
                    /* Extract the filename. */
                    p1 = (wchar_t *) malloc(sizeof(wchar_t) * (FileNameAttribute->name_length_ + 1));

                    if (p1 == nullptr) {
                        jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

                        return FALSE;
                    }

                    wcsncpy_s(p1, FileNameAttribute->name_length_ + 1, FileNameAttribute->name_,
                              FileNameAttribute->name_length_);

                    p1[FileNameAttribute->name_length_] = 0;

                    /* Save the filename in either the Long or the Short filename. We only
                    save the first filename, any additional filenames are hard links. They
                    might be useful for an optimization algorithm that sorts by filename,
                    but which of the hardlinked names should it sort? So we only store the
                    first filename. */
                    if (FileNameAttribute->name_type_ == 2) {
                        if (InodeData->short_filename_ != nullptr) {
                            free(p1);
                        } else {
                            InodeData->short_filename_ = p1;

                            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Short filename = '%s'",
                                              p1);
                        }
                    } else {
                        if (InodeData->long_filename_ != nullptr) {
                            free(p1);
                        } else {
                            InodeData->long_filename_ = p1;

                            jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"    Long filename = '%s'", p1);
                        }
                    }
                }
            }

            /* The AttributeStandardInformation (0x10) contains the CreationTime, LastAccessTime,
            the MftChangeTime, and the file attributes. */
            if (Attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeStandardInformation) {
                StandardInformation = (STANDARD_INFORMATION *) &Buffer[AttributeOffset + ResidentAttribute->
                        value_offset_];

                InodeData->creation_time_ = StandardInformation->creation_time_;
                InodeData->mft_change_time_ = StandardInformation->mft_change_time_;
                InodeData->last_access_time_ = StandardInformation->last_access_time_;
            }

            /* The value of the AttributeData (0x80) is the actual data of the file. */
            if (Attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeData) {
                InodeData->bytes_ = ResidentAttribute->value_length_;
            }
        } else {
            NonResidentAttribute = (NONRESIDENT_ATTRIBUTE *) Attribute;

            /* Save the length (number of bytes) of the data. */
            if (Attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeData && InodeData->bytes_ == 0) {
                InodeData->bytes_ = NonResidentAttribute->data_size_;
            }

            /* Extract the streamname. */
            p1 = nullptr;

            if (Attribute->name_length_ > 0) {
                p1 = (wchar_t *) malloc(sizeof(wchar_t) * (Attribute->name_length_ + 1));

                if (p1 == nullptr) {
                    jkGui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

                    return FALSE;
                }

                wcsncpy_s(p1, Attribute->name_length_ + 1,
                          (wchar_t *) &Buffer[AttributeOffset + Attribute->name_offset_],
                          Attribute->name_length_);

                p1[Attribute->name_length_] = 0;
            }

            /* Create a new stream with a list of fragments for this data. */
            translate_rundata_to_fragmentlist(Data, InodeData, p1, Attribute->attribute_type_,
                                              (BYTE *) &Buffer[AttributeOffset +
                                                               NonResidentAttribute->run_array_offset_],
                                              Attribute->length_ - NonResidentAttribute->run_array_offset_,
                                              NonResidentAttribute->starting_vcn_, NonResidentAttribute->data_size_);

            /* Cleanup the streamname. */
            if (p1 != nullptr) free(p1);

            /* Special case: If this is the $MFT then save data. */
            if (InodeData->inode_ == 0) {
                if (Attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeData && InodeData->mft_data_fragments_ ==
                                                                                   nullptr) {
                    InodeData->mft_data_fragments_ = InodeData->streams_->fragments_;
                    InodeData->mft_data_bytes_ = NonResidentAttribute->data_size_;
                }

                if (Attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeBitmap && InodeData->mft_bitmap_fragments_
                                                                                     == nullptr) {
                    InodeData->mft_bitmap_fragments_ = InodeData->streams_->fragments_;
                    InodeData->mft_bitmap_bytes_ = NonResidentAttribute->data_size_;
                }
            }
        }
    }

    /* Walk through all the attributes and interpret the AttributeLists. We have to
    do this after the DATA and BITMAP attributes have been interpreted, because
    some MFT's have an AttributeList that is stored in fragments that are
    defined in the DATA attribute, and/or contain a continuation of the DATA or
    BITMAP attributes. */
    for (AttributeOffset = 0; AttributeOffset < BufLength; AttributeOffset = AttributeOffset + Attribute->length_) {
        Attribute = (ATTRIBUTE *) &Buffer[AttributeOffset];

        if (*(ULONG *) Attribute == 0xFFFFFFFF) break;
        if (Attribute->attribute_type_ != ATTRIBUTE_TYPE::AttributeAttributeList) continue;

        jkGui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"  Attribute %u: %s", Attribute->attribute_number_,
                          stream_type_names(Attribute->attribute_type_));

        if (Attribute->nonresident_ == 0) {
            ResidentAttribute = (RESIDENT_ATTRIBUTE *) Attribute;

            process_attribute_list(Data, DiskInfo, InodeData,
                                   (BYTE *) &Buffer[AttributeOffset + ResidentAttribute->value_offset_],
                                   ResidentAttribute->value_length_, Depth);
        } else {
            NonResidentAttribute = (NONRESIDENT_ATTRIBUTE *) Attribute;
            Buffer2Length = NonResidentAttribute->data_size_;

            Buffer2 = read_non_resident_data(Data, DiskInfo,
                                             (BYTE *) &Buffer[AttributeOffset +
                                                              NonResidentAttribute->run_array_offset_],
                                             Attribute->length_ - NonResidentAttribute->run_array_offset_, 0,
                                             Buffer2Length);

            process_attribute_list(Data, DiskInfo, InodeData, Buffer2, Buffer2Length, Depth);

            free(Buffer2);
        }
    }

    return TRUE;
}

BOOL ScanNTFS::interpret_mft_record(DefragDataStruct *data, NtfsDiskInfoStruct *disk_info, ItemStruct **inode_array,
                                    uint64_t inode_number, uint64_t max_inode,
                                    FragmentListStruct **mft_data_fragments, uint64_t *mft_data_bytes,
                                    FragmentListStruct **mft_bitmap_fragments, uint64_t *MftBitmapBytes,
                                    BYTE *buffer, uint64_t buf_length) {
    InodeDataStruct inode_data{};
    ItemStruct *item;
    StreamStruct *stream;
    uint64_t base_inode;
    DefragGui *gui = DefragGui::get_instance();

    /* If the record is not in use then quietly exit. */
    const FILE_RECORD_HEADER *file_record_header = (FILE_RECORD_HEADER *) buffer;

    if ((file_record_header->flags_ & 1) != 1) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Inode %I64u is not in use.", inode_number);

        return FALSE;
    }

    /* If the record has a BaseFileRecord then ignore it. It is used by an
    AttributeAttributeList as an extension of another Inode, it's not an
    Inode by itself. */
    base_inode = (uint64_t) file_record_header->base_file_record_.inode_number_low_part_ +
                 ((uint64_t) file_record_header->base_file_record_.inode_number_high_part_ << 32);

    if (base_inode != 0) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        L"Ignoring Inode %I64u, it's an extension of Inode %I64u", inode_number, base_inode);

        return TRUE;
    }

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Processing Inode %I64u...", inode_number);

    /* Show a warning if the Flags have an unknown value. */
    if ((file_record_header->flags_ & 252) != 0) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"  Inode %I64u has Flags = %u", inode_number,
                        file_record_header->flags_);
    }

    /* I think the MFTRecordNumber should always be the inode_number, but it's an XP
    extension and I'm not sure about Win2K.
    Note: why is the MFTRecordNumber only 32 bit? Inode numbers are 48 bit. */
    if (file_record_header->mft_record_number_ != inode_number) {
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        L"  Warning: Inode %I64u contains a different MFTRecordNumber %lu",
                        inode_number, file_record_header->mft_record_number_);
    }

    /* Sanity check. */
    if (file_record_header->attribute_offset_ >= buf_length) {
        gui->show_debug(
                DebugLevel::Progress, nullptr,
                L"Error: attributes in Inode %I64u are outside the FILE record, the MFT may be corrupt.",
                inode_number);

        return FALSE;
    }

    if (file_record_header->bytes_in_use_ > buf_length) {
        gui->show_debug(
                DebugLevel::Progress, nullptr,
                L"Error: in Inode %I64u the record is bigger than the size of the buffer, the MFT may be corrupt.",
                inode_number);

        return FALSE;
    }

    /* Initialize the InodeData struct. */
    inode_data.inode_ = inode_number; /* The Inode number. */
    inode_data.parent_inode_ = 5; /* The Inode number of the parent directory. */
    inode_data.is_directory_ = false;

    if ((file_record_header->flags_ & 2) == 2) inode_data.is_directory_ = true;

    inode_data.long_filename_ = nullptr; /* Long filename. */
    inode_data.short_filename_ = nullptr; /* Short filename (8.3 DOS). */
    inode_data.creation_time_ = 0; /* 1 second = 10000000 */
    inode_data.mft_change_time_ = 0;
    inode_data.last_access_time_ = 0;
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

    /* Interpret the attributes. */
    [[maybe_unused]] int result = process_attributes(data, disk_info, &inode_data,
                                                     &buffer[file_record_header->attribute_offset_],
                                                     buf_length - file_record_header->attribute_offset_, 65535, 0);

    /* Save the mft_data_fragments, mft_data_bytes, mft_bitmap_fragments, and MftBitmapBytes. */
    if (inode_number == 0) {
        *mft_data_fragments = inode_data.mft_data_fragments_;
        *mft_data_bytes = inode_data.mft_data_bytes_;
        *mft_bitmap_fragments = inode_data.mft_bitmap_fragments_;
        *MftBitmapBytes = inode_data.mft_bitmap_bytes_;
    }

    /* Create an item in the data->ItemTree for every stream. */
    stream = inode_data.streams_;
    do {
        /* Create and fill a new item record in memory. */
        item = new ItemStruct();

        if (item == nullptr) {
            gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

            if (inode_data.long_filename_ != nullptr) free(inode_data.long_filename_);
            if (inode_data.short_filename_ != nullptr) free(inode_data.short_filename_);

            cleanup_streams(&inode_data, TRUE);

            return FALSE;
        }

//        item->long_filename_ = construct_stream_name(inode_data.long_filename_, inode_data.short_filename_, stream);
//        item->long_path_ = nullptr;
//        item->short_filename_ = construct_stream_name(inode_data.short_filename_, inode_data.long_filename_, stream);
//        item->short_path_ = nullptr;
        item->set_names(L"", construct_stream_name(inode_data.long_filename_, inode_data.short_filename_, stream),
                        nullptr, construct_stream_name(inode_data.short_filename_, inode_data.long_filename_, stream));

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

    /* Cleanup and return TRUE. */
    if (inode_data.long_filename_ != nullptr) free(inode_data.long_filename_);
    if (inode_data.short_filename_ != nullptr) free(inode_data.short_filename_);

    cleanup_streams(&inode_data, FALSE);

    return TRUE;
}

/* Load the MFT into a list of ItemStruct records in memory. */
BOOL ScanNTFS::analyze_ntfs_volume(DefragDataStruct *data) {
    NtfsDiskInfoStruct disk_info{};
    BYTE *buffer;
    OVERLAPPED g_overlapped;
    ULARGE_INTEGER trans;
    DWORD bytes_read;
    FragmentListStruct *mft_data_fragments;
    uint64_t mft_data_bytes;
    FragmentListStruct *mft_bitmap_fragments;
    uint64_t mft_bitmap_bytes;
    uint64_t max_mft_bitmap_bytes;
    BYTE *mft_bitmap;
    FragmentListStruct *fragment;
    ItemStruct **inode_array;
    uint64_t max_inode;
    ItemStruct *item;
    uint64_t vcn;
    uint64_t real_vcn;
    uint64_t inode_number;
    uint64_t BlockStart;
    uint64_t BlockEnd;

    BYTE BitmapMasks[8] = {1, 2, 4, 8, 16, 32, 64, 128};

    int result;
    ULONG clusters_per_mft_record;
    __timeb64 time{};
    int64_t start_time;
    int64_t end_time;
    wchar_t s1[BUFSIZ];
    uint64_t u1;
    DefragGui *gui = DefragGui::get_instance();

    /* Read the boot block from the disk. */
    buffer = (BYTE *) malloc(mftbuffersize);

    if (buffer == nullptr) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

        return FALSE;
    }

    g_overlapped.Offset = 0;
    g_overlapped.OffsetHigh = 0;
    g_overlapped.hEvent = nullptr;

    result = ReadFile(data->disk_.volume_handle_, buffer, (uint32_t) 512, &bytes_read, &g_overlapped);

    if (result == 0 || bytes_read != 512) {
        defrag_lib_->system_error_str(GetLastError(), s1, BUFSIZ);

        gui->show_debug(DebugLevel::Progress, nullptr, L"Error while reading bootblock: %s", s1);

        free(buffer);

        return FALSE;
    }

    /* Test if the boot block is an NTFS boot block. */
    if (*(ULONGLONG *) &buffer[3] != 0x202020205346544E) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"This is not an NTFS disk (different cookie).");

        free(buffer);

        return FALSE;
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

    gui->show_debug(DebugLevel::Fatal, nullptr, L"This is an NTFS disk.");
    gui->show_debug(DebugLevel::Progress, nullptr, L"  Disk cookie: %I64X", *(ULONGLONG *) &buffer[3]);
    gui->show_debug(DebugLevel::Progress, nullptr, L"  BytesPerSector: %I64u", disk_info.bytes_per_sector_);
    gui->show_debug(DebugLevel::Progress, nullptr, L"  TotalSectors: %I64u", disk_info.total_sectors_);
    gui->show_debug(DebugLevel::Progress, nullptr, L"  SectorsPerCluster: %I64u", disk_info.sectors_per_cluster_);
    gui->show_debug(DebugLevel::Progress, nullptr, L"  SectorsPerTrack: %lu", *(USHORT *) &buffer[24]);
    gui->show_debug(DebugLevel::Progress, nullptr, L"  NumberOfHeads: %lu", *(USHORT *) &buffer[26]);
    gui->show_debug(DebugLevel::Progress, nullptr, L"  MftStartLcn: %I64u", disk_info.mft_start_lcn_);
    gui->show_debug(DebugLevel::Progress, nullptr, L"  Mft2StartLcn: %I64u", disk_info.mft2_start_lcn_);
    gui->show_debug(DebugLevel::Progress, nullptr, L"  BytesPerMftRecord: %I64u", disk_info.bytes_per_mft_record_);
    gui->show_debug(DebugLevel::Progress, nullptr, L"  ClustersPerIndexRecord: %I64u",
                    disk_info.clusters_per_index_record_);
    gui->show_debug(DebugLevel::Progress, nullptr, L"  MediaType: %X", buffer[21]);
    gui->show_debug(DebugLevel::Progress, nullptr, L"  VolumeSerialNumber: %I64X", *(ULONGLONG *) &buffer[72]);

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
    result = ReadFile(data->disk_.volume_handle_, buffer, (uint32_t) disk_info.bytes_per_mft_record_, &bytes_read,
                      &g_overlapped);

    if (result == 0 || bytes_read != disk_info.bytes_per_mft_record_) {
        defrag_lib_->system_error_str(GetLastError(), s1, BUFSIZ);

        gui->show_debug(DebugLevel::Progress, nullptr, L"Error while reading first MFT record: %s", s1);

        free(buffer);

        return FALSE;
    }

    /* Fixup the raw data from disk. This will also test if it's a valid $MFT record. */
    if (fixup_raw_mftdata(data, &disk_info, buffer, disk_info.bytes_per_mft_record_) == FALSE) {
        free(buffer);

        return FALSE;
    }

    /* Extract data from the MFT record and put into an Item struct in memory. If
    there was an error then exit. */
    mft_data_bytes = 0;
    mft_data_fragments = nullptr;
    mft_bitmap_bytes = 0;
    mft_bitmap_fragments = nullptr;

    result = interpret_mft_record(data, &disk_info, nullptr, 0, 0, &mft_data_fragments, &mft_data_bytes,
                                  &mft_bitmap_fragments, &mft_bitmap_bytes, buffer, disk_info.bytes_per_mft_record_);

    if (result == FALSE ||
        mft_data_fragments == nullptr || mft_data_bytes == 0 ||
        mft_bitmap_fragments == nullptr || mft_bitmap_bytes == 0) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"Fatal error, cannot process this disk.");

        free(buffer);

        DefragLib::delete_item_tree(data->item_tree_);

        data->item_tree_ = nullptr;

        return FALSE;
    }

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"MftDataBytes = %I64u, MftBitmapBytes = %I64u",
                    mft_data_bytes, mft_bitmap_bytes);

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

    mft_bitmap = (BYTE *) malloc((size_t) max_mft_bitmap_bytes);

    if (mft_bitmap == nullptr) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

        free(buffer);

        DefragLib::delete_item_tree(data->item_tree_);

        data->item_tree_ = nullptr;

        return FALSE;
    }

    memset(mft_bitmap, 0, (size_t) mft_bitmap_bytes);

    vcn = 0;
    real_vcn = 0;

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Reading $MFT::$BITMAP into memory");

    for (fragment = mft_bitmap_fragments; fragment != nullptr; fragment = fragment->next_) {
        if (fragment->lcn_ != VIRTUALFRAGMENT) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"  Extent Lcn=%I64u, RealVcn=%I64u, Size=%I64u",
                            fragment->lcn_, real_vcn, fragment->next_vcn_ - vcn);

            trans.QuadPart = fragment->lcn_ * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_;

            g_overlapped.Offset = trans.LowPart;
            g_overlapped.OffsetHigh = trans.HighPart;
            g_overlapped.hEvent = nullptr;

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            L"    Reading %I64u clusters (%I64u bytes) from LCN=%I64u",
                            fragment->next_vcn_ - vcn,
                            (fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_,
                            fragment->lcn_);

            result = ReadFile(data->disk_.volume_handle_,
                              &mft_bitmap[real_vcn * disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_],
                              (uint32_t) ((fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ * disk_info.
                                      sectors_per_cluster_),
                              &bytes_read, &g_overlapped);

            if (result == 0 || bytes_read != (fragment->next_vcn_ - vcn) * disk_info.bytes_per_sector_ * disk_info.
                    sectors_per_cluster_) {
                defrag_lib_->system_error_str(GetLastError(), s1, BUFSIZ);

                gui->show_debug(DebugLevel::Progress, nullptr, L"  %s", s1);

                free(mft_bitmap);
                free(buffer);

                DefragLib::delete_item_tree(data->item_tree_);

                data->item_tree_ = nullptr;

                return FALSE;
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

    inode_array = (ItemStruct **) malloc((size_t) (max_inode * sizeof(ItemStruct *)));

    if (inode_array == nullptr) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"Error: malloc() returned nullptr.");

        free(buffer);

        DefragLib::delete_item_tree(data->item_tree_);

        data->item_tree_ = nullptr;

        return FALSE;
    }

    inode_array[0] = data->item_tree_;

    for (inode_number = 1; inode_number < max_inode; inode_number++) {
        inode_array[inode_number] = nullptr;
    }

    /* Read and process all the records in the MFT. The records are read into a
    buffer and then given one by one to the interpret_mft_record() subroutine. */
    fragment = mft_data_fragments;
    BlockEnd = 0;
    vcn = 0;
    real_vcn = 0;

    data->phase_done_ = 0;
    data->phase_todo_ = 0;

    _ftime64_s(&time);

    start_time = time.time * 1000 + time.millitm;

    for (inode_number = 1; inode_number < max_inode; inode_number++) {
        if ((mft_bitmap[inode_number >> 3] & BitmapMasks[inode_number % 8]) == 0) continue;

        data->phase_todo_ = data->phase_todo_ + 1;
    }

    for (inode_number = 1; inode_number < max_inode; inode_number++) {
        if (*data->running_ != RunningState::RUNNING) break;

        /* Ignore the Inode if the bitmap says it's not in use. */
        if ((mft_bitmap[inode_number >> 3] & BitmapMasks[inode_number % 8]) == 0) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, L"Inode %I64u is not in use.", inode_number);

            continue;
        }

        /* Update the progress counter. */
        data->phase_done_ = data->phase_done_ + 1;

        /* Read a block of inode's into memory. */
        if (inode_number >= BlockEnd) {
            /* Slow the program down to the percentage that was specified on the command line. */
            DefragLib::slow_down(data);

            BlockStart = inode_number;
            BlockEnd = BlockStart + mftbuffersize / disk_info.bytes_per_mft_record_;

            if (BlockEnd > mft_bitmap_bytes * 8) BlockEnd = mft_bitmap_bytes * 8;

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

                gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                                L"  Extent Lcn=%I64u, RealVcn=%I64u, Size=%I64u",
                                fragment->lcn_, real_vcn, fragment->next_vcn_ - vcn);
            }
            if (fragment == nullptr) break;
            if (BlockEnd >= u1) BlockEnd = u1;

            trans.QuadPart = (fragment->lcn_ - real_vcn) * disk_info.bytes_per_sector_ *
                             disk_info.sectors_per_cluster_ + BlockStart * disk_info.bytes_per_mft_record_;

            g_overlapped.Offset = trans.LowPart;
            g_overlapped.OffsetHigh = trans.HighPart;
            g_overlapped.hEvent = nullptr;

            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            L"Reading block of %I64u Inodes from MFT into memory, %u bytes from LCN=%I64u",
                            BlockEnd - BlockStart,
                            (uint32_t) ((BlockEnd - BlockStart) * disk_info.bytes_per_mft_record_),
                            trans.QuadPart / (disk_info.bytes_per_sector_ * disk_info.sectors_per_cluster_));

            result = ReadFile(data->disk_.volume_handle_, buffer,
                              (uint32_t) ((BlockEnd - BlockStart) * disk_info.bytes_per_mft_record_), &bytes_read,
                              &g_overlapped);

            if (result == 0 || bytes_read != (BlockEnd - BlockStart) * disk_info.bytes_per_mft_record_) {
                defrag_lib_->system_error_str(GetLastError(), s1, BUFSIZ);

                gui->show_debug(DebugLevel::Progress, nullptr, L"Error while reading Inodes %I64u to %I64u: %s",
                                inode_number,
                                BlockEnd - 1, s1);

                free(buffer);
                free(inode_array);

                DefragLib::delete_item_tree(data->item_tree_);

                data->item_tree_ = nullptr;

                return FALSE;
            }
        }

        /* Fixup the raw data of this Inode. */
        if (fixup_raw_mftdata(data, &disk_info, &buffer[(inode_number - BlockStart) * disk_info.bytes_per_mft_record_],
                              disk_info.bytes_per_mft_record_) == FALSE) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            L"The error occurred while processing Inode %I64u (max %I64u)",
                            inode_number, max_inode);

            continue;
        }

        /* Interpret the Inode's attributes. */
        result = interpret_mft_record(data, &disk_info, inode_array, inode_number, max_inode,
                                      &mft_data_fragments, &mft_data_bytes, &mft_bitmap_fragments, &mft_bitmap_bytes,
                                      &buffer[(inode_number - BlockStart) * disk_info.bytes_per_mft_record_],
                                      disk_info.bytes_per_mft_record_);
    }

    _ftime64_s(&time);

    end_time = time.time * 1000 + time.millitm;

    if (end_time > start_time) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"  Analysis speed: %I64u items per second",
                        max_inode * 1000 / (end_time - start_time));
    }

    free(buffer);

    if (mft_bitmap != nullptr) free(mft_bitmap);

    if (*data->running_ != RunningState::RUNNING) {
        free(inode_array);

        DefragLib::delete_item_tree(data->item_tree_);

        data->item_tree_ = nullptr;

        return FALSE;
    }

    /* Setup the ParentDirectory in all the items with the info in the InodeArray. */
    for (item = DefragLib::tree_smallest(data->item_tree_); item != nullptr; item = DefragLib::tree_next(item)) {
        item->parent_directory_ = inode_array[item->parent_inode_];

        if (item->parent_inode_ == 5) item->parent_directory_ = nullptr;
    }

    free(inode_array);

    return TRUE;
}

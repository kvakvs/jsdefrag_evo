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

/**
 * \brief Process a list of attributes and store the gathered information in the Item struct. Return FALSE if an error occurred.
 */
void
ScanNTFS::process_attribute_list(DefragState &data, NtfsDiskInfoStruct *disk_info, InodeDataStruct *inode_data,
                                 BYTE *buffer, const uint64_t buf_length, const int depth) {
    std::unique_ptr<BYTE[]> buffer_2;
    ATTRIBUTE_LIST *attribute;
    OVERLAPPED g_overlapped;
    DWORD bytes_read;
    DefragGui *gui = DefragGui::get_instance();

    // Sanity checks
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

        // Show debug message
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

        // Find the fragment in the MFT that contains the referenced Inode
        uint64_t vcn = 0;
        uint64_t real_vcn = 0;
        const uint64_t ref_inode_vcn = ref_inode * disk_info->bytes_per_mft_record_ /
                                       (disk_info->bytes_per_sector_ * disk_info->sectors_per_cluster_);

        auto fragment = inode_data->mft_data_fragments_.begin();

        for (; fragment != inode_data->mft_data_fragments_.end(); fragment++) {
            if (!fragment->is_virtual()) {
                if (ref_inode_vcn >= real_vcn && ref_inode_vcn < real_vcn + fragment->next_vcn_ - vcn) {
                    break;
                }

                real_vcn = real_vcn + fragment->next_vcn_ - vcn;
            }

            vcn = fragment->next_vcn_;
        }

        if (fragment == inode_data->mft_data_fragments_.end()) {
            gui->show_debug(
                    DebugLevel::DetailedGapFinding, nullptr,
                    std::format(
                            L"      Error: Inode " NUM_FMT " is an extension of Inode " NUM_FMT ", but does not exist (outside the MFT).",
                            ref_inode, inode_data->inode_));

            continue;
        }

        // Fetch the record of the referenced Inode from disk
        buffer_2 = std::make_unique<BYTE[]>((size_t) disk_info->bytes_per_mft_record_);

        ULARGE_INTEGER trans;
        trans.QuadPart = ((fragment->lcn_ - real_vcn)
                          * disk_info->bytes_per_sector_
                          * disk_info->sectors_per_cluster_)
                         + ref_inode * disk_info->bytes_per_mft_record_;

        g_overlapped.Offset = trans.LowPart;
        g_overlapped.OffsetHigh = trans.HighPart;
        g_overlapped.hEvent = nullptr;

        int result = ReadFile(data.disk_.volume_handle_, buffer_2.get(), (uint32_t) disk_info->bytes_per_mft_record_,
                              &bytes_read,
                              &g_overlapped);

        if (result == 0 || bytes_read != disk_info->bytes_per_mft_record_) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"      Error while reading Inode " NUM_FMT ": reason {}", ref_inode,
                                        Str::system_error(GetLastError())));
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

// Process a list of attributes and store the gathered information in the Item
// struct. Return FALSE if an error occurred
bool ScanNTFS::process_attributes(DefragState &data, NtfsDiskInfoStruct *disk_info,
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

        // Exit the loop if end-marker
        if (attribute_offset + 4 <= buf_length && *(ULONG *) attribute == 0xFFFFFFFF) break;

        // Sanity check
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

            DefragRunner::show_hex(data, buffer, buf_length);

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

            // The value of the AttributeData (0x80) is the actual data of the file
            if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeData) {
                inode_data->bytes_ = resident_attribute->value_length_;
            }
        } else {
            nonresident_attribute = (NONRESIDENT_ATTRIBUTE *) attribute;

            // Save the length (number of bytes) of the data
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

            // Special case: If this is the $MFT (intent: to save data,
            // but for real with STL data structures, there's not much saving now)
            if (inode_data->inode_ == 0) {
                if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeData
                    && inode_data->mft_data_fragments_.empty()) {
                    inode_data->mft_data_fragments_ = inode_data->streams_.begin()->fragments_;
                    inode_data->mft_data_bytes_ = nonresident_attribute->data_size_;
                }

                if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeBitmap
                    && inode_data->mft_bitmap_fragments_.empty()) {
                    inode_data->mft_bitmap_fragments_ = inode_data->streams_.begin()->fragments_;
                    inode_data->mft_bitmap_bytes_ = nonresident_attribute->data_size_;
                }
            }
        }
    }

    // Walk through all the attributes and interpret the AttributeLists. We have to do this after the DATA and BITMAP
    // attributes have been interpreted, because some MFT's have an AttributeList that is stored in fragments that are
    // defined in the DATA attribute, and/or contain a continuation of the DATA or BITMAP attributes.
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

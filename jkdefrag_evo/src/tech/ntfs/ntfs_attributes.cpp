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
                                 const MemSlice &buffer, const int depth) {
    ATTRIBUTE_LIST *attribute;
    OVERLAPPED g_overlapped;
    DWORD bytes_read;
    DefragGui *gui = DefragGui::get_instance();

    // Sanity checks
    if (!buffer || buffer.is_empty()) return;

    if (depth > 1000) {
        gui->show_debug(DebugLevel::Progress, nullptr, L"Error: infinite attribute loop, the MFT may be corrupt.");
        return;
    }

    gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                    std::format(L"    Processing AttributeList for Inode {}, " NUM_FMT " bytes",
                                inode_data->inode_, buffer.length()));

    // Walk through all the attributes and gather information
    for (Bytes64 attribute_offset = {};
         attribute_offset < buffer.length();
         attribute_offset += Bytes64(attribute->length_)) {

        // attribute = (ATTRIBUTE_LIST *) &buffer[attribute_offset.value()];
        attribute = buffer.ptr_to<ATTRIBUTE_LIST>(attribute_offset);

        // Exit if no more attributes. AttributeLists are usually not closed by the 0xFFFFFFFF endmarker. Reaching the
        // end of the buffer is therefore normal and not an error.
        if (attribute_offset + Bytes64(3) > buffer.length()) break;
        if (*(ULONG *) attribute == 0xFFFFFFFF) break;
        if (attribute->length_ < 3) break;
        if (attribute_offset + Bytes64(attribute->length_) > buffer.length()) break;

        // Extract the referenced Inode. If it's the same as the calling Inode then ignore (if we don't ignore, then the
        // program will loop forever, because for some reason the info in the calling Inode is duplicated here...).
        const Inode64 ref_inode = Inode64((uint64_t) attribute->file_reference_number_.inode_number_low_part_ +
                                          ((uint64_t) attribute->file_reference_number_.inode_number_high_part_ << 32));

        if (ref_inode == inode_data->inode_) continue;

        // Show debug message
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
                L"    List attribute: {}", stream_type_names(attribute->attribute_type_)
        ));
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
                L"      LowestVcn = " NUM_FMT ", ref_inode = {}, InodeSequence = " NUM_FMT ", Instance = " NUM_FMT,
                attribute->lowest_vcn_, ref_inode,
                attribute->file_reference_number_.sequence_number_,
                attribute->instance_
        ));

        // Extract the streamname. I don't know why AttributeLists can have names, and the name is not used further
        // down. It is only extracted for debugging purposes.
        if (attribute->name_length_ > 0) {
            auto p1 = std::make_unique<wchar_t[]>(attribute->name_length_ + 1);

            wcsncpy_s(p1.get(), attribute->name_length_ + 1,
                      buffer.ptr_to<wchar_t>(attribute_offset + Bytes64(attribute->name_offset_)),
                      attribute->name_length_);

            p1.get()[attribute->name_length_] = 0;
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                            std::format(L"      AttributeList name = '{}'", p1.get()));
        }

        // Find the fragment in the MFT that contains the referenced Inode
        Clusters64 vcn = {};
        Clusters64 real_vcn = {};

        // Unit: 'inode * 'bytes_per_mft / ('bytes per sector * 'sectors per cluster)
        const Clusters64 ref_inode_vcn = Clusters64(
                ref_inode.value() * disk_info->bytes_per_mft_record_.value() /
                (disk_info->bytes_per_sector_.value() *
                 disk_info->sectors_per_cluster_.value())
        );

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
                            ref_inode.value(), inode_data->inode_.value())
            );

            continue;
        }

        // Fetch the record of the referenced Inode from disk
        auto buffer_2 = UniquePtrSlice::make_new(disk_info->bytes_per_mft_record_);

        ULARGE_INTEGER trans;
        trans.QuadPart = ((fragment->lcn_.as<ULONGLONG>() - real_vcn.as<ULONGLONG>())
                          * disk_info->bytes_per_sector_.as<ULONGLONG>()
                          * disk_info->sectors_per_cluster_.as<ULONGLONG>())
                         + ref_inode.as<ULONGLONG>() * disk_info->bytes_per_mft_record_.as<ULONGLONG>();

        g_overlapped.Offset = trans.LowPart;
        g_overlapped.OffsetHigh = trans.HighPart;
        g_overlapped.hEvent = nullptr;

        int result = ReadFile(data.disk_.volume_handle_, buffer_2.get(), disk_info->bytes_per_mft_record_.as<DWORD>(),
                              &bytes_read, &g_overlapped);

        if (result == 0 || Bytes64(bytes_read) != disk_info->bytes_per_mft_record_) {
            gui->show_debug(DebugLevel::Progress, nullptr,
                            std::format(L"      Error while reading Inode {}: reason {}", ref_inode,
                                        Str::system_error(GetLastError())));
            return;
        }

        // Fixup the raw data
        if (!fixup_raw_mftdata(data, disk_info,
                               MemSlice::from_ptr(buffer_2.get(), disk_info->bytes_per_mft_record_)
        )) {
            gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                    L"The error occurred while processing Inode {}", ref_inode));

            continue;
        }

        // If the Inode is not in use then skip.
        const auto file_record_header = (FILE_RECORD_HEADER *) buffer_2.get();

        if ((file_record_header->flags_ & 1) != 1) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
                    L"      Referenced Inode {} is not in use.", ref_inode));
            continue;
        }

        // If the base_inode inside the Inode is not the same as the calling Inode then skip.
        const Inode64 base_inode = Inode64(
                (uint64_t) file_record_header->base_file_record_.inode_number_low_part_ +
                ((uint64_t) file_record_header->base_file_record_.inode_number_high_part_ << 32)
        );

        if (inode_data->inode_ != base_inode) {
            gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
                    L"      Warning: Inode " NUM_FMT " is an extension of Inode " NUM_FMT ", but thinks it's an extension of Inode " NUM_FMT,
                    ref_inode.value(), inode_data->inode_.value(), base_inode.value()));
            continue;
        }

        // Process the list of attributes in the Inode, by recursively calling the process_attributes() subroutine.
        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
                L"      Processing Inode {} Instance {}", ref_inode, attribute->instance_));

        const Bytes64 limit = disk_info->bytes_per_mft_record_ - Bytes64(file_record_header->attribute_offset_);
        process_attributes(data, disk_info, inode_data,
                           MemSlice::from_ptr(buffer_2.get() + file_record_header->attribute_offset_, limit),
                           attribute->instance_, depth + 1);

        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr, std::format(
                L"      Finished processing Inode {} Instance {}", ref_inode, attribute->instance_
        ));
    }
}

// Process a list of attributes and store the gathered information in the Item
// struct. Return FALSE if an error occurred
bool ScanNTFS::process_attributes(DefragState &data, NtfsDiskInfoStruct *disk_info,
                                  InodeDataStruct *inode_data, const MemSlice &buffer,
                                  const USHORT instance, const int depth) {
    RESIDENT_ATTRIBUTE *resident_attribute;
    NONRESIDENT_ATTRIBUTE *nonresident_attribute;
    DefragGui *gui = DefragGui::get_instance();

    // Walk through all the attributes and gather information. AttributeLists are skipped and interpreted later.
    ATTRIBUTE *attribute;

    for (Bytes64 attribute_offset = {};
         attribute_offset < buffer.length();
         attribute_offset += Bytes64(attribute->length_)) {
        // attribute = (ATTRIBUTE *) &buffer[attribute_offset.value()];
        attribute = buffer.ptr_to<ATTRIBUTE>(attribute_offset.value());

        // Exit the loop if end-marker
        if (attribute_offset + Bytes64(4) <= buffer.length()
            && *(ULONG *) attribute == 0xFFFFFFFF)
            break;

        // Sanity check
        if (attribute_offset + Bytes64(4) > buffer.length() ||
            attribute->length_ < 3 ||
            attribute_offset + Bytes64(attribute->length_) > buffer.length()) {
            gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                    L"Error: attribute in Inode " NUM_FMT " is bigger than the data, the MFT may be corrupt.",
                    inode_data->inode_));
            gui->show_debug(DebugLevel::Progress, nullptr, std::format(
                    L"  buf_length=" NUM_FMT ", attribute_offset=" NUM_FMT ", AttributeLength={}(0x{:x})",
                    buffer.length(), attribute_offset, attribute->length_, attribute->length_));

            DefragRunner::show_hex(data, buffer);

            return false;
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
                // const auto file_name_attribute = (FILENAME_ATTRIBUTE *) &buffer[attribute_offset + resident_attribute->value_offset_];
                const auto file_name_attribute = buffer.ptr_to<FILENAME_ATTRIBUTE>(
                        attribute_offset + Bytes64(resident_attribute->value_offset_));

                inode_data->parent_inode_ = Inode64(
                        file_name_attribute->parent_directory_.inode_number_low_part_ +
                        ((uint64_t) file_name_attribute->parent_directory_.inode_number_high_part_ << 32)
                );

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
                auto standard_information = buffer.ptr_to<STANDARD_INFORMATION>(
                        attribute_offset + Bytes64(resident_attribute->value_offset_));

                inode_data->creation_time_ = std::chrono::microseconds(standard_information->creation_time_);
                inode_data->mft_change_time_ = std::chrono::microseconds(standard_information->mft_change_time_);
                inode_data->last_access_time_ = std::chrono::microseconds(standard_information->last_access_time_);
            }

            // The value of the AttributeData (0x80) is the actual data of the file
            if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeData) {
                inode_data->bytes_ = Bytes64(resident_attribute->value_length_);
            }
        } else {
            nonresident_attribute = (NONRESIDENT_ATTRIBUTE *) attribute;

            // Save the length (number of bytes) of the data
            if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeData && inode_data->bytes_.is_zero()) {
                inode_data->bytes_ = Bytes64(nonresident_attribute->data_size_);
            }

            // Extract the streamname
            std::unique_ptr<wchar_t[]> p2;

            if (attribute->name_length_ > 0) {
                p2 = std::make_unique<wchar_t[]>(attribute->name_length_ + 1);

                wcsncpy_s(p2.get(), attribute->name_length_ + 1,
                          buffer.ptr_to<wchar_t>(attribute_offset + Bytes64(attribute->name_offset_)),
                          attribute->name_length_);

                p2[attribute->name_length_] = 0;
            }

            // Create a new stream with a list of fragments for this data
            auto memv = buffer.sub_view(attribute_offset + Bytes64(nonresident_attribute->run_array_offset_),
                                        Bytes64(attribute->length_ - nonresident_attribute->run_array_offset_));
            translate_rundata_to_fragmentlist(
                    data, inode_data, p2.get(), attribute->attribute_type_, memv,
                    Clusters64(nonresident_attribute->starting_vcn_), Bytes64(nonresident_attribute->data_size_));

            // Cleanup the streamname
            p2.reset();

            // Special case: If this is the $MFT (intent: to save data,
            // but for real with STL data structures, there's not much saving now)
            if (inode_data->inode_.is_zero()) {
                if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeData
                    && inode_data->mft_data_fragments_.empty()) {
                    inode_data->mft_data_fragments_ = inode_data->streams_.begin()->fragments_;
                    inode_data->mft_data_bytes_ = Bytes64(nonresident_attribute->data_size_);
                }

                if (attribute->attribute_type_ == ATTRIBUTE_TYPE::AttributeBitmap
                    && inode_data->mft_bitmap_fragments_.empty()) {
                    inode_data->mft_bitmap_fragments_ = inode_data->streams_.begin()->fragments_;
                    inode_data->mft_bitmap_bytes_ = Bytes64(nonresident_attribute->data_size_);
                }
            }
        }
    }

    // Walk through all the attributes and interpret the AttributeLists. We have to do this after the DATA and BITMAP
    // attributes have been interpreted, because some MFT's have an AttributeList that is stored in fragments that are
    // defined in the DATA attribute, and/or contain a continuation of the DATA or BITMAP attributes.
    for (Bytes64 attribute_offset = {};
         attribute_offset < buffer.length(); attribute_offset += Bytes64(attribute->length_)) {
        attribute = buffer.ptr_to<ATTRIBUTE>(attribute_offset);

        if (*(ULONG *) attribute == 0xFFFFFFFF) break;
        if (attribute->attribute_type_ != ATTRIBUTE_TYPE::AttributeAttributeList) continue;

        gui->show_debug(DebugLevel::DetailedGapFinding, nullptr,
                        std::format(L"  attribute {}: {}", attribute->attribute_number_,
                                    stream_type_names(attribute->attribute_type_)));

        if (attribute->nonresident_ == 0) {
            resident_attribute = (RESIDENT_ATTRIBUTE *) attribute;

            auto memv_offset = attribute_offset + Bytes64(resident_attribute->value_offset_);
            process_attribute_list(data, disk_info, inode_data,
                                   MemSlice::from_ptr(buffer.ptr_to<BYTE>(memv_offset),
                                                      Bytes64(resident_attribute->value_length_)),
                                   depth);
        } else {
            nonresident_attribute = (NONRESIDENT_ATTRIBUTE *) attribute;

            const Bytes64 buffer_2_length = Bytes64(nonresident_attribute->data_size_);
            // Bytes64(attribute->length_ - nonresident_attribute->run_array_offset_),

            auto memv_offset = attribute_offset + Bytes64(nonresident_attribute->run_array_offset_);

            std::unique_ptr<BYTE[]> buffer_2(read_non_resident_data(
                    data, disk_info,
                    MemSlice::from_ptr(buffer.ptr_to<BYTE>(memv_offset), buffer.length() - memv_offset),
                    Bytes64(0), buffer_2_length));

            process_attribute_list(data, disk_info, inode_data,
                                   MemSlice::from_ptr(buffer_2.get(), buffer_2_length),
                                   depth);
        }
    }

    return true;
}

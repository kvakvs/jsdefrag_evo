#include "precompiled_header.h"

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

// Construct the full stream name from the filename, the stream name, and the stream type
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

    std::wstring p1;
    p1.reserve(length);

    if (file_name != nullptr) p1 += file_name;

    p1 += L":";

    if (stream_name != nullptr) p1 += stream_name;

    p1 += L":";
    p1 += stream_type_names(stream_type);
    return p1;
}

// Cleanup the Streams data in an inode_data struct. If CleanFragments is TRUE then
// also cleanup the fragments.
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

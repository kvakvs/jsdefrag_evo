#include "precompiled_header.h"
#include "../../include/itemstruct.h"

/// short_path can be null
void ItemStruct::set_names(const wchar_t *long_path, const wchar_t *long_filename,
                           const wchar_t *short_path, const wchar_t *short_filename) {
    this->long_path_ = long_path;
    this->long_filename_ = long_filename;

    if (!short_path || this->long_path_ == short_path) {
        this->short_path_ = std::nullopt;
    } else {
        this->short_path_ = short_path;
    }

    if (this->long_filename_ == short_filename) {
        this->short_filename_ = std::nullopt;
    } else {
        this->short_filename_ = short_filename;
    }
}

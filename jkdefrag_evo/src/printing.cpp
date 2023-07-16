#include "precompiled_header.h"

namespace Str {
    std::wstring from_char(const char *input) {
        size_t input_size = std::strlen(input);
        std::wstring result(input_size, L' ');
        result.resize(std::mbstowcs(&result[0], input, input_size));
        return result;
    }
}

std::wstring zone_to_str(Zone zone) {
    switch (zone) {
        case Zone::Zone0:
            return L"<Disk Beginning>";
        case Zone::Zone1:
            return L"<All Files>";
        case Zone::Zone2:
            return L"<Disk End>";
        case Zone::Zone3_MaxValue:
            return L"<Zone 3>";
        default:
            return L"None";
    }
}

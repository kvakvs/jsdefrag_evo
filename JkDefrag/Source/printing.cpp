#include "std_afx.h"

namespace Str {
    std::wstring from_char(const char *input) {
        size_t input_size = std::strlen(input);
        std::wstring result(input_size, L' ');
        result.resize(std::mbstowcs(&result[0], input, input_size));
        return result;
    }
}
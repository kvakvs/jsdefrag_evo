#pragma once

template<typename NUM>
constexpr NUM kilobytes(NUM val) { return val * NUM{1024}; }

template<typename NUM>
constexpr NUM gigabytes(NUM val) { return val * NUM{1024} * NUM{1024} * NUM{1024}; }

namespace Str {
    [[nodiscard]] std::wstring from_char(const char *input);

    // static wchar_t lower_case(wchar_t c);

    [[nodiscard]] bool match_mask(const wchar_t *string, const wchar_t *mask);

    [[nodiscard]] std::wstring system_error(DWORD error_code);
}
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
        case Zone::ZoneFirst:
            return L"<Disk Beginning>";
        case Zone::ZoneCommon:
            return L"<All Files>";
        case Zone::ZoneLast:
            return L"<Disk End>";
        case Zone::ZoneAll_MaxValue:
            return L"<Zone 3>";
        default:
            return L"None";
    }
}

// Return a string with the error message for GetLastError()
std::wstring Str::system_error(const DWORD error_code) {
    wchar_t buffer[BUFSIZ];

    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ARGUMENT_ARRAY,
                   nullptr, error_code, 0, buffer, BUFSIZ, nullptr);

    // Strip trailing whitespace
    wchar_t *p1 = wcschr(buffer, '\0');

    while (p1 != buffer) {
        p1--;
        if (!std::iswspace(*p1)) break;
        *p1 = '\0';
    }

    // Add error number.
    return std::format(L"[code {}] {}", error_code, buffer);
}

// // Translate character to lowercase
//wchar_t Str::lower_case(const wchar_t c) {
//    if (std::iswupper(c)) return c - 'A' + 'a';
//
//    return c;
//}

// Compare a string with a mask, case-insensitive. If it matches then return
// true, otherwise false. The mask may contain wildcard characters '?' (any
// character) '*' (any characters).
bool Str::match_mask(const wchar_t *string, const wchar_t *mask) {
    if (string == nullptr) return false; // Just to speed up things
    if (mask == nullptr) return false;
    if (wcscmp(mask, L"*") == 0) return true;

    auto m = mask;
    auto s = string;

    while (*m != L'\0' && *s != L'\0') {
        if (std::towlower(*m) != std::towlower(*s) && *m != '?') {
            if (*m != L'*') return false;

            m++;

            if (*m == L'\0') return true;

            while (*s != L'\0') {
                if (match_mask(s, m)) return true;
                s++;
            }

            return false;
        }

        m++;
        s++;
    }

    while (*m == L'*') m++;

    if (*s == L'\0' && *m == L'\0') return true;

    return false;
}

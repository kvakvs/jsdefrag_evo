#pragma once

template<typename NUM>
constexpr NUM kilobytes(NUM val) { return val * NUM{1024}; }

template<typename NUM>
constexpr NUM gigabytes(NUM val) { return val * NUM{1024} * NUM{1024} * NUM{1024}; }

namespace Str {
    std::wstring from_char(const char *input);
}
#pragma once

#include <chrono>

// 10-millionths of a second, 100ns
using filetime64_t = std::chrono::duration<uint64_t, std::ratio_multiply<std::ratio<100>, std::nano>>;

// Millionths of a second
using micro64_t = std::chrono::duration<uint64_t, std::micro>;

// Thousandths of a second
using milli64_t = std::chrono::duration<uint64_t, std::milli>;

filetime64_t from_system_time();

filetime64_t from_FILETIME(FILETIME &ft);

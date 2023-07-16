#pragma once

#include <chrono>

using micro64_t = std::chrono::duration<uint64_t, std::micro>;
using milli64_t = std::chrono::duration<uint64_t, std::milli>;

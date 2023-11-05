#pragma once

#include <vector>
#include <chrono>

using Wstrings = std::vector<std::wstring>;

// A signed LONGLONG used for cluster position
using Lcn = decltype(_LARGE_INTEGER::QuadPart);

// A signed LONGLONG used for virtual cluster position
using Vcn = decltype(_LARGE_INTEGER::QuadPart);

// A signed LONGLONG, used for counting clusters
using LcnCount = decltype(_LARGE_INTEGER::QuadPart);

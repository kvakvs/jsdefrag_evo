#pragma once

#include <vector>

using Wstrings = std::vector<std::wstring>;

/// A signed LONGLONG used for cluster position, Logical Cluster Number
/// A logical cluster number (LCN) describes the offset of a cluster from some arbitrary point within the volume.
/// LCNs should be treated only as ordinal, or relative, numbers. There is no guaranteed mapping of logical clusters
/// to physical hard disk drive sectors.
using lcn64_t = decltype(_LARGE_INTEGER::QuadPart);

/// a handy conversion from _LARGE_INTEGER to lcn64_t
inline auto lcn_from(const _LARGE_INTEGER& val) -> lcn64_t {
    return val.QuadPart;
}

/// A signed LONGLONG used for virtual cluster position, Virtual Cluster Number
/// Any cluster in a file has a virtual cluster number (VCN), which is its relative offset from the beginning of the file.
using vcn64_t = decltype(_LARGE_INTEGER::QuadPart);

/// A signed LONGLONG, used for counting clusters
using cluster_count64_t = decltype(_LARGE_INTEGER::QuadPart);

/// A number for an inode
using inode_t = uint64_t;

#pragma once

#include <vector>
#include <chrono>

using Wstrings = std::vector<std::wstring>;

/// A signed LONGLONG used for cluster position, Logical Cluster Number
/// A logical cluster number (LCN) describes the offset of a cluster from some arbitrary point within the volume.
/// LCNs should be treated only as ordinal, or relative, numbers. There is no guaranteed mapping of logical clusters
/// to physical hard disk drive sectors.
using lcn64_t = decltype(_LARGE_INTEGER::QuadPart);

/// A signed LONGLONG used for virtual cluster position, Virtual Cluster Number
/// Any cluster in a file has a virtual cluster number (VCN), which is its relative offset from the beginning of the file.
using vcn64_t = decltype(_LARGE_INTEGER::QuadPart);

/// A signed LONGLONG, used for counting clusters
using count64_t = decltype(_LARGE_INTEGER::QuadPart);

/// A number for an inode
using inode_t = uint64_t;

/// An extent is a run of contiguous clusters. For example, suppose a file consisting of 30 clusters is recorded in
/// two extents. The first extent might consist of five contiguous clusters, the other of the remaining 25 clusters.
using extent_t = struct {
    lcn64_t start;
    count64_t count;
};

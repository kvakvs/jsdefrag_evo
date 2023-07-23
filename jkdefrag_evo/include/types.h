#pragma once

#include <vector>
#include <chrono>

#include "numeral.h"

using Wstrings = std::vector<std::wstring>;

#define DECL_UNIT(UNIT) using UNIT = struct {}

#define DECL_GENERIC_VALUE(NAME, UNIT) \
    template<typename T> using NAME = Numeral<T, UNIT>

#define DECL_TYPED_VALUE(STORAGE, DECLNAME, UNIT) \
    using DECLNAME = Numeral<STORAGE, UNIT>

#define DECL_OP(SYMBOL, UNIT1, UNIT2, RESULT) template<typename T> \
    auto operator SYMBOL(const UNIT1<T> &t1, const UNIT2<T> &t2) { \
        return RESULT<T>(t1.value() * t2.value()); \
    }

// Declares a pair of A|B=C and B|A=C
#define DECL_COMMUTATIVE_OP(SYMBOL, UNIT1, UNIT2, RESULT) \
    DECL_OP(SYMBOL, UNIT1, UNIT2, RESULT); \
    DECL_OP(SYMBOL, UNIT2, UNIT1, RESULT)

// Declares a pair A/B=C and A/C=B
#define DECL_DIVISION_OP(UNIT1, UNIT2, RESULT) \
    DECL_OP(/, UNIT1, UNIT2, RESULT); \
    DECL_OP(/, UNIT1, RESULT, UNIT2)

DECL_UNIT(BytesUnit);
DECL_GENERIC_VALUE(Bytes, BytesUnit);
DECL_TYPED_VALUE(uint64_t, Bytes64, BytesUnit);

DECL_UNIT(ClustersUnit);
DECL_GENERIC_VALUE(Clusters, ClustersUnit);
DECL_TYPED_VALUE(uint64_t, Clusters64, ClustersUnit);

DECL_UNIT(InodesUnit);
DECL_GENERIC_VALUE(Inode, InodesUnit);
DECL_TYPED_VALUE(uint64_t, Inode64, InodesUnit);

DECL_UNIT(SectorsUnit);
DECL_GENERIC_VALUE(Sectors, SectorsUnit);
DECL_TYPED_VALUE(uint64_t, Sectors64, SectorsUnit);

// Unit: 'sector / 'cluster
DECL_UNIT(SectorsPerClusterUnit);
DECL_GENERIC_VALUE(SectorsPerCluster, SectorsPerClusterUnit);
DECL_TYPED_VALUE(uint64_t, Sectors64PerCluster, SectorsPerClusterUnit);

// Unit: 'bytes / 'sector
DECL_UNIT(BytesPerSectorUnit);
DECL_GENERIC_VALUE(BytesPerSector, BytesPerSectorUnit);
DECL_TYPED_VALUE(uint64_t, Bytes64PerSector, BytesPerSectorUnit);

DECL_COMMUTATIVE_OP(*, BytesPerSector, Sectors, Bytes);

// Unit: 'bytes / 'cluster
DECL_UNIT(BytesPerClusterUnit);
DECL_GENERIC_VALUE(BytesPerCluster, BytesPerClusterUnit);
DECL_TYPED_VALUE(uint64_t, Bytes64PerCluster, BytesPerClusterUnit);

DECL_COMMUTATIVE_OP(*, BytesPerSector, SectorsPerCluster, BytesPerCluster);

DECL_COMMUTATIVE_OP(*, BytesPerCluster, Clusters, Bytes);

DECL_COMMUTATIVE_OP(*, Clusters, SectorsPerCluster, Sectors);

//template<typename T>
//auto operator/(const Clusters<T> &t1, const BytesPerCluster<T> &t2) {
//    return Bytes<T>(t1.value() * t2.value());
//}
DECL_DIVISION_OP(Bytes, Clusters, BytesPerCluster);

DECL_DIVISION_OP(Sectors, SectorsPerCluster, Clusters);

template<typename NUM=uint64_t>
constexpr Numeral<NUM, BytesUnit> kilobytes(NUM val) { return Numeral<NUM, BytesUnit>(val * NUM{1024}); }

template<typename NUM=uint64_t>
constexpr Numeral<NUM, BytesUnit> gigabytes(NUM val) {
    return Numeral<NUM, BytesUnit>(val * NUM{1024} * NUM{1024} * NUM{1024});
}

DECL_UNIT(PercentageUnit);
DECL_TYPED_VALUE(size_t, Percentage, PercentageUnit);

using isize_t = ptrdiff_t;

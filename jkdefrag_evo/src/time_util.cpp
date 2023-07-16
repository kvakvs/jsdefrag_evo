#include "../include/precompiled_header.h"
#include "../include/time_util.h"

filetime64_t from_system_time() {
    FILETIME system_time1;

    GetSystemTimeAsFileTime(&system_time1);

    ULARGE_INTEGER u;

    u.LowPart = system_time1.dwLowDateTime;
    u.HighPart = system_time1.dwHighDateTime;
    return filetime64_t(u.QuadPart);
}

filetime64_t from_FILETIME(FILETIME &ft) {
    ULARGE_INTEGER u;

    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;

    return filetime64_t(u.QuadPart);
}
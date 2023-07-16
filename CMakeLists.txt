cmake_minimum_required(VERSION 3.18)
project(JkDefragEvo)

if (MSVC)
    set(OPTIONS WIN32)
endif (MSVC)

add_compile_options("/std:c++20")

set(APP_NAME jkdefrag)
set(SRC ${PROJECT_SOURCE_DIR}/jkdefrag_evo/src)
set(INCL ${PROJECT_SOURCE_DIR}/jkdefrag_evo/include)
include_directories(${INCL})

set(HEADER_FILES
        ${INCL}/constants.h
        ${INCL}/types.h
        ${INCL}/precompiled_header.h
        ${INCL}/printing.h
        ${INCL}/defrag_log.h
        ${INCL}/defrag.h
        ${INCL}/time_util.h
        ${INCL}/scan_ntfs.h
        ${INCL}/scan_fat.h
        ${INCL}/itemstruct.h
        ${INCL}/defrag_struct.h
        ${INCL}/defrag_lib.h
        ${INCL}/defrag_data_struct.h
        ${INCL}/defrag_gui.h
        ${INCL}/colormap.h
        ${INCL}/tree.h
        )
set(SOURCE_FILES
        ${SRC}/precompiled_header.cpp
        ${SRC}/main.cpp
        ${SRC}/defrag.cpp
        ${SRC}/time_util.cpp
        ${SRC}/printing.cpp
        ${SRC}/defrag_log.cpp

        ${SRC}/gui/defrag_gui.cpp
        ${SRC}/gui/colormap.cpp
        ${SRC}/gui/paint.cpp

        ${SRC}/tech/defrag_lib.cpp
        ${SRC}/tech/defrag_struct.cpp
        ${SRC}/tech/itemstruct.cpp
        ${SRC}/tech/fat/scan_fat.cpp
        ${SRC}/tech/fat/analyze.cpp
        ${SRC}/tech/fat/directory.cpp
        ${SRC}/tech/ntfs/scan_ntfs.cpp
        ${SRC}/tech/ntfs/analyze.cpp
        ${SRC}/tech/ntfs/attributes.cpp
        ${SRC}/tech/ntfs/mft.cpp
        ${SRC}/tech/ntfs/stream.cpp
        ${SRC}/tech/defrag_lib_finding.cpp
        ${SRC}/tech/defrag_lib_moving.cpp
        ${SRC}/tech/defrag_lib_scan.cpp
        ${SRC}/tech/defrag_data_struct.cpp
        ${SRC}/tech/defrag_lib_methods.cpp
        ${SRC}/tech/methods/fixup.cpp
        ${SRC}/tech/methods/defragment.cpp
        ${SRC}/tech/methods/forced_fill.cpp
        ${SRC}/tech/methods/vacate.cpp
        ${SRC}/tech/methods/optimize_sort.cpp
        ${SRC}/tech/methods/optimize_volume.cpp
        ${SRC}/tech/methods/optimize_up.cpp
        ${SRC}/tech/methods/defrag_one_path.cpp
        ${SRC}/tech/methods/defrag_mountpoints.cpp
        )

add_executable(${APP_NAME} ${OPTIONS}
        ${HEADER_FILES}
        ${SOURCE_FILES}
        )

target_link_libraries(${APP_NAME} DbgHelp GdiPlus)
target_precompile_headers(${APP_NAME} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${INCL}/precompiled_header.h>")

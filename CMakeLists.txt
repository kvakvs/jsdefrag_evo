cmake_minimum_required(VERSION 3.18)
project(JkDefragEvo)

if (MSVC)
    set(OPTIONS WIN32)
endif (MSVC)

add_compile_options("/std:c++20")

set(APP_NAME jkdefrag)
set(SRC ${PROJECT_SOURCE_DIR}/jkdefrag_evo/src)
set(INCL ${PROJECT_SOURCE_DIR}/jkdefrag_evo/include)
set(RSRC ${PROJECT_SOURCE_DIR}/jkdefrag_evo/resource)
include_directories(${INCL})

set(HEADER_FILES
        ${INCL}/app.h
        ${INCL}/numeral.h
        ${INCL}/diskmap.h
        ${INCL}/diskmap_cell.h
        ${INCL}/diskmap_color.h
        ${INCL}/constants.h
        ${INCL}/defrag_gui.h
        ${INCL}/runner.h
        ${INCL}/defrag_log.h
        ${INCL}/defrag_state.h
        ${INCL}/file_node.h
        ${INCL}/mem_util.h
        ${INCL}/precompiled_header.h
        ${INCL}/str_util.h
        ${INCL}/scan_fat.h
        ${INCL}/scan_ntfs.h
        ${INCL}/time_util.h
        ${INCL}/tree.h
        ${INCL}/types.h
        )
set(SOURCE_FILES
        ${SRC}/app.cpp
        ${SRC}/app_debug.cpp
        ${SRC}/defrag_log.cpp
        ${SRC}/main.cpp
        ${SRC}/precompiled_header.cpp
        ${SRC}/str_util.cpp
        ${SRC}/time_util.cpp

        ${SRC}/gui/diskmap.cpp
        ${SRC}/gui/defrag_gui.cpp
        ${SRC}/gui/paint.cpp
        ${SRC}/gui/stats.cpp

        ${SRC}/tech/file_node.cpp
        ${SRC}/tech/runner.cpp

        ${SRC}/tech/defrag/analyze.cpp
        ${SRC}/tech/defrag/defrag_state.cpp
        ${SRC}/tech/defrag/finding.cpp
        ${SRC}/tech/defrag/move_mft.cpp
        ${SRC}/tech/defrag/moving.cpp
        ${SRC}/tech/defrag/scan.cpp

        ${SRC}/tech/ntfs/ntfs_analyze.cpp
        ${SRC}/tech/ntfs/ntfs_attributes.cpp
        ${SRC}/tech/ntfs/ntfs_mft.cpp
        ${SRC}/tech/ntfs/ntfs_scan.cpp
        ${SRC}/tech/ntfs/ntfs_stream.cpp

        ${SRC}/tech/fat/fat_analyze.cpp
        ${SRC}/tech/fat/fat_directory.cpp
        ${SRC}/tech/fat/fat_scan.cpp

        ${SRC}/tech/methods/defrag_mountpoints.cpp
        ${SRC}/tech/methods/defrag_one_path.cpp
        ${SRC}/tech/methods/defragment.cpp
        ${SRC}/tech/methods/fixup.cpp
        ${SRC}/tech/methods/forced_fill.cpp
        ${SRC}/tech/methods/optimize_sort.cpp
        ${SRC}/tech/methods/optimize_up.cpp
        ${SRC}/tech/methods/optimize_volume.cpp
        ${SRC}/tech/methods/vacate.cpp
        )

add_executable(${APP_NAME} ${OPTIONS} ${HEADER_FILES} ${SOURCE_FILES})

target_sources(${APP_NAME} PRIVATE ${RSRC}/jkdefrag.rc)
target_link_libraries(${APP_NAME} DbgHelp GdiPlus)
target_precompile_headers(${APP_NAME} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${INCL}/precompiled_header.h>")

cmake_minimum_required(VERSION 3.18)
project(JkDefrag)

if (MSVC)
    set(OPTIONS WIN32)
endif (MSVC)

add_compile_options("/std:c++20")

set(APP_NAME jkdefrag)
set(SRC ${PROJECT_SOURCE_DIR}/JkDefrag/Source)
include_directories(${SRC})

set(HEADER_FILES
        ${SRC}/constants.h
        ${SRC}/types.h
        ${SRC}/precompiled_header.h
        ${SRC}/printing.h
        ${SRC}/defrag_log.h
        ${SRC}/defrag.h
        ${SRC}/time_util.h
        ${SRC}/tech/scan_ntfs.h
        ${SRC}/tech/scan_fat.h
        ${SRC}/tech/itemstruct.h
        ${SRC}/tech/defrag_struct.h
        ${SRC}/tech/defrag_lib.h
        ${SRC}/tech/defrag_data_struct.h
        ${SRC}/gui/defrag_gui.h
        ${SRC}/gui/colormap.h
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
        ${SRC}/tech/scan_fat.cpp
        ${SRC}/tech/scan_ntfs.cpp
        ${SRC}/tech/itemstruct.cpp
        ${SRC}/tech/defrag_lib_finding.cpp
        ${SRC}/tech/defrag_lib_moving.cpp
        ${SRC}/tech/defrag_lib_tree.cpp
        ${SRC}/tech/defrag_lib_scan.cpp
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
        "$<$<COMPILE_LANGUAGE:CXX>:${SRC}/precompiled_header.h>")

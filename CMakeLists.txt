cmake_minimum_required(VERSION 3.18)
project(JkDefrag)

if (MSVC)
    set(OPTIONS WIN32)
endif (MSVC)

add_compile_options("/std:c++17")

set(APP_NAME jkdefrag)
set(SRC ${PROJECT_SOURCE_DIR}/JkDefrag/Source)
set(HEADER_FILES
        ${SRC}/constants.h
        ${SRC}/types.h
        ${SRC}/std_afx.h
        ${SRC}/scan_ntfs.h
        ${SRC}/scan_fat.h
        ${SRC}/itemstruct.h
        ${SRC}/defrag_struct.h
        ${SRC}/defrag_log.h
        ${SRC}/defrag_lib.h
        ${SRC}/defrag_gui.h
        ${SRC}/defrag_data_struct.h
        ${SRC}/defrag.h)
set(SOURCE_FILES
        ${SRC}/defrag.cpp
        ${SRC}/defrag_gui.cpp
        ${SRC}/defrag_lib.cpp
        ${SRC}/defrag_log.cpp
        ${SRC}/defrag_struct.cpp
        ${SRC}/scan_fat.cpp
        ${SRC}/scan_ntfs.cpp
        ${SRC}/std_afx.cpp
        ${SRC}/itemstruct.cpp
        ${SRC}/defrag_lib_finding.cpp
        ${SRC}/defrag_lib_methods.cpp
        ${SRC}/defrag_lib_moving.cpp
        ${SRC}/defrag_lib_tree.cpp
        ${SRC}/defrag_lib_scan.cpp)

add_executable(${APP_NAME} ${OPTIONS}
        ${HEADER_FILES}
        ${SOURCE_FILES}
        )
target_link_libraries(${APP_NAME} DbgHelp GdiPlus)
target_precompile_headers(${APP_NAME} PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${SRC}/std_afx.h>")

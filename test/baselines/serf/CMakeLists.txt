cmake_minimum_required(VERSION 3.15)

project(SerfBaseline)

file(GLOB_RECURSE SERF_BASELINE_SRC CONFIGURE_DEPENDS *.cc)

add_library(serf_baseline SHARED ${SERF_BASELINE_SRC})

set_target_properties(serf_baseline PROPERTIES
        WINDOWS_EXPORT_ALL_SYMBOLS ON
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/test")

target_include_directories(serf_baseline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

cmake_minimum_required(VERSION 3.15)

project(PerfTest)

# Set C++ standard version
set(CMAKE_CXX_STANDARD 17)

# ---------------------------------------------------------------------------
# Manual build switches: change ON/OFF here.
# ---------------------------------------------------------------------------
set(SERF_ENABLE_ECHOS ON)
set(SERF_ENABLE_BASELINE_SERF ON)

set(SERF_ENABLE_BASELINE_DEFLATE ON)
set(SERF_ENABLE_BASELINE_FPC ON)
set(SERF_ENABLE_BASELINE_LZ4 ON)
set(SERF_ENABLE_BASELINE_CHIMP128 ON)
set(SERF_ENABLE_BASELINE_GORILLA ON)
set(SERF_ENABLE_BASELINE_ELF ON)
set(SERF_ENABLE_BASELINE_ELF_STAR ON)
set(SERF_ENABLE_BASELINE_LZ77 ON)
set(SERF_ENABLE_BASELINE_MACHETE ON)
set(SERF_ENABLE_BASELINE_SZ2 ON)
set(SERF_ENABLE_BASELINE_SNAPPY ON)
set(SERF_ENABLE_BASELINE_ZSTD ON)
set(SERF_ENABLE_BASELINE_SIM_PIECE ON)
set(SERF_ENABLE_BASELINE_SPRINTZ ON)
set(SERF_ENABLE_BASELINE_BUFF_RUST ON)
set(SERF_ENABLE_BASELINE_ALP ON)
set(SERF_ENABLE_BASELINE_SZ_ADT ON)

if(SERF_ENABLE_BASELINE_DEFLATE)
    add_subdirectory(baselines/deflate)
endif()

if(SERF_ENABLE_BASELINE_SERF)
    add_subdirectory(baselines/serf)
endif()

if(SERF_ENABLE_BASELINE_FPC)
    add_subdirectory(baselines/fpc)
endif()

if(SERF_ENABLE_BASELINE_LZ4)
    add_subdirectory(baselines/lz4)
endif()

if(SERF_ENABLE_BASELINE_CHIMP128)
    add_subdirectory(baselines/chimp128)
endif()

if(SERF_ENABLE_BASELINE_GORILLA)
    add_subdirectory(baselines/gorilla)
endif()

if(SERF_ENABLE_BASELINE_ELF)
    add_subdirectory(baselines/elf)
endif()

if(SERF_ENABLE_BASELINE_ELF_STAR)
    add_subdirectory(baselines/elf_star)
endif()

if(SERF_ENABLE_BASELINE_MACHETE)
    add_subdirectory(baselines/machete)
endif()

if(SERF_ENABLE_BASELINE_LZ77)
    add_subdirectory(baselines/lz77)
endif()

if(SERF_ENABLE_BASELINE_SZ2)
    add_subdirectory(baselines/sz2)
endif()

if(SERF_ENABLE_BASELINE_SNAPPY)
    add_subdirectory(baselines/snappy)
endif()

if(SERF_ENABLE_BASELINE_ZSTD)
    add_subdirectory(baselines/zstd/build/cmake)
endif()

if(UNIX)
    target_compile_definitions(ZLIB PRIVATE HAVE_UNISTD_H=1)
endif()

if(SERF_ENABLE_BASELINE_SIM_PIECE)
    add_subdirectory(baselines/sim_piece)
endif()

if(SERF_ENABLE_BASELINE_SPRINTZ)
    add_subdirectory(baselines/sprintz)
endif()

if(SERF_ENABLE_BASELINE_BUFF_RUST)
    add_subdirectory(baselines/buff)
endif()

if(SERF_ENABLE_BASELINE_ALP)
    add_subdirectory(baselines/alp)
endif()

if(SERF_ENABLE_BASELINE_SZ_ADT)
    add_subdirectory(baselines/sz_adt)
endif()

include_directories(${CMAKE_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})

option(SERF_ENABLE_GTEST_TARGETS "Build GoogleTest-based tests and performance program" ON)

if(SERF_ENABLE_GTEST_TARGETS)
message(STATUS "[SelfCheck] Loading GoogleTest module")

# Set GoogleTest version
set(GTEST_VER "1.14.0")

# Load FetchContent Module
include(FetchContent)

# 查找系统安装的 gtest
find_package(GTest REQUIRED)

# # Auto fetch GoogleTest source code
# FetchContent_Declare(
#         googletest
#         DOWNLOAD_EXTRACT_TIMESTAMP true
#         URL https://github.com/google/googletest/archive/refs/tags/v${GTEST_VER}.zip)
# # For Windows: Prevent overriding the parent project's compiler/linker settings
# # set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# FetchContent_MakeAvailable(googletest)

message(STATUS "[SelfCheck] Loaded GoogleTest module")

# Scan and collect all test file
file(GLOB_RECURSE TEST_SRC *_test.cc)

if(NOT SERF_ENABLE_ECHOS AND NOT SERF_ENABLE_BASELINE_SERF)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/serf_test\\.cc$")
endif()
if(NOT SERF_ENABLE_BASELINE_DEFLATE)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/deflate_test\\.cc$")
endif()
if(NOT SERF_ENABLE_BASELINE_FPC)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/fpc_test\\.cc$")
endif()
if(NOT SERF_ENABLE_BASELINE_LZ4)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/lz4_test\\.cc$")
endif()
if(NOT SERF_ENABLE_BASELINE_CHIMP128)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/chimp_test\\.cc$")
endif()
if(NOT SERF_ENABLE_BASELINE_GORILLA)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/gorilla_test\\.cc$")
endif()
if(NOT SERF_ENABLE_BASELINE_ELF)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/elf_test\\.cc$")
endif()
if(NOT SERF_ENABLE_BASELINE_ELF_STAR)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/elf_star_test\\.cc$")
endif()
if(NOT SERF_ENABLE_BASELINE_LZ77)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/lz77_test\\.cc$")
endif()
if(NOT SERF_ENABLE_BASELINE_MACHETE)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/machete_test\\.cc$")
endif()
if(NOT SERF_ENABLE_BASELINE_SIM_PIECE)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/sim_piece_test\\.cc$")
endif()
if(NOT SERF_ENABLE_BASELINE_SPRINTZ)
    list(FILTER TEST_SRC EXCLUDE REGEX ".*/sprintz_test\\.cc$")
endif()

enable_testing()

include(GoogleTest)

foreach(F ${TEST_SRC})
    get_filename_component(TEST_PROG ${F} NAME_WE)
    add_executable(${TEST_PROG} ${F})
    string(REPLACE "_test" "" LIB_NAME ${TEST_PROG})
    target_link_libraries(${TEST_PROG} PRIVATE ${LIB_NAME} GTest::gtest_main)
    if(TEST_PROG STREQUAL "serf_test" AND SERF_ENABLE_BASELINE_SERF)
        target_link_libraries(${TEST_PROG} PRIVATE serf_baseline)
    endif()
    gtest_discover_tests(${TEST_PROG})
endforeach(F)

add_executable(PerformanceProgram Perf.cc)

set(PERFORMANCE_PROGRAM_LIBS GTest::gtest_main)

if(SERF_ENABLE_ECHOS)
    list(APPEND PERFORMANCE_PROGRAM_LIBS serf)
endif()
if(SERF_ENABLE_BASELINE_SERF)
    list(APPEND PERFORMANCE_PROGRAM_LIBS serf_baseline)
endif()
if(SERF_ENABLE_BASELINE_CHIMP128)
    list(APPEND PERFORMANCE_PROGRAM_LIBS chimp)
endif()
if(SERF_ENABLE_BASELINE_DEFLATE)
    list(APPEND PERFORMANCE_PROGRAM_LIBS deflate)
endif()
if(SERF_ENABLE_BASELINE_ELF)
    list(APPEND PERFORMANCE_PROGRAM_LIBS elf)
endif()
if(SERF_ENABLE_BASELINE_ELF_STAR)
    list(APPEND PERFORMANCE_PROGRAM_LIBS elf_star)
endif()
if(SERF_ENABLE_BASELINE_FPC)
    list(APPEND PERFORMANCE_PROGRAM_LIBS fpc)
endif()
if(SERF_ENABLE_BASELINE_GORILLA)
    list(APPEND PERFORMANCE_PROGRAM_LIBS gorilla)
endif()
if(SERF_ENABLE_BASELINE_LZ77)
    list(APPEND PERFORMANCE_PROGRAM_LIBS lz77)
endif()
if(SERF_ENABLE_BASELINE_LZ4)
    list(APPEND PERFORMANCE_PROGRAM_LIBS lz4)
endif()
if(SERF_ENABLE_BASELINE_MACHETE)
    list(APPEND PERFORMANCE_PROGRAM_LIBS machete)
endif()
if(SERF_ENABLE_BASELINE_SZ2)
    list(APPEND PERFORMANCE_PROGRAM_LIBS sz)
endif()
if(SERF_ENABLE_BASELINE_ZSTD)
    list(APPEND PERFORMANCE_PROGRAM_LIBS libzstd_shared)
endif()
if(SERF_ENABLE_BASELINE_SNAPPY)
    list(APPEND PERFORMANCE_PROGRAM_LIBS snappy)
endif()
if(SERF_ENABLE_BASELINE_SIM_PIECE)
    list(APPEND PERFORMANCE_PROGRAM_LIBS sim_piece)
endif()
if(SERF_ENABLE_BASELINE_SPRINTZ)
    list(APPEND PERFORMANCE_PROGRAM_LIBS sprintz)
endif()
if(SERF_ENABLE_BASELINE_BUFF_RUST)
    list(APPEND PERFORMANCE_PROGRAM_LIBS buff)
endif()
if(SERF_ENABLE_BASELINE_ALP)
    list(APPEND PERFORMANCE_PROGRAM_LIBS alp)
endif()

target_link_libraries(PerformanceProgram PRIVATE ${PERFORMANCE_PROGRAM_LIBS})

if(SERF_ENABLE_BASELINE_SZ_ADT)
    add_executable(PerformanceProgram_ADT Perf_Conflict_ADT.cc)
    target_link_libraries(PerformanceProgram_ADT PRIVATE TSZ GTest::gtest_main)
    gtest_discover_tests(PerformanceProgram_ADT)
endif()

gtest_discover_tests(PerformanceProgram)
endif()

add_executable(AdjustDigitCalculator adjust_digit_calculator.cpp)

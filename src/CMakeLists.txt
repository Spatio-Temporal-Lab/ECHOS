cmake_minimum_required(VERSION 3.15)

project(Serf)

# Set C++ standard version
set(CMAKE_CXX_STANDARD 17)

# -O3 Optimization for release version
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Set parallel compilation level as 4
set(CMAKE_BUILD_PARALLEL_LEVEL 4)

set(ECHOS_SRC
        compressor/echos_abs_compressor.cc
        compressor/echos_rel_compressor.cc
        decompressor/echos_abs_decompressor.cc
        decompressor/echos_rel_decompressor.cc
        utils/output_bit_stream.cc)

add_library(serf SHARED ${ECHOS_SRC})

target_include_directories(serf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

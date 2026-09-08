add_library(elf_star STATIC
    ElfStarCompressor.cpp
    ElfStarDecompressor.cpp
    ElfStarCompressor32.cpp
    ElfStarDecompressor32.cpp
    utils.cc
    utils/post_office_solver.cc
    utils/post_office_solver_32.cc
)

target_include_directories(elf_star PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(elf_star PUBLIC cxx_std_17)
target_compile_options(elf_star PRIVATE $<$<CONFIG:Release>:-O3>)

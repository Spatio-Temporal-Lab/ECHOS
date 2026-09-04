cmake_minimum_required(VERSION 3.15)

project(BuffRust)

find_program(BUFF_CARGO_EXECUTABLE
    NAMES cargo
    HINTS "$ENV{HOME}/.cargo/bin")
if(NOT BUFF_CARGO_EXECUTABLE)
    message(FATAL_ERROR "cargo is required to build the native Rust BUFF baseline")
endif()

set(BUFF_RUST_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../HIRE/revision_HIRE_experiments/buff-master/database")
set(BUFF_RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/buff-rust")
set(BUFF_RUST_LIBRARY "${BUFF_RUST_TARGET_DIR}/release/libbuff.so")
set(BUFF_LIBCLANG_DIR "/usr/lib/llvm-17/lib" CACHE PATH
    "Directory containing libclang.so for the native Rust BUFF baseline")
if(NOT EXISTS "${BUFF_LIBCLANG_DIR}/libclang.so")
    message(FATAL_ERROR "libclang.so was not found in ${BUFF_LIBCLANG_DIR}")
endif()

file(GLOB_RECURSE BUFF_RUST_SOURCES CONFIGURE_DEPENDS
    "${BUFF_RUST_SOURCE_DIR}/src/*.rs"
    "${BUFF_RUST_SOURCE_DIR}/Cargo.toml"
    "${BUFF_RUST_SOURCE_DIR}/Cargo.lock"
    "${BUFF_RUST_SOURCE_DIR}/../bit-vec/src/*.rs"
    "${BUFF_RUST_SOURCE_DIR}/../roaring-rs/src/*.rs")

add_custom_command(
    OUTPUT "${BUFF_RUST_LIBRARY}"
    COMMAND "${CMAKE_COMMAND}" -E env
            "CMAKE_POLICY_VERSION_MINIMUM=3.5"
            "LIBCLANG_PATH=${BUFF_LIBCLANG_DIR}"
            "RUSTFLAGS=-Awarnings"
            "${BUFF_CARGO_EXECUTABLE}" +nightly-2022-09-15
            build --release --lib --locked
            --target-dir "${BUFF_RUST_TARGET_DIR}"
    WORKING_DIRECTORY "${BUFF_RUST_SOURCE_DIR}"
    DEPENDS ${BUFF_RUST_SOURCES}
    COMMENT "Building native Rust BUFF baseline"
    VERBATIM)

add_custom_target(buff_rust_build DEPENDS "${BUFF_RUST_LIBRARY}")
add_library(buff SHARED IMPORTED GLOBAL)
set_target_properties(buff PROPERTIES
    IMPORTED_LOCATION "${BUFF_RUST_LIBRARY}"
    IMPORTED_NO_SONAME TRUE
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(buff buff_rust_build)

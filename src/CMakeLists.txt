message(STATUS "Entering src directory")

option(ENABLE_ASSERTS "Assertion failures terminate the program (always enabled in Debug)" OFF)
option(ENABLE_PCH "Use precompiled standard library headers during compilation" ON)
option(ENABLE_UNITY_BUILD "Batch sources into per-area unity translation units (faster cold builds on few-core machines, much slower single-file edits)" OFF)
option(WITH_FIXMES "Build with fixme messages" OFF)
option(WITH_MAEMO "Build with right click mapped to F4 (menu button)" OFF)
set(WITH_SANITIZERS "" CACHE STRING "Semicolon-separated sanitizers to instrument the game with: address, undefined, thread. Linux and macOS only (see the FATAL_ERROR below)")

function(add_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wstring-conversion>
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wtype-limits -Wignored-qualifiers>
        $<$<CXX_COMPILER_ID:MSVC>:/W3 /wd4018 /wd4101 /wd4244 /wd4996>
            # 4018 : example: signed/unsigned mismatch
            # 4101 : example: unreferenced local variable
            # 4244 : example: conversion from 'int' to 'real', possible loss of data
            # 4996 : example: This function or variable may be unsafe (sscanf)
    )
endfunction()

target_compile_features(${JA2_BINARY} PRIVATE cxx_std_20)

target_include_directories(${JA2_BINARY} SYSTEM PRIVATE
    ${SOL_INCLUDE_DIR}
    ${LUA_INCLUDE_DIRS}
    ${MINIAUDIO_INCLUDE_DIRS}
)

macro(add_and_include_subdirectory dir)
    add_subdirectory(${dir})
    target_include_directories(${JA2_BINARY} PRIVATE ${dir})
endmacro()

set(JA2_SOURCES "")
add_and_include_subdirectory(externalized)
add_and_include_subdirectory(game)
add_and_include_subdirectory(sgp)

target_sources(${JA2_BINARY} PRIVATE ${JA2_SOURCES})

if(ENABLE_UNITY_BUILD)
    # The UNITY_GROUP properties set in the subdirectories below batch sources by
    # area, so each unity TU stays coherent instead of one giant translation unit.
    # That folds the grouped areas into 9 compiler invocations and parses their
    # headers once per group instead of once per file.
    #
    # This is OFF by default because it trades the wrong end of the distribution
    # for a machine with many cores. A unity group is one un-parallelisable
    # translation unit, so the work it removes is paid back in lost parallelism,
    # and it makes the most common edit - one file - rebuild the whole group.
    # Measured on Windows/MSYS2 GCC 16, Ninja, 12 hardware threads, per area:
    #
    #   group          unity (1 TU)   per file (total CPU -> wall @ -j12)
    #   Tactical (67)      53.3s              263s -> 21.9s   (unity 2.4x slower)
    #   sgp (43)           28.7s              121s -> 10.1s   (unity 2.9x slower)
    #
    # and for a single-file edit: 50.1s (unity) vs ~4s (per file) in Tactical,
    # 23.1s vs ~3s in sgp. It also wrecks compiler-cache granularity - one 53s
    # cache entry covers 67 files, so editing any one of them invalidates all of
    # it. Turn it on for a release/CI build or on a machine with few cores.
    set_target_properties(${JA2_BINARY} PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_MODE "GROUP")
endif()

if (WITH_SANITIZERS)
    # Which sanitizers, as a set of -fsanitize=... flags. "address" and
    # "undefined" are what CI runs; "thread" is available but not cheap and
    # not in CI (see COMPILATION.md, "Sanitizers").
    set(JA2_SANITIZER_FLAGS "")
    foreach(sanitizer ${WITH_SANITIZERS})
        string(TOLOWER "${sanitizer}" sanitizer)
        if (NOT sanitizer MATCHES "^(address|undefined|thread)$")
            message(FATAL_ERROR "Unknown sanitizer '${sanitizer}'. WITH_SANITIZERS takes "
                "address, undefined and/or thread, e.g. -DWITH_SANITIZERS=\"address;undefined\"")
        endif()
        list(APPEND JA2_SANITIZER_FLAGS "-fsanitize=${sanitizer}")
    endforeach()
    list(REMOVE_DUPLICATES JA2_SANITIZER_FLAGS)

    # The flags above compile everywhere and *link* almost nowhere, which is
    # how WITH_ASAN used to fail: it configured cleanly, instrumented ~500
    # translation units, and then died in the link step with
    # `cannot find -lasan`, minutes into the build. Worse, a toolchain where
    # the runtime is missing but something else satisfies the link can produce
    # a binary with no instrumentation at all.
    #
    # So test the link at configure time instead of at the end of a long
    # build. check_cxx_source_compiles compiles *and* links, which is exactly
    # the thing that used to fail.
    include(CheckCXXSourceCompiles)
    unset(CMAKE_REQUIRED_LIBRARIES)
    string(REPLACE ";" " " JA2_SANITIZER_FLAG_STRING "${JA2_SANITIZER_FLAGS}")
    set(CMAKE_REQUIRED_FLAGS "${JA2_SANITIZER_FLAG_STRING}")
    check_cxx_source_compiles("int main() { return 0; }" JA2_SANITIZERS_LINK)
    unset(CMAKE_REQUIRED_FLAGS)
    if (NOT JA2_SANITIZERS_LINK)
        # MinGW-w64 has no sanitizer runtimes: MSYS2 keeps libasan/libubsan/libtsan in the
        # separate `mingw-w64-*-gcc-libs` packages (not part of the toolchain this repo
        # documents), and upstream MinGW-w64 does not ship ASan or UBSan at all. That is a
        # property of the target, not of the configure step, so there is nothing to fall
        # back to. MSVC has its own ASan (/fsanitize=address), which this flag does not
        # select; it is untested here and stays off.
        message(FATAL_ERROR "WITH_SANITIZERS=\"${WITH_SANITIZERS}\" cannot be linked with "
            "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION} on ${CMAKE_SYSTEM_NAME}: "
            "'${JA2_SANITIZER_FLAG_STRING}' compiles but does not link, so the sanitizer runtime "
            "(e.g. -lasan) is missing. Sanitizers are supported on Linux and macOS only - on "
            "Windows/MSYS2 MinGW64 the runtime lives in the separate mingw-w64-*-gcc-libs "
            "packages, which are not installed, and upstream MinGW-w64 ships none. Configure "
            "without WITH_SANITIZERS, or build this on Linux, where CI runs them (COMPILATION.md, "
            "\"Sanitizers\").")
    endif()

    # Without frame pointers ASan's reports unwind through a bare stack scan and
    # name no functions; Debug keeps them, other build types usually drop them.
    target_compile_options(${JA2_BINARY} PRIVATE
        ${JA2_SANITIZER_FLAGS}
        -fno-omit-frame-pointer)
    target_link_options(${JA2_BINARY} PRIVATE ${JA2_SANITIZER_FLAGS})
    message(STATUS "Building with sanitizers: ${JA2_SANITIZER_FLAG_STRING}")
endif()

if(ENABLE_PCH)
    # The standard library the codebase includes in bulk; every one of these is
    # parsed by most translation units, so precompiling them cuts the per-TU
    # parse cost. `<iostream>` is deliberately absent: libstdc++ puts a static
    # `ios_base::Init` in it, which would then be constructed in every TU.
    target_precompile_headers(${JA2_BINARY} PRIVATE
        <algorithm>
        <array>
        <atomic>
        <bitset>
        <cassert>
        <cctype>
        <cerrno>
        <charconv>
        <chrono>
        <climits>
        <cmath>
        <cstdarg>
        <cstddef>
        <cstdint>
        <cstdio>
        <cstdlib>
        <cstring>
        <ctime>
        <deque>
        <exception>
        <filesystem>
        <forward_list>
        <fstream>
        <functional>
        <initializer_list>
        <iterator>
        <limits>
        <locale>
        <map>
        <memory>
        <mutex>
        <new>
        <numeric>
        <optional>
        <ostream>
        <queue>
        <random>
        <regex>
        <set>
        <span>
        <sstream>
        <stdexcept>
        <stdint.h>
        <stdio.h>
        <stdlib.h>
        <string>
        <string.h>
        <string_view>
        <thread>
        <tuple>
        <type_traits>
        <typeinfo>
        <unordered_map>
        <utility>
        <variant>
        <vector>
    )
endif()

if(WITH_FIXMES)
    message(STATUS "Building with fixme messages")
    target_compile_definitions(${JA2_BINARY} PRIVATE WITH_FIXMES)
endif()

if(WITH_MAEMO)
    message(STATUS "Building with right click mapped to F4 (menu button)")
    target_compile_definitions(${JA2_BINARY} PRIVATE WITH_MAEMO)
endif()

if(NOT WITH_MAGICENUM)
    message(STATUS "Compiling without magic_enum. This build will not have enum-gen functionalities")
    target_compile_definitions(${JA2_BINARY} PRIVATE NO_MAGICENUM_LIB)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR ENABLE_ASSERTS)
    target_compile_definitions(${JA2_BINARY} PRIVATE ENABLE_ASSERTS)
endif()

add_warnings(${JA2_BINARY})

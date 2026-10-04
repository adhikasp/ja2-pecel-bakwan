message(STATUS "Entering src directory")

option(ENABLE_ASSERTS "Assertion failures terminate the program (always enabled in Debug)" OFF)
option(ENABLE_PCH "Use precompiled standard library headers during compilation" ON)
option(WITH_ASAN "Build with address sanitizer" OFF)
option(WITH_FIXMES "Build with fixme messages" OFF)
option(WITH_MAEMO "Build with right click mapped to F4 (menu button)" OFF)

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

set_target_properties(${JA2_BINARY} PROPERTIES
    # The UNITY_GROUP properties set in the subdirectories below batch sources by
    # area, so each unity TU stays coherent instead of one giant translation unit.
    # That folds the grouped areas into 9 compiler invocations and parses their
    # headers once per group instead of once per file, on top of the PCH. Sources
    # with no UNITY_GROUP still compile per file, and src/nativeui opts out
    # entirely with SKIP_UNITY_BUILD_INCLUSION.
    UNITY_BUILD ON
    UNITY_BUILD_MODE "GROUP")

if (WITH_ASAN)
    target_compile_options(${JA2_BINARY} PRIVATE -fsanitize=address)
    target_link_options(${JA2_BINARY} PRIVATE -fsanitize=address)
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

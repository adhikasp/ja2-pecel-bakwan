# \file dependencies/lib-sol2/CMakeLists.txt

option(LOCAL_SOL_LIB "Download and build Sol2 instead of searching the system" ON)
if (NOT LOCAL_SOL_LIB)
    message(STATUS "Using system Sol2")
    find_package(sol2 "3.2.2"
        REQUIRED
        PATH_SUFFIXES lib/cmake/sol2 # path to sol2-config.cmake
    )
    if (NOT sol2_FOUND)
        message(FATAL_ERROR "Sol2 not found")
    endif()

    set(SOL_INCLUDE_DIR "${SOL2_INCLUDE_DIRS}" PARENT_SCOPE)
    return()
endif()

message(STATUS "<sol2>")

# create getter
set(SRC_DIR "${CMAKE_BINARY_DIR}/lib-sol2/src")
set(BUILD_DIR "${CMAKE_BINARY_DIR}/lib-sol2/build")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/getter/CMakeLists.txt.in"
    "${CMAKE_CURRENT_BINARY_DIR}/getter/CMakeLists.txt"
    @ONLY
)

# execute getter
execute_process(COMMAND ${CMAKE_COMMAND} . "-G${CMAKE_GENERATOR}" "-DCMAKE_SYSTEM_VERSION=${CMAKE_SYSTEM_VERSION}" WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/getter" RESULT_VARIABLE getter_result)
if(NOT getter_result EQUAL 0)
    message(FATAL_ERROR "configuring the sol2 getter failed: ${getter_result}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} --build "${CMAKE_CURRENT_BINARY_DIR}/getter" RESULT_VARIABLE getter_result)
if(NOT getter_result EQUAL 0)
    message(FATAL_ERROR "downloading sol2 failed: ${getter_result}")
endif()

# local fixes (gcc 15+, Lua 5.5); applies nothing when they are already in place
execute_process(
    COMMAND ${CMAKE_COMMAND} "-DSOL2_SRC=${SRC_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/apply-fixes.cmake"
    RESULT_VARIABLE fix_result)
if(NOT fix_result EQUAL 0)
    message(FATAL_ERROR "applying the local sol2 fixes failed: ${fix_result}")
endif()

# the download carries the archive's timestamps: refresh it when the pin changes, or everything
# compiled against the previous version stays stale
include(DepRefresh)
dep_refresh(sol2 "3.5.0-gcc15-lua55" "${SRC_DIR}/*")

set(SOL_INCLUDE_DIR ${SRC_DIR}/include PARENT_SCOPE)

message(STATUS "</sol2>")

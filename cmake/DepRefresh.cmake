# \file cmake/DepRefresh.cmake
#
# Dependency archives extract with the timestamps of the archive itself, which are older than
# every object already compiled against the previous version. After a dependency bump make would
# therefore keep the stale objects and link them against the new headers: a silent ABI mismatch
# (seen as undefined symbols at link time, or worse, not at all).
#
# Stamp each pin and touch its extracted tree when the pin changes, so exactly the dependants of
# that dependency rebuild once, right after the bump. Nothing happens on any other configure.
#
#   include(DepRefresh)
#   dep_refresh(<name> <pin> <glob> [<glob> ...])
#
# <pin> is anything that identifies the version, e.g. "5.5.1" or "3.5.0-gcc15-lua55".

function(dep_refresh name pin)
    set(stamp "${CMAKE_BINARY_DIR}/dep-pins/${name}.pin")
    set(previous "")
    if(EXISTS "${stamp}")
        file(READ "${stamp}" previous)
    endif()
    if(NOT previous STREQUAL "${pin}")
        file(GLOB_RECURSE sources ${ARGN})
        if(sources)
            file(TOUCH ${sources})
        endif()
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/dep-pins")
        file(WRITE "${stamp}" "${pin}")
        message(STATUS "${name} pinned to ${pin}: refreshed its sources so dependants rebuild")
    endif()
endfunction()

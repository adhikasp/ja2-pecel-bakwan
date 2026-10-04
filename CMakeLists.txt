# \file CMakeLists.txt

cmake_minimum_required (VERSION 3.18.1)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

## Enable generation of compile_commands.json for better IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

## Some macros

macro(add_custom_templated_target NAME)
    configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/target-${NAME}.cmake.in"
            "${CMAKE_CURRENT_BINARY_DIR}/cmake/target-${NAME}.cmake"
            IMMEDIATE @ONLY)

    add_custom_target(${NAME}
            COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake/target-${NAME}.cmake)
endmacro()

## Project Setup

# compiler caching
find_program(SCCACHE_PROGRAM "sccache")
set(USE_SCCACHE ON CACHE BOOL "Set to OFF to not cache compiler outputs")
if(SCCACHE_PROGRAM AND USE_SCCACHE)
    message(STATUS "Using sccache found at ${SCCACHE_PROGRAM} for caching build results")
    set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}" CACHE STRING "" FORCE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}" CACHE STRING "" FORCE)
endif()

project(ja2-stracciatella)
set(JA2_BINARY "ja2")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# linker: lld links the monolithic ja2 binary (450+ objects, debug info) several times faster
# than the default GNU ld. Only used when the toolchain actually ships it, so nothing changes on
# machines without lld (set -DUSE_LLD=OFF to keep GNU ld).
option(USE_LLD "Link with lld when the toolchain provides it" ON)
if(USE_LLD AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    find_program(LLD_PROGRAM "ld.lld")
    if(LLD_PROGRAM)
        message(STATUS "Linking with lld found at ${LLD_PROGRAM}")
        add_link_options("-fuse-ld=lld")
    endif()
endif()

## Semantic Versioning
# @see https://semver.org/
#
# The version must be set to:
#  * "0.<next minor>.0-dev" in the master branch
#  * "0.<minor>.<patch>" in release tags
#  * "0.<minor>.<next patch>-dev" in the "0.<minor>.x" branches
#
# TODO when releasing 1.0.0 we must decide what is a breaking change.
#      The master branch version will be "<major>.<next minor>.0-dev" before a
#      breaking change and "<next major>.0.0-dev" with a breaking change.

file(READ "version" BASE_SEMVER_VERSION)
# Trim leading and trailing whitespace / newlines
string(STRIP "${BASE_SEMVER_VERSION}" BASE_SEMVER_VERSION)

include(semver-functions)
semver_set(ja2-stracciatella_VERSION "${BASE_SEMVER_VERSION}-git")
if (VERSION_TAG)
    semver_set_prerelease(ja2-stracciatella_VERSION_PRERELEASE "-${VERSION_TAG}")
endif()
find_package(Git)
if (GIT_FOUND)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        RESULT_VARIABLE failed
        OUTPUT_VARIABLE hash
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if (NOT failed)
        list(APPEND ja2-stracciatella_VERSION_BUILDMETADATA "${hash}")
    endif()
endif()
semver_rebuild(ja2-stracciatella_VERSION)
message(STATUS "ja2-stracciatella version: ${ja2-stracciatella_VERSION}")

## Meta Information
set(CONTACT "JA2 Stracciatella Team <no-email@ja2-stracciatella.github.io>")
set(DESCRIPTION "An improved, cross-platform, stable Jagged Alliance 2 runtime.")

## Options
# Default CMAKE_BUILD_TYPE to RelWithDebInfo
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Choose the type of build, options are: Debug RelWithDebInfo." FORCE)
endif()
set(VERSION_TAG "" CACHE STRING "Build date for nightly versioning")
set(EXTRA_DATA_DIR "" CACHE STRING "Directory for externalized data")
set(INSTALL_LIB_DIR lib CACHE PATH "Installation directory for libraries")
set(LOCAL_SDL_LIB "" CACHE STRING "Use local SDL library from this directory")
# @see CARGO_BUILD_TARGET in dependencies/lib-stracciatella/CMakeLists.txt
option(BUILD_SDL_LIB "Build SDL from source" OFF)
option(LOCAL_GTEST_LIB "Build with local gtest lib" ON)
option(LOCAL_SMACKER_LIB "Build with local smacker lib" ON)
# @see LOCAL_STRING_THEORY_LIB in dependencies/lib-string_theory/CMakeLists.txt
option(WITH_UNITTESTS "Build with unittests" ON)
option(WITH_EDITOR_SLF "Include the latest free editor.slf" OFF)
option(WITH_RUST_BINARIES "Include rust binaries in build" ON)
# The native UI of docs/plan/native-modern-game.md (RmlUi + FreeType are fetched at configure time), and the Phase 0
# spikes built on it
if(ANDROID)
    option(WITH_NATIVE_UI "Build the native UI runtime (RmlUi); without it every screen uses its legacy UI" OFF)
    option(WITH_NATIVE_SPIKES "Build the native-UI / render spikes (ja2-spike, ja2.debug(\"uispike_*\"))" OFF)
else()
    option(WITH_NATIVE_UI "Build the native UI runtime (RmlUi); without it every screen uses its legacy UI" ON)
    option(WITH_NATIVE_SPIKES "Build the native-UI / render spikes (ja2-spike, ja2.debug(\"uispike_*\"))" ON)
endif()
if(WITH_NATIVE_SPIKES AND NOT WITH_NATIVE_UI)
    message(FATAL_ERROR "WITH_NATIVE_SPIKES needs WITH_NATIVE_UI")
endif()
# @see LOCAL_LUA_LIB in dependencies/lib-lua/CMakeLists.txt
# @see LOCAL_SOL_LIB in dependencies/lib-sol2/CMakeLists.txt
# @see LOCAL_MINIAUDIO_LIB in dependencies/lib-miniaudio/CMakeLists.txt
set(WITH_CUSTOM_LOCALE "" CACHE STRING "Set a custom locale at the start, leave empty to disable")
# Options specific to building C++ files can be found in the list file for
# the src directory.

if(MSVC)
    # set the source and execution charset
    add_compile_options(/utf-8 /bigobj)
    # suppress the min and max macro definitions
    # enable M_PI and other math constants from <cmath>
    add_compile_definitions(NOMINMAX _USE_MATH_DEFINES)

	foreach(_var
		CMAKE_CXX_FLAGS_DEBUG
		CMAKE_CXX_FLAGS_RELEASE
		CMAKE_CXX_FLAGS_MINSIZEREL
		CMAKE_CXX_FLAGS_RELWITHDEBINFO
		CMAKE_C_FLAGS_DEBUG
		CMAKE_C_FLAGS_RELEASE
		CMAKE_C_FLAGS_MINSIZEREL
		CMAKE_C_FLAGS_RELWITHDEBINFO
	)
        # force /Z7 instead of /ZI to include debug information in obj file
        string(REGEX REPLACE "/Zi|/ZI" "/Z7" _value ${${_var}})
		if(NOT ${${_var}} STREQUAL ${_value})
			set_property(CACHE "${_var}" PROPERTY VALUE "${_value}")
        endif()
	endforeach()
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT "ja2")
endif()

## Build

# The main target
if(ANDROID)
    add_library(${JA2_BINARY} SHARED)
else()
    add_executable(${JA2_BINARY})
endif()

if(NOT "${WITH_CUSTOM_LOCALE}" STREQUAL "")
    add_definitions("-DWITH_CUSTOM_LOCALE=\"${WITH_CUSTOM_LOCALE}\"")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

set(GAME_VERSION "v${ja2-stracciatella_VERSION}")

message(STATUS "Setting directory for libraries to: ${INSTALL_LIB_DIR}")
add_definitions(-DINSTALL_LIB_DIR="${INSTALL_LIB_DIR}")

if(BUILD_SDL_LIB)
    message(STATUS "Building SDL from source")
    add_subdirectory("dependencies/lib-sdl3")
elseif(NOT (LOCAL_SDL_LIB STREQUAL ""))
    message(STATUS "Using local SDL from " "${CMAKE_CURRENT_SOURCE_DIR}/${LOCAL_SDL_LIB}")
    # SDL3Config.cmake needs SDL_CPU_* to determine the library subdirectory for MSVC.
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SDL_CPU_X64 ON CACHE BOOL "" FORCE)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(SDL_CPU_X86 ON CACHE BOOL "" FORCE)
    endif()
    include("${CMAKE_CURRENT_SOURCE_DIR}/${LOCAL_SDL_LIB}")
else()
    find_package(SDL3 REQUIRED CONFIG REQUIRED COMPONENTS SDL3-shared)
endif()
function(print_sdl3_info)
    get_target_property(SDL3_LIBRARIES SDL3::SDL3-shared IMPORTED_LOCATION)
    get_target_property(SDL3_INCLUDES SDL3::Headers INTERFACE_INCLUDE_DIRECTORIES)
    message(STATUS "SDL3 Libraries: ${SDL3_LIBRARIES}; SDL3 Include Dir: ${SDL3_INCLUDES}")
endfunction()
print_sdl3_info()


# Dependencies must be processed first so the src directory can reliably use them.
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/dependencies/lib-magic_enum")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/dependencies/lib-lua")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/dependencies/lib-sol2")
if(LOCAL_SMACKER_LIB)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/dependencies/lib-smacker")
else()
    find_package(Smacker REQUIRED)
endif()
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/dependencies/lib-stracciatella")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/dependencies/lib-string_theory")
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/dependencies/lib-miniaudio")


if(WITH_NATIVE_UI)
    message(STATUS "Building the native UI runtime")
    add_subdirectory(src/nativeui)
    target_link_libraries(${JA2_BINARY} PRIVATE ja2-nativeui-core)
    target_compile_definitions(${JA2_BINARY} PRIVATE WITH_NATIVE_UI)
endif()

# Prepare compilation of our own C++ source files.
add_subdirectory(src)

if(WITH_NATIVE_SPIKES)
    message(STATUS "Building the Phase 0 native spikes")
    add_subdirectory(src/spike)
    target_link_libraries(${JA2_BINARY} PRIVATE ja2-spike-lib)
    target_compile_definitions(${JA2_BINARY} PRIVATE WITH_NATIVE_SPIKES)
    if(WITH_UNITTESTS)
        target_sources(${JA2_BINARY} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/spike/Spike_unittest.cc")
    endif()
endif()

if (WITH_UNITTESTS)
    message(STATUS "Compiling with unittests" )

    if (NOT LOCAL_GTEST_LIB)
        find_package(GTest REQUIRED)
    else()
        message(STATUS "Compiling with local GTest libraries from ${CMAKE_CURRENT_SOURCE_DIR}/dependencies/lib-gtest")
        add_subdirectory("dependencies/lib-gtest")
    endif()
    message(STATUS "GTest Libraries: ${GTEST_LIBRARIES}")

    target_include_directories(ja2 PRIVATE ${GTEST_INCLUDE_DIRS})
    target_compile_definitions(ja2 PRIVATE WITH_UNITTESTS)
endif()

target_link_libraries(${JA2_BINARY} PRIVATE
    SDL3::SDL3-shared
    ${GTEST_LIBRARIES}
    smacker
    ${STRACCIATELLA_LIBRARIES}
    string_theory-internal
    lua
)
if (MINGW)
    target_link_libraries(${JA2_BINARY} PRIVATE ntdll)
endif()
if (WIN32)
    # automation server (-serve) sockets
    target_link_libraries(${JA2_BINARY} PRIVATE ws2_32)
endif()
if (ANDROID)
    target_link_libraries(${JA2_BINARY} PRIVATE android log)
endif()

add_dependencies(${JA2_BINARY} stracciatella)
set_property(SOURCE ${CMAKE_SOURCE_DIR}/src/game/GameVersion.cc APPEND PROPERTY COMPILE_DEFINITIONS "GAME_VERSION=v${ja2-stracciatella_VERSION}")


## Copy assets only when their content differs: these commands run after every relink (and
## ja2-ui-assets on every build), where an unconditional copy_directory rewrites every file
## (~10s of I/O per relink on Windows). copy_directory_if_different needs CMake >= 3.26, older
## CMake keeps the previous behaviour.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.26)
    set(JA2_COPY_ASSETS_COMMAND -E copy_directory_if_different)
else()
    set(JA2_COPY_ASSETS_COMMAND -E copy_directory)
endif()

macro(copy_assets_dir_to_ja2_binary_after_build DIR)
    add_custom_command(TARGET ${JA2_BINARY} POST_BUILD
            COMMAND ${CMAKE_COMMAND} ${JA2_COPY_ASSETS_COMMAND}
            ${CMAKE_CURRENT_SOURCE_DIR}/assets/${DIR} "$<TARGET_FILE_DIR:${JA2_BINARY}>/${DIR}")
endmacro()

copy_assets_dir_to_ja2_binary_after_build("externalized")
copy_assets_dir_to_ja2_binary_after_build("unittests")
copy_assets_dir_to_ja2_binary_after_build("mods")
if(WITH_NATIVE_UI)
    # The native UI: design system (tokens, components, icons, fonts), screens, overlays, strings, gallery and mocks
    # (read at runtime, editable without a rebuild), copied on every build (not only when the binary relinks), so
    # RML/RCSS/SVG edits reach the build dir
    add_custom_target(ja2-ui-assets ALL
        COMMAND ${CMAKE_COMMAND} ${JA2_COPY_ASSETS_COMMAND} "${CMAKE_CURRENT_SOURCE_DIR}/assets/ui" "${CMAKE_CURRENT_BINARY_DIR}/ui")
    add_dependencies(${JA2_BINARY} ja2-ui-assets)
endif()

if(WITH_EDITOR_SLF)
    set(EDITORSLF_FILE "${CMAKE_CURRENT_BINARY_DIR}/externalized/editor.slf")
    find_package(EditorSlf REQUIRED)
    add_custom_templated_target(download-editor-slf)
    add_dependencies(${JA2_BINARY} download-editor-slf)
endif()

if (MSVC)
    add_custom_command(TARGET ${JA2_BINARY} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:SDL3::SDL3-shared> "$<TARGET_FILE_DIR:${JA2_BINARY}>")
endif()
if (APPLE AND NOT (LOCAL_SDL_LIB STREQUAL ""))
    add_custom_command(TARGET ${JA2_BINARY}
            POST_BUILD COMMAND
            ${CMAKE_INSTALL_NAME_TOOL} -add_rpath "@executable_path"
            "$<TARGET_FILE:${JA2_BINARY}>")
endif()

## Auxiliary targets

add_custom_target(run-${JA2_BINARY}
    COMMAND "$<TARGET_FILE:${JA2_BINARY}>"
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:${JA2_BINARY}>"
    DEPENDS "${JA2_BINARY}"
)

add_custom_target(run-${JA2_BINARY}-editor
    COMMAND "$<TARGET_FILE:${JA2_BINARY}>" -editor
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:${JA2_BINARY}>"
    DEPENDS "${JA2_BINARY}"
)

add_custom_target(run-${JA2_BINARY}-unittests
    COMMAND "$<TARGET_FILE:${JA2_BINARY}>" -unittests
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:${JA2_BINARY}>"
    DEPENDS "${JA2_BINARY}"
)

# --- E2E tests: the real game, headless, driven by Lua scripts ---
# They need the original game data (game_dir in your ja2.json) and Python 3
# (tools/ja2ctl.py gives every run a fresh, throwaway home directory).
if (WITH_UNITTESTS)
    enable_testing()
    add_test(
        NAME unittests
        COMMAND "$<TARGET_FILE:${JA2_BINARY}>" -unittests
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:${JA2_BINARY}>"
    )

    # The golden-image resolution tests need Pillow and numpy, which `uv sync`
    # installs into .venv/ from pyproject.toml/uv.lock. Prefer that interpreter
    # over a system one; a system Python with `pip install Pillow numpy` works too.
    set(JA2_VENV_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/.venv/bin/python")
    if (WIN32)
        set(JA2_VENV_PYTHON "${CMAKE_CURRENT_SOURCE_DIR}/.venv/Scripts/python.exe")
    endif()
    if (EXISTS "${JA2_VENV_PYTHON}")
        set(Python3_EXECUTABLE "${JA2_VENV_PYTHON}")
    endif()
    find_package(Python3 COMPONENTS Interpreter)

    if (Python3_Interpreter_FOUND)
        # Warn at configure time: the resolution matrix is long, and finding out
        # about a missing dependency an hour in is unhelpful.
        execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import PIL, numpy"
            RESULT_VARIABLE _ja2_pillow_numpy)
        if (_ja2_pillow_numpy)
            message(WARNING "Python at ${Python3_EXECUTABLE} has no Pillow/numpy, which the golden-image "
                "resolution tests need: run `uv sync` in ${CMAKE_CURRENT_SOURCE_DIR} and re-run cmake")
        endif()
    endif()

    # Phase 0 spikes. "spike" tests need no game data and run in CI; "spike-gamedata" ones drive the real game.
    if (WITH_NATIVE_SPIKES)
        add_test(NAME spike_unittests
            COMMAND "$<TARGET_FILE:${JA2_BINARY}>" -unittests
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:${JA2_BINARY}>")
        # the game's CLI parser does not take gtest flags; gtest reads the filter from the environment too
        set_tests_properties(spike_unittests PROPERTIES ENVIRONMENT "GTEST_FILTER=UiSpike.*:WorldSpike.*")
        add_test(NAME spike_ui_selftest
            COMMAND "$<TARGET_FILE:ja2-spike>" selftest "${CMAKE_CURRENT_BINARY_DIR}/spike-out")
        add_test(NAME spike_renderpath
            COMMAND "$<TARGET_FILE:ja2-spike>" renderpath --quick --no-gpu)
        set_tests_properties(spike_unittests spike_ui_selftest spike_renderpath PROPERTIES LABELS spike)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/spike-out")
        if (Python3_Interpreter_FOUND)
            add_test(NAME spike_asset_tools
                COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${CMAKE_CURRENT_SOURCE_DIR}/tools/assets/tests")
            set_tests_properties(spike_asset_tools PROPERTIES LABELS spike)
            foreach(n ui_spike world_spike asset_usage_tour style_demo gallery)
                add_test(NAME spike_${n}
                    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/ja2ctl.py" run "${CMAKE_CURRENT_SOURCE_DIR}/tests/spike/${n}.lua" --isolated
                        --res 1920x1080 --timeout 600 --out "${CMAKE_CURRENT_BINARY_DIR}/spike-out/${n}")
                set_tests_properties(spike_${n} PROPERTIES LABELS spike-gamedata
                    ENVIRONMENT "JA2_BIN=$<TARGET_FILE:${JA2_BINARY}>")
            endforeach()
        endif()
    endif()

    if (Python3_Interpreter_FOUND)
        set(JA2CTL "${CMAKE_CURRENT_SOURCE_DIR}/tools/ja2ctl.py")
        file(GLOB E2E_SCRIPTS
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/*.lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/*.txt")
        foreach(f ${E2E_SCRIPTS})
            get_filename_component(n ${f} NAME_WE)
            if(n MATCHES "^(tactical_layers|world_renderer|battle_)")
                continue() # run below, with layers or at a native resolution
            endif()
            add_test(
                NAME e2e_${n}
                COMMAND "${Python3_EXECUTABLE}" "${JA2CTL}" run "${f}" --isolated
                    --res 640x480 --timeout 600 --out "${CMAKE_CURRENT_BINARY_DIR}/e2e-out/${n}"
            )
        endforeach()
        # Tactical with the world as a layer of its own: (window, UI scale, world zoom)
        foreach(layers 2560x1440:2:1 3840x2160:3:2 1920x1080:2:1)
            string(REPLACE ":" ";" L ${layers})
            list(GET L 0 lres)
            list(GET L 1 lsu)
            list(GET L 2 lzw)
            add_test(
                NAME e2e_tactical_layers_${lres}_su${lsu}_zw${lzw}
                COMMAND "${Python3_EXECUTABLE}" "${JA2CTL}" run "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/tactical_layers.lua" --isolated
                    --res ${lres} --uiscale ${lsu} --worldzoom ${lzw} --timeout 600
                    --out "${CMAKE_CURRENT_BINARY_DIR}/e2e-out/tactical_layers_${lres}_su${lsu}_zw${lzw}"
            )
        endforeach()
        # Phase 8 world renderer: per-pixel equivalence (CPU pipeline always, GPU where there is one), frames and
        # picking; then the layered tactical test with the whole game drawn by the recorded pipeline.
        add_test(
            NAME e2e_world_renderer
            COMMAND "${Python3_EXECUTABLE}" "${JA2CTL}" run "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/world_renderer.lua" --isolated
                --res 1920x1080 --uiscale 2 --worldzoom 1 --timeout 900
                --out "${CMAKE_CURRENT_BINARY_DIR}/e2e-out/world_renderer"
        )
        add_test(
            NAME e2e_tactical_layers_pipeline
            COMMAND "${Python3_EXECUTABLE}" "${JA2CTL}" run "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/tactical_layers.lua" --isolated
                --res 1920x1080 --uiscale 2 --worldzoom 1 --timeout 600
                --out "${CMAKE_CURRENT_BINARY_DIR}/e2e-out/tactical_layers_pipeline"
        )
        add_test(
            NAME e2e_tactical_layers_ui
            COMMAND "${Python3_EXECUTABLE}" "${JA2CTL}" run "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/tactical_layers_ui.lua" --isolated
                --res 2560x1440 --uiscale 2 --worldzoom 1 --timeout 600
                --out "${CMAKE_CURRENT_BINARY_DIR}/e2e-out/tactical_layers_ui"
        )
        # Battle e2e (docs/plan/e2e-tactical-battles.md): a staged fight driven through the
        # native tactical HUD. The generic loop and the resolution matrix skip battle_*
        # scripts; the native HUD needs 1280x720, and a battle is long.
        foreach(f ${E2E_SCRIPTS})
            get_filename_component(n ${f} NAME_WE)
            if(NOT n MATCHES "^battle_")
                continue()
            endif()
            add_test(
                NAME e2e_${n}
                COMMAND "${Python3_EXECUTABLE}" "${JA2CTL}" run "${f}" --isolated
                    --res 1920x1080 --timeout 900 --out "${CMAKE_CURRENT_BINARY_DIR}/e2e-out/${n}"
            )
        endforeach()
        foreach(check determinism sessions)
            add_test(
                NAME e2e_${check}
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/check_${check}.py"
            )
        endforeach()
        get_property(E2E_TESTS DIRECTORY PROPERTY TESTS)
        list(FILTER E2E_TESTS INCLUDE REGEX "^e2e_")
        set_tests_properties(${E2E_TESTS} PROPERTIES
            LABELS e2e
            ENVIRONMENT "JA2_BIN=$<TARGET_FILE:${JA2_BINARY}>"
        )
        set_tests_properties(e2e_tactical_layers_pipeline PROPERTIES
            ENVIRONMENT "JA2_BIN=$<TARGET_FILE:${JA2_BINARY}>;JA2_WORLD_RENDERER=pipeline")
        # Resolution matrix: every tour at every supported resolution, with a
        # layout check (ja2.assertInsideScreen) and golden-screenshot compare.
        # `ctest -L resolution` is the default sweep and runs 1080p only; the
        # other resolutions are opt-in under the `matrix` label, or all of them
        # by name with `ctest -R resolution_`.
        set(RESOLUTION_MATRIX 640x480 1280x720 1920x1080 2560x1080 3440x1440)
        set(RESOLUTION_DEFAULT 1920x1080)
        # Longest tour first, so a parallel run fills its slots with the expensive tests instead of
        # leaving one slot to grind through the last long test alone. This order is what ctest uses on
        # a run with no cost data yet (a fresh build); later runs schedule by the measured cost from
        # CTestCostData.txt themselves, which is the same order. The cost here is each script's total
        # seconds over a full `ctest -R resolution_` run, descending; scripts it does not know about
        # keep the glob's order and follow.
        set(RESOLUTION_HEAVY_FIRST
            mapscreen_parity
            mapscreen_states
            saveload_parity
            mapscreen_equivalence
            menus_tour
            options_parity
            tactical_parity
            new_game_to_tactical
            video_switch
            tactical_hud
            tactical_move_merc
            credits_parity
            native_ui
            mainmenu_parity
            newgame_parity
            laptop_tour
            map_screen_tour
            loadingscreen_parity
            main_menu
        )
        set(RESOLUTION_SCRIPTS "")
        foreach(n ${RESOLUTION_HEAVY_FIRST})
            foreach(f ${E2E_SCRIPTS})
                get_filename_component(fn ${f} NAME_WE)
                if(fn STREQUAL n)
                    list(APPEND RESOLUTION_SCRIPTS ${f})
                endif()
            endforeach()
        endforeach()
        foreach(f ${E2E_SCRIPTS})
            list(FIND RESOLUTION_SCRIPTS ${f} known)
            if(known EQUAL -1)
                list(APPEND RESOLUTION_SCRIPTS ${f})
            endif()
        endforeach()
        set(RESOLUTION_TESTS "")
        foreach(f ${RESOLUTION_SCRIPTS})
            get_filename_component(n ${f} NAME_WE)
            get_filename_component(ext ${f} EXT)
            if(NOT ext STREQUAL ".lua" OR n MATCHES "^(tactical_layers|video_options|world_renderer|battle_)")
                continue()
            endif()
            foreach(res ${RESOLUTION_MATRIX})
                add_test(
                    NAME resolution_${n}_${res}
                    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e/check_resolution.py"
                        "${f}" ${res} --out "${CMAKE_CURRENT_BINARY_DIR}/e2e-out/resolution/${n}_${res}"
                )
                list(APPEND RESOLUTION_TESTS resolution_${n}_${res})
            endforeach()
        endforeach()
        set_tests_properties(${RESOLUTION_TESTS} PROPERTIES
            TIMEOUT 900
            ENVIRONMENT "JA2_BIN=$<TARGET_FILE:${JA2_BINARY}>"
        )
        # 1080p is the default sweep (`ctest -L resolution`); the other
        # resolutions carry the `matrix` label so they are opt-in.
        foreach(t ${RESOLUTION_TESTS})
            if(t MATCHES "_${RESOLUTION_DEFAULT}$")
                set_tests_properties(${t} PROPERTIES LABELS resolution)
            else()
                set_tests_properties(${t} PROPERTIES LABELS matrix)
            endif()
        endforeach()
        # the map screen tours are long (a campaign, every state) and render in software at 21:9
        foreach(t ${RESOLUTION_TESTS})
            if(t MATCHES "^resolution_mapscreen_")
                set_tests_properties(${t} PROPERTIES TIMEOUT 2400)
            endif()
        endforeach()
        add_custom_target(run-${JA2_BINARY}-resolution
            COMMAND ctest -L resolution --output-on-failure
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            DEPENDS "${JA2_BINARY}"
        )
        add_custom_target(run-${JA2_BINARY}-resolution-full
            COMMAND ctest -R resolution_ --output-on-failure
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            DEPENDS "${JA2_BINARY}"
        )
        add_custom_target(run-${JA2_BINARY}-e2e
            COMMAND ctest -L e2e --output-on-failure
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            DEPENDS "${JA2_BINARY}"
        )
    else()
        message(STATUS "Python 3 not found: e2e tests are not registered")
    endif()
endif()


## Installing and Packaging

set(CPACK_PACKAGE_VERSION_MAJOR ${ja2-stracciatella_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${ja2-stracciatella_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${ja2-stracciatella_VERSION_PATCH})
set(CPACK_PACKAGE_VERSION ${ja2-stracciatella_VERSION})

set(CPACK_PACKAGE_CONTACT ${CONTACT})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${DESCRIPTION})
set(CPACK_PACKAGE_DESCRIPTION ${DESCRIPTION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "JA2 Stracciatella")

set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://ja2-stracciatella.github.io/")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libsdl3, libstdc++6, libgcc1, libc6")

set(CPACK_BUNDLE_NAME "JA2 Stracciatella")
set(CPACK_BUNDLE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/logo.icns")
set(CPACK_BUNDLE_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/assets/distr-files-mac/BundleInfo.plist")
set(CPACK_BUNDLE_STARTUP_COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/assets/distr-files-mac/ja2-startup.sh")

set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortcut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\JA2 Stracciatella.lnk' '$INSTDIR\\\\ja2.exe' '' '$INSTDIR\\\\logo.ico'")
set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
set(CPACK_NSIS_MUI_FINISHPAGE_RUN "ja2.exe")
set(CPACK_NSIS_DISPLAY_NAME "JA2 Stracciatella")
set(CPACK_NSIS_PACKAGE_NAME "JA2 Stracciatella")
set(CPACK_NSIS_URL_INFO_ABOUT "https://ja2-stracciatella.github.io/")

if(UNIX AND NOT MINGW AND NOT APPLE)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PACKAGE_ARCH "x86-64")
    else()
        set(PACKAGE_ARCH "i386")
    endif()
elseif(MINGW)
    set(PACKAGE_ARCH "win-mingw")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PACKAGE_ARCH "${PACKAGE_ARCH}64")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(PACKAGE_ARCH "${PACKAGE_ARCH}32")
    endif()
    # build environment:
    if(MSYS)
        set(PACKAGE_ARCH "${PACKAGE_ARCH}-msys2") # MSYS Makefiles, assuming msys2 shell
    elseif(CMAKE_HOST_UNIX)
        set(PACKAGE_ARCH "${PACKAGE_ARCH}-cross") # cross compiling
    endif()
elseif(MSVC)
    set(PACKAGE_ARCH "win-msvc")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PACKAGE_ARCH "${PACKAGE_ARCH}64")
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(PACKAGE_ARCH "${PACKAGE_ARCH}32")
    endif()
    set(PACKAGE_ARCH "${PACKAGE_ARCH}-${MSVC_TOOLSET_VERSION}")
elseif(APPLE)
    set(PACKAGE_ARCH "${CMAKE_HOST_SYSTEM_PROCESSOR}")
else()
    set(PACKAGE_ARCH "unknown")
endif()
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}_${CPACK_PACKAGE_VERSION}_${PACKAGE_ARCH}")

include(CPack)

if (UNIX AND NOT MINGW AND NOT APPLE AND NOT ANDROID)
    install(TARGETS ${JA2_BINARY} RUNTIME DESTINATION bin)
    if(WITH_RUST_BINARIES)
        install(PROGRAMS "${STRACCIATELLA_EXECUTABLES}" DESTINATION bin)
    endif()
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/externalized assets/mods assets/unittests DESTINATION share/ja2)
    if(WITH_EDITOR_SLF)
        install(FILES "${EDITORSLF_FILE}" DESTINATION share/ja2/externalized)
    endif()
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/distr-files-linux/ja2-stracciatella.desktop DESTINATION share/applications)
    install(
        FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/logo.svg
        RENAME ja2-stracciatella.svg
        DESTINATION share/icons/hicolor/scalable/apps)
    install(DIRECTORY "assets/distr-files-linux/man6" TYPE MAN)
elseif(ANDROID)
    install(TARGETS ${JA2_BINARY} LIBRARY DESTINATION .)
else()
    install(TARGETS ${JA2_BINARY} RUNTIME DESTINATION .)
    if(WITH_RUST_BINARIES)
        install(PROGRAMS "${STRACCIATELLA_EXECUTABLES}" DESTINATION .)
    endif()
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/assets/externalized assets/mods assets/unittests DESTINATION .)
    if(WITH_EDITOR_SLF)
        install(FILES "${EDITORSLF_FILE}" DESTINATION ./externalized)
    endif()
    install(FILES changes.md DESTINATION .)
endif()
if(WITH_NATIVE_UI AND NOT ANDROID)
    # the native UI reads <binary dir>/ui, or <assets dir>/ui (share/ja2/ui on Linux)
    if(UNIX AND NOT MINGW AND NOT APPLE)
        set(NATIVE_UI_INSTALL_DIR share/ja2)
    else()
        set(NATIVE_UI_INSTALL_DIR .)
    endif()
    install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ui" DESTINATION ${NATIVE_UI_INSTALL_DIR})
endif()

if(MSVC OR MINGW)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)

    file(GLOB WIN_DIST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/distr-files-win/*.txt")
    install(FILES ${WIN_DIST_FILES} DESTINATION .)
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/logo.ico DESTINATION .)
endif()

if (MSVC)
    install(FILES $<TARGET_FILE:SDL3::SDL3-shared> DESTINATION .)
endif()

if (MINGW)
    get_target_property(_sdl3_dll SDL3::SDL3-shared IMPORTED_LOCATION_RELEASE)
    get_filename_component(SDL3_DLL_DIR "${_sdl3_dll}" DIRECTORY)
    configure_file("${CMAKE_SOURCE_DIR}/cmake/install-dlls-mingw.cmake.in" "${CMAKE_CURRENT_BINARY_DIR}/install-dlls-mingw.cmake" @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/install-dlls-mingw.cmake")
    unset(_sdl3_dll)
    unset(SDL3_DLL_DIR)
endif()

if(APPLE)
    file(GLOB APPLE_DIST_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/distr-files-mac/*.txt")
    install(FILES ${APPLE_DIST_FILES} DESTINATION .)
    if(NOT (LOCAL_SDL_LIB STREQUAL ""))
        # When using a local SDL3 (e.g. pre-built xcframework), install the
        # SDL3.framework next to the executables. CPack's Bundle generator puts
        # everything into Contents/Resources, which is what @executable_path
        # resolves to. On macOS the dynamic linker resolves
        # @rpath/SDL3.framework, so we must install SDL3.framework itself, not
        # the SDL3.xcframework wrapper.
        get_target_property(_sdl3_location SDL3::SDL3-shared IMPORTED_LOCATION)
        if(NOT _sdl3_location)
            message(FATAL_ERROR "SDL3::SDL3-shared has no IMPORTED_LOCATION")
        endif()
        get_filename_component(_sdl3_fw_path "${_sdl3_location}" REALPATH)
        # Walk up from <path>/SDL3.framework/SDL3 to find the SDL3.framework directory
        while(NOT _sdl3_fw_path MATCHES "/SDL3\\.framework$")
            get_filename_component(_sdl3_fw_path "${_sdl3_fw_path}" DIRECTORY)
            if("${_sdl3_fw_path}" STREQUAL "" OR "${_sdl3_fw_path}" STREQUAL "/")
                message(FATAL_ERROR "Could not find SDL3.framework directory from ${_sdl3_location}")
            endif()
        endwhile()
        install(DIRECTORY "${_sdl3_fw_path}" DESTINATION .)
    endif()
endif()

## Build AppImage

add_custom_target(package-appimage
    COMMAND rm -rf *.AppImage
    COMMAND linuxdeploy --appdir AppDir --output appimage
    COMMAND mv *.AppImage ${PROJECT_NAME}_${CPACK_PACKAGE_VERSION}_${PACKAGE_ARCH}.AppImage
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    DEPENDS "${JA2_BINARY}"
)

## Uninstall

add_custom_templated_target("uninstall")

## Rebuilding contributors.txt

add_custom_templated_target("rebuild-contributors-list")

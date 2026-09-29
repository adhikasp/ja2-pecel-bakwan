# Phase 0 spikes (docs/plan/native-modern-game.md): UI toolkit (RmlUi vs in-house), render path, world renderer.
# RmlUi and, when the system has none, FreeType are downloaded at configure time. RmlUi's own CMake project is
# not used (it installs itself into our packages); its core sources are compiled into a static library here.

include(FetchContent)

FetchContent_Declare(rmlui
    URL https://codeload.github.com/mikke89/RmlUi/tar.gz/refs/tags/6.1
    URL_HASH SHA256=B6088BF31858D31BFE657CAECF49FD12D5A34F9A37FA1C3061757410C4EB0089
    SOURCE_SUBDIR do-not-add-subdirectory
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(rmlui)

find_package(Freetype QUIET)
if(NOT TARGET Freetype::Freetype)
    message(STATUS "FreeType not found: building it from source for the UI spike")
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(freetype
        URL https://codeload.github.com/freetype/freetype/tar.gz/refs/tags/VER-2-13-3
        URL_HASH SHA256=BC5C898E4756D373E0D991BAB053036C5EB2AA7C0D5C67E8662DDC6DA40C4103
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

file(GLOB RMLUI_CORE_SOURCES
    "${rmlui_SOURCE_DIR}/Source/Core/*.cpp"
    "${rmlui_SOURCE_DIR}/Source/Core/Elements/*.cpp"
    "${rmlui_SOURCE_DIR}/Source/Core/Layout/*.cpp"
    "${rmlui_SOURCE_DIR}/Source/Core/FontEngineDefault/*.cpp")
add_library(ja2-rmlui STATIC ${RMLUI_CORE_SOURCES})
target_include_directories(ja2-rmlui SYSTEM PUBLIC "${rmlui_SOURCE_DIR}/Include")
target_compile_definitions(ja2-rmlui PUBLIC RMLUI_STATIC_LIB PRIVATE RMLUI_FONT_ENGINE_FREETYPE RMLUI_VERSION="6.1")
target_compile_features(ja2-rmlui PUBLIC cxx_std_17)
target_link_libraries(ja2-rmlui PRIVATE Freetype::Freetype)
if(NOT MSVC)
    # robin_hood.h tests SIZE_MAX against UINT64_MAX without including <cstdint> (breaks with MinGW)
    target_compile_options(ja2-rmlui PRIVATE -w -include cstdint)
endif()
set_target_properties(ja2-rmlui PROPERTIES UNITY_BUILD OFF)

# Fonts (LatoLatin, SIL OFL, shipped with RmlUi's samples) and the RML/RCSS documents are embedded, so the
# spike runs the same from the build dir, an install, CI and unit tests.
set(SPIKE_ASSETS
    "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf"
    "${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Bold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/savelist.rml"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/savelist.rcss")
set(SPIKE_EMBED_CC "${CMAKE_CURRENT_BINARY_DIR}/spike_embedded_assets.cc")
add_custom_command(
    OUTPUT "${SPIKE_EMBED_CC}"
    COMMAND ${CMAKE_COMMAND} -DOUT=${SPIKE_EMBED_CC} "-DFILES=${SPIKE_ASSETS}" -P "${CMAKE_CURRENT_SOURCE_DIR}/EmbedAssets.cmake"
    DEPENDS ${SPIKE_ASSETS} "${CMAKE_CURRENT_SOURCE_DIR}/EmbedAssets.cmake"
    VERBATIM)

add_library(ja2-spike-lib STATIC
    UiSpikeCommon.cc
    RmlSpike.cc
    InhouseSpike.cc
    RenderPathSpike.cc
    WorldSpikeRaster.cc
    "${SPIKE_EMBED_CC}")
target_include_directories(ja2-spike-lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${CMAKE_SOURCE_DIR}/src/sgp")
target_compile_features(ja2-spike-lib PUBLIC cxx_std_20)
target_link_libraries(ja2-spike-lib PUBLIC SDL3::SDL3-shared ja2-rmlui PRIVATE Freetype::Freetype)
set_target_properties(ja2-spike-lib PROPERTIES UNITY_BUILD OFF)
if(NOT MSVC)
    target_compile_options(ja2-spike-lib PRIVATE -include cstdint)
endif()

add_executable(ja2-spike spike_tool.cc)
target_link_libraries(ja2-spike PRIVATE ja2-spike-lib)
set_target_properties(ja2-spike PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(MINGW)
    target_link_options(ja2-spike PRIVATE -mconsole)
endif()

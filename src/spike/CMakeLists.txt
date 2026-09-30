# Phase 0 spikes (docs/plan/native-modern-game.md): UI toolkit (RmlUi vs in-house), render path, world renderer,
# and the Phase 1 style demos and design-system gallery. RmlUi, FreeType and the UI core come from src/nativeui.

# the in-house toolkit uses FreeType directly (an imported target is only visible in the directory that found it)
if(NOT TARGET Freetype::Freetype)
    find_package(Freetype REQUIRED)
endif()

# Fonts (LatoLatin, SIL OFL, shipped with RmlUi's samples) and the RML/RCSS documents are embedded, so the
# spike runs the same from the build dir, an install, CI and unit tests.
set(SPIKE_ASSETS
    "${RMLUI_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf"
    "${RMLUI_SOURCE_DIR}/Samples/assets/LatoLatin-Bold.ttf"
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
    RmlCommon.cc
    RmlSpike.cc
    StyleDemo.cc
    Gallery.cc
    InhouseSpike.cc
    RenderPathSpike.cc
    WorldSpikeRaster.cc
    "${SPIKE_EMBED_CC}")
target_include_directories(ja2-spike-lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${CMAKE_SOURCE_DIR}/src/sgp")
target_compile_features(ja2-spike-lib PUBLIC cxx_std_20)
target_link_libraries(ja2-spike-lib PUBLIC SDL3::SDL3-shared ja2-nativeui-core PRIVATE Freetype::Freetype)
set_target_properties(ja2-spike-lib PROPERTIES UNITY_BUILD OFF)
if(NOT MSVC)
    target_compile_options(ja2-spike-lib PRIVATE -include cstdint)
endif()

add_executable(ja2-spike spike_tool.cc)
target_link_libraries(ja2-spike PRIVATE ja2-spike-lib)
set_target_properties(ja2-spike PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(MINGW)
    target_compile_options(ja2-spike PRIVATE -include cstdint)
    target_link_options(ja2-spike PRIVATE -mconsole)
endif()

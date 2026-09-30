# The native UI toolkit layer (docs/plan/native-modern-game.md, Phase 2): RmlUi on SDL_Renderer, the design system's
# tokens, fonts and icons. RmlUi and, when the system has none, FreeType are downloaded at configure time. RmlUi's own
# CMake project is not used (it installs itself into our packages); its core sources are compiled into a static
# library here.

include(FetchContent)

FetchContent_Declare(rmlui
    URL https://codeload.github.com/mikke89/RmlUi/tar.gz/refs/tags/6.1
    URL_HASH SHA256=B6088BF31858D31BFE657CAECF49FD12D5A34F9A37FA1C3061757410C4EB0089
    SOURCE_SUBDIR do-not-add-subdirectory
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(rmlui)
set(RMLUI_SOURCE_DIR "${rmlui_SOURCE_DIR}" CACHE INTERNAL "RmlUi sources (the spikes embed its sample fonts)")

find_package(Freetype QUIET)
if(NOT TARGET Freetype::Freetype)
    message(STATUS "FreeType not found: building it from source for the native UI")
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

# The UI's Chinese fallback face (Noto Sans SC, SIL OFL 1.1, 17 MB) is too large for the repository: it is downloaded
# once, pinned by commit and hash, into the build's ui/fonts. Without it Chinese text shows missing glyphs; every
# other shipped language is covered by the committed fonts (docs/ui/design-system.md).
set(NOTO_SC_DIR "${CMAKE_BINARY_DIR}/ui/fonts/notosanssc")
set(NOTO_SC_URL "https://raw.githubusercontent.com/google/fonts/a85815a42757630ce188fdad368c2dfc444d4773/ofl/notosanssc")
if(NOT EXISTS "${NOTO_SC_DIR}/NotoSansSC[wght].ttf")
    file(DOWNLOAD "${NOTO_SC_URL}/NotoSansSC%5Bwght%5D.ttf" "${NOTO_SC_DIR}/NotoSansSC[wght].ttf"
        EXPECTED_HASH SHA256=a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da STATUS noto_status)
    list(GET noto_status 0 noto_code)
    if(noto_code EQUAL 0)
        file(DOWNLOAD "${NOTO_SC_URL}/OFL.txt" "${NOTO_SC_DIR}/OFL.txt")
    else()
        message(WARNING "Could not download Noto Sans SC (Chinese UI fallback): ${noto_status}")
        file(REMOVE "${NOTO_SC_DIR}/NotoSansSC[wght].ttf")
    endif()
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

# Render interface, clock, tokens, fonts, icons: shared by the game, the spikes and the unit tests.
add_library(ja2-nativeui-core STATIC
    UiCore.cc
    IconRaster.cc)
target_include_directories(ja2-nativeui-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ja2-nativeui-core PUBLIC cxx_std_20)
target_link_libraries(ja2-nativeui-core PUBLIC SDL3::SDL3-shared ja2-rmlui)
set_target_properties(ja2-nativeui-core PROPERTIES UNITY_BUILD OFF)
if(NOT MSVC)
    target_compile_options(ja2-nativeui-core PRIVATE -include cstdint)
endif()

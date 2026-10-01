#!/bin/sh
# Compiles the world renderer's compute shader (src/sgp/shaders/world_raster.comp) for every SDL_GPU backend and
# embeds the results in src/sgp/shaders/world_raster.comp.h, which is committed: building the game needs no shader
# tools. Run this after changing the shader.
#   SPIR-V (Vulkan)  glslangValidator
#   DXBC   (D3D12)   spirv-cross --hlsl (SM 5.1), then D3DCompile from the system's d3dcompiler_47.dll (Windows)
#   MSL    (Metal)   spirv-cross --msl from a variant with Metal's flat slot numbers (-DMSL_LAYOUT)
# Needs glslangValidator and spirv-cross (MSYS2: pacman -S mingw-w64-x86_64-glslang mingw-w64-x86_64-spirv-cross)
# and Python 3; the DXBC step needs Windows.
set -e
here=$(cd "$(dirname "$0")" && pwd)
py=$(command -v python3 || command -v python)
cd "$here/../../src/sgp/shaders"
tmp=$(mktemp -d)
glslangValidator -V --target-env vulkan1.0 -o "$tmp/w.spv" world_raster.comp
glslangValidator -V --target-env vulkan1.0 -DMSL_LAYOUT -o "$tmp/wm.spv" world_raster.comp
spirv-cross "$tmp/w.spv" --hlsl --shader-model 51 --output "$tmp/w.hlsl"
spirv-cross "$tmp/wm.spv" --msl --msl-version 20100 --msl-decoration-binding --output "$tmp/w.metal"
win() { if command -v cygpath > /dev/null; then cygpath -w "$1"; else echo "$1"; fi; }
"$py" "$here/dxbc.py" "$(win "$tmp/w.hlsl")" "$(win "$tmp/w.dxbc")"
"$py" "$here/embed.py" world_raster.comp.h world_raster.comp \
	"world_raster_spirv=$(win "$tmp/w.spv")" "world_raster_dxbc=$(win "$tmp/w.dxbc")" "world_raster_msl=$(win "$tmp/w.metal")"
rm -rf "$tmp"

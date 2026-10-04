#!/bin/sh
# Compiles the SDL_GPU shaders (src/sgp/shaders) for every backend and embeds the results in a committed
# header, so building the game needs no shader tools. Run this after changing a shader.
#   SPIR-V (Vulkan)  glslangValidator
#   DXBC   (D3D12)   spirv-cross --hlsl (SM 5.1), then D3DCompile from the system's d3dcompiler_47.dll (Windows)
#   MSL    (Metal)   spirv-cross --msl from a variant with Metal's flat slot numbers (-DMSL_LAYOUT)
# Needs glslangValidator and spirv-cross (MSYS2: pacman -S mingw-w64-x86_64-glslang mingw-w64-x86_64-spirv-cross)
# and Python 3; the DXBC step needs Windows.
#
#   world_raster.comp -> world_raster.comp.h   the world renderer's compute pass (WorldGpu.cc)
#   quad.vert/quad.frag -> quad.h              the layer/UI graphics pipeline (VideoGpu.cc, UiGpu.cc)
set -e
here=$(cd "$(dirname "$0")" && pwd)
py=$(command -v python3 || command -v python)
cd "$here/../../src/sgp/shaders"
tmp=$(mktemp -d)
win() { if command -v cygpath > /dev/null; then cygpath -w "$1"; else echo "$1"; fi; }

# ---- world_raster.comp ---------------------------------------------------------------------------
glslangValidator -V --target-env vulkan1.0 -o "$tmp/w.spv" world_raster.comp
glslangValidator -V --target-env vulkan1.0 -DMSL_LAYOUT -o "$tmp/wm.spv" world_raster.comp
spirv-cross "$tmp/w.spv" --hlsl --shader-model 51 --output "$tmp/w.hlsl"
spirv-cross "$tmp/wm.spv" --msl --msl-version 20100 --msl-decoration-binding --output "$tmp/w.metal"
"$py" "$here/dxbc.py" "$(win "$tmp/w.hlsl")" "$(win "$tmp/w.dxbc")"
"$py" "$here/embed.py" world_raster.comp.h world_raster.comp \
	"world_raster_spirv=$(win "$tmp/w.spv")" "world_raster_dxbc=$(win "$tmp/w.dxbc")" "world_raster_msl=$(win "$tmp/w.metal")"

# ---- quad.vert / quad.frag ------------------------------------------------------------------------
glslangValidator -V --target-env vulkan1.0 -o "$tmp/qv.spv" quad.vert
glslangValidator -V --target-env vulkan1.0 -o "$tmp/qf.spv" quad.frag
glslangValidator -V --target-env vulkan1.0 -DMSL_LAYOUT -o "$tmp/qfm.spv" quad.frag
spirv-cross "$tmp/qv.spv" --hlsl --shader-model 51 --output "$tmp/qv.hlsl"
spirv-cross "$tmp/qf.spv" --hlsl --shader-model 51 --output "$tmp/qf.hlsl"
spirv-cross "$tmp/qv.spv" --msl --msl-version 20100 --msl-decoration-binding --output "$tmp/qv.metal"
spirv-cross "$tmp/qfm.spv" --msl --msl-version 20100 --msl-decoration-binding --output "$tmp/qf.metal"
"$py" "$here/dxbc.py" "$(win "$tmp/qv.hlsl")" "$(win "$tmp/qv.dxbc")" vs_5_1
"$py" "$here/dxbc.py" "$(win "$tmp/qf.hlsl")" "$(win "$tmp/qf.dxbc")" ps_5_1
"$py" "$here/embed.py" quad.h quad \
	"quad_vert_spirv=$(win "$tmp/qv.spv")" "quad_vert_dxbc=$(win "$tmp/qv.dxbc")" "quad_vert_msl=$(win "$tmp/qv.metal")" \
	"quad_frag_spirv=$(win "$tmp/qf.spv")" "quad_frag_dxbc=$(win "$tmp/qf.dxbc")" "quad_frag_msl=$(win "$tmp/qf.metal")"

rm -rf "$tmp"

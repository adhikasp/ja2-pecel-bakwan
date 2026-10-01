#!/bin/sh
# Compiles the world renderer's shaders (src/sgp/shaders/*.comp) to SPIR-V and embeds them as C headers
# (<name>.spv.h next to the source). The headers are committed, so building the game needs no shader compiler;
# run this after changing a shader. Needs glslangValidator (MSYS2: pacman -S mingw-w64-x86_64-glslang,
# Debian/Ubuntu: glslang-tools, macOS: brew install glslang) and Python 3.
set -e
here=$(cd "$(dirname "$0")" && pwd)
py=$(command -v python3 || command -v python)
cd "$here/../../src/sgp/shaders"
for src in *.comp; do
	glslangValidator -V --target-env vulkan1.0 -o "$src.spv" "$src"
	"$py" "$here/embed.py" "$src.spv" "$src.spv.h"
	rm "$src.spv"
done

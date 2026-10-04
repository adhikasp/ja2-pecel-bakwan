#version 450
// The one vertex type of the SDL_GPU presentation (src/sgp/VideoGpu.cc) and of the native UI's GPU render
// interface (src/nativeui/UiGpu.cc): a position already in normalized device coordinates, a texture
// coordinate and a premultiplied colour. Both the compositor's layers and RmlUi's geometry are emitted as
// triangles of these, through the same graphics pipeline. There are no vertex resources (the NDC conversion
// is done on the CPU with the target size), and no uniforms, so the SPIR-V / DXBC / MSL layouts are trivial.
//
// Compiled by tools/shaders/build.sh into quad.h, which is committed: building the game needs no shader tools.

layout(location = 0) in vec2 aPos;   // NDC: (-1,-1) lower-left .. (1,1) upper-right
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor; // premultiplied (SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM)

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

void main()
{
	vUV = aUV;
	vColor = aColor;
	gl_Position = vec4(aPos, 0.0, 1.0);
}

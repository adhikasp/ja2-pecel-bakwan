#version 450
// The fragment stage that goes with quad.vert: one sampled texture times the interpolated (premultiplied)
// colour, written with premultiplied-alpha blending. The compositor binds the layer texture with a white
// vertex colour (and folds brightness into it); the native UI binds RmlUi's atlas / icon / face texture with
// RmlUi's own premultiplied colour.
//
// SDL_GPU resource layout: a sampled texture in set 2 (fragment). For MSL (spirv-cross --msl-decoration-binding)
// the set/binding become a flat index, so the -DMSL_LAYOUT build puts it at 0. Compiled by
// tools/shaders/build.sh into quad.h.

#ifdef MSL_LAYOUT
#define TEX set = 0, binding = 0
#else
#define TEX set = 2, binding = 0
#endif

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 oColor;

layout(TEX) uniform sampler2D uTexture;

void main()
{
	oColor = texture(uTexture, vUV) * vColor;
}

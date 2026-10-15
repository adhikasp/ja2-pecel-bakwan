#include "Visualizer.h"

#include "Animation_Control.h"
#include "Cheats.h"
#include "Handle_UI.h"
#include "Interface.h"
#include "JAScreens.h"
#include "SDL_helpers.h"
#include "SDL3/SDL.h"
#include "Soldier_Control.h"
#include "Structure_Internals.h"
#include "VideoGpu.h"
#include "WorldDef.h"

#include <memory>

namespace Visualizer
{

bool toggledOn{ };
unsigned long long DrawnFrames{};

std::unique_ptr<SDL_Texture, SDLDeleter> bgTexture;
std::unique_ptr<SDL_Texture, SDLDeleter> finalTexture;

constexpr SDL_FRect bgRectDest{ 11, 47, 240, 320 };
constexpr float isoToActualLengthRatio = 0.816f;
constexpr float actualEdgeLength = 120;
constexpr float isoEdgeLength = actualEdgeLength * isoToActualLengthRatio;
constexpr float isoBoxEdgeLength = isoEdgeLength / PROFILE_X_SIZE;
// lengths for an opposite and an adjacent leg of the triangle's isometric angle to the X axis
constexpr float oppLegLength = isoEdgeLength * 0.500f; // 0.500 is sin(30)
constexpr float adjLegLength = isoEdgeLength * 0.866f; // 0.866 is cos(30)
constexpr float oppBoxLegLength = isoBoxEdgeLength * 0.500f;
constexpr float adjBoxLegLength = isoBoxEdgeLength * 0.866f;

constexpr float boxHeight = actualEdgeLength / PROFILE_Z_SIZE;

constexpr SDL_FColor tileColorLvl0   { 0,      1,               0,          SDL_ALPHA_OPAQUE }; // green
constexpr SDL_FColor tileColorLvl1   { 0.254f, 0.141f,          0.94f,      SDL_ALPHA_OPAQUE }; // purple
constexpr SDL_FColor boxTopColor     { 0,      0.647f,          1,          SDL_ALPHA_OPAQUE }; // blue
constexpr SDL_FColor boxRightColor   { 0,      0.647f - 0.118f, 1 - 0.118f, SDL_ALPHA_OPAQUE };
constexpr SDL_FColor boxLeftColor    { 0,      0.647f - 0.059f, 1 - 0.059f, SDL_ALPHA_OPAQUE };
// Alternatives for the checkered pattern 
constexpr SDL_FColor boxTopColorAlt  { 0,      0.565f,          1,          SDL_ALPHA_OPAQUE };
constexpr SDL_FColor boxRightColorAlt{ 0,      0.565f - 0.118f, 1 - 0.118f, SDL_ALPHA_OPAQUE };
constexpr SDL_FColor boxLeftColorAlt { 0,      0.565f - 0.059f, 1 - 0.059f, SDL_ALPHA_OPAQUE };

// The coordinate that every other coordinate is calculated from
constexpr SDL_Vertex baseCoord{ { bgRectDest.w / 2, bgRectDest.h - 5 - oppLegLength * 2}, tileColorLvl0 };

// Green ground rhombus (isometric square)
constexpr SDL_Vertex verticesLvl0[]
{
	   baseCoord,																						 // farthest from the viewer
	{ {baseCoord.position.x + adjLegLength, baseCoord.position.y + oppLegLength},     baseCoord.color }, // right
	{ {baseCoord.position.x,                baseCoord.position.y + oppLegLength * 2}, baseCoord.color }, // nearest
	{ {baseCoord.position.x - adjLegLength, baseCoord.position.y + oppLegLength},     baseCoord.color }, // left
};

// Triangle rendering sequence
const int indices[]
{
   0,1,2,
   2,3,0,
};

// Texture update triggers
GridNo lastCell{ 0 };
int16_t lastInterfaceLvl{ I_GROUND_LEVEL };
AnimationStates lastAnimState{ NOTUSEDANIM1 };

bool Enabled()
{
	return toggledOn;
}

unsigned long long Drawn()
{
	return DrawnFrames;
}

int HitboxFaces(int const x, int const y, int const z, bool const roofOnly, Face out[3])
{
	float const offsetX = baseCoord.position.x + (x - y) * adjBoxLegLength;
	float const offsetY = baseCoord.position.y + (x + y) * oppBoxLegLength - boxHeight * (z + 1);

	// ensure checkered color pattern horizontally ...
	bool isAlt{ (bool)fmod(x + y, 2) };
	// ... and vertically
	if (fmod(z, 2))
	{
		isAlt = !isAlt;
	}

	// Top face: the rhombus of the tile.
	out[0].color = roofOnly ? tileColorLvl1 : (isAlt ? boxTopColorAlt : boxTopColor);
	out[0].x[0] = offsetX;
	out[0].y[0] = offsetY;
	out[0].x[1] = offsetX + adjBoxLegLength;
	out[0].y[1] = offsetY + oppBoxLegLength;
	out[0].x[2] = offsetX;
	out[0].y[2] = offsetY + oppBoxLegLength * 2;
	out[0].x[3] = offsetX - adjBoxLegLength;
	out[0].y[3] = offsetY + oppBoxLegLength;

	if (roofOnly) return 1;

	// Right face: the top's right edge, dropped by the box height.
	out[1].color = isAlt ? boxRightColorAlt : boxRightColor;
	out[1].x[0] = out[0].x[1];
	out[1].y[0] = out[0].y[1];
	out[1].x[1] = offsetX + adjBoxLegLength;
	out[1].y[1] = offsetY + oppBoxLegLength + boxHeight;
	out[1].x[2] = offsetX;
	out[1].y[2] = offsetY + oppBoxLegLength * 2 + boxHeight;
	out[1].x[3] = out[0].x[2];
	out[1].y[3] = out[0].y[2];

	// Left face: the top's left edge and the right face's lower edge.
	out[2].color = isAlt ? boxLeftColorAlt : boxLeftColor;
	out[2].x[0] = out[0].x[3];
	out[2].y[0] = out[0].y[3];
	out[2].x[1] = out[0].x[2];
	out[2].y[1] = out[0].y[2];
	out[2].x[2] = out[1].x[2];
	out[2].y[2] = out[1].y[2];
	out[2].x[3] = offsetX - adjBoxLegLength;
	out[2].y[3] = offsetY + oppBoxLegLength + boxHeight;

	return 3;
}

static void DrawHitbox(SDL_Renderer* const renderer, int const x, int const y, int const z,
					   bool const drawRoofOnly = false)
{
	Face faces[3];
	int const count = HitboxFaces(x, y, z, drawRoofOnly, faces);
	for (int i = 0; i < count; ++i)
	{
		SDL_Vertex vertices[4];
		for (int v = 0; v < 4; ++v)
		{
			vertices[v].position = { faces[i].x[v], faces[i].y[v] };
			vertices[v].color = faces[i].color;
		}
		SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
	}
}

/** Walks every hitbox the tile at lastCell has, at the current interface level, calling @a draw for each one.
 * The shape coalescing is the same for the SDL_Renderer overlay and the SDL_GPU one. */
template <typename Draw> static void ForEachHitbox(Draw&& draw)
{
	uint8_t combinedShapeLvl0[PROFILE_X_SIZE][PROFILE_Y_SIZE]{};
	uint8_t combinedShapeLvl1[PROFILE_X_SIZE][PROFILE_Y_SIZE]{};
	bool isWalkableRoofPresent{ (bool)gpWorldLevelData[lastCell].pRoofHead };
	for (uint8_t loopX = 0; loopX < PROFILE_X_SIZE; loopX++)
	{
		for (uint8_t loopY = 0; loopY < PROFILE_Y_SIZE; loopY++)
		{
			STRUCTURE* structure{ gpWorldLevelData[lastCell].pStructureHead };
			bool isAnyRoofPresent{ };
			while (structure)
			{
				PROFILE* shape{ structure->pShape };
				// Coalesce all the structures' shapes in the tile into a single shape
				if (structure->sCubeOffset == 0)
				{
					combinedShapeLvl0[loopX][loopY] |= (*shape)[loopX][loopY];
				}
				else
				{
					isAnyRoofPresent = structure->fFlags & STRUCTURE_ROOF;
					combinedShapeLvl1[loopX][loopY] |= (*shape)[loopX][loopY];
				}
				structure = structure->pNext;
			}

			for (uint8_t loopZ = 0; loopZ < PROFILE_Z_SIZE; loopZ++)
			{
				if (combinedShapeLvl0[loopX][loopY] & AtHeight[loopZ])
				{
					draw(loopX, loopY, loopZ, false);
				}
				if (loopZ == PROFILE_Z_SIZE - 1 && isWalkableRoofPresent && lastInterfaceLvl == I_ROOF_LEVEL)
				{
					draw(loopX, loopY, loopZ, true);
				}
			}

			if (isAnyRoofPresent && lastInterfaceLvl == I_GROUND_LEVEL)
				continue;

			for (uint8_t loopZ = 0; loopZ < PROFILE_Z_SIZE; loopZ++)
			{
				if (combinedShapeLvl1[loopX][loopY] & AtHeight[loopZ])
				{
					draw(loopX, loopY, loopZ + PROFILE_Z_SIZE, false);
				}
			}
		}
	}
}

static void UpdateTexture(SDL_Renderer* const renderer)
{
	SDL_SetRenderTarget(renderer, finalTexture.get());

	SDL_RenderTexture(renderer, bgTexture.get(), nullptr, nullptr);

	ForEachHitbox([&](int const x, int const y, int const z, bool const roofOnly)
				  { DrawHitbox(renderer, x, y, z, roofOnly); });

	SDL_SetRenderTarget(renderer, nullptr);
}

static void InitTextures(SDL_Renderer* const renderer)
{
	finalTexture.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, bgRectDest.w, bgRectDest.h));

	bgTexture.reset(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, bgRectDest.w, bgRectDest.h));
	SDL_SetRenderTarget(renderer, bgTexture.get());

	// Black background
	SDL_SetRenderDrawColor(renderer, 10, 10, 10, SDL_ALPHA_OPAQUE);
	SDL_RenderFillRect(renderer, nullptr);
	// Green ground tile
	SDL_RenderGeometry(renderer, nullptr, verticesLvl0, 4, indices, 6);

	SDL_SetRenderTarget(renderer, nullptr);
}

void Render(SDL_Renderer * const renderer)
{
	if (!toggledOn)
	{
		return;
	}

	if (guiCurrentScreen != GAME_SCREEN)
	{
		ToggleOnOff();
	}
	if (!finalTexture)
	{
		InitTextures(renderer);
	}

	AnimationStates currAnimState{ };
	if (gpWorldLevelData[lastCell].pMercHead)
	{
		currAnimState = static_cast<AnimationStates>( gpWorldLevelData[lastCell].pMercHead->pSoldier->usAnimState);
	}
	else
	{
		lastAnimState = NOTUSEDANIM1;
	}

	if (lastCell != guiCurrentCursorGridNo || lastInterfaceLvl != gsInterfaceLevel || currAnimState != lastAnimState )
	{
		lastCell = guiCurrentCursorGridNo;
		lastInterfaceLvl = gsInterfaceLevel;
		UpdateTexture(renderer);
	}

	SDL_RenderTexture(renderer, finalTexture.get(), nullptr, &bgRectDest);
	// set this back to total black for subsequent SDL_RenderClear
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
	++DrawnFrames;
}

bool RecordGpuQuads(float const originX, float const originY, float const scale)
{
	if (!toggledOn)
	{
		return false;
	}

	if (guiCurrentScreen != GAME_SCREEN)
	{
		ToggleOnOff();
		return false;
	}

	lastCell = guiCurrentCursorGridNo;
	lastInterfaceLvl = gsInterfaceLevel;

	auto const px = [&](float const x) { return originX + (bgRectDest.x + x) * scale; };
	auto const py = [&](float const y) { return originY + (bgRectDest.y + y) * scale; };
	auto const quad = [&](Face const& f)
	{
		VideoGpu::OverlayQuad(px(f.x[0]), py(f.y[0]), px(f.x[1]), py(f.y[1]), px(f.x[2]), py(f.y[2]), px(f.x[3]),
							  py(f.y[3]), f.color.r, f.color.g, f.color.b, f.color.a);
	};

	// The dark panel the hitboxes sit on.
	VideoGpu::OverlayQuad(px(0), py(0), px(bgRectDest.w), py(0), px(bgRectDest.w), py(bgRectDest.h), px(0),
						  py(bgRectDest.h), 10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);

	// The green ground rhombus.
	Face ground{};
	for (int i = 0; i < 4; ++i)
	{
		ground.x[i] = verticesLvl0[i].position.x;
		ground.y[i] = verticesLvl0[i].position.y;
	}
	ground.color = verticesLvl0[0].color;
	quad(ground);

	Face faces[3];
	ForEachHitbox(
		[&](int const x, int const y, int const z, bool const roofOnly)
		{
			int const count = HitboxFaces(x, y, z, roofOnly, faces);
			for (int i = 0; i < count; ++i) quad(faces[i]);
		});

	++DrawnFrames;
	return true;
}

void ToggleOnOff()
{
	if (guiCurrentScreen == GAME_SCREEN && !toggledOn && CHEATER_CHEAT_LEVEL())
	{
		lastCell = 0;
		toggledOn = true;
	}
	else if (toggledOn)
	{
		toggledOn = false;
		bgTexture.reset();
		finalTexture.reset();
	}
}

}

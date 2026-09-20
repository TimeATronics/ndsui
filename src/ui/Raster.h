// Tiny CPU triangle rasterizer for the carousel: draws textured/flat-shaded
// triangles straight into the UI surface. The Brick's vendor SDL renderer
// silently ignores SDL_RenderGeometryRaw, so rasterizing ourselves is the
// reliable path (and it composites into the same surface as everything else).
//
// All carousel geometry is flat-shaded per face, so the shading is constant
// per triangle: the inner loop is integer texel * shade.
#pragma once

#include <cstdint>

namespace ndsui {

class Canvas;

struct RasterVert {
  float x, y;          // screen pixels
  float u, v;          // texture coords in [0,1]
  float nz;            // NDC depth for the z-buffer (-1 near, +1 far)
  float nx, ny, nz2;   // world normal (culling)
  float wx, wy, wz;    // world position (culling)
};

// shadeR/G/B are 0..255 multipliers applied to the texel (or used flat when
// texPixels is null).
void rasterTriangleFlat(Canvas& c, const RasterVert& a, const RasterVert& b,
                        const RasterVert& d, const uint32_t* texPixels,
                        int texW, int texH, int texPitchPx, int shadeR,
                        int shadeG, int shadeB, float* zbuf, int zbufX,
                        int zbufY, int zbufW, int zbufH);

// Soft drop shadow ellipse (used under carousel carts) written straight into
// the current raster target with per-pixel alpha.
void rasterShadow(Canvas& c, float cx, float cy, float rx, float ry, int alpha);
// rounded-rectangle shadow for box-shaped cartridges
void rasterShadowRect(Canvas& c, float cx, float cy, float hw, float hh,
                      int alpha);

}  // namespace ndsui

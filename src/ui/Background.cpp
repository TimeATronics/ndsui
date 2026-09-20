#include "ui/Background.h"

#include <cmath>
#include <vector>

#include "ui/Components.h"
#include "ui/Theme.h"

namespace ndsui {
namespace ui {

void Background::init() { m_inited = true; }

namespace {

struct Layer {
  int cell;
  int thick;
  int majorEvery;  // 0 = no major lines
  uint32_t line;
  uint32_t major;
  float parallax;
};

struct VLine {
  int x;
  int thick;
  uint32_t col;
};

}  // namespace

// The grid is drawn as one row-major raster pass: SDL_FillRect per line made
// this the most expensive draw in the frame (scattered column writes).
void Background::draw(Canvas& c, float offsetX, float offsetY) {
  drawRows(c, offsetX, offsetY, 0, Canvas::H);
}

void Background::drawRows(Canvas& c, float offsetX, float offsetY, int bandY0,
                          int bandY1) {
  static const Layer kLayers[3] = {
      {96, 2, 0, kGridFar, kGridFar, 0.18f},
      {32, 2, 4, kGridLine, kGridLineMajor, 0.30f},
      {8, 1, 0, kGridFine, kGridFine, 0.55f},
  };
  const int W = Canvas::W, H = Canvas::H;

  // per-row: which layer's horizontal line is on top, and its pixel color
  std::vector<int> topLayer(H, -1);
  std::vector<uint32_t> topCol(H, c.map(kPaper));
  std::vector<VLine> verts[3];

  for (int i = 0; i < 3; ++i) {
    const Layer& L = kLayers[i];
    const uint32_t colLine = c.map(L.line);
    const uint32_t colMajor = c.map(L.major);

    float oy = -offsetY * L.parallax;
    int y0 = (int)fmodf(oy, (float)L.cell);
    if (y0 > 0) y0 -= L.cell;
    for (int y = y0, idx = (int)std::floor(oy / L.cell); y < H;
         y += L.cell, ++idx) {
      bool major = L.majorEvery > 0 && (idx % L.majorEvery == 0);
      int t = major ? L.thick + 1 : L.thick;
      uint32_t col = major ? colMajor : colLine;
      for (int k = 0; k < t; ++k) {
        int yy = y + k;
        if (yy >= 0 && yy < H) {
          topLayer[yy] = i;
          topCol[yy] = col;
        }
      }
    }

    float ox = -offsetX * L.parallax;
    int x0 = (int)fmodf(ox, (float)L.cell);
    if (x0 > 0) x0 -= L.cell;
    for (int x = x0, idx = (int)std::floor(ox / L.cell); x < W;
         x += L.cell, ++idx) {
      bool major = L.majorEvery > 0 && (idx % L.majorEvery == 0);
      verts[i].push_back({x, major ? L.thick + 1 : L.thick,
                          major ? colMajor : colLine});
    }
  }

  if (bandY0 < 0) bandY0 = 0;
  if (bandY1 > H) bandY1 = H;
  for (int y = bandY0; y < bandY1; ++y) {
    uint32_t* row = c.rowPtr(y);
    if (!row) break;
    SDL_memset4(row, topCol[y], W);
    int tl = topLayer[y];
    for (int i = 0; i < 3; ++i) {
      if (i < tl) continue;  // covered by a higher layer's horizontal line
      for (const VLine& v : verts[i]) {
        for (int k = 0; k < v.thick; ++k) {
          int xx = v.x + k;
          if (xx >= 0 && xx < W) row[xx] = v.col;
        }
      }
    }
  }
  c.markRegionDirty(0, bandY0, W, bandY1 - bandY0);
}

}  // namespace ui
}  // namespace ndsui

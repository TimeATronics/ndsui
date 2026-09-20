#include "ui/Raster.h"

#include <algorithm>
#include <cmath>

#include "ui/Canvas.h"

namespace ndsui {

namespace {
inline float edf(float ax, float ay, float bx, float by, float px, float py) {
  return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}
inline int clampi(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}
}  // namespace

void rasterTriangleFlat(Canvas& c, const RasterVert& a, const RasterVert& b,
                        const RasterVert& d, const uint32_t* texPixels,
                        int texW, int texH, int texPitchPx, int shadeR,
                        int shadeG, int shadeB, float* zbuf, int zbufX,
                        int zbufY, int zbufW, int zbufH) {
  uint32_t* fb = c.rowPtr(0);
  if (!fb) return;
  const int pitch = c.pitchPixels();
  const int W = c.targetW(), H = c.targetH();
  const int zstride = zbuf ? zbufW : 0;
  const bool textured = texPixels && texW > 0 && texH > 0;

  float area = edf(a.x, a.y, b.x, b.y, d.x, d.y);
  if (std::fabs(area) < 1e-4f) return;
  float inv = 1.f / area;

  const RasterVert* v[3] = {&a, &b, &d};
  int minX = clampi((int)std::floor(std::min(a.x, std::min(b.x, d.x))), 0,
                    W - 1);
  int maxX = clampi((int)std::ceil(std::max(a.x, std::max(b.x, d.x))), 0,
                    W - 1);
  int minY =
      clampi((int)std::floor(std::min(a.y, std::min(b.y, d.y))), 0, H - 1);
  int maxY =
      clampi((int)std::ceil(std::max(a.y, std::max(b.y, d.y))), 0, H - 1);
  if (zbuf) {
    minX = std::max(minX, zbufX);
    maxX = std::min(maxX, zbufX + zbufW - 1);
    minY = std::max(minY, zbufY);
    maxY = std::min(maxY, zbufY + zbufH - 1);
  }
  if (minX > maxX || minY > maxY) return;

  const float dx0 = (d.y - b.y) * inv;
  const float dx1 = (a.y - d.y) * inv;
  const float dx2 = (b.y - a.y) * inv;
  const float du_dx = a.u * dx0 + b.u * dx1 + d.u * dx2;
  const float dv_dx = a.v * dx0 + b.v * dx1 + d.v * dx2;
  const float dnz_dx = a.nz * dx0 + b.nz * dx1 + d.nz * dx2;
  const float texWf = (float)(texW - 1);
  const float texHf = (float)(texH - 1);
  const int tmaxX = texW - 1, tmaxY = texH - 1;
  const uint32_t flatPx =
      0xFF000000u | ((uint32_t)shadeR << 16) | ((uint32_t)shadeG << 8) |
      (uint32_t)shadeB;

  int touchedX0 = W, touchedY0 = H, touchedX1 = 0, touchedY1 = 0;

  for (int y = minY; y <= maxY; ++y) {
    const float py = (float)y + 0.5f;
    float xl = 1e30f, xr = -1e30f;
    for (int e = 0; e < 3; ++e) {
      const RasterVert& p = *v[e];
      const RasterVert& q = *v[(e + 1) % 3];
      if ((p.y <= py && q.y >= py) || (q.y <= py && p.y >= py)) {
        float dy = q.y - p.y;
        float t = std::fabs(dy) < 1e-9f ? 0.f : (py - p.y) / dy;
        float x = p.x + (q.x - p.x) * t;
        xl = std::min(xl, x);
        xr = std::max(xr, x);
      }
    }
    if (xl > xr) continue;
    int sxp = clampi((int)std::ceil(xl - 0.5f), 0, W - 1);
    int exp = clampi((int)std::floor(xr - 0.5f), 0, W - 1);
    if (sxp > exp) continue;

    float w0 = edf(b.x, b.y, d.x, d.y, sxp + 0.5f, py) * inv;
    float w1 = edf(d.x, d.y, a.x, a.y, sxp + 0.5f, py) * inv;
    float w2 = edf(a.x, a.y, b.x, b.y, sxp + 0.5f, py) * inv;
    float uu = a.u * w0 + b.u * w1 + d.u * w2;
    float vv = a.v * w0 + b.v * w1 + d.v * w2;
    float nz = a.nz * w0 + b.nz * w1 + d.nz * w2;

    uint32_t* row = fb + (size_t)y * pitch;
    float* zrow = zbuf ? zbuf + (size_t)(y - zbufY) * zstride - zbufX : nullptr;

    if (!textured) {
      // flat fill: no interpolation at all
      for (int x = sxp; x <= exp; ++x) {
        if (zrow) {
          float* zp = zrow + x;
          if (nz >= *zp) goto stepf;
          *zp = nz;
        }
        row[x] = flatPx;
        touchedX0 = std::min(touchedX0, x);
        touchedX1 = std::max(touchedX1, x + 1);
        touchedY0 = std::min(touchedY0, y);
        touchedY1 = std::max(touchedY1, y + 1);
      stepf:
        w0 += dx0;
        w1 += dx1;
        w2 += dx2;
        uu += du_dx;
        vv += dv_dx;
        nz += dnz_dx;
      }
    } else {
      const int sr = shadeR, sg = shadeG, sb = shadeB;
      for (int x = sxp; x <= exp; ++x) {
        if (zrow) {
          float* zp = zrow + x;
          if (nz >= *zp) goto stept;
          *zp = nz;
        }
        {
          int tx = (int)(uu * texWf + 0.5f);
          int ty = (int)(vv * texHf + 0.5f);
          tx = tx < 0 ? 0 : (tx > tmaxX ? tmaxX : tx);
          ty = ty < 0 ? 0 : (ty > tmaxY ? tmaxY : ty);
          uint32_t s = texPixels[(size_t)ty * texPitchPx + tx];
          unsigned ta = s >> 24;
          if (ta) {
            int r = (int)((s >> 16) & 0xFF) * sr >> 8;
            int g = (int)((s >> 8) & 0xFF) * sg >> 8;
            int bl = (int)(s & 0xFF) * sb >> 8;
            if (ta >= 255) {
              row[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) |
                       (uint32_t)bl;
            } else {
              uint32_t dst = row[x];
              int dr = (int)((dst >> 16) & 0xFF), dg = (int)((dst >> 8) & 0xFF),
                  db = (int)(dst & 0xFF);
              r = (r * (int)ta + dr * (255 - (int)ta)) >> 8;
              g = (g * (int)ta + dg * (255 - (int)ta)) >> 8;
              bl = (bl * (int)ta + db * (255 - (int)ta)) >> 8;
              row[x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) |
                       (uint32_t)bl;
            }
            touchedX0 = std::min(touchedX0, x);
            touchedX1 = std::max(touchedX1, x + 1);
            touchedY0 = std::min(touchedY0, y);
            touchedY1 = std::max(touchedY1, y + 1);
          }
        }
      stept:
        w0 += dx0;
        w1 += dx1;
        w2 += dx2;
        uu += du_dx;
        vv += dv_dx;
        nz += dnz_dx;
      }
    }
  }
  if (touchedX1 > touchedX0)
    c.markRegionDirty(touchedX0, touchedY0, touchedX1 - touchedX0,
                      touchedY1 - touchedY0);
}

void rasterShadow(Canvas& c, float cx, float cy, float rx, float ry,
                  int alpha) {
  uint32_t* fb = c.rowPtr(0);
  if (!fb || rx < 1.f || ry < 1.f) return;
  const int pitch = c.pitchPixels();
  const int W = c.targetW(), H = c.targetH();
  int x0 = clampi((int)(cx - rx), 0, W - 1);
  int x1 = clampi((int)(cx + rx), 0, W - 1);
  int y0 = clampi((int)(cy - ry), 0, H - 1);
  int y1 = clampi((int)(cy + ry), 0, H - 1);
  for (int y = y0; y <= y1; ++y) {
    float dy = (y + 0.5f - cy) / ry;
    uint32_t* row = fb + (size_t)y * pitch;
    for (int x = x0; x <= x1; ++x) {
      float dx = (x + 0.5f - cx) / rx;
      float d = dx * dx + dy * dy;
      if (d >= 1.f) continue;
      // soft edge falloff
      int a = (int)(alpha * (1.f - d) * (1.f - d));
      if (a <= 0) continue;
      uint32_t dst = row[x];
      int da = (int)(dst >> 24);
      int na = a + da * (255 - a) / 255;
      row[x] = ((uint32_t)na << 24) | (dst & 0x00FFFFFF);
    }
  }
  c.markRegionDirty(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

void rasterShadowRect(Canvas& c, float cx, float cy, float hw, float hh,
                      int alpha) {
  uint32_t* fb = c.rowPtr(0);
  if (!fb || hw < 1.f || hh < 1.f) return;
  const int pitch = c.pitchPixels();
  const int W = c.targetW(), H = c.targetH();
  int x0 = clampi((int)(cx - hw), 0, W - 1);
  int x1 = clampi((int)(cx + hw), 0, W - 1);
  int y0 = clampi((int)(cy - hh), 0, H - 1);
  int y1 = clampi((int)(cy + hh), 0, H - 1);
  const float soft = std::min(hw, hh) * 0.35f + 1.f;
  for (int y = y0; y <= y1; ++y) {
    float dy = std::fabs((float)y + 0.5f - cy) - (hh - soft);
    uint32_t* row = fb + (size_t)y * pitch;
    for (int x = x0; x <= x1; ++x) {
      float dx = std::fabs((float)x + 0.5f - cx) - (hw - soft);
      float d = std::max(dx, dy) / soft;  // rounded-box distance
      if (d >= 1.f) continue;
      int a = (int)(alpha * (1.f - d) * (1.f - d));
      if (a <= 0) continue;
      uint32_t dst = row[x];
      int da = (int)(dst >> 24);
      int na = a + da * (255 - a) / 255;
      row[x] = ((uint32_t)na << 24) | (dst & 0x00FFFFFF);
    }
  }
  c.markRegionDirty(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

}  // namespace ndsui

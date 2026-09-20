#include "ui/Canvas.h"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "ui/Gl.h"
#include "ui/Mesh.h"
#include "ui/Theme.h"
#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {

using namespace gl;

// ---------------------------------------------------------------- init

bool Canvas::init(const std::string& fontPath) {
  SDL_InitSubSystem(SDL_INIT_VIDEO);
  SDL_ShowCursor(0);

  m_headless = getenv("NDS_HEADLESS") != nullptr;
  m_surface = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32,
                                             SDL_PIXELFORMAT_ARGB8888);
  if (!m_surface) return false;
  SDL_SetSurfaceBlendMode(m_surface, SDL_BLENDMODE_NONE);
  SDL_FillRect(m_surface, nullptr, SDL_MapRGB(m_surface->format, 30, 30, 40));
  LOG_INFO("surface: %s bpp=%d amask=%08X pitch=%d", 
           SDL_GetPixelFormatName(m_surface->format->format),
           m_surface->format->BitsPerPixel, m_surface->format->Amask,
           m_surface->pitch);
  if (TTF_Init() != 0) {
    LOG_ERROR("TTF_Init: %s", TTF_GetError());
    return false;
  }
  (void)fontPath;

  // the cart raster surface exists in both paths (blitted when headless)
  m_cartSurface = SDL_CreateRGBSurfaceWithFormat(0, m_cartW, m_cartH, 32,
                                                 SDL_PIXELFORMAT_ARGB8888);
  if (!m_headless) {
    if (!initGl()) {
      LOG_WARN("GL unavailable; running without 3D");
    }
  }
  // fresh surface: everything must be uploaded and nothing is baked yet
  m_uiDirty = true;
  touchAll();
  m_cartTexDirty = false;
  m_cdX0 = m_cdY0 = m_cdX1 = m_cdY1 = 0;
  m_cdPX0 = m_cdPY0 = m_cdPX1 = m_cdPY1 = 0;
  // the cart surface and its z-buffer survive (static members) but their
  // contents are stale: with bbox-limited clearing the first frame after a
  // re-init would test against old depths and render broken models
  if (m_cartSurface)
    memset(m_cartSurface->pixels, 0,
           (size_t)m_cartSurface->pitch * m_cartSurface->h);
  if (!m_zbuf.empty())
    std::fill(m_zbuf.begin(), m_zbuf.end(), 1e30f);
  LOG_INFO("canvas %dx%d ready%s", W, H, m_headless ? " (headless)" : "");
  return true;
}

bool Canvas::initGl() {
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  LOG_INFO("video driver: %s", SDL_GetCurrentVideoDriver());
  // attributes MUST be set before the window is created (SDL picks the EGL
  // config when creating the window; the MALI build fails proc lookups when
  // the context is created without the ES profile)
  if (isDevice()) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  } else {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  }
  m_window = SDL_CreateWindow("NDSUI", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, W, H,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);
  if (!m_window) {
    LOG_ERROR("SDL_CreateWindow: %s", SDL_GetError());
    return false;
  }
  m_gl = SDL_GL_CreateContext(m_window);
  if (!m_gl) {
    LOG_ERROR("SDL_GL_CreateContext: %s", SDL_GetError());
    return false;
  }
  SDL_GL_MakeCurrent(m_window, m_gl);
  SDL_GL_SetSwapInterval(0);
  if (!gl::load()) return false;

  // present path: SDL_Renderer (the one that maps correctly on the MALI
  // driver); its context is separate from ours and only used for presenting
  m_renderer = SDL_CreateRenderer(
      m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!m_renderer) {
    LOG_ERROR("SDL_CreateRenderer: %s", SDL_GetError());
    return false;
  }
  for (int i = 0; i < 2; ++i) {
    m_uiTex[i] = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING, W, H);
    if (!m_uiTex[i]) {
      LOG_ERROR("SDL_CreateTexture: %s", SDL_GetError());
      return false;
    }
  }

  m_cartSurface = SDL_CreateRGBSurfaceWithFormat(0, m_cartW, m_cartH, 32,
                                                 SDL_PIXELFORMAT_ARGB8888);
  m_cartTexture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, m_cartW,
                                    m_cartH);
  if (m_cartTexture) SDL_SetTextureBlendMode(m_cartTexture, SDL_BLENDMODE_BLEND);
  LOG_INFO("gl ready (renderer=%s)", SDL_GetCurrentVideoDriver());
  return true;
}

void Canvas::releaseGl() {
  if (m_cartTexture) SDL_DestroyTexture(m_cartTexture);
  m_cartTexture = nullptr;
  if (m_cartSurface) SDL_FreeSurface(m_cartSurface);
  m_cartSurface = nullptr;
  for (auto& kv : m_texCache)
    if (kv.second) SDL_DestroyTexture(kv.second);
  m_texCache.clear();
  for (auto& kv : m_surfCache)
    if (kv.second) SDL_FreeSurface(kv.second);
  m_surfCache.clear();
  if (m_whiteTex) SDL_DestroyTexture(m_whiteTex);
  m_whiteTex = nullptr;

}

void Canvas::shutdown() {
  for (auto& kv : m_fonts) TTF_CloseFont(kv.second);
  m_fonts.clear();
  if (!m_headless) releaseGl();
  if (m_renderer) SDL_DestroyRenderer(m_renderer);
  if (m_gl) SDL_GL_DeleteContext(m_gl);
  if (m_window) SDL_DestroyWindow(m_window);
  if (m_surface) SDL_FreeSurface(m_surface);
  m_renderer = nullptr;
  m_uiTex[0] = m_uiTex[1] = nullptr;
  m_gl = nullptr;
  m_window = nullptr;
  m_surface = nullptr;
}

TTF_Font* Canvas::font(int px) {
  auto it = m_fonts.find(px);
  if (it != m_fonts.end()) return it->second;
  std::string path;
  for (const char* name : {"Fredoka-Medium.ttf", "Fredoka-Regular.ttf",
                           "NDS12.ttf"}) {
    std::string p = joinPath(assetDir(), name);
    if (fileExists(p)) {
      path = p;
      break;
    }
  }
  TTF_Font* f = path.empty() ? nullptr : TTF_OpenFont(path.c_str(), px);
  if (!f) LOG_ERROR("TTF_OpenFont(%s,%d): %s", path.c_str(), px, TTF_GetError());
  m_fonts[px] = f;
  return f;
}

// ---------------------------------------------------------------- drawing

uint32_t Canvas::map(uint32_t rgb) const {
  return SDL_MapRGB(m_surface->format, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF,
                    rgb & 0xFF);
}

void Canvas::touch(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  if (m_target && m_target != m_surface) {
    // cart frame: track the rendered bbox in cart-surface coordinates
    if (x < 0) {
      w += x;
      x = 0;
    }
    if (y < 0) {
      h += y;
      y = 0;
    }
    if (x + w > m_cartW) w = m_cartW - x;
    if (y + h > m_cartH) h = m_cartH - y;
    if (w <= 0 || h <= 0) return;
    if (x < m_cdX0) m_cdX0 = x;
    if (y < m_cdY0) m_cdY0 = y;
    if (x + w > m_cdX1) m_cdX1 = x + w;
    if (y + h > m_cdY1) m_cdY1 = y + h;
    return;
  }
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  if (x < m_dmgX0) m_dmgX0 = x;
  if (y < m_dmgY0) m_dmgY0 = y;
  if (x + w > m_dmgX1) m_dmgX1 = x + w;
  if (y + h > m_dmgY1) m_dmgY1 = y + h;
  m_uiDirty = true;  // any damage means the texture needs an update
}

void Canvas::touchAll() {
  m_dmgX0 = 0;
  m_dmgY0 = 0;
  m_dmgX1 = W;
  m_dmgY1 = H;
}

void Canvas::clear(uint32_t rgb) { fill(rgb); }

void Canvas::fill(uint32_t rgb) {
  touchAll();
  SDL_FillRect(m_surface, nullptr, map(rgb));
}

void Canvas::rect(int x, int y, int w, int h, uint32_t rgb) {
  if (w <= 0 || h <= 0) return;
  touch(x, y, w, h);
  SDL_Rect r{x, y, w, h};
  SDL_FillRect(m_surface, &r, map(rgb));
}

void Canvas::rectAlpha(int x, int y, int w, int h, uint32_t rgb, int alpha) {
  if (w <= 0 || h <= 0 || alpha <= 0) return;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  if (alpha > 255) alpha = 255;
  int sr = (rgb >> 16) & 0xFF, sg = (rgb >> 8) & 0xFF, sb = rgb & 0xFF;
  for (int yy = y; yy < y + h; ++yy) {
    uint32_t* row = rowPtr(yy);
    if (!row) continue;
    for (int xx = x; xx < x + w; ++xx) {
      uint32_t d = row[xx];
      int dr = (d >> 16) & 0xFF, dg = (d >> 8) & 0xFF, db = d & 0xFF;
      row[xx] = 0xFF000000u |
                ((uint32_t)((sr * alpha + dr * (255 - alpha)) / 255) << 16) |
                ((uint32_t)((sg * alpha + dg * (255 - alpha)) / 255) << 8) |
                (uint32_t)((sb * alpha + db * (255 - alpha)) / 255);
    }
  }
  touch(x, y, w, h);
}

uint32_t* Canvas::rowPtr(int y) {
  SDL_Surface* s = m_target ? m_target : m_surface;
  if (!s || y < 0 || y >= s->h) return nullptr;
  return (uint32_t*)((uint8_t*)s->pixels + (size_t)y * s->pitch);
}

int Canvas::pitchPixels() const {
  SDL_Surface* s = m_target ? m_target : m_surface;
  return s ? s->pitch / 4 : 0;
}

void Canvas::vline(int x, int y, int h, uint32_t rgb) {
  if (h <= 0 || x < 0 || x >= W || y >= H) return;
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (y + h > H) h = H - y;
  if (h <= 0) return;
  touch(x, y, 1, h);
  uint32_t c = map(rgb);
  uint8_t* p = (uint8_t*)m_surface->pixels + (size_t)y * m_surface->pitch +
               (size_t)x * 4;
  for (int i = 0; i < h; ++i) {
    *(uint32_t*)p = c;
    p += m_surface->pitch;
  }
}

void Canvas::rectOutline(int x, int y, int w, int h, uint32_t rgb, int t) {
  rect(x, y, w, t, rgb);
  rect(x, y + h - t, w, t, rgb);
  rect(x, y, t, h, rgb);
  rect(x + w - t, y, t, h, rgb);
}

void Canvas::roundRect(int x, int y, int w, int h, int r, uint32_t rgb) {
  if (w <= 0 || h <= 0) return;
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  if (r <= 0) {
    rect(x, y, w, h, rgb);
    return;
  }
  touch(x, y, w, h);
  uint32_t c = map(rgb);
  for (int row = 0; row < h; ++row) {
    int inset = 0;
    if (row < r) {
      int dy = r - row;
      inset = r - (int)sqrtf((float)(r * r - dy * dy));
    } else if (row >= h - r) {
      int dy = row - (h - r - 1);
      inset = r - (int)sqrtf((float)(r * r - dy * dy));
    }
    SDL_Rect rr{x + inset, y + row, w - 2 * inset, 1};
    SDL_FillRect(m_surface, &rr, c);
  }
}

void Canvas::roundRectOutline(int x, int y, int w, int h, int r, uint32_t rgb,
                              int t) {
  if (w <= 0 || h <= 0) return;
  if (r > h / 2) r = h / 2;
  if (r > w / 2) r = w / 2;
  if (r <= 0) {
    rectOutline(x, y, w, h, rgb, t);
    return;
  }
  touch(x, y, w, h);
  uint32_t c = map(rgb);
  for (int row = 0; row < h; ++row) {
    int inset = 0;
    if (row < r) {
      int dy = r - row;
      inset = r - (int)sqrtf((float)(r * r - dy * dy));
    } else if (row >= h - r) {
      int dy = row - (h - r - 1);
      inset = r - (int)sqrtf((float)(r * r - dy * dy));
    }
    if (row < t || row >= h - t) {
      SDL_Rect rr{x + inset, y + row, w - 2 * inset, 1};
      SDL_FillRect(m_surface, &rr, c);
    } else {
      SDL_Rect l{x + inset, y + row, t, 1};
      SDL_Rect rr2{x + w - inset - t, y + row, t, 1};
      SDL_FillRect(m_surface, &l, c);
      SDL_FillRect(m_surface, &rr2, c);
    }
  }
}

void Canvas::circle(int cx, int cy, int r, uint32_t rgb) {
  if (r <= 0) return;
  touch(cx - r, cy - r, 2 * r + 1, 2 * r + 1);
  uint32_t c = map(rgb);
  for (int dy = -r; dy <= r; ++dy) {
    int dx = (int)sqrtf((float)(r * r - dy * dy));
    SDL_Rect rr{cx - dx, cy + dy, 2 * dx + 1, 1};
    SDL_FillRect(m_surface, &rr, c);
  }
}

void Canvas::line(int x0, int y0, int x1, int y1, int t, uint32_t rgb) {
  int dx = x1 - x0, dy = y1 - y0;
  int steps = std::max(abs(dx), abs(dy));
  if (steps == 0) return;
  for (int i = 0; i <= steps; ++i) {
    int x = x0 + dx * i / steps;
    int y = y0 + dy * i / steps;
    rect(x - t / 2, y - t / 2, t, t, rgb);
  }
}

void Canvas::dots(int x, int y, int w, int h, uint32_t bg, uint32_t dot) {
  rect(x, y, w, h, bg);  // also touches
  uint32_t c = map(dot);
  for (int yy = y; yy < y + h; yy += 8) {
    for (int xx = x; xx < x + w; xx += 8) {
      int dw = std::min(2, x + w - xx);
      int dh = std::min(2, y + h - yy);
      if (dw <= 0 || dh <= 0) continue;
      SDL_Rect r{xx, yy, dw, dh};
      SDL_FillRect(m_surface, &r, c);
    }
  }
}

int Canvas::textWidth(const char* s, int px) {
  TTF_Font* f = font(px);
  if (!f || !s) return 0;
  int w = 0, h = 0;
  TTF_SizeUTF8(f, s, &w, &h);
  return w;
}

int Canvas::textHeight(int px) {
  TTF_Font* f = font(px);
  return f ? TTF_FontHeight(f) : 0;
}

SDL_Surface* Canvas::renderTextSurface(const char* s, int px, uint32_t rgb) {
  TTF_Font* f = font(px);
  if (!f || !s || !*s) return nullptr;
  SDL_Color col{(Uint8)((rgb >> 16) & 0xFF), (Uint8)((rgb >> 8) & 0xFF),
                (Uint8)(rgb & 0xFF), 255};
  SDL_Surface* t = TTF_RenderUTF8_Blended(f, s, col);
  if (!t) return nullptr;
  SDL_Surface* out =
      SDL_ConvertSurfaceFormat(t, SDL_PIXELFORMAT_ARGB8888, 0);
  SDL_FreeSurface(t);
  return out;
}

void Canvas::text(int x, int y, const char* s, uint32_t rgb, int px) {
  TTF_Font* f = font(px);
  if (!f || !s || !*s) return;
  SDL_Color col{(Uint8)((rgb >> 16) & 0xFF), (Uint8)((rgb >> 8) & 0xFF),
                (Uint8)(rgb & 0xFF), 255};
  SDL_Surface* t = TTF_RenderUTF8_Blended(f, s, col);
  if (!t) return;
  SDL_Rect dst{x, y, t->w, t->h};
  touch(x, y, t->w, t->h);
  SDL_BlitSurface(t, nullptr, m_surface, &dst);
  SDL_FreeSurface(t);
}

void Canvas::textCenter(int cx, int y, const char* s, uint32_t rgb, int px) {
  text(cx - textWidth(s, px) / 2, y, s, rgb, px);
}

void Canvas::textRight(int rx, int y, const char* s, uint32_t rgb, int px) {
  text(rx - textWidth(s, px), y, s, rgb, px);
}

void Canvas::textVC(int x, int cy, const char* s, uint32_t rgb, int px) {
  // Fredoka's glyph box leans low; -2px optical nudge reads centered
  text(x, cy - textHeight(px) / 2 - 2, s, rgb, px);
}

void Canvas::textCenterVC(int cx, int cy, const char* s, uint32_t rgb, int px) {
  textVC(cx - textWidth(s, px) / 2, cy, s, rgb, px);
}

void Canvas::textRightVC(int rx, int cy, const char* s, uint32_t rgb, int px) {
  textVC(rx - textWidth(s, px), cy, s, rgb, px);
}

void Canvas::textClipped(int x, int y, int maxW, const char* s, uint32_t rgb,
                         int px) {
  if (!s) return;
  std::string str = s;
  if (textWidth(str.c_str(), px) <= maxW) {
    text(x, y, str.c_str(), rgb, px);
    return;
  }
  while (!str.empty() && textWidth((str + "..").c_str(), px) > maxW)
    str.pop_back();
  text(x, y, (str + "..").c_str(), rgb, px);
}

void Canvas::textCenterClipped(int cx, int y, int maxW, const char* s,
                               uint32_t rgb, int px) {
  if (!s) return;
  std::string str = s;
  if (textWidth(str.c_str(), px) <= maxW) {
    textCenter(cx, y, str.c_str(), rgb, px);
    return;
  }
  while (!str.empty() && textWidth((str + "..").c_str(), px) > maxW)
    str.pop_back();
  textCenter(cx, y, (str + "..").c_str(), rgb, px);
}

// ---------------------------------------------------------------- 3D

void Canvas::beginCartFrame() {
  if (!m_cartSurface) return;
  // stash the previous frame's bbox BEFORE this frame overwrites it, so the
  // erase step knows which pixels to clean
  m_cdPX0 = m_cdX0;
  m_cdPY0 = m_cdY0;
  m_cdPX1 = m_cdX1;
  m_cdPY1 = m_cdY1;
  m_target = m_cartSurface;
  m_cdX0 = m_cartW;
  m_cdY0 = m_cartH;
  m_cdX1 = 0;
  m_cdY1 = 0;
  // transparent background: the UI's paper shows through. Only the previous
  // frame's bbox can still hold pixels (anything older was cleared when it
  // was the previous bbox), so clear just that region - a full memset of the
  // 960x372 surface every frame was a sizable chunk of the cart cost.
  if (m_cdPX1 > m_cdPX0 && m_cdPY1 > m_cdPY0) {
    uint8_t* base = (uint8_t*)m_cartSurface->pixels;
    for (int y = m_cdPY0; y < m_cdPY1; ++y)
      memset(base + (size_t)y * m_cartSurface->pitch + (size_t)m_cdPX0 * 4, 0,
             (size_t)(m_cdPX1 - m_cdPX0) * 4);
    // the z-buffer region that will be tested against must start clean too
    if (m_zbufW == m_cartW && m_zbufH == m_cartH) {
      for (int y = m_cdPY0; y < m_cdPY1; ++y)
        std::fill(m_zbuf.begin() + (size_t)y * m_zbufW + m_cdPX0,
                  m_zbuf.begin() + (size_t)y * m_zbufW + m_cdPX1, 1e30f);
    }
  }
}

void Canvas::endCartFrame() {
  m_target = m_surface;
  m_cartTexDirty = true;
}

void Canvas::clearSurfaceCache() {
  for (auto& kv : m_surfCache)
    if (kv.second) SDL_FreeSurface(kv.second);
  m_surfCache.clear();
}

SDL_Texture* Canvas::whiteTexture() {
  if (m_whiteTex) return m_whiteTex;
  if (!m_renderer) return nullptr;
  unsigned char px[4] = {255, 255, 255, 255};
  m_whiteTex = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STATIC, 1, 1);
  if (m_whiteTex) SDL_UpdateTexture(m_whiteTex, nullptr, px, 4);
  return m_whiteTex;
}

SDL_Surface* Canvas::surfaceForPath(const std::string& pngPath) {
  auto it = m_surfCache.find(pngPath);
  if (it != m_surfCache.end()) return it->second;
  SDL_Surface* raw = IMG_Load(pngPath.c_str());
  if (!raw) {
    m_surfCache[pngPath] = nullptr;
    return nullptr;
  }
  SDL_Surface* conv = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
  SDL_FreeSurface(raw);
  // keep box art small: the rasterizer samples it per pixel and a big surface
  // thrashes the cache
  if (conv && (conv->w > 192 || conv->h > 192)) {
    int nw = conv->w, nh = conv->h;
    if (nw >= nh) {
      nh = std::max(1, nh * 192 / nw);
      nw = 192;
    } else {
      nw = std::max(1, nw * 192 / nh);
      nh = 192;
    }
    SDL_Surface* small =
        SDL_CreateRGBSurfaceWithFormat(0, nw, nh, 32, SDL_PIXELFORMAT_ARGB8888);
    if (small) {
      SDL_BlitScaled(conv, nullptr, small, nullptr);
      SDL_FreeSurface(conv);
      conv = small;
    }
  }
  m_surfCache[pngPath] = conv;
  return conv;
}

bool Canvas::ensureZbuf(int w, int h) {
  if (w <= 0 || h <= 0) return false;
  if (m_zbufW == w && m_zbufH == h && !m_zbuf.empty()) return true;
  m_zbufW = w;
  m_zbufH = h;
  m_zbuf.assign((size_t)w * h, 1e30f);
  return true;
}

void Canvas::clearZbuf() {
  if (!m_zbuf.empty())
    std::fill(m_zbuf.begin(), m_zbuf.end(), 1e30f);
}

SDL_Texture* Canvas::textureForPath(const std::string& pngPath) {
  auto it = m_texCache.find(pngPath);
  if (it != m_texCache.end()) return it->second;
  if (!m_renderer) return nullptr;
  SDL_Surface* raw = IMG_Load(pngPath.c_str());
  if (!raw) {
    m_texCache[pngPath] = nullptr;
    return nullptr;
  }
  SDL_Surface* conv = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ABGR8888, 0);
  SDL_FreeSurface(raw);
  SDL_Texture* tex = conv ? SDL_CreateTextureFromSurface(m_renderer, conv)
                          : nullptr;
  if (conv) SDL_FreeSurface(conv);
  if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
  m_texCache[pngPath] = tex;
  return tex;
}

void Canvas::present() {
  // Composite the cart surface into the UI surface first (software blit with
  // alpha). Drawing it as a blended texture is not reliable on the Brick's
  // vendor renderer - it ignores the alpha and paints a black box.
  if (m_cartSurface && m_cartTexDirty && m_cartDw > 0 && m_cartDh > 0) {
    if (m_cdX1 > m_cdX0 && m_cdY1 > m_cdY0) {
      // only the rendered bbox moves: blit and damage just that (scaled).
      // The composite must stay strictly inside the carousel rect: the
      // save-under erase only restores pixels inside it, so anything blitted
      // past the edges would stick around (coloured strips at the sides).
      float sx = (float)m_cartDw / m_cartW, sy = (float)m_cartDh / m_cartH;
      SDL_Rect src{m_cdX0, m_cdY0, m_cdX1 - m_cdX0, m_cdY1 - m_cdY0};
      int dx0 = m_cartDx + (int)std::floor(src.x * sx);
      int dy0 = m_cartDy + (int)std::floor(src.y * sy);
      int dx1 = m_cartDx + (int)std::ceil((src.x + src.w) * sx);
      int dy1 = m_cartDy + (int)std::ceil((src.y + src.h) * sy);
      int rx1 = m_cartDx + m_cartDw, ry1 = m_cartDy + m_cartDh;
      if (dx0 < m_cartDx) dx0 = m_cartDx;
      if (dy0 < m_cartDy) dy0 = m_cartDy;
      if (dx1 > rx1) dx1 = rx1;
      if (dy1 > ry1) dy1 = ry1;
      if (dx1 > dx0 && dy1 > dy0) {
        SDL_Rect dst{dx0, dy0, dx1 - dx0, dy1 - dy0};
        SDL_BlitScaled(m_cartSurface, &src, m_surface, &dst);
        touch(dst.x, dst.y, dst.w, dst.h);
      }
    }
    m_cartTexDirty = false;
  }
  if (m_headless) return;
  if (!m_renderer || !m_uiTex[0]) return;
  Uint32 t0 = SDL_GetTicks();
  // NOTE: one UI texture only. The old ping-pong pair made
  // partial uploads land on alternating textures, which showed up as
  // flicker (top bar redraws) on every carousel move.
  Uint32 t1 = t0;
  if (m_uiDirty) {
    int dx0 = m_dmgX0, dy0 = m_dmgY0, dx1 = m_dmgX1, dy1 = m_dmgY1;
    // Partial rect uploads turned out to be unreliable on the Brick's vendor
    // renderer (stale/mixed pixels showed up as top-bar flicker), so the
    // whole surface goes out every time. Kept for reference:
    //   bool partial = dx1 > dx0 && dy1 > dy0 &&
    //                  (int64_t)(dx1 - dx0) * (dy1 - dy0) < (int64_t)W * H * 60 / 100;
    bool partial = false;
    (void)dx0;
    (void)dy0;
    (void)dx1;
    (void)dy1;
    {
      static const bool dbgAll = getenv("NDS_UPLOADDBG") != nullptr;
      static int lastMode = -1;
      int mode = partial ? 1 : 0;
      if (dbgAll || mode != lastMode) {
        lastMode = mode;
        LOG_INFO("upload: %s box=(%d,%d)-(%d,%d)", partial ? "partial" : "FULL",
                 dx0, dy0, dx1, dy1);
      }
    }
    if (partial) {
      // SDL_UpdateTexture with a rect is markedly faster than the
      // LockTexture row-copy path on the Brick's driver
      SDL_Rect rc{dx0, dy0, dx1 - dx0, dy1 - dy0};
      const uint8_t* src = (const uint8_t*)m_surface->pixels;
      if (SDL_UpdateTexture(m_uiTex[0], &rc,
                            src + (size_t)dy0 * m_surface->pitch +
                                (size_t)dx0 * 4,
                            m_surface->pitch) == 0) {
        ++m_nPartial;
        m_tUploadPartial += SDL_GetTicks() - t1;
      }
    } else {
      if (SDL_UpdateTexture(m_uiTex[0], nullptr, m_surface->pixels,
                            m_surface->pitch) == 0) {
        ++m_nFull;
        m_tUploadFull += SDL_GetTicks() - t1;
      }
    }
    m_uiDirty = false;
    m_dmgX0 = m_dmgY0 = W;
    m_dmgX1 = m_dmgY1 = 0;
  }
  m_tUpload = m_tUploadFull + m_tUploadPartial;
  Uint32 t2 = SDL_GetTicks();
  SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 255);
  SDL_RenderClear(m_renderer);
  SDL_RenderCopy(m_renderer, m_uiTex[0], nullptr, nullptr);
  Uint32 t3 = SDL_GetTicks();
  SDL_RenderPresent(m_renderer);  // vsync
  Uint32 t4 = SDL_GetTicks();
  m_tCart += t1 - t0;
  m_tUpload += t2 - t1;
  m_tCopy += t3 - t2;
  m_tPresent += t4 - t3;
  ++m_tFrames;
}

void Canvas::logPerfStats() {
  if (!m_tFrames) return;
  LOG_INFO("perf/frame: cart=%.1f upload=%.1f (full %.1f/%d, part %.1f/%d) "
           "copy=%.1f present=%.1f ms (%d)",
           (double)m_tCart / m_tFrames, (double)m_tUpload / m_tFrames,
           m_nFull ? (double)m_tUploadFull / m_nFull : 0.0, m_nFull,
           m_nPartial ? (double)m_tUploadPartial / m_nPartial : 0.0,
           m_nPartial, (double)m_tCopy / m_tFrames,
           (double)m_tPresent / m_tFrames, m_tFrames);
  m_tCart = m_tUpload = m_tCopy = m_tPresent = 0;
  m_tUploadFull = m_tUploadPartial = 0;
  m_nFull = m_nPartial = 0;
  m_tFrames = 0;
}

void Canvas::saveRegion(int x, int y, int w, int h,
                        std::vector<unsigned char>& buf) {
  if (!m_surface || w <= 0 || h <= 0) return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  buf.resize((size_t)w * h * 4);
  const uint8_t* src = (const uint8_t*)m_surface->pixels;
  for (int r = 0; r < h; ++r)
    memcpy(buf.data() + (size_t)r * w * 4,
           src + (size_t)(y + r) * m_surface->pitch + (size_t)x * 4,
           (size_t)w * 4);
}

void Canvas::restoreRegion(int x, int y, int w, int h,
                           const std::vector<unsigned char>& buf, int dmgX,
                           int dmgY, int dmgW, int dmgH) {
  if (!m_surface || buf.empty() || w <= 0 || h <= 0) return;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  if (buf.size() < (size_t)w * h * 4) return;
  uint8_t* dst = (uint8_t*)m_surface->pixels;
  for (int r = 0; r < h; ++r)
    memcpy(dst + (size_t)(y + r) * m_surface->pitch + (size_t)x * 4,
           buf.data() + (size_t)r * w * 4, (size_t)w * 4);
  if (dmgW > 0 && dmgH > 0)
    touch(dmgX, dmgY, dmgW, dmgH);
  else
    touch(x, y, w, h);
}

bool Canvas::screenshot(const std::string& bmpPath) {
  // capture the composited output (includes the carousel geometry) when the
  // renderer exists; headless falls back to the software surface
  if (!m_headless && m_renderer && m_uiTex[0]) {
    SDL_Surface* shot =
        SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (shot) {
      if (SDL_RenderReadPixels(m_renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                               shot->pixels, shot->pitch) == 0) {
        bool ok = SDL_SaveBMP(shot, bmpPath.c_str()) == 0;
        SDL_FreeSurface(shot);
        return ok;
      }
      SDL_FreeSurface(shot);
    }
  }
  return SDL_SaveBMP(m_surface, bmpPath.c_str()) == 0;
}

}  // namespace ndsui

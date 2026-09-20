// Canvas: software ARGB8888 surface for all UI drawing (presented through
// SDL_Renderer, the path proven on the Brick's driver); the 3D carousel is
// rendered in our own GL context and read back into the surface.
// Draw only when dirty.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <SDL.h>
#include <SDL_ttf.h>

namespace ndsui {

class Mesh;

class Canvas {
 public:
  static constexpr int W = 1024;
  static constexpr int H = 768;

  bool init(const std::string& fontPath);
  void shutdown();
  bool inited() const { return m_surface != nullptr; }
  bool headless() const { return m_headless; }

  SDL_Surface* surface() { return m_surface; }
  uint32_t map(uint32_t rgb) const;  // 0xRRGGBB -> surface pixel
  // direct raster access for bulk backgrounds (32bpp surface)
  uint32_t* rowPtr(int y);
  int pitchPixels() const;
  // --- drawing (all take 0xRRGGBB colors) ---
  void clear(uint32_t rgb);
  void fill(uint32_t rgb);
  void rect(int x, int y, int w, int h, uint32_t rgb);
  // translucent fill (software blend) - modal backdrops
  void rectAlpha(int x, int y, int w, int h, uint32_t rgb, int alpha);
  void vline(int x, int y, int h, uint32_t rgb);
  void rectOutline(int x, int y, int w, int h, uint32_t rgb, int t = 2);
  void roundRect(int x, int y, int w, int h, int r, uint32_t rgb);
  void roundRectOutline(int x, int y, int w, int h, int r, uint32_t rgb,
                        int t = 2);
  void pill(int x, int y, int w, int h, uint32_t rgb) {
    roundRect(x, y, w, h, h / 2, rgb);
  }
  void pillOutline(int x, int y, int w, int h, uint32_t rgb, int t = 2) {
    roundRectOutline(x, y, w, h, h / 2, rgb, t);
  }
  void circle(int cx, int cy, int r, uint32_t rgb);
  void line(int x0, int y0, int x1, int y1, int t, uint32_t rgb);
  void dots(int x, int y, int w, int h, uint32_t bg, uint32_t dot);

  int textWidth(const char* s, int px);
  int textHeight(int px);
  // standalone text surface (ARGB8888, caller owns): used for the console
  // name embossed onto the 3D cartridges
  SDL_Surface* renderTextSurface(const char* s, int px, uint32_t rgb);
  void text(int x, int y, const char* s, uint32_t rgb, int px);
  void textCenter(int cx, int y, const char* s, uint32_t rgb, int px);
  void textRight(int rx, int y, const char* s, uint32_t rgb, int px);
  // vertically centered around cy (optical offset for the rounded font)
  void textVC(int x, int cy, const char* s, uint32_t rgb, int px);
  void textCenterVC(int cx, int cy, const char* s, uint32_t rgb, int px);
  void textRightVC(int rx, int cy, const char* s, uint32_t rgb, int px);
  void textClipped(int x, int y, int maxW, const char* s, uint32_t rgb, int px);
  void textCenterClipped(int cx, int y, int maxW, const char* s, uint32_t rgb,
                         int px);

  // --- 3D carousel ---
  // Rasterized by the SDL renderer itself (SDL_RenderGeometryRaw) so it uses
  // the renderer's proven GL context: no second context, no FBO readback.
  bool has3D() const { return m_renderer != nullptr && m_cartSurface; }
  // 3D carts render into their own small surface; present() composites it
  // over the UI (so rotating carts never re-upload the full UI texture)
  void beginCartFrame();   // clears + retargets the raster surface
  void endCartFrame();     // marks the cart texture for upload
  void markCartDirty() { m_cartTexDirty = true; }  // re-blit only
  bool inCartFrame() const { return m_target != m_surface; }
  int targetW() const {
    SDL_Surface* t = m_target ? m_target : m_surface;
    return t ? t->w : W;
  }
  int targetH() const {
    SDL_Surface* t = m_target ? m_target : m_surface;
    return t ? t->h : H;
  }
  void setCartDest(int x, int y, int w, int h) {
    m_cartDx = x;
    m_cartDy = y;
    m_cartDw = w;
    m_cartDh = h;
  }
  int cartSurfaceW() const { return m_cartW; }
  int cartSurfaceH() const { return m_cartH; }
  void setCarouselRect(int x, int y, int w, int h) {
    m_cartRx = x;
    m_cartRy = y;
    m_cartRw = w;
    m_cartRh = h;
  }
  int carouselRectX() const { return m_cartRx; }
  int carouselRectY() const { return m_cartRy; }
  int carouselRectW() const { return m_cartRw; }
  int carouselRectH() const { return m_cartRh; }
  SDL_Texture* whiteTexture();
  SDL_Texture* textureForPath(const std::string& pngPath);  // cached
  SDL_Surface* surfaceForPath(const std::string& pngPath);  // cached (ARGB8888)
  // drop cached art/icons (after a library refresh so new files are picked up)
  void clearSurfaceCache();
  // z-buffer for the carousel rasterizer (sized to the carousel rect)
  bool ensureZbuf(int w, int h);
  void clearZbuf();
  float* zbuf() { return m_zbuf.data(); }
  int zbufW() const { return m_zbufW; }
  int zbufH() const { return m_zbufH; }

  void present();      // uploads the UI surface if marked, composites, swaps
  void logPerfStats();  // logs + resets the per-stage frame timings
  void markUiDirty() { m_uiDirty = true; touchAll(); }
  void markRegionDirty(int x, int y, int w, int h) { touch(x, y, w, h); }
  bool screenshot(const std::string& bmpPath);
  // save-under helpers (cheap restore of the paper under the carousel)
  void saveRegion(int x, int y, int w, int h, std::vector<unsigned char>& buf);
  // dmg* limits the damage reporting (the pixel copy still covers x,y,w,h)
  void restoreRegion(int x, int y, int w, int h,
                     const std::vector<unsigned char>& buf, int dmgX = -1,
                     int dmgY = -1, int dmgW = -1, int dmgH = -1);
  // bounding box of the carts rendered into the cart surface (cart coords)
  int cartDmgX0() const { return m_cdX0; }
  int cartDmgY0() const { return m_cdY0; }
  int cartDmgX1() const { return m_cdX1; }
  int cartDmgY1() const { return m_cdY1; }
  int cartDmgPX0() const { return m_cdPX0; }
  int cartDmgPY0() const { return m_cdPY0; }
  int cartDmgPX1() const { return m_cdPX1; }
  int cartDmgPY1() const { return m_cdPY1; }

 private:
  TTF_Font* font(int px);
  bool initGl();
  void releaseGl();

  SDL_Window* m_window = nullptr;
  SDL_GLContext m_gl = nullptr;
  SDL_Renderer* m_renderer = nullptr;
  SDL_Texture* m_uiTex[2] = {nullptr, nullptr};
  int m_texIdx = 0;
  SDL_Surface* m_surface = nullptr;
  bool m_headless = false;
  std::map<int, TTF_Font*> m_fonts;

  int m_cartRx = 0, m_cartRy = 56, m_cartRw = W, m_cartRh = H - 56 - 136;
  SDL_Surface* m_cartSurface = nullptr;
  SDL_Texture* m_cartTexture = nullptr;
  SDL_Surface* m_target = nullptr;   // raster target (UI surface or cart)
  bool m_cartTexDirty = false;
  int m_cartW = 960, m_cartH = 372;   // cart buffer (matches the games band)
  int m_cartDx = 0, m_cartDy = 0, m_cartDw = 0, m_cartDh = 0;
  int m_cdX0 = 0, m_cdY0 = 0, m_cdX1 = 0, m_cdY1 = 0;      // this frame
  int m_cdPX0 = 0, m_cdPY0 = 0, m_cdPX1 = 0, m_cdPY1 = 0;  // previous
  bool m_uiDirty = true;
  SDL_Texture* m_whiteTex = nullptr;
  std::map<std::string, SDL_Texture*> m_texCache;
  std::map<std::string, SDL_Surface*> m_surfCache;
  std::vector<float> m_zbuf;
  int m_zbufW = 0, m_zbufH = 0;

  // per-stage frame timings (debug)
  uint32_t m_tCart = 0, m_tUpload = 0, m_tCopy = 0, m_tPresent = 0;
  uint32_t m_tUploadFull = 0, m_tUploadPartial = 0;
  int m_nFull = 0, m_nPartial = 0;
  int m_tFrames = 0;

  // damaged-region tracking for partial texture uploads
  void touch(int x, int y, int w, int h);
  void touchAll();
  int m_dmgX0 = 0, m_dmgY0 = 0, m_dmgX1 = 0, m_dmgY1 = 0;
};

}  // namespace ndsui

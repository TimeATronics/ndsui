#include "ui/Images.h"

#include <SDL_image.h>

#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {

namespace {
constexpr size_t kMaxEntries = 48;
}

ImageCache::~ImageCache() { clear(); }

void ImageCache::clear() {
  for (auto& kv : m_cache)
    if (kv.second.surface) SDL_FreeSurface(kv.second.surface);
  m_cache.clear();
  for (auto& kv : m_trimmed)
    if (kv.second) SDL_FreeSurface(kv.second);
  m_trimmed.clear();
}

SDL_Surface* ImageCache::get(const std::string& path, int fitW, int fitH) {
  std::string key = fmt("%s@%dx%d", path.c_str(), fitW, fitH);
  auto it = m_cache.find(key);
  if (it != m_cache.end()) {
    it->second.used = ++m_clock;
    return it->second.surface;
  }

  SDL_Surface* raw = IMG_Load(path.c_str());
  if (!raw) {
    m_cache[key] = Entry{nullptr, ++m_clock};  // negative cache
    return nullptr;
  }
  SDL_Surface* conv =
      SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
  SDL_FreeSurface(raw);
  if (!conv) return nullptr;

  // scale to fit preserving aspect
  double sx = (double)fitW / conv->w;
  double sy = (double)fitH / conv->h;
  double s = sx < sy ? sx : sy;
  if (s > 1.0) s = 1.0;  // never upscale (keeps pixel art crisp)
  int tw = (int)(conv->w * s), th = (int)(conv->h * s);
  if (tw < 1) tw = 1;
  if (th < 1) th = 1;

  SDL_Surface* out =
      SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32, SDL_PIXELFORMAT_ARGB8888);
  if (out) {
    SDL_BlitScaled(conv, nullptr, out, nullptr);
  }
  SDL_FreeSurface(conv);
  if (!out) return nullptr;

  if (m_cache.size() >= kMaxEntries) {
    // evict least recently used
    auto lru = m_cache.begin();
    for (auto i = m_cache.begin(); i != m_cache.end(); ++i)
      if (i->second.used < lru->second.used) lru = i;
    if (lru->second.surface) SDL_FreeSurface(lru->second.surface);
    m_cache.erase(lru);
  }
  m_cache[key] = Entry{out, ++m_clock};
  return out;
}

SDL_Surface* ImageCache::getTrimmed(const std::string& path, int size) {
  std::string key = fmt("%s@t%d", path.c_str(), size);
  auto it = m_trimmed.find(key);
  if (it != m_trimmed.end()) return it->second;
  SDL_Surface* src = get(path, 4096, 4096);  // full-size ARGB copy
  SDL_Surface* out = nullptr;
  if (src) {
    // opaque bounding box: the icons ship with transparent padding, which
    // otherwise makes them render far smaller than the card they sit in
    int minX = src->w, minY = src->h, maxX = -1, maxY = -1;
    for (int y = 0; y < src->h; ++y) {
      const uint32_t* row =
          (const uint32_t*)((const uint8_t*)src->pixels + (size_t)y * src->pitch);
      for (int x = 0; x < src->w; ++x) {
        if ((row[x] >> 24) < 16) continue;
        if (x < minX) minX = x;
        if (x > maxX) maxX = x;
        if (y < minY) minY = y;
        if (y > maxY) maxY = y;
      }
    }
    SDL_Rect crop{0, 0, src->w, src->h};
    if (maxX >= minX && maxY >= minY)
      crop = SDL_Rect{minX, minY, maxX - minX + 1, maxY - minY + 1};
    double s = (double)size / crop.w;
    double sy = (double)size / crop.h;
    if (sy < s) s = sy;
    int tw = (int)(crop.w * s), th = (int)(crop.h * s);
    if (tw < 1) tw = 1;
    if (th < 1) th = 1;
    out = SDL_CreateRGBSurfaceWithFormat(0, tw, th, 32,
                                         SDL_PIXELFORMAT_ARGB8888);
    if (out) SDL_BlitScaled(src, &crop, out, nullptr);
  }
  m_trimmed[key] = out;
  return out;
}

}  // namespace ndsui

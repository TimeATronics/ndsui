// PNG loading + scaling + small LRU cache (box art, icons, backgrounds).
#pragma once

#include <map>
#include <string>

#include <SDL.h>

namespace ndsui {

class ImageCache {
 public:
  ~ImageCache();

  // Returns a cached ARGB surface scaled to fit (fitW, fitH) preserving
  // aspect ratio, or nullptr when the file is missing/unreadable.
  SDL_Surface* get(const std::string& path, int fitW, int fitH);
  // Like get(), but the transparent padding of the image is cropped away
  // first, so the visible art fills the requested size (console icons).
  SDL_Surface* getTrimmed(const std::string& path, int size);
  void clear();

 private:
  struct Entry {
    SDL_Surface* surface = nullptr;
    Uint32 used = 0;
  };
  std::map<std::string, Entry> m_cache;
  // trimmed icons are referenced by the UI for a long time, so they are
  // kept out of the LRU (eviction left dangling StripItem icons)
  std::map<std::string, SDL_Surface*> m_trimmed;
  Uint32 m_clock = 0;
};

}  // namespace ndsui

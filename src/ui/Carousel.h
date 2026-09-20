// 3D cartridge carousel: shows the selected game's cartridge/disc (tilted
// rest pose) with its neighbors, renders only when something changed.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "core/Systems.h"
#include "ui/Canvas.h"
#include "ui/Mesh.h"

namespace ndsui {

struct AppContext;

class Carousel {
 public:
  Carousel(Canvas& canvas, AppContext& ctx);

  void setSystem(const System& sys);            // mesh model for this system
  void setModelOverride(const std::string& name);  // preview a model directly
  void setGames(const std::vector<const Game*>& games);
  void setSelection(int index);
  void rotate(float deltaDeg);       // horizontal spin (yaw)
  void rotatePitch(float deltaDeg);  // vertical spin (pitch)
  int selection() const { return m_sel; }

  void setSmall(bool s) { m_small = s; invalidate(); }
  void tick();            // easing only (marks dirty while moving)
  void drawOverlay();     // rasterizes the cartridges over the drawn UI
  void invalidate();
  bool dirty() const { return m_dirty; }
  // drop derived surfaces (opaque art) after a library refresh
  void clearCaches();

 private:
  void loadModelMap();
  // the generated OBJ models are authored camera-facing (see the .cpp)
  void modelFixMat(const std::string& model, const Mesh* mesh, Mat4* pre) const;
  Mesh* meshFor(const std::string& systemId);
  SDL_Surface* textureFor(const Game* g);
  // embossed console-name strip for the cartridges (u in [2,3] in the mesh)
  SDL_Surface* nameTexture(const System& sys);
  // CD underside sheen used on the back of the disc models
  SDL_Surface* rainbowTexture();
  // box art composited over an opaque disc-paper colour: the disc label is a
  // circular crop of the art, so transparent pixels would otherwise leave
  // holes in the disc face
  SDL_Surface* opaqueArt(SDL_Surface* art, uint32_t bg);
  uint32_t bodyColorFor(const std::string& model) const;

  Canvas& m_canvas;
  AppContext& m_ctx;
  std::map<std::string, std::string> m_map;     // system id -> model name
  std::map<std::string, Mesh> m_meshes;         // model name -> mesh
  std::map<std::string, SDL_Surface*> m_nameTex;  // console -> embossed name
  SDL_Surface* m_rainbow = nullptr;               // CD underside sheen
  std::map<SDL_Surface*, SDL_Surface*> m_opaqueArt;  // art -> opaque copy
  std::vector<const Game*> m_games;
  int m_sel = 0;
  float m_animPos = 0.f;                        // eased position (in items)
  float m_yaw = 0.f;                            // user rotation (deg)
  float m_animYaw = 0.f;
  float m_pitch = 0.f;                          // user tilt (deg)
  float m_animPitch = 0.f;
  std::string m_systemId;
  std::string m_forceModel;  // debug: show this model for every cart
  std::string m_model = "default";
  bool m_small = false;   // root showcase: smaller models
  bool m_dirty = true;
};

}  // namespace ndsui

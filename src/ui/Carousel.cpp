#include "ui/Carousel.h"

#include <algorithm>
#include <cmath>

#include "screens/Screen.h"
#include <dirent.h>

#include "ui/Raster.h"
#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {

namespace {

float ease(float cur, float target, float k) {
  float d = target - cur;
  if (std::fabs(d) < 0.002f) return target;
  return cur + d * k;
}
}  // namespace

Carousel::Carousel(Canvas& canvas, AppContext& ctx)
    : m_canvas(canvas), m_ctx(ctx) {
  loadModelMap();
}

void Carousel::loadModelMap() {
  FILE* fp = fopen(joinPath(joinPath(assetDir(), "models"), "map.txt").c_str(),
                   "r");
  if (!fp) {
    LOG_WARN("carousel: no model map");
    return;
  }
  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    std::string s = trim(line);
    size_t sp = s.find(' ');
    if (sp == std::string::npos) continue;
    m_map[upper(trim(s.substr(0, sp)))] = trim(s.substr(sp + 1));
  }
  fclose(fp);
  static const bool dbg = getenv("NDS_GLDBG") != nullptr;
  if (dbg) LOG_INFO("carousel: %zu system->model mappings", m_map.size());
}

Mesh* Carousel::meshFor(const std::string& systemId) {
  auto it = m_map.find(upper(systemId));
  // ids can be system ids (mapped to a model) or model names directly
  std::string model = it != m_map.end() ? it->second : systemId;
  {
    std::string dir = joinPath(assetDir(), "models");
    std::string o = joinPath(dir, model + ".obj");
    if (!fileExists(o)) model = "default";
  }
  m_model = model;
  auto mit = m_meshes.find(model);
  if (mit != m_meshes.end()) return &mit->second;

  Mesh& mesh = m_meshes[model];
  if (mesh.valid()) return &mesh;
  std::string dir = joinPath(assetDir(), "models");
  std::string objPath = joinPath(dir, model + ".obj");
  if (mesh.loadObj(objPath)) {
    mesh.setFlipV(true);
  } else {
    LOG_WARN("carousel: cannot load model %s", model.c_str());
  }
  return &mesh;
}

void Carousel::modelFixMat(const std::string& model, const Mesh* mesh,
                           Mat4* pre) const {
  // The generated cuboid/disc OBJ models are already authored facing the
  // camera (X right, Y up, Z toward viewer): no pose fix at all.
  (void)model;
  (void)mesh;
  *pre = Mat4::identity();
}

void Carousel::setModelOverride(const std::string& name) {
  m_model = name;
  m_forceModel = name;
  meshFor(name);
  m_yaw = 0.f;
  m_animYaw = 0.f;
  invalidate();
}

void Carousel::setSystem(const System& sys) {
  if (m_systemId == sys.id) return;
  m_systemId = sys.id;
  meshFor(sys.id);
  m_sel = 0;
  m_animPos = 0.f;
  invalidate();
}

void Carousel::setGames(const std::vector<const Game*>& games) {
  m_games = games;
  if (m_sel >= (int)m_games.size()) m_sel = std::max(0, (int)m_games.size() - 1);
  invalidate();
}

void Carousel::setSelection(int index) {
  int n = (int)m_games.size();
  if (n == 0) return;
  int next = std::clamp(index, 0, n - 1);
  if (next != m_sel) {
    // rotation belongs to the item the user rotated, not to the slot
    m_yaw = 0.f;
    m_animYaw = 0.f;
    m_pitch = 0.f;
    m_animPitch = 0.f;
  }
  m_sel = next;
  invalidate();
}

void Carousel::rotate(float deltaDeg) {
  m_yaw += deltaDeg;
  // full turns are allowed; keep the eased value in a sane range
  if (m_yaw > 360.f) {
    m_yaw -= 360.f;
    m_animYaw -= 360.f;
  } else if (m_yaw < -360.f) {
    m_yaw += 360.f;
    m_animYaw += 360.f;
  }
  invalidate();
}

void Carousel::rotatePitch(float deltaDeg) {
  m_pitch += deltaDeg;
  if (m_pitch > 360.f) {
    m_pitch -= 360.f;
    m_animPitch -= 360.f;
  } else if (m_pitch < -360.f) {
    m_pitch += 360.f;
    m_animPitch += 360.f;
  }
  invalidate();
}

void Carousel::invalidate() { m_dirty = true; }

void Carousel::clearCaches() {
  for (auto& kv : m_opaqueArt)
    if (kv.second && kv.second != kv.first) SDL_FreeSurface(kv.second);
  m_opaqueArt.clear();
  invalidate();
}

SDL_Surface* Carousel::nameTexture(const System& sys) {
  auto it = m_nameTex.find(sys.id);
  if (it != m_nameTex.end()) return it->second;
  std::string label = upper(sys.label.empty() ? sys.id : sys.label);
  SDL_Surface* main = m_canvas.renderTextSurface(label.c_str(), 64, 0xFFFFFF);
  SDL_Surface* shadow =
      m_canvas.renderTextSurface(label.c_str(), 64, 0x0A0A0A);
  SDL_Surface* out = nullptr;
  if (main) {
    int w = main->w + 8, h = main->h + 8;
    out = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
                                         SDL_PIXELFORMAT_ARGB8888);
    if (out) {
      SDL_FillRect(out, nullptr, 0);
      // embossed: dark copy down-right, bright copy up-left
      if (shadow) {
        SDL_SetSurfaceAlphaMod(shadow, 150);
        SDL_Rect d{5, 5, shadow->w, shadow->h};
        SDL_BlitSurface(shadow, nullptr, out, &d);
        SDL_FreeSurface(shadow);
      }
      SDL_SetSurfaceAlphaMod(main, 235);
      SDL_Rect d{2, 2, main->w, main->h};
      SDL_BlitSurface(main, nullptr, out, &d);
    }
    SDL_FreeSurface(main);
  }
  m_nameTex[sys.id] = out;
  return out;
}

SDL_Surface* Carousel::opaqueArt(SDL_Surface* art, uint32_t rgb) {
  if (!art) return nullptr;
  auto it = m_opaqueArt.find(art);
  if (it != m_opaqueArt.end()) return it->second;
  SDL_Surface* out = SDL_CreateRGBSurfaceWithFormat(
      0, art->w, art->h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!out) return art;
  SDL_FillRect(out, nullptr, SDL_MapRGBA(out->format, (rgb >> 16) & 0xFF,
                                         (rgb >> 8) & 0xFF, rgb & 0xFF, 0xFF));
  SDL_BlitSurface(art, nullptr, out, nullptr);
  m_opaqueArt[art] = out;
  return out;
}

SDL_Surface* Carousel::rainbowTexture() {
  if (m_rainbow) return m_rainbow;
  // Diffraction-grating look for the read side of a CD/DVD: a silver metal
  // base with a fan of rainbow colour sweeping around the hub (the tracks
  // are circular, so the diffracted light spreads radially -> a pinwheel of
  // soft colour sectors, not concentric rings). The angle functions use whole
  // multiples of 2*pi so the pattern is continuous at the seam.
  const int N = 256;
  SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(
      0, N, N, 32, SDL_PIXELFORMAT_ARGB8888);
  if (!s) return nullptr;
  const float kPi = 3.14159265f;
  for (int y = 0; y < N; ++y) {
    uint32_t* row = (uint32_t*)((uint8_t*)s->pixels + (size_t)y * s->pitch);
    for (int x = 0; x < N; ++x) {
      float dx = (x + 0.5f) / N * 2.f - 1.f;
      float dy = (y + 0.5f) / N * 2.f - 1.f;
      float r = std::sqrt(dx * dx + dy * dy);
      if (r > 1.f) r = 1.f;
      float t = std::atan2(dy, dx) / (2.f * kPi);  // -0.5 .. 0.5
      // three rainbow sweeps around the disc, slightly warped with radius
      float w = 6.f * kPi * t + 2.2f * r;
      float rr = 0.5f + 0.5f * std::sin(w);
      float gg = 0.5f + 0.5f * std::sin(w - 2.0944f);
      float bb = 0.5f + 0.5f * std::sin(w - 4.1888f);
      // brushed metal: bright silver, a touch darker toward the rim
      float silver = 0.90f - 0.14f * r;
      // the colour only flares in some sectors (uneven like real diffraction)
      float sheen = 0.42f * (0.35f + 0.65f * (0.5f + 0.5f * std::cos(12.f * kPi * t)));
      sheen *= 0.55f + 0.45f * (1.f - std::fabs(2.f * r - 1.f));
      // the hub reads as a plain clear/silver ring
      if (r < 0.14f) sheen *= r / 0.14f;
      int cr = (int)((silver + sheen * (rr - 0.5f)) * 255.f);
      int cg = (int)((silver + sheen * (gg - 0.5f)) * 255.f);
      int cb = (int)((silver + sheen * (bb - 0.5f)) * 255.f);
      cr = std::min(255, std::max(0, cr));
      cg = std::min(255, std::max(0, cg));
      cb = std::min(255, std::max(0, cb));
      row[x] = 0xFF000000u | ((uint32_t)cr << 16) | ((uint32_t)cg << 8) |
               (uint32_t)cb;
    }
  }
  m_rainbow = s;
  return s;
}


SDL_Surface* Carousel::textureFor(const Game* g) {
  if (!g) return nullptr;
  std::string base = g->path;
  size_t slash = base.find_last_of('/');
  if (slash != std::string::npos) base = base.substr(slash + 1);
  size_t dot = base.find_last_of('.');
  if (dot != std::string::npos) base = base.substr(0, dot);
  const System& sys = m_ctx.systems[g->systemIndex];
  // exact folder first (the stock art mixes png and jpg)
  const char* exts[] = {".png", ".jpg", ".jpeg"};
  for (const char* e : exts) {
    std::string img =
        joinPath(joinPath(sdcardRoot() + "/Imgs", sys.id), base + e);
    if (fileExists(img)) return m_canvas.surfaceForPath(img);
  }
  // folder names drift (PSP games live under Imgs/PSP while the system id is
  // PPSSPP): fall back to a name index of every Imgs subfolder
  static std::map<std::string, std::string> index;
  if (index.empty()) {
    std::string root = sdcardRoot() + "/Imgs";
    DIR* d = opendir(root.c_str());
    if (d) {
      struct dirent* e;
      while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        std::string sub = joinPath(root, e->d_name);
        DIR* sd = opendir(sub.c_str());
        if (!sd) continue;
        struct dirent* se;
        while ((se = readdir(sd))) {
          std::string n = se->d_name;
          std::string ln = lower(n);
          for (const char* e : {".png", ".jpg", ".jpeg"}) {
            size_t el = strlen(e);
            if (ln.size() > el && ln.substr(ln.size() - el) == e) {
              index[ln.substr(0, ln.size() - el)] = joinPath(sub, n);
              break;
            }
          }
        }
        closedir(sd);
      }
      closedir(d);
    }
    static const bool dbg = getenv("NDS_GLDBG") != nullptr;
    if (dbg) LOG_INFO("art index: %zu images", index.size());
  }
  auto it = index.find(lower(base));
  if (it != index.end()) return m_canvas.surfaceForPath(it->second);
  return nullptr;
}

uint32_t Carousel::bodyColorFor(const std::string& id) const {
  // one shell, a distinct colour per console
  std::string m = id;
  for (char& ch : m) ch = (char)tolower((unsigned char)ch);
  auto has = [&](const char* k) { return m.find(k) != std::string::npos; };
  if (has("gba")) return 0x7C4DFF;   // indigo
  if (has("gbc")) return 0x2AC3B0;   // teal
  if (has("gb")) return 0xD9A521;    // yellow
  if (has("n64")) return 0x2B3A67;   // midnight blue
  if (has("nes") || has("fc")) return 0x9AA0A6;  // grey
  if (has("snes") || has("sfc")) return 0xB9A9E0;  // lavender
  if (has("nds") || has("3ds")) return 0xEDEDED;   // white
  if (has("switch")) return 0xE0453A;  // red
  if (has("vita") || has("psp")) return 0x2E2E38;  // charcoal
  if (has("ps") || has("cd")) return 0x8A8A94;     // dark silver disc
  if (has("dc")) return 0xE07A2A;    // orange
  if (has("md") || has("genesis")) return 0x1F1F28;  // black
  if (has("gg")) return 0x3B7DD8;    // blue
  if (has("wsc") || has("ws")) return 0xB5502A;   // brick
  if (has("ngp") || has("ngpc")) return 0x4468A8;  // cobalt
  if (has("lynx")) return 0x8A5A2B;  // tan
  if (has("pce")) return 0xD0A040;   // amber
  if (has("atari")) return 0x6B4A2B;  // brown
  if (has("cps")) return 0x2B6B4A;
  if (has("arcade") || has("mame") || has("fbneo") || has("pgm") ||
      has("neogeo"))
    return 0xB03A3A;                 // arcade red
  if (has("java") || has("ports") || has("easyrpg") || has("openbor") ||
      has("pico8"))
    return 0x3E8E5A;                 // homebrew green
  return 0x5A5A6E;
}


void Carousel::drawOverlay() {
  m_dirty = false;
  if (m_games.empty()) return;

  // render into the dedicated cart surface (scaled over the UI on present)
  const int rx = 0, ry = 0;
  const int rw = m_canvas.cartSurfaceW();
  const int rh = m_canvas.cartSurfaceH();
  if (rw <= 0 || rh <= 0) return;

  float aspect = (float)rw / (float)rh;
  Mat4 proj = Mat4::perspective(30.f, aspect, 1.f, 200.f);
  Mat4 view = Mat4::lookAt(0.f, 0.f, 20.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f);

  float lx = 0.12f, ly = 0.22f, lz = 0.97f;  // light from the viewer
  float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
  const float light[3] = {lx / ll, ly / ll, lz / ll};

  const std::vector<Mesh::Vertex>* vp = nullptr;
  const std::vector<unsigned short>* ip = nullptr;

  struct Tri {
    RasterVert v[3];
    const uint32_t* tex;
    int tw, th, tp;
    int shadeR, shadeG, shadeB;
  };
  std::vector<Tri> tris;
  tris.reserve(1024);

  static const int kOrder[3] = {1, -1, 0};
  for (int oi = 0; oi < 3; ++oi) {
    int idx = m_sel + kOrder[oi];
    if (idx < 0 || idx >= (int)m_games.size()) continue;
    // every cartridge can come from a different system (mixed showcase)
    const System& gsys = m_ctx.systems[m_games[idx]->systemIndex];
    SDL_Surface* art = textureFor(m_games[idx]);
    if (art && art->format->format != SDL_PIXELFORMAT_ARGB8888) art = nullptr;
    std::string modelId = m_forceModel.empty() ? gsys.id : m_forceModel;
    // resolve the system id through the map first: the disc check below must
    // compare model names ("cd"/"psp"), not system ids ("PS"/"DC"/"PPSSPP")
    {
      auto it = m_map.find(upper(modelId));
      if (it != m_map.end()) modelId = it->second;
    }
    // discs stay discs, everything else picks the cuboid that matches the
    // box art's orientation (landscape art -> horizontal cartridge)
    const bool isDisc = (modelId == "cd" || modelId == "psp");
    if (!isDisc && art && art->w > art->h) modelId = "wide";
    Mesh* gmesh = meshFor(modelId);
    if (!gmesh || !gmesh->valid()) continue;
    SDL_Surface* nameTex = isDisc ? nullptr : nameTexture(gsys);
    SDL_Surface* rainbow = isDisc ? rainbowTexture() : nullptr;
    // disc label = circular crop of the art: opaque paper underneath, or the
    // transparent parts of the art would punch holes through the face
    SDL_Surface* discArt = isDisc ? opaqueArt(art, 0x8E929A) : art;
    vp = &gmesh->verts();
    ip = &gmesh->indices();
    const auto& V = *vp;
    const auto& I = *ip;
    float dx = (float)idx - m_animPos;
    float x = dx * 11.2f;
    float z = -std::fabs(dx) * 4.3f;
    bool focused = kOrder[oi] == 0;
    float sc = focused ? 1.22f
                       : 0.80f - std::min(0.12f, (std::fabs(dx) - 1.f) * 0.10f);
    if (sc < 0.5f) sc = 0.5f;
    // focused cart floats a touch higher (selection "lift")
    float lift = focused ? -0.55f * (1.f - std::min(1.f, std::fabs(dx))) : 0.f;

    // only the focused cartridge carries the user's rotation; neighbours
    // keep their default pose (and snap back when focus leaves them)
    Mat4 pre = Mat4::identity();
    modelFixMat(modelId, gmesh, &pre);
    float yaw = focused ? (m_animYaw + 34.f) : 34.f;
    // every model fits the band no matter its source units
    float fit = gmesh->fitScaleTo(m_small ? 7.0f : 9.5f,
                                  m_small ? 5.0f : 6.75f, pre.m);
    float s3 = sc * fit;
    // centre the model first (store models are not origin-centred)
    float ccx = (gmesh->bboxMinX() + gmesh->bboxMaxX()) * 0.5f;
    float ccy = (gmesh->bboxMinY() + gmesh->bboxMaxY()) * 0.5f;
    float ccz = (gmesh->bboxMinZ() + gmesh->bboxMaxZ()) * 0.5f;
    float pitch = focused ? (m_animPitch - 20.f) : -20.f;
    Mat4 model = Mat4::mul(
        Mat4::translate(x, 0.7f + lift, z),
        Mat4::mul(
            Mat4::rotateY(yaw),
            Mat4::mul(Mat4::rotateX(pitch),
                      Mat4::mul(Mat4::mul(Mat4::scale(s3, s3, s3), pre),
                                Mat4::translate(-ccx, -ccy, -ccz)))));
    Mat4 mvp = Mat4::mul(proj, Mat4::mul(view, model));

    uint32_t body = bodyColorFor(gsys.id);
    float br = ((body >> 16) & 0xFF) / 255.f;
    float bg = ((body >> 8) & 0xFF) / 255.f;
    float bb = (body & 0xFF) / 255.f;

    // Fit the box art to the label plane without stretching it: the label
    // quads (u in [0,1]) are scaled about their centre so the art keeps its
    // aspect. The cart face shows around the label like a real sticker.
    // (The brand strip uses u in [2,3] and is left untouched.)
    bool labelFit = false;
    float lcx = 0.f, lcy = 0.f, lsx = 1.f, lsy = 1.f;
    if (art && !isDisc) {
      float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
      for (const Mesh::Vertex& v : V) {
        if (v.u < -0.5f || v.u > 1.5f) continue;
        mnx = std::min(mnx, v.px);
        mxx = std::max(mxx, v.px);
        mny = std::min(mny, v.py);
        mxy = std::max(mxy, v.py);
      }
      if (mxx > mnx && mxy > mny) {
        lcx = (mnx + mxx) * 0.5f;
        lcy = (mny + mxy) * 0.5f;
        float labA = (mxx - mnx) / (mxy - mny);
        float artA = (float)art->w / (float)art->h;
        if (artA < labA) {
          lsx = artA / labA;
        } else {
          lsy = labA / artA;
        }
        labelFit = true;
      }
    }

    // posed bounding box (used by the shadow, sticker and disc label)
    float bmin[3], bmax[3];
    gmesh->boundsAfter(pre.m, bmin, bmax);
    // pre is a pure rotation: its transpose maps posed -> raw
    auto toRaw = [&](float px, float py, float pz, float* out) {
      out[0] = pre.m[0] * px + pre.m[1] * py + pre.m[2] * pz;
      out[1] = pre.m[4] * px + pre.m[5] * py + pre.m[6] * pz;
      out[2] = pre.m[8] * px + pre.m[9] * py + pre.m[10] * pz;
    };
    // project a posed-space point to screen pixels
    auto project = [&](float px, float py, float pz, float* sx, float* sy) {
      float raw[3];
      toRaw(px, py, pz, raw);
      float X = mvp.m[0] * raw[0] + mvp.m[4] * raw[1] + mvp.m[8] * raw[2] +
                mvp.m[12];
      float Y = mvp.m[1] * raw[0] + mvp.m[5] * raw[1] + mvp.m[9] * raw[2] +
                mvp.m[13];
      float W = mvp.m[3] * raw[0] + mvp.m[7] * raw[1] + mvp.m[11] * raw[2] +
                mvp.m[15];
      if (std::fabs(W) < 1e-6f) W = 1e-6f;
      *sx = rx + ((X / W) * 0.5f + 0.5f) * rw;
      *sy = ry + (1.f - ((Y / W) * 0.5f + 0.5f)) * rh;
    };
    // shadow: the model's projected footprint, seated just under it
    {
      float sxmin = 1e30f, sxmax = -1e30f, symax = -1e30f;
      for (int c = 0; c < 8; ++c) {
        float x = (c & 1) ? bmax[0] : bmin[0];
        float y = (c & 2) ? bmax[1] : bmin[1];
        float z = (c & 4) ? bmax[2] : bmin[2];
        float sx, sy;
        project(x, y, z, &sx, &sy);
        sxmin = std::min(sxmin, sx);
        sxmax = std::max(sxmax, sx);
        symax = std::max(symax, sy);
      }
      float w = sxmax - sxmin;
      // height of the footprint on screen (bottom face only)
      float sybmin = 1e30f;
      for (int c = 0; c < 4; ++c) {
        float x = (c & 1) ? bmax[0] : bmin[0];
        float y = (c & 2) ? bmax[1] : bmin[1];
        float z = bmin[2];
        float sx, sy;
        project(x, y, z, &sx, &sy);
        sybmin = std::min(sybmin, sy);
      }
      (void)sybmin;
      if (w > 4.f && w < (float)rw * 3.f) {
        float rx = std::min(w * 0.30f, (float)rw * 0.16f);
        rasterShadow(m_canvas, (sxmin + sxmax) * 0.5f, symax - rx * 0.10f, rx,
                     rx * 0.26f, focused ? 105 : 60);
      }
    }

    auto emit = [&](int vi) -> RasterVert {
      const Mesh::Vertex& v = V[vi];
      float px = v.px, py = v.py;
      if (labelFit && v.u >= -0.5f && v.u <= 1.5f) {
        px = lcx + (px - lcx) * lsx;
        py = lcy + (py - lcy) * lsy;
      }
      RasterVert r{};
      float X = mvp.m[0] * px + mvp.m[4] * py + mvp.m[8] * v.pz + mvp.m[12];
      float Y = mvp.m[1] * px + mvp.m[5] * py + mvp.m[9] * v.pz + mvp.m[13];
      float Z = mvp.m[2] * px + mvp.m[6] * py + mvp.m[10] * v.pz + mvp.m[14];
      float Wc = mvp.m[3] * px + mvp.m[7] * py + mvp.m[11] * v.pz + mvp.m[15];
      if (std::fabs(Wc) < 1e-6f) Wc = 1e-6f;
      float ndx = X / Wc, ndy = Y / Wc;
      r.x = rx + (ndx * 0.5f + 0.5f) * rw;
      r.y = ry + (1.f - (ndy * 0.5f + 0.5f)) * rh;
      r.nz = Z / Wc;
      r.u = v.u >= 1.5f ? v.u - 2.f : v.u;  // brand strip -> 0..1
      r.v = gmesh->flipV() ? (1.f - v.v) : v.v;
      r.nx = model.m[0] * v.nx + model.m[4] * v.ny + model.m[8] * v.nz;
      r.ny = model.m[1] * v.nx + model.m[5] * v.ny + model.m[9] * v.nz;
      r.nz2 = model.m[2] * v.nx + model.m[6] * v.ny + model.m[10] * v.nz;
      r.wx = model.m[0] * px + model.m[4] * py + model.m[8] * v.pz +
             model.m[12];
      r.wy = model.m[1] * px + model.m[5] * py + model.m[9] * v.pz +
             model.m[13];
      r.wz = model.m[2] * px + model.m[6] * py + model.m[10] * v.pz +
             model.m[14];
      return r;
    };

    auto emitTri = [&](size_t t) {
      Tri tri;
      RasterVert a = emit((int)I[t]);
      RasterVert b = emit((int)I[t + 1]);
      RasterVert d = emit((int)I[t + 2]);

      // face normal (flat shading) + backface cull against the eye
      float fnx = a.nx + b.nx + d.nx;
      float fny = a.ny + b.ny + d.ny;
      float fnz = a.nz2 + b.nz2 + d.nz2;
      float fwx = (a.wx + b.wx + d.wx) / 3.f;
      float fwy = (a.wy + b.wy + d.wy) / 3.f;
      float fwz = (a.wz + b.wz + d.wz) / 3.f;
      float cvx = -fwx, cvy = -fwy, cvz = 20.f - fwz;
      // backface cull against the eye (keep grazing faces: a hard zero test
      // popped them in/out mid-rotation). The discs are thin, so their far
      // flat face can still poke through at grazing angles - cull that one
      // strictly so the cover art never appears from behind.
      float nzLocal = (V[I[t]].nz + V[I[t + 1]].nz + V[I[t + 2]].nz) / 3.f;
      float cullTol = (isDisc && std::fabs(nzLocal) > 0.5f) ? 0.02f : -0.04f;
      float fnl = std::sqrt(fnx * fnx + fny * fny + fnz * fnz);
      if (fnx * cvx + fny * cvy + fnz * cvz <= cullTol * fnl) return;

      // pick the surface for this triangle:
      //  - discs: top face = box art, bottom face = CD rainbow sheen, rim =
      //    plain body colour
      //  - carts: label quads (u 0..1) = box art, brand strip (u 2..3) =
      //    the embossed console name, body quads (no uv) = console colour
      const float tu0 = V[I[t]].u, tu1 = V[I[t + 1]].u, tu2 = V[I[t + 2]].u;
      float tumin = std::min(tu0, std::min(tu1, tu2));
      SDL_Surface* src = nullptr;
      if (isDisc) {
        if (nzLocal < -0.5f)
          src = rainbow;
        else if (nzLocal > 0.5f)
          src = discArt;
      } else if (tumin >= 1.5f) {
        src = nameTex;
      } else if (tumin >= -0.5f) {
        src = art;
      }
      if (src && src->format->format != SDL_PIXELFORMAT_ARGB8888) src = nullptr;
      int tw = 0, th = 0, tp = 0;
      const uint32_t* tex = nullptr;
      if (src) {
        tex = (const uint32_t*)src->pixels;
        tw = src->w;
        th = src->h;
        tp = src->pitch / 4;
      }
      bool textured = tex != nullptr;

      float nl = std::sqrt(fnx * fnx + fny * fny + fnz * fnz);
      if (nl > 1e-6f) {
        fnx /= nl;
        fny /= nl;
        fnz /= nl;
      }
      float lam = std::max(0.f, fnx * light[0] + fny * light[1] +
                                    fnz * light[2]);
      float vlen = std::sqrt(cvx * cvx + cvy * cvy + cvz * cvz);
      float facing = vlen > 1e-6f
                         ? std::fabs(fnx * cvx + fny * cvy + fnz * cvz) / vlen
                         : 1.f;
      // soft specular sheen (light dir + view dir halfway)
      float hx = light[0] + (vlen > 1e-6f ? cvx / vlen : 0.f);
      float hy = light[1] + (vlen > 1e-6f ? cvy / vlen : 0.f);
      float hz = light[2] + (vlen > 1e-6f ? cvz / vlen : 0.f);
      float hl = std::sqrt(hx * hx + hy * hy + hz * hz);
      float spec = 0.f;
      if (hl > 1e-6f) {
        float d = (fnx * hx + fny * hy + fnz * hz) / hl;
        if (d > 0.f) {
          d *= d;
          d *= d;
          d *= d;
          d *= d;  // ^16
          spec = 0.55f * d;
        }
      }
      float dim = focused ? 1.f : 0.72f;
      float sh = (0.88f + 0.26f * lam + 0.18f * (1.f - facing)) * dim + spec;
      float tint = 0.5f + 0.5f * fny;
      // the body is filled with the console colour; the label keeps the art
      // as drawn (shading only)
      float b0 = textured ? 1.f : br;
      float b1 = textured ? 1.f : bg;
      float b2 = textured ? 1.f : bb;
      auto cl8 = [](float f) {
        int i = (int)(f * 255.f + 0.5f);
        return i < 0 ? 0 : (i > 255 ? 255 : i);
      };
      tri.shadeR = cl8(b0 * (sh + 0.08f * tint));
      tri.shadeG = cl8(b1 * (sh + 0.09f * tint));
      tri.shadeB = cl8(b2 * (sh + 0.12f * tint));
      tri.tex = tex;
      tri.tw = tw;
      tri.th = th;
      tri.tp = tp;
      tri.v[0] = a;
      tri.v[1] = b;
      tri.v[2] = d;
      tris.push_back(tri);
    };

    for (size_t t = 0; t + 2 < I.size(); t += 3) emitTri(t);
  }

  float* zbuf = nullptr;
  // the z-buffer is cleared for the previous frame's bbox in beginCartFrame()
  if (m_canvas.ensureZbuf(rw, rh)) zbuf = m_canvas.zbuf();
  static const int onlyMode = []() {
    const char* e = getenv("NDS_CARTDBG");
    return e ? atoi(e) : 0;
  }();
  for (const Tri& t : tris) {
    if (onlyMode == 1 && !t.tex) continue;   // labels only
    if (onlyMode == 2 && t.tex) continue;    // body only
    rasterTriangleFlat(m_canvas, t.v[0], t.v[1], t.v[2], t.tex, t.tw, t.th,
                       t.tp, t.shadeR, t.shadeG, t.shadeB, zbuf, rx, ry, rw,
                       rh);
  }
}

void Carousel::tick() {
  float ny = ease(m_animYaw, m_yaw, 0.22f);
  if (std::fabs(ny - m_animYaw) > 0.01f) {
    m_animYaw = ny;
    m_dirty = true;
  }
  float npi = ease(m_animPitch, m_pitch, 0.22f);
  if (std::fabs(npi - m_animPitch) > 0.01f) {
    m_animPitch = npi;
    m_dirty = true;
  }
  float np = ease(m_animPos, (float)m_sel, 0.24f);
  if (std::fabs(np - m_animPos) > 0.002f) {
    m_animPos = np;
    m_dirty = true;
  }
}

}  // namespace ndsui

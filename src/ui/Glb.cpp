#include "ui/Glb.h"

#include <SDL_image.h>

#include <cmath>
#include <functional>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

#include "core/Json.h"
#include "ui/Mat4.h"
#include "ui/Mesh.h"
#include "util/Log.h"

namespace ndsui {

namespace {

struct Glb {
  std::vector<unsigned char> json;
  std::vector<unsigned char> bin;
};

bool readFile(const std::string& path, std::vector<unsigned char>& out) {
  FILE* fp = fopen(path.c_str(), "rb");
  if (!fp) return false;
  fseek(fp, 0, SEEK_END);
  long n = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (n <= 0) {
    fclose(fp);
    return false;
  }
  out.resize((size_t)n);
  size_t got = fread(out.data(), 1, (size_t)n, fp);
  fclose(fp);
  return got == (size_t)n;
}

uint32_t rd32(const unsigned char* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

bool parseGlb(const std::vector<unsigned char>& data, Glb& out) {
  if (data.size() < 20) return false;
  if (memcmp(data.data(), "glTF", 4) != 0) return false;
  uint32_t total = rd32(data.data() + 8);
  if (total > data.size()) total = (uint32_t)data.size();
  size_t off = 12;
  while (off + 8 <= total) {
    uint32_t len = rd32(data.data() + off);
    uint32_t type = rd32(data.data() + off + 4);
    off += 8;
    if (off + len > total) return false;
    if (type == 0x4E4F534A)  // "JSON"
      out.json.assign(data.begin() + off, data.begin() + off + len);
    else if (type == 0x004E4942)  // "BIN"
      out.bin.assign(data.begin() + off, data.begin() + off + len);
    off += len;
    off = (off + 3) & ~3u;
  }
  return !out.json.empty();
}

int compCount(const std::string& t) {
  if (t == "SCALAR") return 1;
  if (t == "VEC2") return 2;
  if (t == "VEC3") return 3;
  if (t == "VEC4") return 4;
  return 0;
}

// Applies a node transform (matrix or TRS) to the accumulated parent.
Mat4 nodeTransform(const JsonValue* node) {
  const JsonValue* m = node->get("matrix");
  if (m && m->isArray() && m->arr.size() == 16) {
    Mat4 r;
    for (int i = 0; i < 16; ++i)
      r.m[i] = (float)(m->arr[i] ? m->arr[i]->number : 0.0);
    return r;
  }
  Mat4 r = Mat4::identity();
  const JsonValue* s = node->get("scale");
  const JsonValue* t = node->get("translation");
  const JsonValue* q = node->get("rotation");
  Mat4 trsT = Mat4::identity();
  Mat4 trsS = Mat4::identity();
  if (t && t->isArray() && t->arr.size() >= 3)
    trsT = Mat4::translate((float)t->arr[0]->number, (float)t->arr[1]->number,
                           (float)t->arr[2]->number);
  if (q && q->isArray() && q->arr.size() >= 4) {
    float x = (float)q->arr[0]->number, y = (float)q->arr[1]->number,
          z = (float)q->arr[2]->number, w = (float)q->arr[3]->number;
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;
    Mat4 rot = Mat4::identity();
    rot.m[0] = 1 - 2 * (yy + zz);
    rot.m[1] = 2 * (xy + wz);
    rot.m[2] = 2 * (xz - wy);
    rot.m[4] = 2 * (xy - wz);
    rot.m[5] = 1 - 2 * (xx + zz);
    rot.m[6] = 2 * (yz + wx);
    rot.m[8] = 2 * (xz + wy);
    rot.m[9] = 2 * (yz - wx);
    rot.m[10] = 1 - 2 * (xx + yy);
    r = rot;
  }
  if (s && s->isArray() && s->arr.size() >= 3)
    trsS = Mat4::scale((float)s->arr[0]->number, (float)s->arr[1]->number,
                       (float)s->arr[2]->number);
  // glTF TRS order: T * R * S
  return Mat4::mul(trsT, Mat4::mul(r, trsS));
}

void transformPoint(const Mat4& m, float x, float y, float z, float* out) {
  out[0] = m.m[0] * x + m.m[4] * y + m.m[8] * z + m.m[12];
  out[1] = m.m[1] * x + m.m[5] * y + m.m[9] * z + m.m[13];
  out[2] = m.m[2] * x + m.m[6] * y + m.m[10] * z + m.m[14];
}

void transformDir(const Mat4& m, float x, float y, float z, float* out) {
  out[0] = m.m[0] * x + m.m[4] * y + m.m[8] * z;
  out[1] = m.m[1] * x + m.m[5] * y + m.m[9] * z;
  out[2] = m.m[2] * x + m.m[6] * y + m.m[10] * z;
}

struct Reader {
  const unsigned char* base = nullptr;
  int stride = 0;
  int comps = 0;
  int compType = 0;

  float f(int idx, int c) const {
    const unsigned char* p = base + (size_t)idx * stride + (size_t)c * 4;
    return *(const float*)p;
  }
  unsigned u(int idx) const {
    const unsigned char* p = base + (size_t)idx * stride;
    if (compType == 5123) return *(const uint16_t*)p;
    if (compType == 5125) return *(const uint32_t*)p;
    if (compType == 5121) return *p;
    return 0;
  }
};

}  // namespace

bool loadGlb(const std::string& path, Mesh& mesh, SDL_Surface** outTex,
             bool* outFlipV) {
  std::vector<unsigned char> data;
  if (!readFile(path, data)) return false;
  Glb glb;
  if (!parseGlb(data, glb)) {
    LOG_WARN("glb: parse failed for %s", path.c_str());
    return false;
  }
  JsonPtr doc = jsonParse(std::string((const char*)glb.json.data(),
                                      glb.json.size()));
  if (!doc || !doc->isObject()) {
    LOG_WARN("glb: bad json in %s", path.c_str());
    return false;
  }
  const JsonValue* accessors = doc->get("accessors");
  const JsonValue* views = doc->get("bufferViews");
  if (!accessors || !views) return false;

  auto accessorReader = [&](int accIdx, Reader& r) -> bool {
    if (accIdx < 0 || accIdx >= (int)accessors->arr.size()) return false;
    const JsonValue* acc = accessors->arr[accIdx].get();
    if (!acc) return false;
    const JsonValue* bv = acc->get("bufferView");
    if (!bv) return false;
    int vi = (int)bv->number;
    if (vi < 0 || vi >= (int)views->arr.size()) return false;
    const JsonValue* view = views->arr[vi].get();
    int vOff = 0;
    if (const JsonValue* o = view->get("byteOffset")) vOff = (int)o->number;
    const JsonValue* so = acc->get("byteOffset");
    int aOff = so ? (int)so->number : 0;
    r.base = glb.bin.data() + vOff + aOff;
    r.comps = compCount(acc->getString("type", "VEC3"));
    const JsonValue* ct = acc->get("componentType");
    r.compType = ct ? (int)ct->number : 5126;
    int elem = r.comps * (r.compType == 5126 ? 4 : (r.compType == 5123 ? 2 : (r.compType == 5125 ? 4 : 1)));
    const JsonValue* st = view->get("byteStride");
    r.stride = st ? (int)st->number : elem;
    return true;
  };

  // material -> base colour surface (cached; needs materials/textures/images)
  const JsonValue* materials = doc->get("materials");
  const JsonValue* textures = doc->get("textures");
  const JsonValue* images = doc->get("images");
  std::map<int, SDL_Surface*> surfCache;  // image index -> surface
  auto surfaceForMaterial = [&](int matIdx) -> SDL_Surface* {
    if (matIdx < 0 || !materials || !textures || !images ||
        matIdx >= (int)materials->arr.size())
      return nullptr;
    const JsonValue* mat = materials->arr[matIdx].get();
    const JsonValue* pbr = mat->get("pbrMetallicRoughness");
    const JsonValue* bct = pbr ? pbr->get("baseColorTexture") : nullptr;
    if (!bct || !bct->get("index")) {
      // older Sketchfab exports use the specular-glossiness extension
      const JsonValue* ext = mat->get("extensions");
      const JsonValue* sg =
          ext ? ext->get("KHR_materials_pbrSpecularGlossiness") : nullptr;
      bct = sg ? sg->get("diffuseTexture") : nullptr;
    }
    if (!bct || !bct->get("index")) return nullptr;
    int ti = (int)bct->get("index")->number;
    if (ti < 0 || ti >= (int)textures->arr.size()) return nullptr;
    const JsonValue* src = textures->arr[ti]->get("source");
    if (!src) return nullptr;
    int ii = (int)src->number;
    auto it = surfCache.find(ii);
    if (it != surfCache.end()) return it->second;
    SDL_Surface* surf = nullptr;
    if (ii >= 0 && ii < (int)images->arr.size()) {
      const JsonValue* bv = images->arr[ii]->get("bufferView");
      if (bv) {
        int bvi = (int)bv->number;
        if (bvi >= 0 && bvi < (int)views->arr.size()) {
          const JsonValue* view = views->arr[bvi].get();
          const JsonValue* ov = view->get("byteOffset");
          int off = ov ? (int)ov->number : 0;
          int len = (int)view->get("byteLength")->number;
          if (off + len <= (int)glb.bin.size()) {
            SDL_RWops* rw = SDL_RWFromConstMem(glb.bin.data() + off, len);
            SDL_Surface* raw = rw ? IMG_Load_RW(rw, 1) : nullptr;
            if (raw) {
              surf = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
              SDL_FreeSurface(raw);
            }
          }
        }
      }
    }
    surfCache[ii] = surf;
    return surf;
  };

  // walk the scene and collect the triangles with their world transform
  mesh.mutableVerts().clear();
  mesh.mutableIndices().clear();
  mesh.mutableParts().clear();
  Mat4 identity = Mat4::identity();

  std::function<void(int, const Mat4&)> visit = [&](int nodeIdx,
                                                    const Mat4& parent) {
    if (!doc->get("nodes") || nodeIdx < 0 ||
        nodeIdx >= (int)doc->get("nodes")->arr.size())
      return;
    const JsonValue* node = doc->get("nodes")->arr[nodeIdx].get();
    Mat4 world = Mat4::mul(parent, nodeTransform(node));
    const JsonValue* meshIdx = node->get("mesh");
    const JsonValue* children = node->get("children");
    if (meshIdx) {
      int mi = (int)meshIdx->number;
      const JsonValue* meshes = doc->get("meshes");
      if (meshes && mi >= 0 && mi < (int)meshes->arr.size()) {
        const JsonValue* prims = meshes->arr[mi]->get("primitives");
        if (prims && prims->isArray()) {
          for (const auto& primPtr : prims->arr) {
            const JsonValue* prim = primPtr.get();
            if (!prim) continue;
            const JsonValue* attrs = prim->get("attributes");
            if (!attrs) continue;
            const JsonValue* posAcc = attrs->get("POSITION");
            if (!posAcc) continue;
            Reader rp, rn, ru, ri;
            if (!accessorReader((int)posAcc->number, rp)) continue;
            int nrmIdx = -1, uvIdx = -1, idxIdx = -1;
            if (const JsonValue* a = attrs->get("NORMAL")) nrmIdx = (int)a->number;
            if (const JsonValue* a = attrs->get("TEXCOORD_0"))
              uvIdx = (int)a->number;
            if (const JsonValue* a = prim->get("indices")) idxIdx = (int)a->number;
            const JsonValue* cnt = accessors->arr[(int)posAcc->number].get()
                                       ->get("count");
            int vcount = cnt ? (int)cnt->number : 0;
            if (vcount <= 0) continue;
            bool hasN = nrmIdx >= 0 && accessorReader(nrmIdx, rn);
            bool hasUv = uvIdx >= 0 && accessorReader(uvIdx, ru);
            bool hasIdx = idxIdx >= 0 && accessorReader(idxIdx, ri);
            size_t baseVert = mesh.mutableVerts().size();
            if (baseVert + (size_t)vcount > 65000) {
              LOG_WARN("glb: too many verts (%zu+%d)", baseVert, vcount);
              continue;
            }
            size_t firstIdx = mesh.mutableIndices().size();
            for (int i = 0; i < vcount; ++i) {
              float p[3], n[3];
              transformPoint(world, rp.f(i, 0), rp.f(i, 1), rp.f(i, 2), p);
              if (hasN) {
                transformDir(world, rn.f(i, 0), rn.f(i, 1), rn.f(i, 2), n);
                float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                if (l > 1e-6f) {
                  n[0] /= l;
                  n[1] /= l;
                  n[2] /= l;
                }
              } else {
                n[0] = 0;
                n[1] = 0;
                n[2] = 1;
              }
              Mesh::Vertex v{};
              v.px = p[0];
              v.py = p[1];
              v.pz = p[2];
              v.nx = n[0];
              v.ny = n[1];
              v.nz = n[2];
              v.u = hasUv ? ru.f(i, 0) : 0.f;
              v.v = hasUv ? ru.f(i, 1) : 0.f;
              mesh.mutableVerts().push_back(v);
            }
            int icount = hasIdx ? [&]() {
              const JsonValue* c2 =
                  accessors->arr[idxIdx].get()->get("count");
              return c2 ? (int)c2->number : 0;
            }() : vcount;
            for (int i = 0; i + 2 < icount; i += 3) {
              for (int k = 0; k < 3; ++k) {
                unsigned idx =
                    hasIdx ? ri.u(i + k) : (unsigned)(i + k);
                if (idx >= (unsigned)vcount) idx = 0;
                mesh.mutableIndices().push_back((unsigned short)(baseVert + idx));
              }
            }
            {
              Mesh::Part part;
              part.first = (int)firstIdx;
              part.count = (int)(mesh.mutableIndices().size() - firstIdx);
              const JsonValue* miPtr = prim->get("material");
              part.tex = surfaceForMaterial(miPtr ? (int)miPtr->number : -1);
              mesh.mutableParts().push_back(part);
            }
          }
        }
      }
    }
    if (children && children->isArray())
      for (const auto& c : children->arr)
        if (c) visit((int)c->number, world);
  };

  const JsonValue* scene = nullptr;
  const JsonValue* scenes = doc->get("scenes");
  const JsonValue* sceneIdx = doc->get("scene");
  if (scenes && scenes->isArray()) {
    int si = sceneIdx ? (int)sceneIdx->number : 0;
    if (si >= 0 && si < (int)scenes->arr.size())
      scene = scenes->arr[si].get();
  }
  if (scene) {
    const JsonValue* roots = scene->get("nodes");
    if (roots && roots->isArray())
      for (const auto& r : roots->arr)
        if (r) visit((int)r->number, identity);
  } else if (doc->get("nodes")) {
    for (int i = 0; i < (int)doc->get("nodes")->arr.size(); ++i)
      visit(i, identity);
  }

  if (mesh.mutableVerts().empty()) {
    LOG_WARN("glb: no geometry in %s", path.c_str());
    return false;
  }
  mesh.recomputeBounds();

  // legacy field: the first part's texture (kept for simple consumers)
  if (outTex) {
    *outTex = mesh.parts().empty() ? nullptr : mesh.parts()[0].tex;
    if (outFlipV) *outFlipV = false;
  }
  static const bool dbg = getenv("NDS_GLDBG") != nullptr;
  if (dbg)
    LOG_INFO("glb: %s -> %zu verts, %zu tris%s", path.c_str(),
             mesh.mutableVerts().size(), mesh.mutableIndices().size() / 3,
             (outTex && *outTex) ? ", texture" : "");
  return true;
}

}  // namespace ndsui

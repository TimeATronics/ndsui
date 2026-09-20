// OBJ mesh loading + CPU-side vertex data for the carousel. The carousel is
// rasterized through SDL_RenderGeometryRaw (the renderer's own GL context),
// so no FBO/readback/extra-context juggling on the Brick's Mali driver.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "ui/Mat4.h"

struct SDL_Surface;

namespace ndsui {

class Mesh {
 public:
  struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
  };

  // A sub-range of the index buffer with its own base-colour texture: the
  // store models are split into many materials (shell, label, screws, ...).
  struct Part {
    int first = 0;
    int count = 0;
    SDL_Surface* tex = nullptr;
  };

  bool loadObj(const std::string& path);  // CPU side only
  bool valid() const { return !m_verts.empty() && !m_index.empty(); }
  const std::string& path() const { return m_path; }
  const std::vector<Vertex>& verts() const { return m_verts; }
  const std::vector<unsigned short>& indices() const { return m_index; }
  std::vector<Vertex>& mutableVerts() { return m_verts; }
  std::vector<unsigned short>& mutableIndices() { return m_index; }
  std::vector<Part>& mutableParts() { return m_parts; }
  const std::vector<Part>& parts() const { return m_parts; }
  void recomputeBounds();  // after loading geometry from any format
  float bboxMinX() const { return m_minX; }
  float bboxMaxX() const { return m_maxX; }
  float bboxMinY() const { return m_minY; }
  float bboxMaxY() const { return m_maxY; }
  float bboxMinZ() const { return m_minZ; }
  float extX() const { return m_maxX - m_minX; }
  float extY() const { return m_maxY - m_minY; }
  float extZ() const { return m_maxZ - m_minZ; }
  float bboxMaxZ() const { return m_maxZ; }
  // models can bring their own base-colour texture (glTF/GLB)
  SDL_Surface* ownTexture() const { return m_ownTex; }
  void setOwnTexture(SDL_Surface* s) { m_ownTex = s; }
  bool flipV() const { return m_flipV; }
  void setFlipV(bool f) { m_flipV = f; }
  // uniform scale so the mesh fits a target width/height (source models come
  // in arbitrary units - Sketchfab downloads especially)
  // bounding box after an arbitrary (rotation) transform - used to place the
  // box-art sticker on the true front face no matter how the model is posed
  void boundsAfter(const float* pre, float outMin[3], float outMax[3]) const {
    float mnx = 1e30f, mny = 1e30f, mnz = 1e30f;
    float mxx = -1e30f, mxy = -1e30f, mxz = -1e30f;
    const float xs[2] = {m_minX, m_maxX};
    const float ys[2] = {m_minY, m_maxY};
    const float zs[2] = {m_minZ, m_maxZ};
    for (int i = 0; i < 8; ++i) {
      float x = xs[i & 1], y = ys[(i >> 1) & 1], z = zs[(i >> 2) & 1];
      float px, py, pz;
      if (pre) {
        px = pre[0] * x + pre[4] * y + pre[8] * z + pre[12];
        py = pre[1] * x + pre[5] * y + pre[9] * z + pre[13];
        pz = pre[2] * x + pre[6] * y + pre[10] * z + pre[14];
      } else {
        px = x;
        py = y;
        pz = z;
      }
      mnx = std::min(mnx, px);
      mxx = std::max(mxx, px);
      mny = std::min(mny, py);
      mxy = std::max(mxy, py);
      mnz = std::min(mnz, pz);
      mxz = std::max(mxz, pz);
    }
    outMin[0] = mnx;
    outMin[1] = mny;
    outMin[2] = mnz;
    outMax[0] = mxx;
    outMax[1] = mxy;
    outMax[2] = mxz;
  }

  float fitScaleTo(float targetW, float targetH,
                   const float* pre = nullptr) const {
    // if a pose fix is applied the model's own axes may be lying down, so
    // measure the bounding box after that transform
    float w, h;
    if (pre) {
      float mnx = 1e30f, mny = 1e30f, mxx = -1e30f, mxy = -1e30f;
      const float xs[2] = {m_minX, m_maxX};
      const float ys[2] = {m_minY, m_maxY};
      const float zs[2] = {m_minZ, m_maxZ};
      for (int i = 0; i < 8; ++i) {
        float x = xs[i & 1], y = ys[(i >> 1) & 1], z = zs[(i >> 2) & 1];
        float px = pre[0] * x + pre[4] * y + pre[8] * z + pre[12];
        float py = pre[1] * x + pre[5] * y + pre[9] * z + pre[13];
        mnx = std::min(mnx, px);
        mxx = std::max(mxx, px);
        mny = std::min(mny, py);
        mxy = std::max(mxy, py);
      }
      w = std::max(0.01f, mxx - mnx);
      h = std::max(0.01f, mxy - mny);
    } else {
      w = std::max(0.01f, m_maxX - m_minX);
      h = std::max(0.01f, m_maxY - m_minY);
    }
    return std::min(targetW / w, targetH / h);
  }

 private:
  std::vector<Vertex> m_verts;
  std::vector<unsigned short> m_index;
  std::string m_path;
  float m_minX = -1.f, m_maxX = 1.f, m_minY = -1.f, m_maxY = 1.f;
  float m_minZ = -1.f, m_maxZ = 1.f;
  SDL_Surface* m_ownTex = nullptr;
  bool m_flipV = true;
  std::vector<Part> m_parts;
};

}  // namespace ndsui

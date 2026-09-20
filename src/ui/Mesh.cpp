#include "ui/Mesh.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "util/Log.h"
#include "util/Platform.h"

namespace ndsui {

void Mesh::recomputeBounds() {
  if (m_verts.empty()) return;
  m_minX = m_minY = m_minZ = 1e30f;
  m_maxX = m_maxY = m_maxZ = -1e30f;
  for (const Vertex& v : m_verts) {
    m_minX = std::min(m_minX, v.px);
    m_maxX = std::max(m_maxX, v.px);
    m_minY = std::min(m_minY, v.py);
    m_maxY = std::max(m_maxY, v.py);
    m_minZ = std::min(m_minZ, v.pz);
    m_maxZ = std::max(m_maxZ, v.pz);
  }
}

bool Mesh::loadObj(const std::string& path) {
  m_path = path;
  FILE* fp = fopen(path.c_str(), "r");
  if (!fp) {
    LOG_WARN("mesh: cannot open %s", path.c_str());
    return false;
  }
  std::vector<float> pos, uv, nrm;
  char line[512];
  while (fgets(line, sizeof(line), fp)) {
    if (line[0] == 'v' && line[1] == ' ') {
      float x, y, z;
      if (sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3) {
        pos.push_back(x);
        pos.push_back(y);
        pos.push_back(z);
      }
    } else if (line[0] == 'v' && line[1] == 't') {
      float u, v;
      if (sscanf(line + 3, "%f %f", &u, &v) == 2) {
        uv.push_back(u);
        uv.push_back(v);
      }
    } else if (line[0] == 'v' && line[1] == 'n') {
      float x, y, z;
      if (sscanf(line + 3, "%f %f %f", &x, &y, &z) == 3) {
        nrm.push_back(x);
        nrm.push_back(y);
        nrm.push_back(z);
      }
    } else if (line[0] == 'f' && line[1] == ' ') {
      int vi[8], ti[8], ni[8], n = 0;
      char* p = line + 2;
      while (*p && n < 8) {
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p || *p == '\n' || *p == '\r') break;
        // OBJ vertex refs: v | v/vt | v//vn | v/vt/vn (parse each field)
        int a = 0, b = 0, c = 0;
        char* q = p;
        a = (int)strtol(q, &q, 10);
        if (*q == '/') {
          ++q;
          if (*q != '/') b = (int)strtol(q, &q, 10);
          if (*q == '/') {
            ++q;
            c = (int)strtol(q, &q, 10);
          }
        }
        while (*q && *q != ' ' && *q != '\t' && *q != '\n' && *q != '\r') ++q;
        p = q;
        if (a <= 0) break;
        vi[n] = a - 1;
        ti[n] = b > 0 ? b - 1 : -1;
        ni[n] = c > 0 ? c - 1 : -1;
        ++n;
      }
      for (int i = 2; i < n; ++i) {  // fan triangulate
        int tri[3] = {0, i - 1, i};
        for (int k = 0; k < 3; ++k) {
          int idx = tri[k];
          Vertex vx{};
          if (vi[idx] * 3 + 2 < (int)pos.size()) {
            vx.px = pos[vi[idx] * 3];
            vx.py = pos[vi[idx] * 3 + 1];
            vx.pz = pos[vi[idx] * 3 + 2];
          }
          if (ni[idx] >= 0 && ni[idx] * 3 + 2 < (int)nrm.size()) {
            vx.nx = nrm[ni[idx] * 3];
            vx.ny = nrm[ni[idx] * 3 + 1];
            vx.nz = nrm[ni[idx] * 3 + 2];
          }
          if (ti[idx] >= 0 && ti[idx] * 2 + 1 < (int)uv.size()) {
            vx.u = uv[ti[idx] * 2];
            vx.v = uv[ti[idx] * 2 + 1];
          } else {
            vx.u = -1.f;
            vx.v = -1.f;
          }
          m_index.push_back((unsigned)m_verts.size());
          m_verts.push_back(vx);
        }
      }
    }
  }
  fclose(fp);
  recomputeBounds();
  static const bool dbg = getenv("NDS_GLDBG") != nullptr;
  if (dbg)
    LOG_INFO("mesh %s: %zu verts %zu indices", fileBase(path).c_str(),
             m_verts.size(), m_index.size());
  return !m_verts.empty();
}

}  // namespace ndsui

// Tiny column-major 4x4 matrix helpers (GL style).
#pragma once

#include <cmath>
#include <cstring>

namespace ndsui {

struct Mat4 {
  float m[16];  // column major

  static Mat4 identity() {
    Mat4 r;
    std::memset(r.m, 0, sizeof(r.m));
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
    return r;
  }

  static Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) {
        float s = 0;
        for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
        r.m[c * 4 + row] = s;
      }
    return r;
  }

  static Mat4 translate(float x, float y, float z) {
    Mat4 r = identity();
    r.m[12] = x;
    r.m[13] = y;
    r.m[14] = z;
    return r;
  }

  static Mat4 scale(float x, float y, float z) {
    Mat4 r = identity();
    r.m[0] = x;
    r.m[5] = y;
    r.m[10] = z;
    return r;
  }

  static Mat4 rotateX(float deg) {
    float a = deg * 3.14159265f / 180.f, c = std::cos(a), s = std::sin(a);
    Mat4 r = identity();
    r.m[5] = c;
    r.m[6] = s;
    r.m[9] = -s;
    r.m[10] = c;
    return r;
  }
  static Mat4 rotateY(float deg) {
    float a = deg * 3.14159265f / 180.f, c = std::cos(a), s = std::sin(a);
    Mat4 r = identity();
    r.m[0] = c;
    r.m[2] = -s;
    r.m[8] = s;
    r.m[10] = c;
    return r;
  }
  static Mat4 rotateZ(float deg) {
    float a = deg * 3.14159265f / 180.f, c = std::cos(a), s = std::sin(a);
    Mat4 r = identity();
    r.m[0] = c;
    r.m[1] = s;
    r.m[4] = -s;
    r.m[5] = c;
    return r;
  }

  static Mat4 perspective(float fovDeg, float aspect, float zn, float zf) {
    float f = 1.f / std::tan(fovDeg * 3.14159265f / 360.f);
    Mat4 r;
    std::memset(r.m, 0, sizeof(r.m));
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf);
    r.m[11] = -1.f;
    r.m[14] = (2.f * zf * zn) / (zn - zf);
    return r;
  }

  static Mat4 lookAt(float ex, float ey, float ez, float cx, float cy, float cz,
                     float ux, float uy, float uz) {
    float fx = cx - ex, fy = cy - ey, fz = cz - ez;
    float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
    fx /= fl;
    fy /= fl;
    fz /= fl;
    float sx = fy * uz - fz * uy, sy = fz * ux - fx * uz, sz = fx * uy - fy * ux;
    float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
    sx /= sl;
    sy /= sl;
    sz /= sl;
    float ux2 = sy * fz - sz * fy, uy2 = sz * fx - sx * fz, uz2 = sx * fy - sy * fx;
    Mat4 r = identity();
    r.m[0] = sx;  r.m[4] = sy;  r.m[8]  = sz;
    r.m[1] = ux2; r.m[5] = uy2; r.m[9]  = uz2;
    r.m[2] = -fx; r.m[6] = -fy; r.m[10] = -fz;
    r.m[12] = -(sx * ex + sy * ey + sz * ez);
    r.m[13] = -(ux2 * ex + uy2 * ey + uz2 * ez);
    r.m[14] = -(-fx * ex - fy * ey - fz * ez);
    return r;
  }
};

}  // namespace ndsui

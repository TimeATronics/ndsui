#!/usr/bin/env python3
"""Tiny OBJ software rasterizer for previewing the carousel models.

Usage: preview_obj.py model.obj out.png [yaw_deg] [pitch_deg] [texture.png]

Renders a shaded 3/4 view with an optional box-art texture on the label
plane (the label is the face whose UVs span 0..1).
"""
import math
import sys

from PIL import Image, ImageDraw


def parse_obj(path):
    verts, uvs, norms, faces = [], [], [], []
    for line in open(path):
        p = line.split()
        if not p:
            continue
        if p[0] == "v":
            verts.append(tuple(float(x) for x in p[1:4]))
        elif p[0] == "vt":
            uvs.append(tuple(float(x) for x in p[1:3]))
        elif p[0] == "vn":
            norms.append(tuple(float(x) for x in p[1:4]))
        elif p[0] == "f":
            face = []
            for spec in p[1:]:
                parts = spec.split("/")
                vi = int(parts[0]) - 1
                ti = int(parts[1]) - 1 if len(parts) > 1 and parts[1] else -1
                ni = int(parts[2]) - 1 if len(parts) > 2 and parts[2] else -1
                face.append((vi, ti, ni))
            faces.append(face)
    return verts, uvs, norms, faces


def rot(p, yaw, pitch):
    x, y, z = p
    cy, sy = math.cos(yaw), math.sin(yaw)
    x, z = x * cy - z * sy, x * sy + z * cy
    cp, sp = math.cos(pitch), math.sin(pitch)
    y, z = y * cp - z * sp, y * sp + z * cp
    return (x, y, z)


def render(obj_path, out_path, yaw_deg=28, pitch_deg=-12, tex_path=None,
           W=640, H=640):
    verts, uvs, norms, faces = parse_obj(obj_path)
    tex = Image.open(tex_path).convert("RGB") if tex_path else None

    # fit model
    xs = [v[0] for v in verts]
    ys = [v[1] for v in verts]
    scale = min(W / (max(xs) - min(xs)), H / (max(ys) - min(ys))) * 0.62

    yaw, pitch = math.radians(yaw_deg), math.radians(pitch_deg)
    camz = 26.0
    img = Image.new("RGB", (W, H), (231, 231, 231))
    d = ImageDraw.Draw(img)
    zbuf = [[-1e9] * W for _ in range(H)]

    light = (0.45, 0.55, 0.70)

    def project(p):
        x, y, z = rot(p, yaw, pitch)
        z += camz
        f = 900.0
        return (W / 2 + f * x / z, H / 2 - f * y / z, z)

    for face in faces:
        pts = [project(verts[vi]) for vi, _, _ in face]
        is_label = all(ti >= 0 for _, ti, _ in face) and \
            any((abs(uvs[ti][0] - u) < 0.01 and abs(uvs[ti][1] - v) < 0.01)
                for _, ti, _ in face
                for (u, v) in [(0, 0), (1, 1), (0, 1), (1, 0)])
        if is_label and tex and len(face) == 4:
            # sample the texture at a grid inside the quad
            n = 14
            uv = [uvs[f[1]] for f in face]
            for i in range(n):
                for j in range(n):
                    u0, v0 = i / n, j / n
                    u1, v1 = (i + 1) / n, (j + 1) / n
                    def lerp_uv(u, v):
                        # bilinear over the quad corners (uv[0]=TL,1=TR,2=BR,3=BL)
                        tx = u
                        ty = v
                        ax = lerp(pts[0][0], pts[1][0], tx)
                        ay = lerp(pts[0][1], pts[1][1], ty)
                        bx = lerp(pts[3][0], pts[2][0], tx)
                        by = lerp(pts[3][1], pts[2][1], ty)
                        return (lerp(ax, bx, ty), lerp(ay, by, ty),
                                lerp(pts[0][2], pts[2][2], ty))
                    x0, y0, z0 = lerp_uv(u0, v0)
                    x1, y1, z1 = lerp_uv(u1, v1)
                    px = int((u0 + u1) / 2 * tex.width)
                    py = int((v0 + v1) / 2 * tex.height)
                    px = min(max(px, 0), tex.width - 1)
                    py = min(max(py, 0), tex.height - 1)
                    col = tex.getpixel((px, py))
                    shade = 0.85
                    col = tuple(int(c * shade) for c in col)
                    d.rectangle([x0, y0, x1, y1], fill=col)
        else:
            # flat polygon with lambert shading
            if len(face) >= 3:
                vi, _, ni = face[0]
                n = norms[ni] if ni >= 0 else (0, 0, 1)
                nr = rot(n, yaw, pitch)
                lam = max(0.18, sum(a * b for a, b in zip(nr, light)))
                base = (64, 120, 200) if "n64" in obj_path else (150, 150, 160)
                col = tuple(int(c * (0.35 + 0.65 * lam)) for c in base)
                d.polygon([(p[0], p[1]) for p in pts], fill=col)
    img.save(out_path)
    print("wrote", out_path)


def lerp(a, b, t):
    return a + (b - a) * t


if __name__ == "__main__":
    obj = sys.argv[1]
    out = sys.argv[2]
    yaw = float(sys.argv[3]) if len(sys.argv) > 3 else 28
    pitch = float(sys.argv[4]) if len(sys.argv) > 4 else -12
    tex = sys.argv[5] if len(sys.argv) > 5 else None
    render(obj, out, yaw, pitch, tex)

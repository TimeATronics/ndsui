#!/usr/bin/env python3
"""Convert an STL (binary or ascii) into a GLB for the carousel.

STL has no UVs or materials, so the result is geometry only: the carousel
renders it with the body colour and stamps the game's box art as a sticker
(which is exactly what we want for cartridge shells).

Usage: python3 tools/stl2glb.py in.stl out.glb
"""
import struct
import sys


def read_stl(path):
    data = open(path, "rb").read()
    tris = []
    if data[:5] == b"solid" and b"facet" in data[:512]:
        # ascii
        verts = []
        for line in data.decode("ascii", "replace").splitlines():
            line = line.strip()
            if line.startswith("vertex"):
                p = line.split()
                verts.append((float(p[1]), float(p[2]), float(p[3])))
                if len(verts) == 3:
                    tris.append(tuple(verts))
                    verts = []
        return tris
    n = struct.unpack("<I", data[80:84])[0]
    off = 84
    for i in range(n):
        if off + 50 > len(data):
            break
        vals = struct.unpack_from("<12fH", data, off)
        tris.append(((vals[3], vals[4], vals[5]),
                     (vals[6], vals[7], vals[8]),
                     (vals[9], vals[10], vals[11])))
        off += 50
    return tris


def main():
    inp, outp = sys.argv[1], sys.argv[2]
    tris = read_stl(inp)
    if not tris:
        sys.exit("no triangles read")
    # weld identical positions, flat normals
    vmap = {}
    pos, nrm, idx = [], [], []
    for t in tris:
        a, b, c = t
        ux, uy, uz = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
        vx, vy, vz = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
        nx, ny, nz = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
        l = (nx * nx + ny * ny + nz * nz) ** 0.5 or 1.0
        n3 = (nx / l, ny / l, nz / l)
        tri = []
        for p in t:
            key = (round(p[0], 5), round(p[1], 5), round(p[2], 5))
            j = vmap.get(key)
            if j is None:
                j = len(pos)
                vmap[key] = j
                pos.append(p)
                nrm.append(n3)
            tri.append(j)
        if tri[0] == tri[1] or tri[1] == tri[2] or tri[0] == tri[2]:
            continue
        idx.extend(tri)
    if len(pos) > 65535:
        sys.exit(f"too many verts for 16-bit indices: {len(pos)}")
    print(f"{inp}: {len(tris)} tris -> {len(pos)} verts, {len(idx)//3} tris")

    blob = bytearray()
    views, accs = [], []

    def add(data, target):
        while len(blob) % 4:
            blob.append(0)
        off = len(blob)
        blob.extend(data)
        views.append({"buffer": 0, "byteOffset": off, "byteLength": len(data),
                      "target": target})
        return len(views) - 1

    vp = add(b"".join(struct.pack("<3f", *p) for p in pos), 34962)
    accs.append({"bufferView": vp, "componentType": 5126, "count": len(pos),
                 "type": "VEC3"})
    ap = len(accs) - 1
    vn = add(b"".join(struct.pack("<3f", *n) for n in nrm), 34962)
    accs.append({"bufferView": vn, "componentType": 5126, "count": len(nrm),
                 "type": "VEC3"})
    an = len(accs) - 1
    vi = add(b"".join(struct.pack("<H", i) for i in idx), 34963)
    accs.append({"bufferView": vi, "componentType": 5123,
                 "count": len(idx), "type": "SCALAR"})
    ai = len(accs) - 1
    import json
    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": ap, "NORMAL": an},
            "indices": ai, "material": 0}]}],
        "materials": [{"pbrMetallicRoughness": {}}],
        "accessors": accs,
        "bufferViews": views,
        "buffers": [{"byteLength": len(blob)}],
    }
    js = json.dumps(gltf).encode()
    while len(js) % 4:
        js += b" "
    total = 12 + 8 + len(js) + 8 + len(blob)
    with open(outp, "wb") as f:
        f.write(b"glTF" + struct.pack("<II", 2, total))
        f.write(struct.pack("<I", len(js)) + b"JSON" + js)
        f.write(struct.pack("<I", len(blob)) + b"BIN\x00" + bytes(blob))
    print(f"wrote {outp}")


if __name__ == "__main__":
    main()

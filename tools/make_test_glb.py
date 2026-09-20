#!/usr/bin/env python3
"""Generate a test GLB (a textured box with a node rotation) to exercise the
glTF loader without needing a download."""
import json
import struct
import sys
import zlib


def png_solid(w, h, rgb):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    raw = b""
    for y in range(h):
        raw += b"\x00"
        for x in range(w):
            r, g, b = rgb
            if (x // 16 + y // 16) % 2 == 0:
                raw += bytes((r, g, b))
            else:
                raw += bytes((min(255, r + 40), min(255, g + 40), b))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def build(path):
    # box: 2.0 x 3.0 x 0.6, front face at +z
    hw, hh, hd = 1.0, 1.5, 0.3
    verts = [
        (-hw, -hh, hd), (hw, -hh, hd), (hw, hh, hd), (-hw, hh, hd),
        (-hw, -hh, -hd), (hw, -hh, -hd), (hw, hh, -hd), (-hw, hh, -hd),
    ]
    uvs = [(0, 1), (1, 1), (1, 0), (0, 0), (0, 1), (1, 1), (1, 0), (0, 0)]
    norms = [(0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 0, 1),
             (0, 0, -1), (0, 0, -1), (0, 0, -1), (0, 0, -1)]
    idx = [0, 1, 2, 0, 2, 3, 5, 4, 7, 5, 7, 6, 4, 0, 3, 4, 3, 7,
           1, 5, 6, 1, 6, 2, 3, 2, 6, 3, 6, 7, 4, 5, 1, 4, 1, 0]

    png = png_solid(64, 64, (200, 60, 60))
    pos = b"".join(struct.pack("<3f", *v) for v in verts)
    uv = b"".join(struct.pack("<2f", *v) for v in uvs)
    nrm = b"".join(struct.pack("<3f", *v) for v in norms)
    ind = b"".join(struct.pack("<H", i) for i in idx)

    pad = lambda b: b + b"\x00" * ((4 - len(b) % 4) % 4)
    pos_o = 0
    uv_o = pos_o + len(pos)
    nrm_o = uv_o + len(uv)
    ind_o = nrm_o + len(nrm)
    img_o = ind_o + len(ind)
    blob = pad(pos) + pad(uv) + pad(nrm) + pad(ind) + pad(png)

    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "rotation": [-0.7071, 0, 0, 0.7071]}],
        "meshes": [{"primitives": [{
            "attributes": {"POSITION": 0, "TEXCOORD_0": 1, "NORMAL": 2},
            "indices": 3, "material": 0}]}],
        "materials": [{"pbrMetallicRoughness": {
            "baseColorTexture": {"index": 0}}}],
        "textures": [{"source": 0}],
        "images": [{"mimeType": "image/png", "bufferView": 4}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 8, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5126, "count": 8, "type": "VEC2"},
            {"bufferView": 2, "componentType": 5126, "count": 8, "type": "VEC3"},
            {"bufferView": 3, "componentType": 5123, "count": len(idx), "type": "SCALAR"},
        ],
        "bufferViews": [
            {"buffer": 0, "byteOffset": pos_o, "byteLength": len(pos)},
            {"buffer": 0, "byteOffset": uv_o, "byteLength": len(uv)},
            {"buffer": 0, "byteOffset": nrm_o, "byteLength": len(nrm)},
            {"buffer": 0, "byteOffset": ind_o, "byteLength": len(ind)},
            {"buffer": 0, "byteOffset": img_o, "byteLength": len(png)},
        ],
        "buffers": [{"byteLength": len(blob)}],
    }
    js = pad(json.dumps(gltf).encode())
    with open(path, "wb") as f:
        total = 12 + 8 + len(js) + 8 + len(blob)
        f.write(b"glTF" + struct.pack("<II", 2, total))
        f.write(struct.pack("<I", len(js)) + b"JSON" + js)
        f.write(struct.pack("<I", len(blob)) + b"BIN\x00" + blob)
    print(f"{path}: {len(blob)} bytes bin")


if __name__ == "__main__":
    build(sys.argv[1] if len(sys.argv) > 1 else "assets/models/snes.glb")

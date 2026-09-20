#!/usr/bin/env python3
"""Clean up store GLB models for the carousel:

  * bake the node transforms
  * drop stray parts (base planes, tiny floating bits) - --drop i,j / --minpart
  * decimate with grid clustering (--grid N: cells across the largest axis)
  * flat per-face normals, degenerate triangles removed
  * writes a compact GLB (per-part base colour textures preserved, 1k -> as-is)

Usage:
  python3 tools/clean_glb.py in.glb out.glb [--grid 72] [--drop 0,5]
                            [--minpart 0.02] [--keep 0,1,2,3]
"""
import json
import struct
import sys
import zlib


def read_glb(path):
    data = open(path, "rb").read()
    assert data[:4] == b"glTF"
    total = struct.unpack("<I", data[8:12])[0]
    off, js, bin_ = 12, None, b""
    while off + 8 <= min(total, len(data)):
        clen, ctype = struct.unpack("<II", data[off:off + 8])
        off += 8
        if ctype == 0x4E4F534A:
            js = json.loads(data[off:off + clen].decode("utf-8", "replace"))
        elif ctype == 0x004E4942:
            bin_ = data[off:off + clen]
        off = (off + clen + 3) & ~3
    return js, bin_


def acc_floats(js, bin_, idx, comps):
    a = js["accessors"][idx]
    bv = js["bufferViews"][a["bufferView"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    stride = bv.get("byteStride", comps * 4)
    out = []
    for i in range(a["count"]):
        base = off + i * stride
        out.append(struct.unpack_from("<" + "f" * comps, bin_, base))
    return out


def acc_indices(js, bin_, idx):
    a = js["accessors"][idx]
    bv = js["bufferViews"][a["bufferView"]]
    off = bv.get("byteOffset", 0) + a.get("byteOffset", 0)
    ct = a["componentType"]
    fmt = {5123: "H", 5125: "I", 5121: "B"}[ct]
    size = {5123: 2, 5125: 4, 5121: 1}[ct]
    stride = bv.get("byteStride", size)
    out = []
    for i in range(a["count"]):
        out.append(struct.unpack_from("<" + fmt, bin_, off + i * stride)[0])
    return out


def mat_mul(a, b):
    r = [0.0] * 16
    for c in range(4):
        for row in range(4):
            s = 0.0
            for k in range(4):
                s += a[k * 4 + row] * b[c * 4 + k]
            r[c * 4 + row] = s
    return r


def node_matrix(n):
    if "matrix" in n:
        return list(n["matrix"])
    r = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]
    t = n.get("translation", [0, 0, 0])
    q = n.get("rotation", [0, 0, 0, 1])
    s = n.get("scale", [1, 1, 1])
    x, y, z, w = q
    rot = [
        1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y), 0,
        2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x), 0,
        2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y), 0,
        0, 0, 0, 1,
    ]
    sc = [s[0], 0, 0, 0, 0, s[1], 0, 0, 0, 0, s[2], 0, 0, 0, 0, 1]
    tr = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, t[0], t[1], t[2], 1]
    return mat_mul(tr, mat_mul(rot, sc))


def xform(m, p):
    x, y, z = p
    return (m[0] * x + m[4] * y + m[8] * z + m[12],
            m[1] * x + m[5] * y + m[9] * z + m[13],
            m[2] * x + m[6] * y + m[10] * z + m[14])


def main():
    inp, outp = sys.argv[1], sys.argv[2]
    grid, drop, minpart = 72, set(), 0.0
    args = sys.argv[3:]
    for i, a in enumerate(args):
        if a == "--grid":
            grid = int(args[i + 1])
        elif a == "--drop":
            drop = set(int(v) for v in args[i + 1].split(","))
        elif a == "--minpart":
            minpart = float(args[i + 1])
    js, bin_ = read_glb(inp)

    parts = []  # (worldmatrix, positions, uvs, indices, material)
    counter = [0]

    def visit(ni, parent):
        n = js["nodes"][ni]
        world = mat_mul(parent, node_matrix(n))
        if "mesh" in n:
            for prim in js["meshes"][n["mesh"]]["primitives"]:
                pi = counter[0]
                counter[0] += 1
                pos = acc_floats(js, bin_, prim["attributes"]["POSITION"], 3)
                uv = (acc_floats(js, bin_, prim["attributes"]["TEXCOORD_0"], 2)
                      if "TEXCOORD_0" in prim["attributes"] else [(0, 0)] * len(pos))
                idx = acc_indices(js, bin_, prim["indices"])
                mat = prim.get("material", 0)
                parts.append((world, pos, uv, idx, mat, pi))
        for c in n.get("children", []):
            visit(c, world)

    scene = js.get("scene", 0)
    for root in js["scenes"][scene].get("nodes", []):
        visit(root, [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1])

    # bake transforms; measure the overall bbox in world space
    baked = []
    mn = [1e30] * 3
    mx = [-1e30] * 3
    for (world, pos, uv, idx, mat, pi) in parts:
        wp = [xform(world, p) for p in pos]
        for p in wp:
            for k in range(3):
                mn[k] = min(mn[k], p[k])
                mx[k] = max(mx[k], p[k])
        baked.append((wp, uv, idx, mat, pi))
    exts = [mx[k] - mn[k] for k in range(3)]
    mainlen = max(exts)

    kept, dropped = [], []
    for (wp, uv, idx, mat, pi) in baked:
        if pi in drop:
            dropped.append(pi)
            continue
        # bbox of this part
        pmn = [1e30] * 3
        pmx = [-1e30] * 3
        for p in wp:
            for k in range(3):
                pmn[k] = min(pmn[k], p[k])
                pmx[k] = max(pmx[k], p[k])
        pe = [pmx[k] - pmn[k] for k in range(3)]
        if minpart > 0 and max(pe) < mainlen * minpart:
            dropped.append(pi)
            continue
        kept.append((wp, uv, idx, mat, pi))
    print(f"{inp}: kept {len(kept)} parts, dropped {dropped}")

    cell = mainlen / max(8, grid)
    out_parts = []
    stats = [0, 0]
    for (wp, uv, idx, mat, pi) in kept:
        # grid-cluster weld
        vmap = {}
        newpos, newuv, newidx = [], [], []
        for t in range(0, len(idx) - 2, 3):
            tri = []
            for k in range(3):
                vi = idx[t + k]
                # weld by position cell only: UV-accurate welding barely
                # merges anything on textured meshes
                key = (round(wp[vi][0] / cell), round(wp[vi][1] / cell),
                       round(wp[vi][2] / cell))
                j = vmap.get(key)
                if j is None:
                    j = len(newpos)
                    vmap[key] = j
                    newpos.append(wp[vi])
                    newuv.append(uv[vi])
                tri.append(j)
            if tri[0] == tri[1] or tri[1] == tri[2] or tri[0] == tri[2]:
                continue
            newidx.extend(tri)
        if not newidx:
            continue
        # flat normals
        normals = [None] * len(newpos)
        for t in range(0, len(newidx) - 2, 3):
            a, b, c = (newpos[newidx[t + k]] for k in range(3))
            ux, uy, uz = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
            vx, vy, vz = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
            nx, ny, nz = (uy * vz - uz * vy, uz * vx - ux * vz,
                          ux * vy - uy * vx)
            l = (nx * nx + ny * ny + nz * nz) ** 0.5 or 1.0
            n3 = (nx / l, ny / l, nz / l)
            for k in range(3):
                normals[newidx[t + k]] = n3
        out_parts.append((newpos, newuv, [n or (0, 0, 1) for n in normals],
                          newidx, mat))
        stats[0] += len(newpos)
        stats[1] += len(newidx) // 3
    print(f"  -> {stats[0]} verts {stats[1]} tris")

    # ---- write the GLB ----
    # gather the source images + the material -> image mapping
    img_bytes = []
    for im in js.get("images", []):
        bv = js["bufferViews"][im["bufferView"]]
        off = bv.get("byteOffset", 0)
        img_bytes.append(bin_[off:off + bv["byteLength"]])
    mat_img = {}
    for mi, m in enumerate(js.get("materials", [])):
        pbr = m.get("pbrMetallicRoughness", {})
        bct = pbr.get("baseColorTexture")
        if not bct:
            sg = m.get("extensions", {}).get(
                "KHR_materials_pbrSpecularGlossiness", {})
            bct = sg.get("diffuseTexture")
        if bct is not None:
            ti = bct["index"]
            src = js.get("textures", [])[ti].get("source")
            mat_img[mi] = src

    blob = bytearray()
    views, accs, out_mats, out_mesh_prims = [], [], [], []
    align = lambda: blob.extend(b"\x00" * ((4 - len(blob) % 4) % 4))

    def add_view(data, target=None):
        align()
        off = len(blob)
        blob.extend(data)
        v = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
        if target:
            v["target"] = target
        views.append(v)
        return len(views) - 1

    used_mats = {}
    for (pos, uv, nrm, idx, mat) in out_parts:
        posb = b"".join(struct.pack("<3f", *p) for p in pos)
        uvb = b"".join(struct.pack("<2f", *t) for t in uv)
        nrb = b"".join(struct.pack("<3f", *n) for n in nrm)
        if len(pos) > 65535:
            sys.exit("too many verts for 16-bit indices")
        idxb = b"".join(struct.pack("<H", i) for i in idx)
        vp = add_view(posb, 34962)
        vu = add_view(uvb, 34962)
        vn = add_view(nrb, 34962)
        vi = add_view(idxb, 34963)
        accs.append({"bufferView": vp, "componentType": 5126,
                     "count": len(pos), "type": "VEC3"})
        ap = len(accs) - 1
        accs.append({"bufferView": vu, "componentType": 5126,
                     "count": len(uv), "type": "VEC2"})
        au = len(accs) - 1
        accs.append({"bufferView": vn, "componentType": 5126,
                     "count": len(nrm), "type": "VEC3"})
        an = len(accs) - 1
        accs.append({"bufferView": vi, "componentType": 5123,
                     "count": len(idx), "type": "SCALAR"})
        ai = len(accs) - 1
        # material (dedup by source image)
        src = mat_img.get(mat)
        key = src if src is not None else -1
        omi = used_mats.get(key)
        if omi is None:
            omi = len(out_mats)
            used_mats[key] = omi
            if src is not None:
                vimg = add_view(img_bytes[src])
                out_mats.append({"pbrMetallicRoughness": {
                    "baseColorTexture": {"index": omi}}})
                # texture/image entries are built later; remember the view
                out_mats[omi]["_imgview"] = vimg
            else:
                out_mats.append({"pbrMetallicRoughness": {}})
        out_mesh_prims.append({
            "attributes": {"POSITION": ap, "TEXCOORD_0": au, "NORMAL": an},
            "indices": ai, "material": omi})
    # textures/images arrays
    out_textures, out_images = [], []
    for m in out_mats:
        iv = m.pop("_imgview", None)
        if iv is not None:
            out_images.append({"mimeType": "image/png", "bufferView": iv})
            out_textures.append({"source": len(out_images) - 1})
            m["pbrMetallicRoughness"]["baseColorTexture"] = {
                "index": len(out_textures) - 1}
    gltf = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0}],
        "meshes": [{"primitives": out_mesh_prims}],
        "materials": out_mats,
        "textures": out_textures,
        "images": out_images,
        "accessors": accs,
        "bufferViews": views,
        "buffers": [{"byteLength": len(blob)}],
    }
    jsb = json.dumps(gltf).encode()
    jsb += b" " * ((4 - len(jsb) % 4) % 4)
    align()
    total = 12 + 8 + len(jsb) + 8 + len(blob)
    with open(outp, "wb") as f:
        f.write(b"glTF" + struct.pack("<II", 2, total))
        f.write(struct.pack("<I", len(jsb)) + b"JSON" + jsb)
        f.write(struct.pack("<I", len(blob)) + b"BIN\x00" + bytes(blob))
    print(f"  wrote {outp}")


if __name__ == "__main__":
    main()

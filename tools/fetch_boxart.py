#!/usr/bin/env python3
"""Fetch box art for the games on the device from libretro-thumbnails
(no credentials needed) and place it where the launcher already looks:
  <sdcard>/Imgs/<SYSTEM>/<rom base>.png

Matching: libretro uses No-Intro naming ("Game (USA)") while ROMs here often
use "(U)": we try a list of name variants and prefix-match the repo listing.

Usage:
  python3 tools/fetch_boxart.py --ssh root@192.168.137.24 [--limit N]
                                [--only SYS] [--dry]
"""
import argparse
import json
import os
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

BASE = "https://api.github.com/repos/libretro-thumbnails"
RAW = "https://raw.githubusercontent.com/libretro-thumbnails"
CACHE = os.path.join(os.path.expanduser("~"), ".cache", "lt-lists")

# our system id -> libretro repo
REPOS = {
    "GB": "Nintendo_-_Game_Boy",
    "GBC": "Nintendo_-_Game_Boy_Color",
    "GBA": "Nintendo_-_Game_Boy_Advance",
    "FC": "Nintendo_-_Nintendo_Entertainment_System",
    "NES": "Nintendo_-_Nintendo_Entertainment_System",
    "SFC": "Nintendo_-_Super_Nintendo_Entertainment_System",
    "SNES": "Nintendo_-_Super_Nintendo_Entertainment_System",
    "N64": "Nintendo_-_Nintendo_64",
    "NDS": "Nintendo_-_Nintendo_DS",
    "PS": "Sony_-_PlayStation",
    "PSP": "Sony_-_PlayStation_Portable",
    "DC": "Sega_-_Dreamcast",
    "MD": "Sega_-_Mega_Drive_-_Genesis",
    "GG": "Sega_-_Game_Gear",
    "MS": "Sega_-_Master_System_-_Mark_III",
    "PCE": "NEC_-_PC_Engine_-_TurboGrafx_16",
    "WS": "Bandai_-_WonderSwan",
    "WSC": "Bandai_-_WonderSwan_Color",
    "NGP": "SNK_-_Neo_Geo_Pocket",
    "NGPC": "SNK_-_Neo_Geo_Pocket_Color",
    "A2600": "Atari_-_Atari_2600",
    "A7800": "Atari_-_Atari_7800",
    "LYNX": "Atari_-_Lynx",
    "VB": "Nintendo_-_Virtual_Boy",
    "C64": "Commodore_-_Commodore_64",
    "AMIGA": "Commodore_-_Amiga",
    "ZXS": "Sinclair_-_ZX_Spectrum",
    "MSX": "Microsoft_-_MSX",
    "DOS": "DOS",
}


def sh(args, **kw):
    return subprocess.run(args, capture_output=True, text=True, **kw)


def ssh(host, cmd):
    return sh(["ssh", "-o", "StrictHostKeyChecking=no", host, cmd])


def scp_down(host, remote, local):
    return sh(["scp", "-o", "StrictHostKeyChecking=no", f"{host}:{remote}", local])


def scp_up(host, local, remote):
    return sh(["scp", "-o", "StrictHostKeyChecking=no", local, f"{host}:{remote}"])


def get(url, binary=False):
    req = urllib.request.Request(url, headers={"User-Agent": "ndsui-boxart"})
    with urllib.request.urlopen(req, timeout=40) as r:
        return r.read() if binary else r.read().decode()


def repo_listing(repo):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, repo.replace("/", "_") + ".json")
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    names, page = [], 1
    while True:
        try:
            data = json.loads(get(f"{BASE}/{repo}/contents/Named_Boxarts?page={page}&per_page=1000"))
        except Exception as e:
            print(f"  !! listing {repo} page {page}: {e}")
            break
        if not isinstance(data, list) or not data:
            break
        names += [x["name"] for x in data if x.get("name", "").endswith(".png")]
        if len(data) < 1000:
            break
        page += 1
    with open(path, "w") as f:
        json.dump(names, f)
    return names


def variants(base):
    out = [base]
    regions = ["(U)", "(USA)", "(Europe)", "(World)", "(Japan)", "(UE)"]
    for r in regions:
        if r in base:
            for r2 in ["USA", "Europe", "World", "Japan"]:
                out.append(base.replace(r, f"({r2})"))
            out.append(base.replace(r, "").replace("  ", " ").strip())
    # strip all parenthesised tags
    import re
    out.append(re.sub(r"\s*\([^)]*\)", "", base).strip())
    return out


def best_match(listing, base):
    lower = {n.lower(): n for n in listing}
    pref = {}
    for n in listing:
        pref.setdefault(n[:14].lower(), []).append(n)
    for v in variants(base):
        cands = pref.get(v[:14].lower(), [])
        cands = [c for c in cands if c.lower().startswith(v.lower())]
        if not cands:
            cands = [c for c in listing if c.lower().startswith(v.lower())]
        if cands:
            for want in ["(USA)", "(Europe)", "(World)", "(Japan)"]:
                for c in cands:
                    if want in c:
                        return c
            return sorted(cands)[0]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ssh", default="root@192.168.137.24")
    ap.add_argument("--sdcard", default="/mnt/SDCARD")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--only", default="")
    ap.add_argument("--dry", action="store_true")
    args = ap.parse_args()
    host = args.ssh

    # collect the games per system from the device
    games = {}
    out = ssh(host, f"ls -d {args.sdcard}/Emus/*/ 2>/dev/null")
    sysdirs = [l.strip().rstrip("/") for l in out.stdout.splitlines() if l.strip()]
    for d in sysdirs:
        sid = os.path.basename(d)
        if args.only and sid.upper() != args.only.upper():
            continue
        if sid.upper() not in REPOS:
            continue
        out = ssh(host, f"ls '{d}../..{os.path.basename(d)}' 2>/dev/null; true")
        # roms live in <sdcard>/Roms/<SID>
        romdir = f"{args.sdcard}/Roms/{sid}"
        out = ssh(host, f"ls '{romdir}' 2>/dev/null; true")
        roms = [l.strip() for l in out.stdout.splitlines() if l.strip()]
        if roms:
            games[sid.upper()] = (romdir, roms)

    total = matched = 0
    tmpdir = tempfile.mkdtemp(prefix="ltart")
    tarname = os.path.join(tempfile.gettempdir(), "ltart.tar")
    tf = tarfile.open(tarname, "w")
    for sid, (romdir, roms) in sorted(games.items()):
        repo = REPOS[sid]
        print(f"== {sid}: {len(roms)} roms ({repo})")
        listing = repo_listing(repo)
        print(f"   {len(listing)} boxarts online")
        for rom in roms:
            if args.limit and total >= args.limit:
                break
            total += 1
            stem = os.path.splitext(rom)[0]
            have = ssh(host, f"test -f '{args.sdcard}/Imgs/{sid}/{stem}.png' && echo yes")
            if have.stdout.strip() == "yes":
                continue
            hit = best_match(listing, stem)
            if not hit:
                print(f"   miss: {rom}")
                continue
            matched += 1
            if args.dry:
                print(f"   {rom}  ->  {hit}")
                continue
            url = f"{RAW}/{repo}/master/Named_Boxarts/{urllib.parse.quote(hit)}"
            try:
                blob = get(url, binary=True)
            except Exception as e:
                print(f"   download failed {hit}: {e}")
                continue
            local = os.path.join(tmpdir, sid, stem + ".png")
            os.makedirs(os.path.dirname(local), exist_ok=True)
            with open(local, "wb") as f:
                f.write(blob)
            tf.add(local, arcname=f"Imgs/{sid}/{stem}.png")
            print(f"   {rom}  ->  {hit}")
    tf.close()
    print(f"matched {matched}/{total}")
    if matched and not args.dry:
        scp_up(host, tarname, "/tmp/ltart.tar")
        ssh(host, f"cd {args.sdcard} && tar xf /tmp/ltart.tar && rm /tmp/ltart.tar && "
                  f"ls Imgs | head -5 && echo unpacked")
    os.remove(tarname)


if __name__ == "__main__":
    import urllib.parse
    main()

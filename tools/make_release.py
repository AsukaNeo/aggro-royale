#!/usr/bin/env python3
"""make_release.py - build Aggro Royale's downloadable zip. Run from aggro-royale/.

  py -3 tools/make_release.py --version 0.9.0-beta [--xbe <default.xbe>]...
                              [--skip-build] [--allow-dirty]

Release builds can only be made on a machine with the game: the code inside
aggro_royale.exe is lifted from the player's own default.xbe (src/recomp/gen,
never committed), so no CI can build it. Build from clean worktrees of the
tag (this folder and ../xboxrecomp at v0.16.0 with patches/ applied), with
src/recomp/gen copied in. This script:

  1. configures build-release/ with -DAR_RELEASE=ON (no search of this
     machine's game folders; see CMakeLists.txt) and builds it;
  2. assembles dist/AggroRoyale-<version>/ from an allow-list: the game, the
     setup, a short player README, a default aggroroyale.ini, the licences;
  3. zips it, then checks the zip holds exactly the allow-list, no game files,
     and no piece of any --xbe (the games' own programs) inside either exe.

Nothing is published: upload the zip yourself (a GitHub release).
"""
import argparse
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TOOLKIT = os.path.abspath(os.path.join(ROOT, "..", "xboxrecomp"))
BUILD = os.path.join(ROOT, "build-release")
DIST = os.path.join(ROOT, "dist")

DEFAULT_INI = """; Aggro Royale settings. F10 (or Back+Start on a controller) opens the
; settings overlay, which changes these and can save them.
[display]
; renderer: gpu (Direct3D 11) or software. render_scale (1-4, x 640x480)
; and widescreen (1 = 16:9) take effect at the next start.
renderer=gpu
render_scale=2
widescreen=1
vsync=1
filter=sharp
scale=fit
fullscreen=0
window_scale=2
fps_counter=0
; anisotropy: 0 (the game's own) or 2-16. sharpen: 0 (off) to 100. fxaa: 0 or 1.
anisotropy=0
sharpen=0
fxaa=0
; frame_gen: off, blend or motion -- extra frames up to your display's refresh rate.
frame_gen=off

[input]
rumble=1
deadzone=0

[game]
; unlock_all: 1 = every level and unlock from a fresh career.
unlock_all=1
"""


# Published name -> source. Nothing else may be in the zip.
def allow_list(game_exe, setup_exe):
    return {
        "aggro_royale.exe": game_exe,
        "aggro_setup.exe": setup_exe,
        "README.txt": None,                     # generated
        "aggroroyale.ini": None,                # generated
        "licenses/AggroRoyale-LICENSE.txt": os.path.join(ROOT, "LICENSE"),
        "licenses/AggroRoyale-NOTICE.txt": os.path.join(ROOT, "NOTICE"),
        "licenses/xboxrecomp-LICENSE.txt": os.path.join(TOOLKIT, "LICENSE"),
        "licenses/xboxrecomp-NOTICE.txt": os.path.join(TOOLKIT, "NOTICE"),
        "licenses/LGPL-2.1.txt": os.path.join(TOOLKIT, "LICENSES", "LGPL-2.1.txt"),
    }


FORBIDDEN_EXT = (".xbe", ".iso", ".xiso", ".wma", ".xmv", ".bik", ".wav", ".bmp",
                 ".png", ".log", ".c", ".h", ".pdb", ".json", ".zal", ".geo", ".txr")


def run(cmd):
    print(">", " ".join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


def probes_of(path):
    """The XBE's header and windows of varied bytes from across it."""
    data = open(path, "rb").read()
    probes = [data[:256]]
    for start in range(len(data) // 8, len(data), len(data) // 8):
        for o in range(start, min(start + 65536, len(data) - 128), 64):
            if len(set(data[o:o + 128])) >= 32:     # padding is in any exe
                probes.append(data[o:o + 128])
                break
    return probes


def verify(zip_path, names, exes, xbes):
    problems = []
    with zipfile.ZipFile(zip_path) as z:
        listed = [i.filename for i in z.infolist() if not i.is_dir()]
    root = listed[0].split("/", 1)[0] + "/" if listed else ""
    rel = sorted(n[len(root):] for n in listed)
    if rel != sorted(names):
        problems.append("zip contents differ from the allow-list: %s" %
                        sorted(set(rel) ^ set(names)))
    for n in rel:
        if n.lower().endswith(FORBIDDEN_EXT):
            problems.append("forbidden file type in zip: " + n)
    if not xbes:
        problems.append("no --xbe given: the exes were not checked for game code")
    for xbe in xbes:
        probes = probes_of(xbe)
        for exe in exes:
            data = open(exe, "rb").read()
            hits = sum(1 for p in probes if p and p in data)
            print("%s vs %s: %d of %d probes found" % (os.path.basename(exe), xbe, hits, len(probes)))
            if hits:
                problems.append("%d probes from %s found inside %s" % (hits, xbe, exe))
    for exe in exes:
        data = open(exe, "rb").read().lower()
        for marker in (ROOT, TOOLKIT):
            if marker.lower().encode() in data:
                print("warning: %s contains a path from this machine (%s)" % (exe, marker))
    return problems


def uncommitted(repo, paths):
    try:
        out = subprocess.run(["git", "status", "--short", "--"] + paths, cwd=repo,
                             capture_output=True, text=True, check=True)
        return out.stdout.splitlines()
    except (OSError, subprocess.CalledProcessError):
        return ["(could not run git in %s)" % repo]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--version", required=True)
    ap.add_argument("--xbe", action="append", default=[],
                    help="a game's default.xbe to check the exes against (repeat)")
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--allow-dirty", action="store_true",
                    help="test build: allow uncommitted source changes")
    args = ap.parse_args()
    version = args.version

    dirty = ["aggro-royale: " + l for l in uncommitted(ROOT, ["src", "CMakeLists.txt", "patches", "seeds"])]
    if dirty:
        print("uncommitted source changes:\n  " + "\n  ".join(dirty))
        if not args.allow_dirty:
            sys.exit("commit them first, or pass --allow-dirty for a test build "
                     "that must not be published")
        version += "-dirty"

    if not args.skip_build:
        run(["cmake", "-S", ".", "-B", BUILD, "-A", "x64", "-DAR_RELEASE=ON",
             "-DAR_VERSION=" + version])
        run(["cmake", "--build", BUILD, "--config", "Release", "--", "-m"])
    game = os.path.join(BUILD, "Release", "aggro_royale.exe")
    setup = os.path.join(BUILD, "Release", "aggro_setup.exe")
    for exe in (game, setup):
        if not os.path.isfile(exe):
            sys.exit("no release exe at %s; build it first" % exe)

    name = "AggroRoyale-%s" % version
    stage = os.path.join(DIST, name)
    if os.path.isdir(stage):
        shutil.rmtree(stage)
    os.makedirs(os.path.join(stage, "licenses"))
    files = allow_list(game, setup)
    for dest, src in files.items():
        if src:
            if not os.path.isfile(src):
                sys.exit("missing %s (for %s)" % (src, dest))
            shutil.copyfile(src, os.path.join(stage, *dest.split("/")))
    readme = open(os.path.join(ROOT, "tools", "release_readme.txt"), encoding="utf-8").read()
    with open(os.path.join(stage, "README.txt"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(readme.replace("{version}", version))
    with open(os.path.join(stage, "aggroroyale.ini"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write(DEFAULT_INI)

    zip_path = os.path.join(DIST, name + "-windows-x64.zip")
    if os.path.exists(zip_path):
        os.remove(zip_path)
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for dest in files:
            z.write(os.path.join(stage, *dest.split("/")), name + "/" + dest)

    problems = verify(zip_path, list(files), (game, setup), args.xbe)
    if problems:
        for p in problems:
            print("PROBLEM:", p)
        sys.exit("the zip failed its checks; do not publish it")
    print("ok: %s (%.1f MB)" % (zip_path, os.path.getsize(zip_path) / 1048576.0))


if __name__ == "__main__":
    main()

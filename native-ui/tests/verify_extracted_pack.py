"""Host golden test for the on-device firmware asset extractor.

Runs against a pack produced by `paf_extract_pack VITA_ROOT OUT` (same code as the
application's extractor) and compares it with the golden manifests recorded when
the pack was prepared offline from firmware 3.74 (the git-ignored
native-ui/resources directory). Every golden decoded-pixel / PCM / byte hash must
match. PNG bytes are encoder specific, so images are compared by decoded pixels,
which this script re-derives independently with Pillow from the produced PNGs.

    python -I verify_extracted_pack.py --pack EXTRACTED --golden native-ui/resources

Needs Pillow. Neither firmware nor extracted data is ever committed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import wave

from PIL import Image


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load(path):
    return json.loads(path.read_text(encoding="utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pack", type=Path, required=True)
    parser.add_argument("--golden", type=Path, required=True)
    args = parser.parse_args()
    pack, golden = args.pack, args.golden
    problems, checked = [], {}

    def count(category, ok=True):
        total, bad = checked.get(category, (0, 0))
        checked[category] = (total + 1, bad + (0 if ok else 1))
        return ok

    def expect(category, what, mine, theirs):
        if not count(category, mine == theirs):
            problems.append(f"[{category}] {what}: got {mine!r}, golden {theirs!r}")

    # images: decoded RGBA must match the golden hash, checked on the produced file.
    g_images = load(golden / "images.json")
    m_images = {i["path"]: i for i in load(pack / "images.json")["images"]}
    for image in g_images["images"]:
        path = image["path"]
        mine = m_images.get(path)
        if mine is None or not (pack / path).is_file():
            count("images", False)
            problems.append(f"[images] missing {path}")
            continue
        with Image.open(pack / path) as png:
            rgba = png.convert("RGBA")
            expect("images", path + " size", rgba.size, (image["width"], image["height"]))
            expect("images", path + " pixels", sha(rgba.tobytes()), image["rgbaSha256"])
    for source in g_images["sources"]:
        mine = [s for s in load(pack / "images.json")["sources"] if s["name"] == source["name"]]
        expect("image sources", source["name"], (mine[0]["sha256"], mine[0]["images"]) if mine else None,
               (source["sha256"], source["images"]))
    extra = set(m_images) - {i["path"] for i in g_images["images"]}
    if extra:
        problems.append(f"[images] extra files not in golden: {sorted(extra)[:5]} ({len(extra)})")

    # sounds: PCM of every channel, mono/stereo mix and the WAV bytes themselves.
    g_sounds = load(golden / "audio/sounds.json")
    m_sounds = {s["path"]: s for s in load(pack / "audio/sounds.json")["sounds"]}
    for sound in g_sounds["sounds"]:
        mine = m_sounds.get(sound["path"])
        if mine is None:
            count("sounds", False)
            problems.append(f"[sounds] missing {sound['path']}")
            continue
        wav_path = pack / "audio" / sound["path"]
        with wave.open(str(wav_path), "rb") as wav:
            frames = wav.readframes(wav.getnframes())
            expect("sounds", sound["path"] + " format", (wav.getnchannels(), wav.getframerate(), wav.getnframes()),
                   (sound["channels"], sound["rate"], sound["samples"]))
        expect("sounds", sound["path"] + " pcm", sha(frames), sound["pcmSha256"])
        expect("sounds", sound["path"] + " wav", sha(wav_path.read_bytes()), sound["wavSha256"])
        expect("sounds", sound["path"] + " channels",
               [(c["channel"], c["sha256"], c["pcmSha256"]) for c in mine["originalChannels"]],
               [(c["channel"], c["sha256"], c["pcmSha256"]) for c in sound["originalChannels"]])
    m_sources = {s["name"]: s for s in load(pack / "audio/sounds.json")["sources"]}
    for source in g_sounds["sources"]:
        mine = m_sources.get(source["name"])
        expect("sound sources", source["name"],
               (mine["sha256"], mine["sounds"], mine["bindings"]) if mine else None,
               (source["sha256"], source["sounds"], source["bindings"]))

    # fonts: byte-identical copies
    for font in load(golden / "fonts.json"):
        path = pack / font["path"]
        expect("fonts", font["path"], (sha(path.read_bytes()), path.stat().st_size) if path.is_file() else None,
               (font["sha256"], font["bytes"]))

    # effects: decoded mips and the lock wallpaper
    g_effects = load(golden / "effects/effects.json")
    for texture in g_effects["textures"]:
        for mip in texture["mips"]:
            path = pack / mip["path"]
            if not path.is_file():
                count("effects", False)
                problems.append(f"[effects] missing {mip['path']}")
                continue
            with Image.open(path) as png:
                expect("effects", mip["path"], (png.mode, png.size, sha(png.tobytes())),
                       (mip["mode"], (mip["width"], mip["height"]), mip["pixelSha256"]))
    lock = g_effects["lockWallpaper"]
    expect("effects", "lock wallpaper", sha((pack / lock["path"]).read_bytes()), lock["sha256"])
    expect("effects", "effect sources",
           [(t["source"], t["sourceSha256"]) for t in load(pack / "effects/effects.json")["textures"]],
           [(t["source"], t["sourceSha256"]) for t in g_effects["textures"]])

    # lookups
    for lookup in load(golden / "effects/lookups/lookups.json")["images"]:
        with Image.open(pack / lookup["path"]) as png:
            expect("lookups", lookup["path"], (png.mode, png.size, sha(png.tobytes())),
                   ("L", (lookup["width"], lookup["height"]), lookup["pixelSha256"]))

    # system applications: byte-identical copies of the firmware files
    for path in sorted((golden / "system-apps").rglob("*")):
        if not path.is_file() or "ECVS00001" in path.parts:
            continue
        relative = path.relative_to(golden).as_posix()
        mine = pack / relative
        expect("system-apps", relative, sha(mine.read_bytes()) if mine.is_file() else None, sha(path.read_bytes()))

    for category, (total, bad) in checked.items():
        print(f"{category:14s} {total - bad}/{total} match")
    if problems:
        print(f"{len(problems)} mismatches:")
        for line in problems[:60]:
            print(" ", line)
        return 1
    print("All golden hashes match")
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Prepare local shell effect textures and the lock wallpaper, offline only.

Accepts the observed uncompressed BGRA8/A8 DDS variants. Every supplied mip is
preserved separately; no resizing, generated mipmaps or color conversion.
Requires Pillow. Output belongs in the Git-ignored native-ui/resources pack.
"""

import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
from PIL import Image


def sha(data):
    return hashlib.sha256(data).hexdigest()


def decode_dds(data):
    if len(data) < 128 or data[:4] != b"DDS ":
        raise ValueError("Incomplete DDS header")
    size, flags, height, width, pitch, depth, levels = struct.unpack_from("<7I", data, 4)
    pixel = struct.unpack_from("<8I", data, 76)
    caps = struct.unpack_from("<5I", data, 108)
    if size != 124 or not width or not height or depth or caps[1] or caps[2]:
        raise ValueError("Unsupported DDS dimensions or surface kind")
    if pixel == (32, 65, 0, 32, 0xff0000, 0xff00, 0xff, 0xff000000):
        mode, encoding, bytes_per_pixel = "RGBA", "bgra8", 4
    elif pixel == (32, 2, 0, 8, 0, 0, 0, 255):
        # L stores the original alpha byte, not invented RGB channels.
        mode, encoding, bytes_per_pixel = "L", "alpha8", 1
    else:
        raise ValueError(f"Unsupported DDS pixel format: {pixel}")
    levels = max(1, levels)
    if levels > max(width, height).bit_length():
        raise ValueError("DDS has too many mip levels")
    # These references contain tightly packed rows. Reject other pitches rather
    # than silently treating padding as texels. Some headers use total image size.
    if flags & 8 and pitch != width * bytes_per_pixel:
        raise ValueError("DDS row padding is not supported")
    offset, result = 128, []
    for level in range(levels):
        count = width * height * bytes_per_pixel
        raw = data[offset:offset + count]
        if len(raw) != count:
            raise ValueError("Truncated DDS mip level")
        if encoding == "bgra8":
            image = Image.frombytes(mode, (width, height), raw, "raw", "BGRA")
            # Independent byte-plane check, including hidden RGB under alpha=0.
            pixels = image.tobytes()
            if any(pixels[c::4] != raw[s::4] for c, s in enumerate((2, 1, 0, 3))):
                raise ValueError("DDS channel conversion differs")
        else:
            image = Image.frombytes(mode, (width, height), raw)
            if image.tobytes() != raw:
                raise ValueError("DDS alpha conversion differs")
        result.append((level, image))
        offset += count
        width, height = max(1, width // 2), max(1, height // 2)
    if offset != len(data):
        raise ValueError("Unconsumed DDS payload")
    return encoding, result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--effects", type=Path, required=True)
    parser.add_argument("--lock-image", type=Path, required=True)
    parser.add_argument("--pack", type=Path, required=True)
    args = parser.parse_args()
    target = args.pack / "effects"
    if target.exists():
        raise ValueError("Effect pack output must be a new directory")
    sources = sorted(args.effects.rglob("*.dds"))
    if not sources:
        raise ValueError("No effect texture inputs")
    # Decode/validate every input before creating any pack files.
    decoded = [(p, p.read_bytes(), decode_dds(p.read_bytes())) for p in sources]
    lock_data = args.lock_image.read_bytes()
    with Image.open(io.BytesIO(lock_data)) as lock:
        if lock.format != "PNG":
            raise ValueError("Lock wallpaper must be PNG")
        lock.load()
        lock_size = lock.size
    target.mkdir(parents=True)
    manifest = {"version": 1, "runtimeSource": "bundled effect mip PNG assets", "textures": []}
    for source, data, (encoding, levels) in decoded:
        relative = source.relative_to(args.effects).with_suffix("")
        entry = {"source": source.relative_to(args.effects).as_posix(),
                 "sourceSha256": sha(data), "encoding": encoding, "mips": []}
        for level, image in levels:
            path = target / relative / f"mip-{level}.png"
            path.parent.mkdir(parents=True, exist_ok=True)
            image.save(path, compress_level=9)
            with Image.open(path) as check:
                if check.mode != image.mode or check.tobytes() != image.tobytes():
                    raise ValueError("PNG round trip differs")
            entry["mips"].append({"level": level, "path": path.relative_to(args.pack).as_posix(),
                "width": image.width, "height": image.height, "mode": image.mode,
                "pixelSha256": sha(image.tobytes()), "pngSha256": sha(path.read_bytes())})
        manifest["textures"].append(entry)
    (target / "keylock.png").write_bytes(lock_data)
    manifest["lockWallpaper"] = {"path": "effects/keylock.png", "sha256": sha(lock_data),
        "width": lock_size[0], "height": lock_size[1]}
    (target / "effects.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Prepared {len(decoded)} textures / {sum(len(v[2][1]) for v in decoded)} original mips and lock wallpaper")


if __name__ == "__main__":
    main()

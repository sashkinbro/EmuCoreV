"""Offline image preparation using our standalone native resource/GIM readers.

Inputs are local development references. The result is an ordinary PNG asset
pack; the application does not contain or invoke the firmware containers or
this preparation tool. Requires Pillow and an Android device for the native CLI.
"""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import uuid
from PIL import Image


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", required=True)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--exporter", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("sources", type=Path, nargs="+")
    args = parser.parse_args()
    sources = sorted(args.sources, key=lambda p: p.name)
    if len({p.stem for p in sources}) != len(sources):
        raise ValueError("Resource source names must be unique")
    if args.output.exists():
        raise ValueError("Image pack output must be a new directory")
    base = [args.adb, "-s", args.serial]
    remote = "/data/local/tmp/paf-image-pack-" + uuid.uuid4().hex

    def run(*command):
        try:
            return subprocess.check_output([*base, *map(str, command)], stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as error:
            raise RuntimeError(error.output.decode(errors="replace")) from error

    manifest = {"version": 1, "runtimeSource": "bundled PNG assets", "sources": [], "images": []}
    with tempfile.TemporaryDirectory(prefix="native-image-pack-") as temporary:
        temporary = Path(temporary)
        run("shell", "mkdir", remote)
        try:
            run("push", args.exporter, remote + "/exporter")
            run("shell", "chmod", "755", remote + "/exporter")
            entries = []
            for index, source in enumerate(sources):
                run("push", source, remote + "/input.rco")
                metadata = json.loads(run("shell", remote + "/exporter", remote + "/input.rco", remote + f"/group{index}"))
                run("pull", remote + f"/group{index}", temporary / str(index))
                entries.append((source.stem, index, metadata))
                manifest["sources"].append({"name": source.name, "sha256": sha(source.read_bytes()), "images": len(metadata)})
                print(source.name, len(metadata), "images", flush=True)
            args.output.mkdir(parents=True)
            for group, index, metadata in entries:
                directory = args.output / "images" / group
                directory.mkdir(parents=True)
                for image in metadata:
                    raw = temporary / str(index) / image["file"]
                    if image["format"] in ("png", "dds"):
                        with Image.open(raw) as source:
                            width, height = source.size
                            pixels = source.convert("RGBA").tobytes()
                    else:
                        pixels = raw.read_bytes()
                        width, height = image["width"], image["height"]
                    if len(pixels) != width * height * 4:
                        raise ValueError("Incomplete native image output")
                    path = directory / (image["id"] + ".png")
                    Image.frombytes("RGBA", (width, height), pixels).save(path, compress_level=9)
                    # The ready pack must preserve every decoded channel.
                    with Image.open(path) as png:
                        if png.convert("RGBA").tobytes() != pixels:
                            raise ValueError("Prepared PNG differs from decoded source")
                    manifest["images"].append({"path": path.relative_to(args.output).as_posix(),
                        "resourceId": image["id"], "width": width, "height": height,
                        "rgbaSha256": sha(pixels), "pngSha256": sha(path.read_bytes())})
            (args.output / "images.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
            print("Prepared", len(manifest["images"]), "PNG assets; runtime extraction is not needed")
        finally:
            run("shell", "rm", "-rf", remote)


if __name__ == "__main__":
    main()

"""Copy locally supplied SFNT PVF fonts into the ignored ready resource pack."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("pack", type=Path)
args = parser.parse_args()
fonts = sorted(args.source.glob("*.pvf"))
if not fonts:
    raise ValueError("No local fonts supplied")
output = args.pack / "fonts"
output.mkdir(parents=True, exist_ok=True)
manifest = []
for source in fonts:
    data = source.read_bytes()
    suffix = {b"OTTO": ".otf", b"\x00\x01\x00\x00": ".ttf"}.get(data[:4])
    if suffix is None:
        raise ValueError(f"Not a supported SFNT font: {source.name}")
    target = output / (source.stem + suffix)
    shutil.copyfile(source, target)
    manifest.append({"path": target.relative_to(args.pack).as_posix(),
                     "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)})
(args.pack / "fonts.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
print(f"Prepared {len(manifest)} fonts without changing their contents")

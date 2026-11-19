"""Prepare local debug-only bubble reference data, never tracked in Git."""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("capture", type=Path)
parser.add_argument("pack", type=Path)
parser.add_argument("--environment", type=Path)
args = parser.parse_args()
sizes = {"stream-0.bin": 608 * 32, "indices.bin": 1220 * 2,
         "vertex-ubo-0.bin": 96 * 4, "fragment-ubo-0.bin": 10 * 4,
         "fragment-texture-0.bin": 128 * 128, "fragment-texture-0-palette.bin": 1024}
contents = {}
for name, size in sizes.items():
    data = (args.capture / name).read_bytes()
    if len(data) != size:
        raise ValueError(f"Unexpected preview reference size: {name}")
    contents[name] = data
environment = args.environment.read_bytes() if args.environment else bytes(64 * 64 * 4)
if len(environment) != 64 * 64 * 4:
    raise ValueError("Environment must be 64x64 RGBA")
contents["environment.rgba"] = environment
output = args.pack / "preview" / "bubble"
output.mkdir(parents=True, exist_ok=True)
for name, data in contents.items():
    (output / name).write_bytes(data)
print("Prepared debug reference geometry/pose; complete widget reconstruction remains pending")
if not args.environment:
    print("Environment is a zero-filled placeholder, not the original reflection map")

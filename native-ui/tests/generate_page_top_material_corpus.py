"""Generate top-surface vertex material corpus for the native controller.

Each record stores the fixture inputs (size, shape point, style) and the exact
original values for the recovered top-surface uniforms: MVP/ModelMatrix (pose and
light corpora), consolidated shape material and the point-light packs. The C++
device test assembles the native block and compares the recorded slots.

    python native-ui/tests/generate_page_top_material_corpus.py OWNED_DECRYPTED_LIBPAF OUTPUT.bin
"""
import argparse
import importlib.util
import struct
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
FIXTURES = [
    (960.0, 544.0, 960.0 / 544.0 - 0.02, 0.99, 0.21137017, 0.60689974, 0.008927729, 3.1533337),
    (896.0, 448.0, 1.9, 0.9, 0.18, 0.95, 0.012, 4.0),
    (840.0, 512.0, 1.5, 0.8, 0.18, 0.95, 0.012, 4.0),
    (1234.0, 567.0, 2.1, 0.95, 0.2, 0.9, 0.01, 3.3),
]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


oracle = load("material_oracle", BASE / "tests/generate_page_material_reference.py")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    image = args.reference.read_bytes()
    records = []
    for fixture in FIXTURES:
        width, height, point_x, point_y = fixture[:4]
        style = fixture[4:8]
        matrices = oracle.record(image, width, height, (point_x, point_y), style, matrices=True)
        shape = oracle.record(image, width, height, (point_x, point_y), style, matrices=False)
        lights = oracle.record(image, width, height, (point_x, point_y), style, lights=True)
        values = list(matrices) + list(shape) + list(lights)
        if len(values) != 85:
            raise ValueError("Unexpected oracle record length")
        records.append(struct.pack("<8f", *fixture) + struct.pack("<85I", *values))
    args.output.write_bytes(b"PAFTPM01" + struct.pack("<I", len(records)) + b"".join(records))
    print("Generated", len(records), "top-material records")


if __name__ == "__main__":
    main()

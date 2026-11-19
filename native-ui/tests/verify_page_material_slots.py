"""Cross-validate the traced PageTurn material setter slots against verified builders.

Runs the committed offline draw tracer for the flat and folded fixtures, then
checks the traced setter blocks against the already bit-exact material/pose/light
oracles for the *same* controlled fixture. This proves the recovered
setter-slot -> semantic mapping, not new renderer behaviour.

    python native-ui/tests/verify_page_material_slots.py OWNED_DECRYPTED_LIBPAF

The reference is validated by the tracer and the oracle; no firmware data is
written into the source tree.
"""
import argparse
import importlib.util
import struct
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
FIXTURES = [
    # width, height, fold point, style, folded
    (960.0, 544.0, (960.0 / 544.0 - 0.02, 0.99),
        (0.21137017, 0.60689974, 0.008927729, 3.1533337), False),
    (896.0, 448.0, (1.9, 0.9), (0.18, 0.95, 0.012, 4.0), False),
    (840.0, 512.0, (1.5, 0.8), (0.18, 0.95, 0.012, 4.0), False),
    (960.0, 544.0, (960.0 / 544.0 - 0.02, 0.99),
        (0.21137017, 0.60689974, 0.008927729, 3.1533337), True),
    (896.0, 448.0, (1.9, 0.9), (0.18, 0.95, 0.012, 4.0), True),
]


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


tracer = load_module("page_draw_tracer", BASE / "tests/trace_page_draw_protocol.py")
oracle = load_module("material_oracle", BASE / "tests/generate_page_material_reference.py")


def words_to_floats(words):
    return list(struct.unpack("<%df" % len(words),
        b"".join(struct.pack("<I", value) for value in words)))


def slots(events, material_object):
    result = {}
    for event in events:
        if event["kind"] == "setter" and event["object"] == material_object and event["block"]:
            result[event["method"]] = words_to_floats(event["block"])
    return result


def check(actual, offset, expected, label):
    got = actual[offset:offset + len(expected)]
    if got != expected:
        raise SystemExit("%s mismatch: traced %s expected %s" % (
            label, got, expected))


def verify(image, width, height, point, style, fold):
    tracer.WIDTH, tracer.HEIGHT = width, height
    tracer.POINT, tracer.STYLE = point, style
    events = tracer.trace(image, fold)
    blocks = slots(events, "0x14" if fold else "0xc9c")
    shape = words_to_floats(oracle.record(image, width, height, point, style, matrices=False))
    matrices = words_to_floats(oracle.record(image, width, height, point, style, matrices=True))
    lights = words_to_floats(oracle.record(image, width, height, point, style, lights=True))
    # Recovered top-vertex setter order: MVP, ModelMatrix, Grep, FoldPlane,
    # Params, Deepest, Aspect, NormalScaleZ, ..., MaterialPack, PointLightPack2,
    # PointLightPack1. Matrix corpus is model+mvp; light corpus is position+color.
    check(blocks["0x2"], 0, matrices[16:32], "slot MVP")
    check(blocks["0x3"], 0, matrices[0:16], "slot ModelMatrix")
    check(blocks["0x4"], 0, shape[4:6], "slot shape.Grep")
    check(blocks["0x5"], 0, shape[10:14], "slot shape.FoldPlane")
    check(blocks["0x6"], 0, shape[6:10], "slot shape.Params")
    check(blocks["0x7"], 0, shape[0:4], "slot shape.Deepest")
    check(blocks["0x8"], 0, shape[14:18], "slot shape.Aspect")
    check(blocks["0x9"], 0, shape[18:19], "slot shape.NormalScaleZ")
    check(blocks["0xb"], 9, shape[19:21], "slot MaterialPack fresnel pair")
    check(blocks["0xc"], 0, lights[16:32], "slot PointLightPack2")
    check(blocks["0xd"], 0, lights[0:16], "slot PointLightPack1")
    print("PASS %s %.0fx%.0f material setter-slot mapping: %d slots against verified oracles"
        % ("folded" if fold else "flat", width, height, len(blocks)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    image = args.reference.read_bytes()
    for width, height, point, style, fold in FIXTURES:
        verify(image, width, height, point, style, fold)


if __name__ == "__main__":
    main()

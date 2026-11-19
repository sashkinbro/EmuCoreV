"""Validate the committed PageTurn material-order reference.

Checks that the recorded material registration order matches the labels the
verified draw-trace slot check uses, that every program lists only known
PageTurn parameters and that all constructor-default values are exact IEEE-754
bit patterns. Offline only; it reads no firmware.

    python native-ui/tests/verify_page_material_reference.py
"""
import json
import struct
from pathlib import Path

BASE = Path(__file__).resolve().parents[1]
REFERENCE = BASE / "tests/reference/page_turn_material_order.json"

TOP_VERTEX = ["MVP", "ModelMatrix", "shape.Grep", "shape.FoldPlane", "shape.Params",
    "shape.Deepest", "shape.Aspect", "shape.NormalScaleZ", "lit_params.Eye",
    "MaterialPack", "PointLightPack2", "PointLightPack1", "UVTransform", "SqueezeZ"]
BOTTOM_VERTEX = ["MVP", "shape.Grep", "shape.FoldPlane", "shape.Params", "shape.Deepest",
    "shape.Aspect", "shape.NormalScaleZ", "Shadow", "UVTransform", "SqueezeZ"]
KNOWN = set(TOP_VERTEX) | set(BOTTOM_VERTEX) | {"ExpMinusTexture", "ExpMinusRange",
    "Tex", "CornerSize", "CornerTex", "CornerRefract", "BackSideOpacity", "Source",
    "Border", "Level", "Color", "EmptyShadow", "EmptyColor", "EmptyShadowColor",
    "EmptyBackground", "EmptyBackgroundUVFactor", "UVtransform"}


def f32(bits):
    if not (isinstance(bits, str) and bits.startswith("0x") and len(bits) <= 10):
        raise SystemExit("Invalid float bit pattern %r" % (bits,))
    return struct.unpack("<f", struct.pack("<I", int(bits, 16)))[0].hex()


def main():
    data = json.loads(REFERENCE.read_text())
    vertex, fragment = data["vertex"], data["fragment"]
    if vertex["page_turn_top.vp"] != TOP_VERTEX:
        raise SystemExit("top vertex order differs from the verified slot labels")
    if vertex["page_turn_top_round.vp"] != TOP_VERTEX:
        raise SystemExit("rounded top vertex order differs")
    if vertex["page_turn_bottom.vp"] != BOTTOM_VERTEX + ["ExpMinusTexture", "ExpMinusRange"]:
        raise SystemExit("bottom vertex order differs from the verified slot labels")
    if len(vertex) != 6 or len(fragment) != 7:
        raise SystemExit("Unexpected PageTurn program count")
    for name, parameters in list(vertex.items()) + list(fragment.items()):
        unknown = [parameter for parameter in parameters if parameter not in KNOWN]
        if unknown:
            raise SystemExit("%s lists unknown parameters %s" % (name, unknown))
    defaults = data["constructor_defaults_bits"]
    for section in ("material_pack", "style", "point_light_default", "point_light_second"):
        def walk(value, label):
            if isinstance(value, dict):
                for key, item in value.items():
                    walk(item, label + "." + key)
            elif isinstance(value, list):
                for index, item in enumerate(value):
                    walk(item, "%s[%d]" % (label, index))
            else:
                f32(value)
        walk(defaults[section], section)
    if defaults["corner_texture"] != "/page_turn/textures/page_corner.dds":
        raise SystemExit("corner texture reference differs")
    print("PASS PageTurn material reference: %d vertex / %d fragment programs, "
        "constructor defaults are exact bit patterns" % (len(vertex), len(fragment)))


if __name__ == "__main__":
    main()

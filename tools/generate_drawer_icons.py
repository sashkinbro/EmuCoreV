#!/usr/bin/env python3
"""Render EmuCoreV Android vector launcher layers into drawer-ready PNG files.

The renderer intentionally depends only on Pillow and the Python standard library.
Run it from any directory:

    python tools/generate_drawer_icons.py
    python tools/generate_drawer_icons.py --check
"""

from __future__ import annotations

import argparse
import math
import re
import struct
import sys
import tempfile
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw


ANDROID_NS = "{http://schemas.android.com/apk/res/android}"
TOKEN_PATTERN = re.compile(
    r"[A-Za-z]|[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?"
)
COMMAND_ARITY = {
    "M": 2,
    "L": 2,
    "H": 1,
    "V": 1,
    "C": 6,
    "S": 4,
    "Q": 4,
    "T": 2,
    "Z": 0,
}


@dataclass(frozen=True)
class VectorLayer:
    viewport_width: float
    viewport_height: float
    paths: tuple[tuple[str, str, float], ...]


def parse_color(value: str, fill_alpha: float) -> tuple[int, int, int, int]:
    raw = value.removeprefix("#")
    if len(raw) == 3:
        raw = "".join(character * 2 for character in raw)
    if len(raw) == 4:
        raw = "".join(character * 2 for character in raw)
    if len(raw) == 6:
        alpha = 255
        red, green, blue = (int(raw[index : index + 2], 16) for index in (0, 2, 4))
    elif len(raw) == 8:
        alpha = int(raw[0:2], 16)
        red, green, blue = (int(raw[index : index + 2], 16) for index in (2, 4, 6))
    else:
        raise ValueError(f"Unsupported Android color: {value}")
    return red, green, blue, round(alpha * max(0.0, min(fill_alpha, 1.0)))


def load_vector(path: Path) -> VectorLayer:
    root = ET.parse(path).getroot()
    viewport_width = float(root.attrib[f"{ANDROID_NS}viewportWidth"])
    viewport_height = float(root.attrib[f"{ANDROID_NS}viewportHeight"])
    paths: list[tuple[str, str, float]] = []
    for element in root.iter():
        if element.tag.rsplit("}", 1)[-1] != "path":
            continue
        path_data = element.attrib.get(f"{ANDROID_NS}pathData")
        fill_color = element.attrib.get(f"{ANDROID_NS}fillColor")
        if path_data and fill_color:
            paths.append(
                (
                    path_data,
                    fill_color,
                    float(element.attrib.get(f"{ANDROID_NS}fillAlpha", "1")),
                )
            )
    return VectorLayer(viewport_width, viewport_height, tuple(paths))


def cubic_point(
    start: tuple[float, float],
    control_one: tuple[float, float],
    control_two: tuple[float, float],
    end: tuple[float, float],
    amount: float,
) -> tuple[float, float]:
    inverse = 1.0 - amount
    return (
        inverse**3 * start[0]
        + 3 * inverse**2 * amount * control_one[0]
        + 3 * inverse * amount**2 * control_two[0]
        + amount**3 * end[0],
        inverse**3 * start[1]
        + 3 * inverse**2 * amount * control_one[1]
        + 3 * inverse * amount**2 * control_two[1]
        + amount**3 * end[1],
    )


def quadratic_point(
    start: tuple[float, float],
    control: tuple[float, float],
    end: tuple[float, float],
    amount: float,
) -> tuple[float, float]:
    inverse = 1.0 - amount
    return (
        inverse**2 * start[0] + 2 * inverse * amount * control[0] + amount**2 * end[0],
        inverse**2 * start[1] + 2 * inverse * amount * control[1] + amount**2 * end[1],
    )


def path_polygons(path_data: str) -> list[list[tuple[float, float]]]:
    tokens = TOKEN_PATTERN.findall(path_data.replace(",", " "))
    polygons: list[list[tuple[float, float]]] = []
    points: list[tuple[float, float]] = []
    current = (0.0, 0.0)
    subpath_start = current
    last_cubic_control: tuple[float, float] | None = None
    last_quadratic_control: tuple[float, float] | None = None
    command: str | None = None
    index = 0

    def absolute_pair(x: float, y: float, relative: bool) -> tuple[float, float]:
        return (current[0] + x, current[1] + y) if relative else (x, y)

    def finish_polygon() -> None:
        nonlocal points
        if len(points) >= 3:
            polygons.append(points)
        points = []

    while index < len(tokens):
        token = tokens[index]
        if token.isalpha():
            command = token
            index += 1
        if command is None:
            raise ValueError(f"Path data starts without a command: {path_data}")

        upper = command.upper()
        relative = command.islower()
        if upper not in COMMAND_ARITY:
            raise ValueError(f"Unsupported Android path command {command!r} in {path_data}")
        if upper == "Z":
            if points and points[-1] != subpath_start:
                points.append(subpath_start)
            finish_polygon()
            current = subpath_start
            last_cubic_control = None
            last_quadratic_control = None
            command = None
            continue

        arity = COMMAND_ARITY[upper]
        if index + arity > len(tokens):
            raise ValueError(f"Incomplete command {command!r} in {path_data}")
        values = [float(value) for value in tokens[index : index + arity]]
        index += arity

        if upper == "M":
            if points:
                finish_polygon()
            current = absolute_pair(values[0], values[1], relative)
            subpath_start = current
            points = [current]
            command = "l" if relative else "L"
        elif upper == "L":
            current = absolute_pair(values[0], values[1], relative)
            points.append(current)
        elif upper == "H":
            current = (current[0] + values[0], current[1]) if relative else (values[0], current[1])
            points.append(current)
        elif upper == "V":
            current = (current[0], current[1] + values[0]) if relative else (current[0], values[0])
            points.append(current)
        elif upper == "C":
            start = current
            control_one = absolute_pair(values[0], values[1], relative)
            control_two = absolute_pair(values[2], values[3], relative)
            end = absolute_pair(values[4], values[5], relative)
            points.extend(
                cubic_point(start, control_one, control_two, end, step / 24)
                for step in range(1, 25)
            )
            current = end
            last_cubic_control = control_two
        elif upper == "S":
            start = current
            control_one = (
                (2 * current[0] - last_cubic_control[0], 2 * current[1] - last_cubic_control[1])
                if last_cubic_control is not None
                else current
            )
            control_two = absolute_pair(values[0], values[1], relative)
            end = absolute_pair(values[2], values[3], relative)
            points.extend(
                cubic_point(start, control_one, control_two, end, step / 24)
                for step in range(1, 25)
            )
            current = end
            last_cubic_control = control_two
        elif upper == "Q":
            start = current
            control = absolute_pair(values[0], values[1], relative)
            end = absolute_pair(values[2], values[3], relative)
            points.extend(
                quadratic_point(start, control, end, step / 24)
                for step in range(1, 25)
            )
            current = end
            last_quadratic_control = control
        elif upper == "T":
            start = current
            control = (
                (
                    2 * current[0] - last_quadratic_control[0],
                    2 * current[1] - last_quadratic_control[1],
                )
                if last_quadratic_control is not None
                else current
            )
            end = absolute_pair(values[0], values[1], relative)
            points.extend(
                quadratic_point(start, control, end, step / 24)
                for step in range(1, 25)
            )
            current = end
            last_quadratic_control = control

        if upper not in {"C", "S"}:
            last_cubic_control = None
        if upper not in {"Q", "T"}:
            last_quadratic_control = None

    finish_polygon()
    return polygons


def render_layer(vector: VectorLayer, size: int, supersampling: int = 4) -> Image.Image:
    render_size = size * supersampling
    scale_x = render_size / vector.viewport_width
    scale_y = render_size / vector.viewport_height
    result = Image.new("RGBA", (render_size, render_size), (0, 0, 0, 0))
    for path_data, fill_color, fill_alpha in vector.paths:
        path_image = Image.new("RGBA", result.size, (0, 0, 0, 0))
        draw = ImageDraw.Draw(path_image)
        color = parse_color(fill_color, fill_alpha)
        for polygon in path_polygons(path_data):
            draw.polygon(
                [(round(x * scale_x), round(y * scale_y)) for x, y in polygon],
                fill=color,
            )
        result = Image.alpha_composite(result, path_image)
    return result.resize((size, size), Image.Resampling.LANCZOS)


def render_icon(background_path: Path, foreground_path: Path, size: int) -> Image.Image:
    background = render_layer(load_vector(background_path), size)
    foreground = render_layer(load_vector(foreground_path), size)
    return Image.alpha_composite(background, foreground)


def save_png(image: Image.Image, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    image.save(destination, format="PNG", optimize=True, compress_level=9)


def png_dimensions(path: Path) -> tuple[int, int]:
    data = path.read_bytes()[:24]
    if len(data) != 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"Not a valid PNG file: {path}")
    return struct.unpack(">II", data[16:24])


def generated_icons(project_root: Path, size: int) -> Iterable[tuple[Image.Image, Path]]:
    drawable = project_root / "app" / "src" / "main" / "res" / "drawable"
    output = project_root / "app" / "src" / "main" / "res" / "drawable-nodpi"
    variants = (
        ("", "ic_drawer_app.png"),
        ("_pro", "ic_drawer_app_pro.png"),
    )
    for suffix, file_name in variants:
        yield (
            render_icon(
                drawable / f"ic_launcher{suffix}_background.xml",
                drawable / f"ic_launcher{suffix}_foreground.xml",
                size,
            ),
            output / file_name,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--size", type=int, default=512)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.size <= 0:
        parser.error("--size must be positive")

    project_root = Path(__file__).resolve().parents[1]
    generated = list(generated_icons(project_root, args.size))
    if args.check:
        mismatches: list[str] = []
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            for image, destination in generated:
                candidate = temporary_root / destination.name
                save_png(image, candidate)
                if not destination.is_file() or candidate.read_bytes() != destination.read_bytes():
                    mismatches.append(destination.name)
        if mismatches:
            print("Outdated generated icons: " + ", ".join(mismatches), file=sys.stderr)
            return 1
        for _, destination in generated:
            if png_dimensions(destination) != (args.size, args.size):
                print(f"Unexpected PNG dimensions: {destination}", file=sys.stderr)
                return 1
        print("Drawer icons are up to date.")
        return 0

    for image, destination in generated:
        save_png(image, destination)
        print(f"Generated {destination.relative_to(project_root)} ({args.size}x{args.size})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

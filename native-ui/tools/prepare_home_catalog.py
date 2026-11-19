#!/usr/bin/env python3
"""Prepare an offline Home preview catalog from selected extracted Vita app files."""
import argparse
import json
import re
import shutil
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

def read_sfo(path: Path) -> dict[str, str]:
    data = path.read_bytes()
    if len(data) < 20 or data[:4] != b"\0PSF":
        raise ValueError(f"Invalid PSF/SFO file: {path}")
    _version, key_table, data_table, count = struct.unpack_from("<IIII", data, 4)
    if count > 4096 or 20 + count * 16 > len(data):
        raise ValueError(f"Invalid SFO index table: {path}")
    values: dict[str, str] = {}
    for index in range(count):
        key_offset, fmt, length, maximum, value_offset = struct.unpack_from(
            "<HHIII", data, 20 + index * 16
        )
        if key_offset >= len(data) - key_table:
            raise ValueError(f"Invalid key offset in SFO: {path}")
        key_end = data.find(b"\0", key_table + key_offset)
        if key_end < 0:
            raise ValueError(f"Unterminated SFO key: {path}")
        if value_offset + length > len(data) - data_table:
            raise ValueError(f"Invalid value offset in SFO: {path}")
        if fmt != 0x0204:
            continue
        key = data[key_table + key_offset:key_end].decode("ascii")
        raw = data[data_table + value_offset:data_table + value_offset + length]
        values[key] = raw.split(b"\0", 1)[0].decode("utf-8", "strict")
    return values


def copy_livearea(source_root: Path, title_id: str, destination_root: Path) -> dict | None:
    source = source_root / title_id
    template = source / "template.xml"
    if not template.is_file():
        return None
    source_root = source.resolve()
    destination = destination_root / title_id
    destination.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(template, destination / "template.xml")
    tree = ET.parse(template)
    image_refs: set[str] = set()
    for element in tree.iter():
        if not element.text:
            continue
        value = element.text.strip()
        if Path(value).suffix.lower() not in (".png", ".jpg", ".jpeg"):
            continue
        source_image = (source_root / value).resolve()
        try:
            relative = source_image.relative_to(source_root)
        except ValueError as error:
            raise ValueError(f"LiveArea asset escapes source directory: {value}") from error
        if not source_image.is_file():
            raise ValueError(f"LiveArea template references missing image: {source_image}")
        image_refs.add(relative.as_posix())
    for relative in sorted(image_refs):
        source_image = source_root / Path(relative)
        target = destination / Path(relative)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source_image, target)
    return {
        "template": f"livearea/{title_id}/template.xml",
        "images": [f"livearea/{title_id}/{path}" for path in sorted(image_refs)],
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source_root", type=Path, help="Directory containing <TITLE_ID>/sce_sys files")
    parser.add_argument("output", type=Path, help="Ignored native-ui/resources/preview/home destination")
    parser.add_argument("--livearea-root", type=Path,
                        help="Optional directory containing <TITLE_ID>/template.xml and its source assets")
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    icons = args.output / "icons"
    icons.mkdir(exist_ok=True)
    games = []
    seen_ids: set[str] = set()
    for source in sorted(path for path in args.source_root.iterdir() if path.is_dir()):
        if not (source / "param.sfo").is_file() or not (source / "icon0.png").is_file():
            continue
        metadata = read_sfo(source / "param.sfo")
        title_id = metadata.get("TITLE_ID", "")
        if not re.fullmatch(r"[A-Z0-9]{9}", title_id) or title_id != source.name:
            raise ValueError(f"SFO TITLE_ID does not match source directory: {source}")
        if title_id in seen_ids:
            raise ValueError(f"Duplicate TITLE_ID: {title_id}")
        seen_ids.add(title_id)
        title = metadata.get("TITLE")
        content_id = metadata.get("CONTENT_ID", "")
        if not title:
            raise ValueError(f"SFO missing TITLE: {title_id}")
        source_icon = source / "icon0.png"
        if not source_icon.is_file() or source_icon.stat().st_size == 0:
            raise ValueError(f"Missing game icon: {source_icon}")
        icon_path = icons / f"{title_id}.png"
        shutil.copyfile(source_icon, icon_path)
        game = {
            "id": title_id,
            "title": title,
            "contentId": content_id,
            "icon": f"icons/{title_id}.png",
        }
        if args.livearea_root:
            livearea = copy_livearea(args.livearea_root, title_id, args.output / "livearea")
            if livearea is not None:
                game["liveArea"] = livearea
        games.append(game)
    catalog = {"version": 1, "games": games}
    (args.output / "catalog.json").write_text(
        json.dumps(catalog, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"Prepared {len(games)} Home preview games in {args.output}")


if __name__ == "__main__":
    main()

"""Catalog native exported UI components without copying game assets.

Records definition kinds, sprite frame labels and initializer fingerprints.
The host generator uses the exported names and definition kinds.
"""

import argparse
import hashlib
import json
import struct
import zipfile
import zlib
from pathlib import Path

from inspect_ui_assets import cstring, inspect_movie


DEFINITIONS = {2: "shape", 6: "bitmap", 7: "button", 10: "font", 11: "text", 14: "sound", 20: "bitmap", 21: "bitmap", 22: "shape", 32: "shape", 33: "text", 34: "button", 35: "bitmap", 36: "bitmap", 37: "edit_text", 39: "sprite", 46: "morph_shape", 48: "font", 60: "video", 75: "font", 83: "shape", 84: "morph_shape", 87: "binary", 90: "bitmap", 91: "font"}


def catalog_movie(data):
    metadata = inspect_movie(data)
    body = zlib.decompress(data[8:]) if data[:3] in (b"CFX", b"CWS") else data[8:]
    start = (5 + 4 * (body[0] >> 3) + 7) // 8 + 4
    definitions = {}
    initializers = {}
    exporter_version = None

    def walk(start, end, timeline):
        nonlocal exporter_version
        position = start
        frame = 1
        while position < end:
            tag_offset = position
            header = struct.unpack_from("<H", body, position)[0]
            position += 2
            code, size = header >> 6, header & 63
            if size == 63:
                size = struct.unpack_from("<I", body, position)[0]
                position += 4
            payload = body[position:position + size]
            if code == 1000:
                exporter_version = struct.unpack_from("<H", payload)[0]
            elif code == 1009:
                # Native loader 0x181C6C990: u32 ID, u16 format/width/height,
                # two length-prefixed strings, optional exporter-version byte.
                raw_id, image_format, width, height = struct.unpack_from("<IHHH", payload)
                cursor = 10
                strings = []
                for _ in range(2):
                    length = payload[cursor]
                    cursor += 1
                    if cursor + length > size:
                        raise ValueError("External image string crosses tag boundary")
                    strings.append(payload[cursor:cursor + length].decode("utf-8"))
                    cursor += length
                if exporter_version is None:
                    raise ValueError("External image lacks exporter version")
                extra = payload[cursor] if exporter_version >= 0x36E else None
                cursor += int(exporter_version >= 0x36E)
                if cursor != size:
                    raise ValueError("External image layout does not consume payload")
                identifier = raw_id & 0x9FFFF
                if identifier in definitions:
                    raise ValueError(f"Duplicate definition ID {identifier}")
                definitions[identifier] = {"kind": "external_image", "tag": code, "body_offset": tag_offset, "payload_bytes": size, "sha256": hashlib.sha256(payload).hexdigest(), "raw_id": raw_id, "format": image_format, "width": width, "height": height, "export_name": strings[0], "file_name": strings[1], "version_byte": extra}
            elif code in DEFINITIONS:
                identifier = struct.unpack_from("<H", payload)[0]
                if identifier in definitions:
                    raise ValueError(f"Duplicate definition ID {identifier}")
                definition = {"kind": DEFINITIONS[code], "tag": code, "body_offset": tag_offset, "payload_bytes": size, "sha256": hashlib.sha256(payload).hexdigest()}
                definitions[identifier] = definition
                if code == 39:
                    definition.update(frames=struct.unpack_from("<H", payload, 2)[0], labels=[], action_tags=0, placement_tags=0)
                    walk(position + 4, position + size, definition)
            elif code == 59:
                identifier = struct.unpack_from("<H", payload)[0]
                initializers.setdefault(identifier, []).append({"body_offset": tag_offset, "action_bytes": size - 2, "sha256": hashlib.sha256(payload[2:]).hexdigest()})
            if timeline is not None:
                if code == 43:
                    timeline["labels"].append({"frame": frame, "name": cstring(payload, 0)[0]})
                elif code == 12:
                    timeline["action_tags"] += 1
                elif code in (4, 26, 70, 94):
                    timeline["placement_tags"] += 1
            if code == 1:
                frame += 1
            position += size
            if code == 0:
                break

    walk(start, len(body), None)
    imports = {symbol["id"]: {"url": dependency["url"], "name": symbol["name"]} for dependency in metadata["imports"] for symbol in dependency["symbols"]}
    exports = []
    for export in metadata["exports"]:
        identifier = export["id"]
        entry = dict(export)
        entry["definition"] = definitions.get(identifier)
        entry["import"] = imports.get(identifier)
        entry["initializers"] = initializers.get(identifier, [])
        # A package name is an explicit namespace convention, not evidence
        # that an empty sprite is useful visual artwork.
        entry["package_export"] = export["name"].startswith("__Packages.")
        exports.append(entry)
    return {"sha256": metadata["sha256"], "exports": exports}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, default=Path("tools/data/native_ui/ui-component-catalog.json"))
    args = parser.parse_args()
    movies = []
    with zipfile.ZipFile(args.archive) as archive:
        for name in sorted(archive.namelist()):
            if name.lower().startswith("libs/ui/") and name.lower().endswith((".gfx", ".swf")):
                movies.append({"file": name, **catalog_movie(archive.read(name))})
    report = {"archive": args.archive.name, "offset_basis": "uncompressed body, excluding 8-byte file header", "movies": movies}
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    exports = [export for movie in movies for export in movie["exports"]]
    unresolved = sum(export["definition"] is None and export["import"] is None for export in exports)
    print(f"Cataloged {len(movies)} movies and {len(exports)} exports; {unresolved} definitions unresolved")


if __name__ == "__main__":
    main()

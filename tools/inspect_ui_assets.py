"""Read UI metadata from a game PAK without extracting or modifying assets.

SWF tag numbers follow ruffle-rs/ruffle swf/src/tag_code.rs and the Adobe
SWF specification. Scaleform extension tags are counted, not guessed.
The report contains metadata for the native UI host generator.
"""

import argparse
import collections
import hashlib
import json
import struct
import xml.etree.ElementTree as ET
import zipfile
import zlib
from pathlib import Path


def cstring(data, offset):
    end = data.index(b"\0", offset)
    return data[offset:end].decode("utf-8", errors="replace"), end + 1


def inspect_movie(data):
    magic = data[:3]
    if magic not in (b"CFX", b"GFX", b"CWS", b"FWS"):
        raise ValueError(f"Unsupported movie signature {magic!r}")
    body = zlib.decompress(data[8:]) if magic in (b"CFX", b"CWS") else data[8:]
    expected = struct.unpack_from("<I", data, 4)[0]
    if len(body) + 8 != expected:
        raise ValueError("Uncompressed size differs from movie header")
    nbits = body[0] >> 3
    bit_position = 5
    bounds = []
    for _ in range(4):
        value = 0
        for _ in range(nbits):
            value = (value << 1) | ((body[bit_position // 8] >> (7 - bit_position % 8)) & 1)
            bit_position += 1
        bounds.append(value - (1 << nbits) if value & (1 << (nbits - 1)) else value)
    offset = (bit_position + 7) // 8
    rate, frames = struct.unpack_from("<HH", body, offset)
    counts = collections.Counter()
    imports = []
    exports = []
    initializers = []
    attributes = []

    def walk(stream, start, end):
        pos = start
        while pos < end:
            if pos + 2 > end:
                raise ValueError("Truncated tag header")
            header = struct.unpack_from("<H", stream, pos)[0]
            pos += 2
            code, size = header >> 6, header & 63
            if size == 63:
                if pos + 4 > end:
                    raise ValueError("Truncated long tag size")
                size = struct.unpack_from("<I", stream, pos)[0]
                pos += 4
            if pos + size > end:
                raise ValueError(f"Tag {code} crosses container boundary")
            payload = stream[pos:pos + size]
            counts[code] += 1
            if code == 39:
                if size < 4:
                    raise ValueError("Truncated sprite")
                walk(payload, 4, size)
            elif code == 69:
                attributes.append(struct.unpack_from("<I", payload)[0])
            elif code == 59:
                if size < 3:
                    raise ValueError("Truncated sprite initializer")
                initializers.append({"id": struct.unpack_from("<H", payload)[0], "action_bytes": size - 2, "sha256": hashlib.sha256(payload[2:]).hexdigest()})
            elif code in (57, 71):
                url, cursor = cstring(payload, 0)
                # KCD2's optimized GFX loader reads count before the two
                # extra bytes (0x181C6BE5E..0x181C6BEBA). Standard SWF
                # ImportAssets2 puts its reserved bytes before the count.
                if code == 71 and magic in (b"FWS", b"CWS"):
                    cursor += 2
                count = struct.unpack_from("<H", payload, cursor)[0]
                cursor += 2
                if code == 71 and magic in (b"GFX", b"CFX"):
                    cursor += 2
                symbols = []
                for _ in range(count):
                    char_id = struct.unpack_from("<H", payload, cursor)[0]
                    symbol, cursor = cstring(payload, cursor + 2)
                    symbols.append({"id": char_id, "name": symbol})
                if cursor != len(payload):
                    raise ValueError("Import symbol count does not consume its tag payload")
                imports.append({"url": url, "symbols": symbols})
            elif code == 56:
                count = struct.unpack_from("<H", payload)[0]
                cursor = 2
                for _ in range(count):
                    char_id = struct.unpack_from("<H", payload, cursor)[0]
                    symbol, cursor = cstring(payload, cursor + 2)
                    exports.append({"id": char_id, "name": symbol})
            pos += size
            if code == 0:
                if pos != end:
                    raise ValueError("Bytes remain after End tag")
                return
        raise ValueError("Missing End tag")

    walk(body, offset + 4, len(body))
    return {
        "signature": magic.decode(), "version": data[3],
        "sha256": hashlib.sha256(data).hexdigest(),
        "uncompressed_bytes": expected,
        "stage_pixels": [(bounds[1] - bounds[0]) / 20, (bounds[3] - bounds[2]) / 20],
        "frame_rate": rate / 256, "root_frames": frames,
        "tag_counts_including_sprites": dict(sorted(counts.items())),
        "avm1_action_tags": counts[12] + counts[59],
        "avm2_abc_tags": counts[72] + counts[82],
        "file_attributes": attributes,
        "as3_flag": any(value & 8 for value in attributes),
        "imports": imports, "exports": exports,
        "initializers": initializers,
    }


def inspect_archive(path):
    result = {"archive": path.name, "elements": [], "movies": [], "fonts": []}
    with zipfile.ZipFile(path) as archive:
        for name in sorted(archive.namelist()):
            lower = name.lower()
            if not lower.startswith("libs/ui/"):
                continue
            if lower.startswith("libs/ui/uielements/") and lower.endswith(".xml"):
                root = ET.fromstring(archive.read(name))
                for element in root:
                    entry = {"xml": name, "group": root.get("name"), "attributes": dict(element.attrib), "sections": {}}
                    for section in element:
                        entry["sections"][section.tag] = {
                            "attributes": dict(section.attrib),
                            "children": [{"tag": child.tag, "attributes": dict(child.attrib), "children": [{"tag": item.tag, "attributes": dict(item.attrib)} for item in child]} for child in section],
                        }
                    result["elements"].append(entry)
            elif lower.endswith((".gfx", ".swf")):
                result["movies"].append({"file": name, **inspect_movie(archive.read(name))})
            elif lower == "libs/ui/fontconfig.xml":
                root = ET.fromstring(archive.read(name))
                result["fonts"] = [{"attributes": dict(config.attrib), "maps": [dict(mapping.attrib) for mapping in config]} for config in root]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = inspect_archive(args.archive)
    args.output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"Inspected {len(report['elements'])} elements and {len(report['movies'])} movies: {args.output}")


if __name__ == "__main__":
    main()

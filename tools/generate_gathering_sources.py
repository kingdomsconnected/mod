#!/usr/bin/env python3
"""Compile authored medicinal herb placements from the installed game's paks.

Native format locations in WHGame.dll:
  18095D364 / 18095CF84: 32-byte octree header and object block length.
  1803FD684: tagged object records, merged nodes (23) and group rows.
  1834DEBD0 / 180CFAB6C: sector headers and 12-byte compressed instances.
  18043C258 / 18043C4D0: pickable test, model CRC, exact float position decode.
  1834DB738 / 1805DDF74: 16-metre sector bounds.

Walks the complete terrain octree to EOF, verifies every sector against its
authored group identities/counts, and preserves native uint16 positions.
Only medicinal herbs are included. Nonmerged mushrooms and quest entities
are not part of this catalog. No runtime game files are needed by the server.
"""

import argparse
from collections import Counter
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import zipfile

from generate_gate_catalog import DEFAULT_GAME, zread

LEVELS = ("trosecko", "kutnohorsko", "klaster")
FLOAT = struct.Struct("<f")


def float32(value):
    return FLOAT.unpack(FLOAT.pack(value))[0]


# Multiplication by 16 is exact; preserve the rounding of native float division.
LOCAL_COORDINATES = tuple(float32(value / 65535.0) * 16.0 for value in range(65536))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def merged_nodes(blob):
    """Read every octree node and object; unknown versions/types fail closed."""
    def u32(offset):
        return struct.unpack_from("<I", blob, offset)[0]

    merged = []
    counts = Counter()

    def objects(offset, end):
        while offset < end:
            kind = u32(offset)
            at = offset + 4
            counts[kind] += 1
            if kind == 1:
                size = 100
            elif kind == 2:
                raise ValueError("Ordinary vegetation now exists in terrain.dat; review herb coverage")
            elif kind == 23:
                size = 68 + 12 * u32(at + 40)
                merged.append(at)
            elif kind == 7:
                size = 116
            elif kind == 12:
                size = 68
            elif kind == 10:
                size = 152 + 12 * u32(at + 92)
            elif kind == 9:
                size = 144 + 4 * blob[at + 43] + 12 * (u32(at + 124) + u32(at + 136))
            elif kind == 11:
                size = (108 + 12 * u32(at + 80) + 2 * u32(at + 84) + 3) & ~3
                size += 16 * u32(at + 88) + 40 * u32(at + 92) + 12 * u32(at + 96)
            else:
                raise ValueError(f"Unknown terrain object {kind} at {offset}")
            offset = (at + size + 3) & ~3
        require(offset == end, "Object block length mismatch")

    def node(offset):
        version, mask = struct.unpack_from("<HH", blob, offset)
        require(version == 8 and mask < 256, "Unexpected octree header")
        end = offset + 32 + u32(offset + 28)
        require(end <= len(blob), "Truncated octree node")
        objects(offset + 32, end)
        for child in range(8):
            if mask & (1 << child):
                end = node(end)
        return end

    require(blob[0] == 29 and u32(4) == len(blob), "Unexpected terrain header")
    dimension = u32(8) * struct.unpack_from("<f", blob, 12)[0]
    bounds = struct.pack("<6f", 0, 0, 0, dimension, dimension, dimension)
    root = blob.find(bounds) - 4
    require(root >= 0, "Terrain octree root missing")
    require(node(root) == len(blob), "Terrain octree did not reach EOF")
    return merged, counts


def extract(game, level, materials):
    directory = game / "Data/Levels" / level
    with zipfile.ZipFile(directory / "level.pak") as archive:
        vegetation = {int(row.get("Id")): row for row in ET.fromstring(zread(archive, "leveldata.xml")).find("Vegetation")}
    with zipfile.ZipFile(directory / "terrain.pak") as archive:
        blob = zread(archive, "terrain/terrain.dat")
        group_kinds = []
        for index in range(struct.unpack_from("<I", blob, 32)[0]):
            at = 36 + 360 * index
            path = blob[at:at + 256].split(b"\0", 1)[0].decode("ascii").lower()
            vegetation_id = struct.unpack_from("<I", blob, at + 320)[0] - 1
            row = vegetation.get(vegetation_id)
            require(row is not None and path == row.get("FileName").lower(), "Vegetation table disagrees with level XML")
            kind = 0
            if row.get("Pickable") == "1" and "/mushrooms/" not in path:
                require(path in materials, f"Unknown pickable herb model: {path}")
                require(row.get("AutoMerged") == "1", f"Nonmerged herb needs a new reader: {path}")
                kind = materials[path]
            group_kinds.append(kind)

        nodes, object_counts = merged_nodes(blob)
        seen = set()
        groups = []
        counts = Counter()
        for at in nodes:
            low = struct.unpack_from("<3f", blob, at + 44)
            high = struct.unpack_from("<3f", blob, at + 56)
            require(all(high[i] - low[i] == 16 and low[i] % 16 == 0 for i in range(3)), "Unexpected merged sector bounds")
            sector = tuple(int(value / 16) for value in low)
            # The filename uses abs(center), while the quantizer uses floor(position).
            parts = ["n" + str(-value - 1) if value < 0 else str(value) for value in sector]
            name = "terrain/merged_meshes_sectors/sector_" + "_".join(parts) + "_0.dat"
            require(name not in seen, "Duplicate merged sector")
            seen.add(name)
            data = zread(archive, name)
            cursor = 0
            for index in range(struct.unpack_from("<I", blob, at + 40)[0]):
                vegetation_index, count, key = struct.unpack_from("<III", blob, at + 68 + 12 * index)
                magic, x, y, z, actual_key, actual_count = struct.unpack_from("<6I", data, cursor)
                require((magic, actual_key, actual_count) == (0xCAFEBAB6, key, count), f"Sector group mismatch: {name}")
                require((x, y, z) == tuple(-v - 1 if v < 0 else v for v in sector), "Sector coordinates disagree")
                cursor += 24
                require(cursor + count * 12 <= len(data), "Truncated sector instances")
                kind = group_kinds[vegetation_index]
                if kind:
                    counts[kind] += count
                    positions = b"".join(data[pos:pos + 6] for pos in range(cursor, cursor + count * 12, 12))
                    groups.append((sector, kind, count, positions))
                cursor += count * 12
            require(cursor == len(data), f"Trailing sector bytes: {name}")
        require(seen == {name for name in archive.namelist() if "/sector_" in name}, "Sector catalog coverage mismatch")

    packed = bytearray()
    unique_count = 0
    previous_sector = None
    seen_positions = set()
    for sector, kind, count, positions in sorted(groups):
        if sector != previous_sector:
            seen_positions.clear()
            previous_sector = sector
        unique_positions = bytearray()
        for offset in range(0, len(positions), 6):
            quantized = struct.unpack_from("<3H", positions, offset)
            key = (kind, *(float32(LOCAL_COORDINATES[value] + sector[axis] * 16) for axis, value in enumerate(quantized)))
            if key not in seen_positions:
                seen_positions.add(key)
                unique_positions.extend(positions[offset:offset + 6])
        count = len(unique_positions) // 6
        if count:
            unique_count += count
            packed.extend(struct.pack("<BhhhI", kind, *sector, count))
            packed.extend(unique_positions)
    print(f"{level}: {sum(counts.values())} raw / {unique_count} unique herbs, {len(groups)} groups, {len(seen)} verified sectors; raw kinds {dict(sorted(counts.items()))}")
    return bytes(packed), unique_count


def write_catalog(datasets, output):
    """Header, level table, then each level's native packed groups.

    Layout (little-endian): b"KCGS", u32 version, u32 level count; per level
    char[16] name, u32 source count, u32 data offset, u32 data size.
    """
    datasets = list(datasets)
    table = 12 + 28 * len(datasets)
    header = bytearray(b"KCGS" + struct.pack("<II", 1, len(datasets)))
    body = bytearray()
    for level, (packed, count) in datasets:
        header += level.encode("ascii").ljust(16, b"\0") + struct.pack("<III", count, table + len(body), len(packed))
        body += packed
    output.write_bytes(bytes(header + body))
    print(f"Wrote {output} ({output.stat().st_size} bytes)")


def generate(game, output):
    with zipfile.ZipFile(game / "Data/Tables.pak") as archive:
        materials = {row.get("material_name").lower(): int(row.get("pickable_area_id")) for row in ET.fromstring(zread(archive, "Libs/Tables/item/pickable_area_material.xml"))[0]}
    write_catalog(((level, extract(game, level, materials)) for level in LEVELS), output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, default=Path(DEFAULT_GAME))
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / 'code/shared/generated/gathering_sources.bin')
    args = parser.parse_args()
    generate(args.game, args.output)

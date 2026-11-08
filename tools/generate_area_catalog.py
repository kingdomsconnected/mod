#!/usr/bin/env python3
"""Mines the levels' own gameplay areas into code/shared/generated/area_catalog.cpp.

Three classes of level entity describe a place rather than a sound or a
render cell, and each carries the `Properties.Label` the AI reads:

  - `TriggerArea`: a Lua carrier with no `<Area>` of its own. Its prism is
    baked into `<level>/level.pak/triggerareas.fubar`, which
    `wh::xgenaimodule::LoadTriggerAreasFubar` (WHGame.dll 0x180D9E1C8) reads:
    `u32 version, u32 hash, u32 count`, then per area `u64 entity GUID,
    f32 height, u32 n, n x Vec3` in world space.
  - `AreaUnion`: no geometry; it joins the `TriggerArea`s it links as
    `area_union_member`, and is inside wherever one of them is
    (`C_AreaUnion::IsPointInside`, 0x1805CCF2C).
  - `SmartAreaShape`: a CryEngine area shape inline in the level XML, points
    relative to the entity and transformed by its `Pos`/`Rotate`/`Scale`.

Door links are the level's own `crime_door[type($enum:crime_doorKind.X)]`
links to `AnimDoor`s. A `SmartAreaShape` with `guidLocationId` is named after
its row in `Libs/Tables/rpg/location.xml`.

For every area the bounds the engine tests against are precomputed here, so
the runtime test is a box check and a crossing test and nothing else. See
`code/shared/features/area/area_geometry.h` for the tests themselves.

Usage:
    python tools/generate_area_catalog.py [--game <install dir>]
"""

import argparse
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile

from generate_gate_catalog import DEFAULT_GAME, entity_guid, escape, zread
from generate_poi_catalog import load_english, load_locations, pretty

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(REPO_ROOT, "code", "shared", "generated", "area_catalog.cpp")

AREA_CLASSES = ("TriggerArea", "AreaUnion", "SmartAreaShape")
DOOR_LINK_RE = re.compile(r"^crime_door\[type\(\$enum:crime_doorKind\.(\w+)\)\]$")
DOOR_LINKS = {"entrance": "Entrance", "basic": "Basic", "storage": "Storage"}

FUBAR_MEMBER = "triggerareas.fubar"
FUBAR_VERSION = 3


def read_fubar(archive):
    """`guid -> (height, [world points])` from the level's baked TriggerArea prisms."""
    member = next(name for name in archive.namelist() if name.lower() == FUBAR_MEMBER)
    blob = zread(archive, member)
    version, _hash, count = struct.unpack_from("<III", blob, 0)
    if version != FUBAR_VERSION:
        raise ValueError("{} is version {}, this reads {}".format(member, version, FUBAR_VERSION))

    offset = 12
    prisms = {}
    for _ in range(count):
        guid, height, points = struct.unpack_from("<QfI", blob, offset)
        offset += 16
        prisms[guid] = (height, [struct.unpack_from("<3f", blob, offset + 12 * index) for index in range(points)])
        offset += 12 * points
    if offset != len(blob):
        raise ValueError("{} has {} trailing bytes after {} records".format(member, len(blob) - offset, count))
    return prisms


def parse_vector(text, count, default):
    if text is None:
        return default
    parts = text.split(",")
    if len(parts) != count:
        raise ValueError("expected {} components in {!r}".format(count, text))
    return tuple(float(part) for part in parts)


def rotate(quaternion, vector):
    """Rotates `vector` by a w,x,y,z quaternion, the order `CXmlNode::getAttr(Quat)` reads."""
    w, x, y, z = quaternion
    vx, vy, vz = vector
    tx = 2.0 * (y * vz - z * vy)
    ty = 2.0 * (z * vx - x * vz)
    tz = 2.0 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty), vy + w * ty + (z * tx - x * tz), vz + w * tz + (x * ty - y * tx))


def labels_of(entity):
    properties = entity.find("Properties")
    raw = properties.get("Label", "") if properties is not None else ""
    return [label.strip() for label in raw.split(";") if label.strip()]


def trigger_area_bounds(height, points):
    """`C_TriggerArea::SetShape` (0x18062EBFC): every point, plus the first raised by the height."""
    corners = list(points) + [(points[0][0], points[0][1], points[0][2] + height)]
    return tuple(min(p[axis] for p in corners) for axis in range(3)) + tuple(max(p[axis] for p in corners) for axis in range(3))


def shape_bounds(height, points):
    """`CArea` shape: the floor is the lowest point and the ceiling `height` above it."""
    floor = min(p[2] for p in points)
    return (min(p[0] for p in points), min(p[1] for p in points), floor, max(p[0] for p in points), max(p[1] for p in points), floor + height)


def areas_in_level(archive, locations, english):
    """Every gameplay area a level places, in GUID order, with its members and door links resolved."""
    prisms = read_fubar(archive)
    by_id = {}
    for member in archive.namelist():
        if not member.lower().endswith(".xml"):
            continue
        try:
            blob = zread(archive, member)
        except Exception:
            continue
        if not any(name.encode() in blob for name in AREA_CLASSES + ("AnimDoor",)):
            continue
        for entity in ET.fromstring(blob).iter("Entity"):
            entity_id = entity.get("EntityId")
            if entity_id is None or entity.get("EntityClass") is None:
                continue
            if entity_id in by_id:
                raise ValueError("{}: EntityId {} is placed twice".format(member, entity_id))
            by_id[entity_id] = entity

    areas = {}
    for entity in by_id.values():
        klass = entity.get("EntityClass")
        if klass not in AREA_CLASSES:
            continue
        guid = entity_guid(entity.get("EntityGuid", ""))
        if guid == 0:
            raise ValueError("{} {!r} has no usable EntityGuid".format(klass, entity.get("Name")))

        area = {"class": klass, "name": entity.get("Name", ""), "labels": labels_of(entity), "points": [], "height": 0.0, "members": [], "doors": [], "location": ""}
        for link in entity.iter("Link"):
            target = by_id.get(link.get("TargetId"))
            if target is None:
                continue
            name = link.get("Name", "")
            door = DOOR_LINK_RE.match(name)
            if door is not None and target.get("EntityClass") == "AnimDoor":
                if door.group(1) not in DOOR_LINKS:
                    raise ValueError("{!r} links a door as {!r}".format(area["name"], door.group(1)))
                area["doors"].append((entity_guid(target.get("EntityGuid", "")), DOOR_LINKS[door.group(1)]))
            elif name == "area_union_member" and target.get("EntityClass") == "TriggerArea":
                area["members"].append(entity_guid(target.get("EntityGuid", "")))

        if klass == "TriggerArea":
            if guid not in prisms:
                raise ValueError("TriggerArea {!r} ({:#x}) has no prism in {}".format(area["name"], guid, FUBAR_MEMBER))
            area["height"], area["points"] = prisms[guid]
            if len(area["points"]) < 3:
                continue
            area["bounds"] = trigger_area_bounds(area["height"], area["points"])
        elif klass == "SmartAreaShape":
            node = entity.find("Area")
            if node is None:
                continue
            origin = parse_vector(entity.get("Pos"), 3, (0.0, 0.0, 0.0))
            quaternion = parse_vector(entity.get("Rotate"), 4, (1.0, 0.0, 0.0, 0.0))
            scale = parse_vector(entity.get("Scale"), 3, (1.0, 1.0, 1.0))
            for point in node.iter("Point"):
                local = parse_vector(point.get("Pos"), 3, (0.0, 0.0, 0.0))
                turned = rotate(quaternion, (local[0] * scale[0], local[1] * scale[1], local[2] * scale[2]))
                area["points"].append((origin[0] + turned[0], origin[1] + turned[1], origin[2] + turned[2]))
            area["height"] = float(node.get("Height", "0"))
            if len(area["points"]) < 3:
                continue
            # A CArea shape with no height is open top and bottom. None ships, and the runtime relies on every
            # level area having a ceiling, so a patch that adds one must be handled here rather than slip through.
            if area["height"] <= 0.0:
                raise ValueError("SmartAreaShape {!r} has no height; the catalog assumes every level area has a ceiling".format(area["name"]))
            area["bounds"] = shape_bounds(area["height"], area["points"])
            properties = entity.find("Properties")
            location = locations.get(properties.get("guidLocationId", "")) if properties is not None else None
            if location is not None:
                internal = location.get("location_name", "")
                area["location"] = english.get(internal, pretty(internal.removeprefix("location_")))
        areas[guid] = area

    # A union is its members, so its bounds are theirs; one whose members all
    # lack geometry tests nothing and is left out.
    for guid, area in list(areas.items()):
        if area["class"] != "AreaUnion":
            continue
        area["members"] = [member for member in area["members"] if member in areas and areas[member]["class"] == "TriggerArea"]
        if not area["members"]:
            del areas[guid]
            continue
        boxes = [areas[member]["bounds"] for member in area["members"]]
        area["bounds"] = tuple(min(box[axis] for box in boxes) for axis in range(3)) + tuple(max(box[axis] for box in boxes) for axis in range(3, 6))
    return areas


def number(value):
    text = "{:.3f}".format(value).rstrip("0")
    text = text + "0" if text.endswith(".") else text
    return ("0.0" if text == "-0.0" else text) + "F"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default=DEFAULT_GAME)
    arguments = parser.parse_args()

    if not os.path.isdir(arguments.game):
        print("Game directory not found: {}".format(arguments.game), file=sys.stderr)
        return 1

    locations = load_locations(arguments.game)
    english = load_english(arguments.game)

    levels_root = os.path.join(arguments.game, "Data", "Levels")
    levels = {}
    for level in sorted(os.listdir(levels_root)):
        path = os.path.join(levels_root, level, "level.pak")
        if not os.path.isfile(path):
            continue
        with zipfile.ZipFile(path) as archive:
            if not any(name.lower() == FUBAR_MEMBER for name in archive.namelist()):
                continue
            levels[level] = areas_in_level(archive, locations, english)
        areas = levels[level]
        counts = {klass: sum(1 for area in areas.values() if area["class"] == klass) for klass in AREA_CLASSES}
        print("  {}: {} TriggerArea, {} AreaUnion, {} SmartAreaShape, {} door link(s)".format(level, counts["TriggerArea"], counts["AreaUnion"], counts["SmartAreaShape"], sum(len(area["doors"]) for area in areas.values())))

    total = sum(len(areas) for areas in levels.values())
    if total == 0:
        print("No areas found -- refusing to write an empty catalog.", file=sys.stderr)
        return 1

    rows, points, members, doors = [], [], [], []
    point_total = 0
    for level, areas in levels.items():
        for guid, area in sorted(areas.items()):
            first_point, first_member, first_door = point_total, len(members), len(doors)
            point_total += len(area["points"])
            points.append(" ".join("{{{}, {}, {}}},".format(number(p[0]), number(p[1]), number(p[2])) for p in area["points"]))
            members.extend(area["members"])
            doors.extend(area["doors"])
            bounds = area["bounds"]
            rows.append('        {{"{}", {:#018x}ULL, LevelAreaClass::{}, "{}", "{}", "{}", {}, {}U, {}U, {}U, {}U, {}U, {}U, {{{}, {}, {}}}, {{{}, {}, {}}}}},'.format(
                escape(level), guid, area["class"], escape(area["name"]), escape(";".join(area["labels"])), escape(area["location"]), number(area["height"]),
                first_point, len(area["points"]), first_member, len(area["members"]), first_door, len(area["doors"]),
                number(bounds[0]), number(bounds[1]), number(bounds[2]), number(bounds[3]), number(bounds[4]), number(bounds[5])))

    point_count = sum(len(area["points"]) for areas in levels.values() for area in areas.values())
    lines = []
    lines.append('#include "shared/game/area_catalog.h"')
    lines.append("")
    lines.append("// Generated by tools/generate_area_catalog.py. Do not edit by hand.")
    lines.append("//")
    lines.append("// Every TriggerArea (geometry from triggerareas.fubar), AreaUnion and")
    lines.append("// SmartAreaShape the shipped levels place, with the bounds the engine")
    lines.append("// tests against precomputed.")
    lines.append("")
    lines.append("#include <iterator>")
    lines.append("")
    lines.append("namespace KCDC::Shared::Game {")
    lines.append("    namespace {")
    lines.append("        // Plain constant arrays rather than std::vector initializer lists: at")
    lines.append("        // this size those compile into per-element dynamic initialisation.")
    lines.append("        constexpr LevelArea kAreas[] = {")
    lines.extend("    " + row for row in rows)
    lines.append("        };")
    lines.append("")
    lines.append("        constexpr LevelAreaPoint kPoints[] = {")
    lines.extend("            " + line for line in points if line)
    lines.append("        };")
    lines.append("")
    lines.append("        constexpr std::uint64_t kMembers[] = {")
    lines.extend("            {:#018x}ULL,".format(member) for member in members)
    lines.append("        };")
    lines.append("")
    lines.append("        constexpr LevelAreaDoor kDoors[] = {")
    lines.extend("            {{{:#018x}ULL, AreaDoorLink::{}}},".format(guid, link) for guid, link in doors)
    lines.append("        };")
    lines.append("    } // namespace")
    lines.append("")
    lines.append("    // {} area(s), {} point(s).".format(total, point_count))
    lines.append("    const std::vector<LevelArea> g_LevelAreas(std::begin(kAreas), std::end(kAreas));")
    lines.append("    const std::vector<LevelAreaPoint> g_LevelAreaPoints(std::begin(kPoints), std::end(kPoints));")
    lines.append("    const std::vector<std::uint64_t> g_LevelAreaMembers(std::begin(kMembers), std::end(kMembers));")
    lines.append("    const std::vector<LevelAreaDoor> g_LevelAreaDoors(std::begin(kDoors), std::end(kDoors));")
    lines.append("} // namespace KCDC::Shared::Game")
    lines.append("")

    with open(OUTPUT, "w", encoding="utf8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print("Wrote {} area(s), {} point(s) to {}".format(total, point_count, OUTPUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())

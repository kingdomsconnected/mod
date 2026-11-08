#!/usr/bin/env python3
"""Generate the KCDC smart object blueprint table from an installed Kingdom Come: Deliverance II.

A bench, a bed, a dice table or an alchemy table is not one entity. The editor
builds it from a prefab under `Prefabs/` in `Data/IPL_GameData.pak`: the
furniture as brushes, and a handful of entities the game's interaction runs
on -- the `StanceSmartObject` a body sits on and the `ActionTrigger` that
offers "Sit", the `Bed` and its `BedTrigger`, the `AlchemyTable` with its
`AlchemyItem`s, the dice table's `SmartObjectHolder` and its tagpoints --
linked to each other by name. The level then places a copy of that prefab
object by object.

The mod spawns the same parts the same way, so the shape of every prefab
worth placing is mined here and compiled in as
`code/shared/generated/smart_object_blueprints.cpp`.

Kept: every entity a player's interaction reaches, with its prefab
`Properties` -- nested tables flattened as `Table.key`, the form the client
assigns them in -- its `Geometry` when the editor placed it as a
`GeomEntity`, and its links to other kept parts, and every brush.
Dropped: the AI fittings a player never touches (`PrefabPort`,
`SchedulerHub`, `GhostDummy`), and the `Shop` a smithy carries, which is a
vendor rather than part of the forge. A part's transform is composed with its
prefab parents, so every part is posed in the prefab's own frame.

A prefab can place another (`Type="Prefab"`, by `PrefabTemplateName`). The
listed ones are expanded into the prefab that places them, and a link to one
of their `PrefabPort`s is followed through the outer prefab's `NestedLinks`
to the entity it forwards to: the smithy's `Smithery` stands in
`smithy_workshop_base`, and reaches the bellows bag and the coal of the forge
`smithy_workshop` builds around it that way.

An `ItemSlot` the station entity links to is marked as a fixture: the
alchemy table's recipe book, the smithy's tongs and hammer. The client lets
those spawn their item, as it does the level's own fixtures.

Every model a part loads when it spawns -- its `object_Model` or `Geometry`
-- and every brush's is looked up in the game's paks, and one the game does
not ship fails the run.

Usage:
    python tools/generate_smart_object_catalog.py [--game <install dir>]
"""

from __future__ import annotations

import argparse
import math
import os
import posixpath
import sys
import xml.etree.ElementTree as ET
import zipfile

from generate_gate_catalog import DEFAULT_GAME

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(REPO_ROOT, "code", "shared", "generated", "smart_object_blueprints.cpp")
GAMEDATA_PAK = os.path.join("Data", "IPL_GameData.pak")

# The prefabs a player can use on their own, and what each is for. The quest
# copies and the fireplaces (whose cook pots are the level's own, keyed by the
# level's GUIDs) are left out, and so is `profession/apothecary/alchemyTable`:
# every alchemy table the levels place is an `alchemyTable_wip`, and that draft
# names a bellows model the game does not ship and stands its props off its
# table.
PREFABS = {
    "Bed/bed_bench": "Bed",
    "Bed/bed_high": "Bed",
    "Bed/bed_high_double": "Bed",
    "Bed/bed_low": "Bed",
    "Bed/bed_low_ver1": "Bed",
    "Bed/bed_low_ver2": "Bed",
    "Bench/bench_1place_low": "Seat",
    "Bench/bench_1place_low_table": "Seat",
    "Bench/bench_2places_high": "Seat",
    "Bench/bench_2places_low": "Seat",
    "Bench/bench_2places_low_table": "Seat",
    "Bench/bench_3places_low": "Seat",
    "Chair/chair_high": "Seat",
    "Chair/chair_low": "Seat",
    "Tent/tent_1": "Bed",
    "Table/table_dice": "Dice",
    "profession/apothecary/alchemyTable_wip": "Alchemy",
    "profession/blacksmith/smithy_workshop": "Smithing",
    "profession/blacksmith/smithy_workshop_small": "Smithing",
    "profession/butcher/butcher_dryer": "Drying",
    "profession/butcher/butcher_smokeHouse": "Smoking",
}

DROPPED_CLASSES = ("PrefabPort", "PrefabRoot", "SchedulerHub", "GhostDummy", "Shop", "Comment")

# Nested prefabs expanded into the one that places them, by template. The
# chest and the idle spot the smithy's base nests in turn are its shop's stash
# and a blacksmith's, and stay out.
EXPANDED_PREFABS = ("profession/blacksmith/smithy_workshop_base",)

# A prefab nesting deeper than this is not one the editor builds.
MAX_NESTING = 8

# What makes a part the station: its class, or for the dice table the links
# the client's dice module and the dice catalog look for.
STATION_CLASSES = {"Alchemy": "AlchemyTable", "Smithing": "Smithery", "Drying": "FoodProcessingTrigger", "Smoking": "FoodProcessingTrigger"}
DICE_LINKS = ("seat1", "seat2", "dice_cup", "seat1_SO", "seat2_SO")

# A part spawned with more links than this is not something a prefab draws.
MAX_PARTS = 255



def parse_vec(text, fallback):
    if not text:
        return fallback
    parts = [float(p) for p in text.split(",")]
    return (parts[0], parts[1], parts[2])


def parse_quat(text):
    """Prefab rotations are written w,x,y,z; the engine wants x,y,z,w."""
    if not text:
        return (0.0, 0.0, 0.0, 1.0)
    w, x, y, z = (float(p) for p in text.split(","))
    length = math.sqrt(w * w + x * x + y * y + z * z) or 1.0
    return (x / length, y / length, z / length, w / length)


def quat_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def quat_rotate(q, v):
    x, y, z, w = q
    vx, vy, vz = v
    # v + 2w(q x v) + 2 q x (q x v)
    cx = y * vz - z * vy
    cy = z * vx - x * vz
    cz = x * vy - y * vx
    ccx = y * cz - z * cy
    ccy = z * cx - x * cz
    ccz = x * cy - y * cx
    return (vx + 2.0 * (w * cx + ccx), vy + 2.0 * (w * cy + ccy), vz + 2.0 * (w * cz + ccz))


def local_pose(obj):
    return parse_vec(obj.get("Pos"), (0.0, 0.0, 0.0)), parse_quat(obj.get("Rotate")), parse_vec(obj.get("Scale"), (1.0, 1.0, 1.0))


IDENTITY_POSE = ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0), (1.0, 1.0, 1.0))


def compose(frame, pose):
    """`pose`, given in `frame`, in the frame `frame` is given in."""
    (frame_position, frame_rotation, frame_scale), (position, rotation, scale) = frame, pose
    scaled = (position[0] * frame_scale[0], position[1] * frame_scale[1], position[2] * frame_scale[2])
    moved = quat_rotate(frame_rotation, scaled)
    return ((frame_position[0] + moved[0], frame_position[1] + moved[1], frame_position[2] + moved[2]), quat_mul(frame_rotation, rotation), (scale[0] * frame_scale[0], scale[1] * frame_scale[1], scale[2] * frame_scale[2]))


def prefab_pose(obj, by_id, depth=0):
    """The object's pose in the prefab's frame, through every parent."""
    parent_id = obj.get("Parent")
    if not parent_id:
        return local_pose(obj)
    if depth > 32 or parent_id not in by_id:
        raise SystemExit("{}: parent {} is missing or cyclic".format(obj.get("Name"), parent_id))
    return compose(prefab_pose(by_id[parent_id], by_id, depth + 1), local_pose(obj))


def shipped_model(path, shipped, where):
    """`path`, once it is known to be one the game ships."""
    if path and path.lower() not in shipped:
        raise SystemExit("{}: '{}' is not in the game's paks".format(where, path))
    return path


def flatten_properties(node, prefix=""):
    """`Properties` attributes, and every nested table's as `Table.key`."""
    if node is None:
        return []
    pairs = [(prefix + key, value) for key, value in node.attrib.items()]
    for child in node:
        pairs.extend(flatten_properties(child, prefix + child.tag + "."))
    return pairs


def nested_path(path, relative):
    """The path of prefab object ids from the outermost prefab, for one written relative to `path`."""
    if not relative:
        return path
    return path + "/" + relative if path else relative


def collect(read_prefab, name, path, frame, depth, items, forwards):
    """Every object of a prefab and of the listed prefabs it places, posed in the outermost frame.

    `items` gets each object keyed by its path and id; `forwards` maps a port's
    key to the keys its outer prefab's `NestedLinks` forward it to.
    """
    if depth > MAX_NESTING:
        raise SystemExit("{}: prefabs nest deeper than {}".format(name, MAX_NESTING))
    root = ET.fromstring(read_prefab(name))
    objects = root.find("Objects")
    if objects is None:
        raise SystemExit("{}: no Objects".format(name))
    by_id = {o.get("Id"): o for o in objects}
    for obj in objects:
        pose = prefab_pose(obj, by_id) if frame is IDENTITY_POSE else compose(frame, prefab_pose(obj, by_id))
        if obj.get("Type") == "Prefab":
            if obj.get("PrefabTemplateName") in EXPANDED_PREFABS:
                collect(read_prefab, obj.get("PrefabTemplateName"), nested_path(path, obj.get("Id")), pose, depth + 1, items, forwards)
            continue
        items.append({"key": (path, obj.get("Id")), "obj": obj, "pose": pose})
    nested = root.find("NestedLinks")
    for link in nested if nested is not None else []:
        source = (nested_path(path, link.get("SourcePath")), link.get("SourceId"))
        forwards.setdefault(source, []).append(((nested_path(path, link.get("TargetPath")), link.get("TargetId")), link.get("Name") or ""))
    return root


def link_targets(key, name, index_of, ports, forwards, seen=frozenset()):
    """The kept parts a link to `key` reaches: the part itself, or every entity a port forwards it to."""
    if key in index_of:
        return [(index_of[key], name)]
    if key not in ports or key in seen:
        return []
    properties = ports[key].find("Properties")
    forwarded_name = properties.get("esForwardedLinkName") if properties is not None else None
    targets = []
    for target, nested_name in forwards.get(key, []):
        targets.extend(link_targets(target, nested_name if forwarded_name == "Target" else name, index_of, ports, forwards, seen | {key}))
    return targets


def blueprint_from(name, kind, read_prefab, shipped):
    items = []
    forwards = {}
    root = collect(read_prefab, name, "", IDENTITY_POSE, 0, items, forwards)

    entities = [item for item in items if item["obj"].get("Type") != "Brush" and item["obj"].get("EntityClass") and item["obj"].get("EntityClass") not in DROPPED_CLASSES]
    brushes = [item for item in items if item["obj"].get("Type") == "Brush" and item["obj"].get("Prefab")]
    ports = {item["key"]: item["obj"] for item in items if item["obj"].get("EntityClass") == "PrefabPort"}
    if len(entities) > MAX_PARTS:
        raise SystemExit("{}: {} entities, more than a part index holds".format(name, len(entities)))

    index_of = {item["key"]: index for index, item in enumerate(entities)}
    parts = []
    for item in entities:
        obj = item["obj"]
        position, rotation, scale = item["pose"]
        links = []
        node = obj.find("EntityLinks")
        for link in node if node is not None else []:
            links.extend(link_targets((item["key"][0], link.get("TargetId")), link.get("Name") or "", index_of, ports, forwards))
        where = "{} {}".format(name, obj.get("Name"))
        properties = [(key, shipped_model(value, shipped, where) if key == "object_Model" else value) for key, value in flatten_properties(obj.find("Properties"))]
        parts.append(
            {
                "name": obj.get("Name") or "",
                "entity_class": obj.get("EntityClass"),
                "geometry": shipped_model((obj.get("Geometry") or "").replace("\\", "/").lower(), shipped, where),
                "position": position,
                "rotation": rotation,
                "scale": scale,
                "properties": properties,
                "links": links,
                "fixture": False,
            }
        )

    station = -1
    if kind == "Dice":
        for index, part in enumerate(parts):
            if all(any(link_name == wanted for _, link_name in part["links"]) for wanted in DICE_LINKS):
                station = index
        if station < 0:
            raise SystemExit("{}: no part links {}".format(name, ", ".join(DICE_LINKS)))
    elif kind in STATION_CLASSES:
        candidates = [index for index, part in enumerate(parts) if part["entity_class"] == STATION_CLASSES[kind]]
        if len(candidates) != 1:
            raise SystemExit("{}: expected one {}, found {}".format(name, STATION_CLASSES[kind], len(candidates)))
        station = candidates[0]

    if station >= 0:
        for target, _ in parts[station]["links"]:
            if parts[target]["entity_class"] == "ItemSlot":
                parts[target]["fixture"] = True

    meshes = []
    for item in brushes:
        position, rotation, scale = item["pose"]
        mesh = shipped_model(item["obj"].get("Prefab").replace("\\", "/").lower(), shipped, "{} {}".format(name, item["obj"].get("Name")))
        meshes.append({"mesh": mesh, "position": position, "rotation": rotation, "scale": scale})

    return {"name": name, "guid": root.get("Id") or "", "kind": kind, "parts": parts, "brushes": meshes, "station": station}


def fmt(value):
    text = "{:.9g}".format(value)
    return text + "F" if ("." in text or "e" in text or "E" in text) else text + ".0F"


def cstr(text):
    return text.replace("\\", "\\\\").replace('"', '\\"')


def ident(name):
    return "".join(piece[:1].upper() + piece[1:] for piece in name.replace("/", "_").replace("-", "_").split("_") if piece)


def emit(blueprints):
    out = []
    out.append("// Generated by tools/generate_smart_object_catalog.py from Data/IPL_GameData.pak.")
    out.append("// Do not edit by hand.")
    out.append("")
    out.append('#include "shared/game/smart_object_blueprints.h"')
    out.append("")
    out.append("#include <cctype>")
    out.append("")
    out.append("namespace KCDC::Shared::Game {")
    for blueprint in blueprints:
        stem = ident(blueprint["name"])
        out.append("    // {}: {} part(s), {} brush(es).".format(blueprint["name"], len(blueprint["parts"]), len(blueprint["brushes"])))
        for index, part in enumerate(blueprint["parts"]):
            if part["properties"]:
                pairs = ", ".join('{{"{}", "{}"}}'.format(cstr(key), cstr(value)) for key, value in part["properties"])
                out.append("    static const PrefabProperty k{}Part{}Properties[] = {{{}}};".format(stem, index, pairs))
            if part["links"]:
                pairs = ", ".join('{{{}, "{}"}}'.format(target, cstr(link_name)) for target, link_name in part["links"])
                out.append("    static const SmartObjectLink k{}Part{}Links[] = {{{}}};".format(stem, index, pairs))
        out.append("    static const SmartObjectPart k{}Parts[] = {{".format(stem))
        for index, part in enumerate(blueprint["parts"]):
            px, py, pz = part["position"]
            rx, ry, rz, rw = part["rotation"]
            sx, sy, sz = part["scale"]
            properties = "k{}Part{}Properties, {}".format(stem, index, len(part["properties"])) if part["properties"] else "nullptr, 0"
            links = "k{}Part{}Links, {}".format(stem, index, len(part["links"])) if part["links"] else "nullptr, 0"
            out.append(
                '        {{"{}", "{}", "{}", {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}}},'.format(
                    cstr(part["name"]), part["entity_class"], cstr(part["geometry"]), fmt(px), fmt(py), fmt(pz), fmt(rx), fmt(ry), fmt(rz), fmt(rw), fmt(sx), fmt(sy), fmt(sz), properties, links, "true" if part["fixture"] else "false"
                )
            )
        out.append("    };")
        if blueprint["brushes"]:
            out.append("    static const SmartObjectBrush k{}Brushes[] = {{".format(stem))
            for brush in blueprint["brushes"]:
                px, py, pz = brush["position"]
                rx, ry, rz, rw = brush["rotation"]
                sx, sy, sz = brush["scale"]
                out.append('        {{"{}", {}, {}, {}, {}, {}, {}, {}, {}, {}, {}}},'.format(cstr(brush["mesh"]), fmt(px), fmt(py), fmt(pz), fmt(rx), fmt(ry), fmt(rz), fmt(rw), fmt(sx), fmt(sy), fmt(sz)))
            out.append("    };")
        out.append("")

    out.append("    const std::vector<SmartObjectBlueprint> g_SmartObjectBlueprints = {")
    for blueprint in blueprints:
        stem = ident(blueprint["name"])
        brushes = "k{0}Brushes, sizeof(k{0}Brushes) / sizeof(k{0}Brushes[0])".format(stem) if blueprint["brushes"] else "nullptr, 0"
        out.append(
            '        {{"{}", "{}", SmartObjectKind::{}, k{}Parts, sizeof(k{}Parts) / sizeof(k{}Parts[0]), {}, {}}},'.format(
                blueprint["name"], blueprint["guid"], blueprint["kind"], stem, stem, stem, brushes, blueprint["station"]
            )
        )
    out.append("    };")
    out.append("")
    out.append("    const SmartObjectBlueprint *FindSmartObjectBlueprint(std::string_view name) {")
    out.append("        for (const SmartObjectBlueprint &blueprint : g_SmartObjectBlueprints) {")
    out.append("            if (blueprint.name.size() != name.size()) {")
    out.append("                continue;")
    out.append("            }")
    out.append("            bool same = true;")
    out.append("            for (std::size_t index = 0; index < name.size() && same; ++index) {")
    out.append("                same = std::tolower(static_cast<unsigned char>(blueprint.name[index])) == std::tolower(static_cast<unsigned char>(name[index]));")
    out.append("            }")
    out.append("            if (same) {")
    out.append("                return &blueprint;")
    out.append("            }")
    out.append("        }")
    out.append("        return nullptr;")
    out.append("    }")
    out.append("")
    out.append("    const char *SmartObjectKindName(SmartObjectKind kind) {")
    out.append("        switch (kind) {")
    for kind, text in (("Seat", "seat"), ("Bed", "bed"), ("Dice", "dice"), ("Alchemy", "alchemy"), ("Smithing", "smithing"), ("Drying", "drying"), ("Smoking", "smoking"), ("Sharpening", "sharpening")):
        out.append('        case SmartObjectKind::{}: return "{}";'.format(kind, text))
    out.append("        }")
    out.append('        return "seat";')
    out.append("    }")
    out.append("} // namespace KCDC::Shared::Game")
    out.append("")
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default=DEFAULT_GAME)
    parser.add_argument("-o", "--output", default=OUTPUT)
    arguments = parser.parse_args()

    pak = os.path.join(arguments.game, GAMEDATA_PAK)
    if not os.path.isfile(pak):
        print("{} not found".format(pak), file=sys.stderr)
        return 1

    # Every file the game ships, for the models the prefabs name.
    shipped = set()
    data = os.path.join(arguments.game, "Data")
    for root, _, files in os.walk(data):
        for file in files:
            if file.lower().endswith(".pak"):
                try:
                    with zipfile.ZipFile(os.path.join(root, file)) as archive:
                        shipped.update(member.lower() for member in archive.namelist())
                except zipfile.BadZipFile:
                    continue

    blueprints = []
    with zipfile.ZipFile(pak) as archive:
        members = {member.lower(): member for member in archive.namelist()}
        def read_prefab(name):
            member = members.get(("prefabs/" + name + ".xml").lower())
            if member is None:
                raise SystemExit("prefab {} is missing from {}".format(name, pak))
            return archive.read(member)

        for name, kind in sorted(PREFABS.items(), key=lambda item: item[0].lower()):
            blueprint = blueprint_from(name, kind, read_prefab, shipped)
            print("  {}: {} part(s), {} brush(es), station part {}".format(name, len(blueprint["parts"]), len(blueprint["brushes"]), blueprint["station"]))
            blueprints.append(blueprint)

    with open(arguments.output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(emit(blueprints))
    print("{} smart object blueprints -> {}".format(len(blueprints), arguments.output))
    return 0


if __name__ == "__main__":
    sys.exit(main())

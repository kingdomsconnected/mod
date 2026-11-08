#!/usr/bin/env python3
"""Mines the game's battle ladders and its battlement stone pile into
code/shared/generated/siege_defence_catalog.cpp.

KCD2 storms walls with its own `Ladder` entities: the Suchdol siege ladder
(`Prefabs/Ladder/battleLadder_10000.xml`) and a village ladder fitted to a
low wall (`battleLadder_350.xml`). Each prefab carries the climbable
`Ladder` -- its mesh, its climb height, its lean against the wall -- and the
smart object the defender stands on to push it off with a halberd. The
battlements are defended from `Prefabs/hurlingStone/*.xml`: a
`StoneThrowingPile` linked to a path node, a pick point and the nine stones of
`hurlingStonePile_a.xml`.

This reads those prefabs and writes what the mod spawns from them. The two
ladders the game has no battle prefab for -- the 5.75 m and 2.25 m village
ladders, whose meshes and broken meshes ship -- are derived from
`battleLadder_350` as declared below.

Usage:
    python tools/generate_siege_defence_catalog.py [--game <install dir>]
"""

import argparse
import math
import os
import re
import sys

import generate_gate_catalog as gates
from generate_area_catalog import rotate

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(REPO_ROOT, "code", "shared", "generated", "siege_defence_catalog.cpp")

LADDER_DIR = "objects/manmade/common_fixtures/ladders/"

# The battle prefab's `Offsets.getOnDistanceAwayTop` (9.5 on the siege
# ladder) is not taken: `Ladder.lua` puts the top get-off that far below the
# top, which suits the battle's scripted AI climb and throws a player off two
# rungs up. Every ladder keeps the class default.
#
# The battle prefabs, by the name the scripting API gives them, with the
# broken mesh the game ships for each (the prefabs swap the ladder for an item
# lying in a drop slot instead, which has no broken form).
BATTLE_LADDERS = [
    ("siege", "prefabs/ladder/battleladder_10000.xml", LADDER_DIR + "ladder_siege_10m_broken.cgf"),
    ("short", "prefabs/ladder/battleladder_350.xml", LADDER_DIR + "ladder_rustic_350_broken.cgf"),
]

# The village ladders with a broken mesh and a push tag of their own but no
# battle prefab. Built like `short`: the village ladder's climb (the number in
# its name, as `ladder_225.xml` .. `ladder_400.xml` give it) less the quarter
# metre `battleLadder_350` takes off `ladder_350`'s 3.5 m so its top rests on
# the palisade, and the pusher's place scaled with the height.
DERIVED_LADDERS = [
    ("tall", 5.75, "575"),
    ("low", 2.25, "225"),
]
PALISADE_REST = 0.25
DERIVED_FROM = "short"

STONE_PILE_PREFAB = "prefabs/hurlingstone/hurlingstonepile_onesided_walls_left_a.xml"
STONE_HEAP_PREFAB = "prefabs/hurlingstone/hurlingstonepile_a.xml"


def floats(text):
    return [float(v) for v in text.split(",")]


def attributes(tag):
    return dict(re.findall(r'(\w+)="([^"]*)"', tag))


def objects(xml):
    """Each `<Object ...>` with its own attributes and the properties nested in it."""
    found = []
    for match in re.finditer(r"<Object\b([^>]*?)(/>|>(.*?)</Object>)", xml, re.S):
        attrs = attributes(match.group(1))
        body = match.group(3) or ""
        properties = {}
        for tag in re.findall(r"<(?:Properties|Offsets)\b([^>]*)/?>", body):
            properties.update(attributes(tag))
        links = re.findall(r'<Link\b[^>]*Name="([^"]*)"', body)
        found.append((attrs, properties, links))
    return found


def quat_mul(a, b):
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (aw * bw - ax * bx - ay * by - az * bz, aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw)


def yaw_quat(radians):
    return (math.cos(radians / 2.0), 0.0, 0.0, math.sin(radians / 2.0))


def quat_yaw(q):
    w, x, y, z = q
    return math.atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z))


def battle_ladder(paks, name, path, broken):
    xml = paks.read(path)
    if xml is None:
        raise RuntimeError("missing " + path)
    xml = xml.decode("latin1")
    ladder = push = None
    tags = None
    for attrs, properties, links in objects(xml):
        if attrs.get("EntityClass") == "Ladder":
            ladder = (attrs, properties)
            for link in links:
                height = re.match(r"ladderHeight\['(\d+)'\]", link.replace("&apos;", "'"))
                if height:
                    tags = height.group(1)
        elif attrs.get("EntityClass") == "SmartObjectHolder":
            push = attrs
    if ladder is None or push is None:
        raise RuntimeError("{} has no ladder or no push smart object".format(path))
    attrs, properties = ladder
    model = properties["fileModel"].lower()
    rotation = floats(attrs.get("Rotate", "1,0,0,0"))
    lean = math.degrees(2.0 * math.atan2(rotation[1], rotation[0]))
    if tags is None:
        # `battleLadder_350` links no height; its village ladder's name carries it.
        tags = re.search(r"ladder_rustic_(\d+)\.cgf", model).group(1)
    if paks.read(broken) is None:
        raise RuntimeError("missing broken mesh " + broken)
    return {
        "name": name,
        "model": model,
        "broken": broken,
        "height": float(properties["height"]),
        "lean": lean,
        "tags": tags,
        "push_from": tuple(floats(push.get("Pos", "0,0,0"))),
        "note": "{}: {}".format(os.path.basename(path), os.path.basename(model)),
    }


def derived_ladder(paks, base, name, village_height, tags):
    model = "{}ladder_rustic_{}.cgf".format(LADDER_DIR, tags)
    broken = "{}ladder_rustic_{}_broken.cgf".format(LADDER_DIR, tags)
    for mesh in (model, broken):
        if paks.read(mesh) is None:
            raise RuntimeError("missing " + mesh)
    height = village_height - PALISADE_REST
    scale = height / base["height"]
    return {
        "name": name,
        "model": model,
        "broken": broken,
        "height": height,
        "lean": base["lean"],
        "tags": tags,
        "push_from": (base["push_from"][0], base["push_from"][1] * scale, base["push_from"][2] * scale),
        "note": "derived from {}: {}".format(base["name"], os.path.basename(model)),
    }


def stone_pile(paks):
    pile_xml = paks.read(STONE_PILE_PREFAB).decode("latin1")
    heap_xml = paks.read(STONE_HEAP_PREFAB).decode("latin1")

    pile = node = pick = heap_at = None
    heap_rotation = (1.0, 0.0, 0.0, 0.0)
    for attrs, properties, _ in objects(pile_xml):
        kind = attrs.get("EntityClass", "")
        if kind == "StoneThrowingPile":
            pile = (attrs, properties)
        elif kind == "StoneThrowingNode":
            node = tuple(floats(attrs["Pos"]))
        elif attrs.get("Name") == "PickPoint":
            pick = tuple(floats(attrs["Pos"]))
        elif attrs.get("PrefabTemplateName") == "hurlingStone/hurlingStonePile_a":
            heap_at = tuple(floats(attrs["Pos"]))
            heap_rotation = tuple(floats(attrs.get("Rotate", "1,0,0,0")))
    if pile is None or node is None or pick is None or heap_at is None:
        raise RuntimeError(STONE_PILE_PREFAB + " is not the layout this expects")

    pile_attrs, pile_properties = pile
    pile_at = tuple(floats(pile_attrs["Pos"]))
    pile_rotation = tuple(floats(pile_attrs.get("Rotate", "1,0,0,0")))

    # The way the thrower faces once the grab has aligned them: from the
    # path node to the pile, turned a quarter right.
    toward = (pile_at[0] - node[0], pile_at[1] - node[1])
    length = math.hypot(toward[0], toward[1])
    facing = (toward[1] / length, -toward[0] / length)
    # Turned so that facing is +Y, and moved so that the pile is the origin.
    turn = math.atan2(facing[0], facing[1])
    frame = yaw_quat(turn)

    def place(point):
        moved = (point[0] - pile_at[0], point[1] - pile_at[1], point[2] - pile_at[2])
        return rotate(frame, moved)

    stones = []
    for attrs, _, _ in objects(heap_xml):
        if attrs.get("EntityClass") != "GeomEntity":
            continue
        local = tuple(floats(attrs["Pos"]))
        at = rotate(heap_rotation, local)
        at = (at[0] + heap_at[0], at[1] + heap_at[1], at[2] + heap_at[2])
        rotation = quat_mul(frame, quat_mul(heap_rotation, tuple(floats(attrs.get("Rotate", "1,0,0,0")))))
        scale = floats(attrs.get("Scale", "1,1,1"))[0]
        stones.append({"model": attrs["Geometry"].lower(), "at": place(at), "rotation": rotation, "scale": scale, "name": attrs["Name"]})
    stones.sort(key=lambda stone: stone["name"])
    if len(stones) != 9:
        raise RuntimeError("{} holds {} stones, not 9".format(STONE_HEAP_PREFAB, len(stones)))

    return {
        "pile": place(pile_at),
        "pile_yaw": math.degrees(quat_yaw(quat_mul(frame, pile_rotation))),
        "pile_scale": floats(pile_attrs.get("Scale", "1,1,1"))[0],
        "pile_model": pile_properties.get("object_Model", "").lower(),
        "throw_type": pile_properties.get("esThrowType", "hole"),
        "node": place(node),
        "pick": place(pick),
        "stones": stones,
    }


def number(value):
    return "{:.4f}F".format(0.0 if abs(value) < 5e-5 else value)


def point(values):
    return "{{{}}}".format(", ".join(number(v) for v in values))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default=gates.DEFAULT_GAME)
    arguments = parser.parse_args()

    if not os.path.isdir(arguments.game):
        print("Game directory not found: {}".format(arguments.game), file=sys.stderr)
        return 1

    print("Indexing paks under {} ...".format(arguments.game))
    paks = gates.PakSet(arguments.game)

    ladders = [battle_ladder(paks, name, path, broken) for name, path, broken in BATTLE_LADDERS]
    base = next(ladder for ladder in ladders if ladder["name"] == DERIVED_FROM)
    ladders += [derived_ladder(paks, base, name, height, tags) for name, height, tags in DERIVED_LADDERS]
    ladders.sort(key=lambda ladder: -ladder["height"])
    for ladder in ladders:
        print("  ladder {}: {:.2f} m, lean {:.2f}, tag {} ({})".format(ladder["name"], ladder["height"], ladder["lean"], ladder["tags"], ladder["note"]))

    pile = stone_pile(paks)
    print("  pile: {} stones, throw type {}, node {}".format(len(pile["stones"]), pile["throw_type"], tuple(round(v, 3) for v in pile["node"])))

    lines = []
    lines.append('#include "shared/game/siege_defence_catalog.h"')
    lines.append("")
    lines.append("// Generated by tools/generate_siege_defence_catalog.py. Do not edit by hand.")
    lines.append("//")
    lines.append("// The ladders are the battle-ladder prefabs' `Ladder` entities and push")
    lines.append("// smart objects; `tall` and `low` are derived from `short`. The pile is")
    lines.append("// hurlingStonePile_onesided_walls_left_a, turned so the throw is +Y.")
    lines.append("")
    lines.append("namespace KCDC::Shared::Game {")
    lines.append("    const std::vector<SiegeLadderAsset> g_SiegeLadderAssets = {")
    for ladder in ladders:
        lines.append("        // {}".format(ladder["note"]))
        lines.append("        {{\"{}\", \"{}\", \"{}\", {}, {}, \"{}\", {}}},".format(
            ladder["name"], ladder["model"], ladder["broken"], number(ladder["height"]), number(ladder["lean"]), ladder["tags"], point(ladder["push_from"])))
    lines.append("    };")
    lines.append("")
    lines.append("    const StonePileLayout g_StonePileLayout = {")
    lines.append("        {}, {}, {}, \"{}\",".format(point(pile["pile"]), number(pile["pile_yaw"]), number(pile["pile_scale"]), pile["pile_model"]))
    lines.append("        \"{}\",".format(pile["throw_type"]))
    lines.append("        {}, {},".format(point(pile["node"]), point(pile["pick"])))
    lines.append("        {{")
    for stone in pile["stones"]:
        lines.append("            {{\"{}\", {}, {{{}}}, {}}},".format(stone["model"], point(stone["at"]), ", ".join(number(v) for v in stone["rotation"]), number(stone["scale"])))
    lines.append("        }},")
    lines.append("    };")
    lines.append("} // namespace KCDC::Shared::Game")
    lines.append("")

    with open(OUTPUT, "w", encoding="utf8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print("Wrote {}".format(OUTPUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())

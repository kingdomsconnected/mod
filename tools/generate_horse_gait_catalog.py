#!/usr/bin/env python3
"""Generate the KCDC horse gait catalog from an installed Kingdom Come: Deliverance II.

What a horse's legs can show at a given speed is decided in two places, and
this reads both.

Which gait the horse is in follows its speed. `HorseRuntimeData::
ApplyRuntimePaceAndDelta` (0x1806CF3D4) states the pace as
`C_RPGMovement::GetMoveStyleForSpeed` (0x181FD3AB0) of the runtime speed, plus
one, and that function picks the first of the soul's move styles whose speed
is at least the asked-for one. A horse's move styles are the rows of
`Libs/Tables/rpg/soul_archetype_movement.xml` for the `Horse` archetype: the
style id (`logical_speed`), the Mannequin tag its pace sets (`mn_tag`: walk,
run, sprint, dash) and the speeds a soul's own style is clamped between
(`real_speed_min`, `real_speed_max`).

How fast that gait's animation can go is its blendspace. The horse's
Mannequin database (`kcd_horse_database.adb`) plays one parametric blendspace
per gait tag -- `2d_walk_turn` under `walk`, `2d_trot_turn` under `run`, with
`player+smoothRider` and `cart` variants -- and each is one clip at a range
of playback scales over a MoveSpeed and a TurnSpeed axis. CryEngine clamps the
asked-for parameters to the axis range (`ParametricSampler.cpp`), and an
example that states its own value (`SetPara0`, `SetPara1`) bounds what the
clip covers inside it. Past that range the travel the body is asked for and
the stride its legs play part, and the feet slide.

So each blendspace row carries its fragment's tags, the gait tag among them,
and the MoveSpeed and TurnSpeed ranges it can show: the axis range, narrowed
to the stated examples where it has any. The blendspaces are found where the
horse's `.chrparams` says its animations are.

Emitted as `code/shared/generated/horse_gait_catalog.cpp`.

Usage:
    python tools/generate_horse_gait_catalog.py <game directory>
"""

from __future__ import annotations

import argparse
import os
import posixpath
import re
import sys
import xml.etree.ElementTree as ET
import zipfile

TABLES_PAK = os.path.join("Data", "Tables.pak")
ANIMATIONS_PAK = os.path.join("Data", "Animations.pak")
CHARACTERS_PAKS = (os.path.join("Data", "Characters.pak"), os.path.join("Data", "IPL_Characters-part0.pak"))

ARCHETYPE_TABLE = "Libs/Tables/rpg/soul_archetype.xml"
ARCHETYPE_MOVEMENT_TABLE = "Libs/Tables/rpg/soul_archetype_movement.xml"
HORSE_ARCHETYPE_NAME = "Horse"

HORSE_DATABASE = "Animations/Mannequin/ADB/kcd_horse_database.adb"
HORSE_CHRPARAMS = "Objects/characters/animals/horse/horse.chrparams"

DEFAULT_OUTPUT = os.path.join("code", "shared", "generated", "horse_gait_catalog.cpp")

ATTR_RE = re.compile(r'(\w+)="([^"]*)"')


def read_rows(raw, tag):
    return [dict(ATTR_RE.findall(match.group(0))) for match in re.finditer(r"<%s\b[^>]*/>" % tag, raw)]


def read_move_styles(tables):
    archetypes = read_rows(tables.read(ARCHETYPE_TABLE).decode("utf-8", "replace"), "soul_archetype")
    horse = [row["soul_archetype_id"] for row in archetypes if row.get("soul_archetype_name") == HORSE_ARCHETYPE_NAME]
    if len(horse) != 1:
        sys.exit("expected one '%s' soul archetype, found %d" % (HORSE_ARCHETYPE_NAME, len(horse)))
    rows = [row for row in read_rows(tables.read(ARCHETYPE_MOVEMENT_TABLE).decode("utf-8", "replace"), "soul_archetype_movement") if row.get("soul_archetype_id") == horse[0]]
    styles = sorted(((int(row["logical_speed"]), row["mn_tag"], float(row["real_speed_min"]), float(row["real_speed_max"])) for row in rows), key=lambda style: style[0])
    if [style[0] for style in styles] != list(range(len(styles))):
        sys.exit("the horse's move styles are not numbered 0..%d: %r" % (len(styles) - 1, styles))
    return styles


def read_blendspace_folders(game_dir):
    for pak_name in CHARACTERS_PAKS:
        path = os.path.join(game_dir, pak_name)
        if not os.path.exists(path):
            continue
        with zipfile.ZipFile(path) as pak:
            if HORSE_CHRPARAMS not in pak.namelist():
                continue
            root = ET.fromstring(pak.read(HORSE_CHRPARAMS))
            animations = root.find("AnimationList")
            base = next(entry.get("path") for entry in animations if entry.get("name") == "#filepath")
            folders = [posixpath.join(base, posixpath.dirname(entry.get("path"))) for entry in animations if entry.get("path", "").endswith("*.bspace")]
            return [folder.lower() for folder in folders]
    sys.exit("no %s in %s" % (HORSE_CHRPARAMS, ", ".join(CHARACTERS_PAKS)))


def axis(root, index):
    params = root.find("Dimensions").findall("Param")
    if index >= len(params):
        return None
    attrs = {key.lower(): value for key, value in params[index].attrib.items()}
    return attrs["name"], float(attrs["min"]), float(attrs["max"])


def stated(root, index):
    values = []
    for example in root.find("ExampleList").findall("Example"):
        attrs = {key.lower(): value for key, value in example.attrib.items()}
        if "setpara%d" % index in attrs:
            values.append(float(attrs["setpara%d" % index]))
    return values


def shown_range(root, index, name):
    found = axis(root, index)
    if found is None or found[0] != name:
        sys.exit("blendspace axis %d is %r, not %s" % (index, found, name))
    low, high = found[1], found[2]
    values = stated(root, index)
    if values:
        low, high = max(low, min(values)), min(high, max(values))
    return low, high


def read_blendspaces(animations, folders, gait_tags):
    names = {name.lower(): name for name in animations.namelist()}
    database = ET.fromstring(animations.read(HORSE_DATABASE))
    rows = []
    for fragment in database.iter("Fragment"):
        tags = fragment.get("Tags")
        if tags is None:
            continue  # a transition's fragment, which has no tags of its own
        tag_list = tags.split("+")
        gaits = [tag for tag in tag_list if tag in gait_tags]
        if len(gaits) != 1:
            continue
        for animation in fragment.iter("Animation"):
            clip = animation.get("name", "")
            path = next((names[key] for key in (posixpath.join(folder, clip.lower() + ".bspace") for folder in folders) if key in names), None)
            if path is None:
                continue
            root = ET.fromstring(animations.read(path))
            if [axis(root, index)[0] if axis(root, index) else None for index in (0, 1)] != ["MoveSpeed", "TurnSpeed"]:
                continue  # a turn in place or a transition under the same tag, not the gait's locomotion
            move = shown_range(root, 0, "MoveSpeed")
            turn = shown_range(root, 1, "TurnSpeed")
            rows.append((clip, tags, gaits[0], move, turn))
    rows.sort(key=lambda row: (row[2], row[1], row[0]))
    return rows


def c_float(value):
    text = ("%.4f" % value).rstrip("0")
    return (text + "0" if text.endswith(".") else text) + "F"


def emit(styles, blendspaces):
    lines = [
        "// Generated by tools/generate_horse_gait_catalog.py from Data/Tables.pak,",
        "// Data/Animations.pak and the horse's .chrparams. Do not edit by hand.",
        "",
        '#include "shared/game/horse_gait.h"',
        "",
        "namespace KCDC::Shared::Game {",
        "    const std::vector<HorseMoveStyleEntry> g_HorseMoveStyles = {",
    ]
    for style_id, tag, low, high in styles:
        lines.append('        {%d, "%s", %s, %s},' % (style_id, tag, c_float(low), c_float(high)))
    lines.append("    };")
    lines.append("")
    lines.append("    const std::vector<HorseGaitBlendspaceEntry> g_HorseGaitBlendspaces = {")
    for clip, tags, gait, move, turn in blendspaces:
        lines.append('        {"%s", "%s", "%s", %s, %s, %s, %s},' % (clip, tags, gait, c_float(move[0]), c_float(move[1]), c_float(turn[0]), c_float(turn[1])))
    lines.append("    };")
    lines.append("} // namespace KCDC::Shared::Game")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("game_dir")
    parser.add_argument("--output", default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    with zipfile.ZipFile(os.path.join(args.game_dir, TABLES_PAK)) as tables:
        styles = read_move_styles(tables)
    folders = read_blendspace_folders(args.game_dir)
    with zipfile.ZipFile(os.path.join(args.game_dir, ANIMATIONS_PAK)) as animations:
        blendspaces = read_blendspaces(animations, folders, {style[1] for style in styles})
    for _, tag, _, _ in styles:
        if not any(row[2] == tag for row in blendspaces):
            sys.exit("no blendspace plays the '%s' gait" % tag)

    with open(args.output, "w", newline="\n", encoding="ascii") as out:
        out.write(emit(styles, blendspaces))
    print("%d move styles, %d gait blendspaces -> %s" % (len(styles), len(blendspaces), args.output))


if __name__ == "__main__":
    main()

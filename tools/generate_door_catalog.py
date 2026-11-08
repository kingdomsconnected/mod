#!/usr/bin/env python3
"""Mines every placed AnimDoor into code/shared/generated/door_catalog.cpp.

A door is a level entity of class `AnimDoor`, driven by
`Scripts/Entities/Doors/AnimDoor.lua`. The server has no world to look in, so
without this table it only learns a door exists when some client reports
building one. This walks the level paks instead and records, per door:

  - the level `EntityGuid`, in the encoding `IEntity::GetGuid()` returns
  - where it stands and which way it faces, from `Pos` and `Rotate`
  - its `Properties.Lock` block: authored lock state, lock type, reversal,
    lockpickability, dynamic-key eligibility and the key's item class
  - the leaf mesh it hangs (`object_Model`)

and, once per leaf mesh, what an AnimDoor needs to swing it: the animation
set, the smart-object helpers and type every placement of it agrees on. That
table is what a script or the World Builder hangs a new door from.

Every attribute a placement leaves out takes the default AnimDoor.lua declares
in its own `Properties.Lock` table, which is read out of `Scripts.pak` rather
than typed here.

Usage:
    python tools/generate_door_catalog.py [--game <install dir>]
"""

import argparse
import math
import os
import re
import sys
import xml.etree.ElementTree as ET
import zipfile

from generate_gate_catalog import DEFAULT_GAME, entity_guid, escape, zread

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(REPO_ROOT, "code", "shared", "generated", "door_catalog.cpp")

ANIM_DOOR_SCRIPT = "scripts/entities/doors/animdoor.lua"

# esLockTypes as AnimDoor.lua compares it, in the order DoorLockType declares.
LOCK_TYPES = {"none/none": "None", "manual/none": "Manual", "manual/key": "ManualKey", "key/key": "Key"}

# The Lock properties this catalog records. Their defaults come from the Lua.
LOCK_FIELDS = ("bLocked", "bLockOutside", "bCanLockPick", "bCanUnlockWithDynamicKey", "bLockReversed", "esLockTypes", "guidItemClassId")


def lua_lock_defaults(game):
    """The `Properties.Lock` defaults AnimDoor.lua declares."""
    with zipfile.ZipFile(os.path.join(game, "Data", "Scripts.pak")) as archive:
        member = next(name for name in archive.namelist() if name.lower().replace("\\", "/") == ANIM_DOOR_SCRIPT)
        text = zread(archive, member).decode("utf8", "replace")

    block = re.search(r"\bLock\s*=\s*\{(.*?)\}", text, re.S)
    if block is None:
        raise ValueError("AnimDoor.lua declares no Properties.Lock table")

    defaults = {}
    for name, value in re.findall(r"(\w+)\s*=\s*([^,\n]+)", block.group(1)):
        value = value.split("--")[0].strip()
        if value in ("true", "false"):
            defaults[name] = value == "true"
        elif value.startswith('"'):
            defaults[name] = value.strip('"')
    missing = [field for field in LOCK_FIELDS if field not in defaults]
    if missing:
        raise ValueError("AnimDoor.lua Properties.Lock has no default for {}".format(", ".join(missing)))
    return defaults


def lua_bool(text):
    return text.strip() not in ("0", "false", "")


def forward_of(rotate):
    """The entity's forward axis, `IEntity::GetForwardDir()`, from `Rotate`.

    CryEngine reads the attribute as w,x,y,z (`CXmlNode::getAttr(Quat)`), and
    the forward direction is column 1 of the rotation matrix -- the one
    AnimDoor:IsAccessedFromFront dots the player's offset against.
    """
    w, x, y, z = rotate
    forward = (2.0 * (x * y - w * z), 1.0 - 2.0 * (x * x + z * z))
    length = math.hypot(forward[0], forward[1])
    if length < 1e-6:
        raise ValueError("door faces straight up or down")
    return forward[0] / length, forward[1] / length


def parse_vector(text, count, default):
    if text is None:
        return default
    parts = text.split(",")
    if len(parts) != count:
        raise ValueError("expected {} components in {!r}".format(count, text))
    return tuple(float(part) for part in parts)


def doors_in_level(archive, defaults):
    """Every AnimDoor a level places, keyed by its runtime GUID."""
    doors = {}
    for member in archive.namelist():
        if not member.lower().endswith(".xml"):
            continue
        try:
            blob = zread(archive, member)
        except Exception:
            continue
        if b'"AnimDoor"' not in blob:
            continue

        for entity in ET.fromstring(blob).iter("Entity"):
            if entity.get("EntityClass") != "AnimDoor":
                continue
            guid = entity_guid(entity.get("EntityGuid", ""))
            if guid == 0:
                raise ValueError("{}: AnimDoor {!r} has no usable EntityGuid".format(member, entity.get("Name")))
            if guid in doors:
                raise ValueError("{}: AnimDoor GUID {:#x} is placed twice".format(member, guid))

            lock = {}
            properties = entity.find("Properties")
            node = properties.find("Lock") if properties is not None else None
            for field in LOCK_FIELDS:
                raw = node.get(field) if node is not None else None
                if raw is None:
                    lock[field] = defaults[field]
                elif isinstance(defaults[field], bool):
                    lock[field] = lua_bool(raw)
                else:
                    lock[field] = raw
            if lock["esLockTypes"] not in LOCK_TYPES:
                raise ValueError("{}: AnimDoor {!r} has lock type {!r}".format(member, entity.get("Name"), lock["esLockTypes"]))

            # A few cinematic layers place an AnimDoor with no leaf of its own;
            # such a placement names no model row.
            model = (properties.get("object_Model", "") if properties is not None else "").replace("\\", "/").lower()
            doors[guid] = {
                "model": model,
                "animSet": properties.get("esDoorAnimSet", "") if properties is not None else "",
                # `_OmitNav` only keeps the navmesh from linking that one placement.
                "helpers": (properties.get("soclass_SmartObjectHelpers", "") if properties is not None else "").replace("_OmitNav", ""),
                "smartObjectType": properties.get("guidSmartObjectType", "") if properties is not None else "",
                "name": entity.get("Name", ""),
                "position": parse_vector(entity.get("Pos"), 3, (0.0, 0.0, 0.0)),
                "forward": forward_of(parse_vector(entity.get("Rotate"), 4, (1.0, 0.0, 0.0, 0.0))),
                "lock": lock,
            }
    return doors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default=DEFAULT_GAME)
    arguments = parser.parse_args()

    if not os.path.isdir(arguments.game):
        print("Game directory not found: {}".format(arguments.game), file=sys.stderr)
        return 1

    defaults = lua_lock_defaults(arguments.game)
    print("AnimDoor.lua lock defaults: {}".format({field: defaults[field] for field in LOCK_FIELDS}))

    levels_root = os.path.join(arguments.game, "Data", "Levels")
    levels = {}
    for level in sorted(os.listdir(levels_root)):
        path = os.path.join(levels_root, level, "level.pak")
        if not os.path.isfile(path):
            continue
        with zipfile.ZipFile(path) as archive:
            levels[level] = doors_in_level(archive, defaults)
        doors = levels[level]
        print("  {}: {} door(s), {} start locked, {} keyed, {} not lockpickable".format(
            level, len(doors), sum(1 for d in doors.values() if d["lock"]["bLocked"] or d["lock"]["bLockOutside"]),
            sum(1 for d in doors.values() if d["lock"]["guidItemClassId"]), sum(1 for d in doors.values() if not d["lock"]["bCanLockPick"])))

    total = sum(len(doors) for doors in levels.values())
    if total == 0:
        print("No AnimDoor placements found -- refusing to write an empty catalog.", file=sys.stderr)
        return 1

    # One row per leaf mesh, with the settings most of its placements agree on.
    models = {}
    for doors in levels.values():
        for door in doors.values():
            if not door["model"]:
                continue
            row = models.setdefault(door["model"], {"count": 0, "settings": {}})
            row["count"] += 1
            key = (door["animSet"], door["helpers"], door["smartObjectType"])
            row["settings"][key] = row["settings"].get(key, 0) + 1
    model_paths = sorted(models)
    model_index = {path: index for index, path in enumerate(model_paths)}
    if len(model_paths) >= 0xFFFF:
        raise ValueError("more door models than a uint16 index holds")
    print("  {} door model(s)".format(len(model_paths)))

    lines = []
    lines.append('#include "shared/game/door_catalog.h"')
    lines.append("")
    lines.append("// Generated by tools/generate_door_catalog.py. Do not edit by hand.")
    lines.append("//")
    lines.append("// Every AnimDoor placed in the shipped levels, with its Properties.Lock")
    lines.append("// block resolved against the defaults AnimDoor.lua declares, and every")
    lines.append("// leaf mesh they hang.")
    lines.append("")
    lines.append("namespace KCDC::Shared::Game {")
    lines.append("    const std::vector<DoorModel> g_DoorModels = {")
    for path in model_paths:
        settings = max(models[path]["settings"].items(), key=lambda item: (item[1], item[0]))[0]
        if not settings[0] or not settings[1] or not settings[2]:
            raise ValueError("door model {} has no animation set, helpers or smart-object type".format(path))
        lines.append('        {{"{}", "{}", "{}", "{}", {}}},'.format(escape(path), escape(settings[0]), escape(settings[1]), escape(settings[2]), models[path]["count"]))
    lines.append("    };")
    lines.append("")
    lines.append("    const std::vector<DoorPlacement> g_DoorPlacements = {")
    for level, doors in levels.items():
        for guid, door in sorted(doors.items()):
            lock = door["lock"]
            lines.append('        {{"{}", {:#018x}ULL, "{}", {:.4f}F, {:.4f}F, {:.4f}F, {:.5f}F, {:.5f}F, "{}", DoorLockType::{}, {}, {}, {}, {}, {}}},'.format(
                escape(level), guid, escape(door["name"]), door["position"][0], door["position"][1], door["position"][2], door["forward"][0], door["forward"][1],
                escape(lock["guidItemClassId"]), LOCK_TYPES[lock["esLockTypes"]], "true" if lock["bLocked"] or lock["bLockOutside"] else "false",
                "true" if lock["bLockReversed"] else "false", "true" if lock["bCanLockPick"] else "false", "true" if lock["bCanUnlockWithDynamicKey"] else "false", model_index[door["model"]] if door["model"] else "kNoDoorModel"))
    lines.append("    };")
    lines.append("} // namespace KCDC::Shared::Game")
    lines.append("")

    with open(OUTPUT, "w", encoding="utf8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print("Wrote {} door(s) to {}".format(total, OUTPUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())

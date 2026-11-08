#!/usr/bin/env python3
"""Mines every placed Stash into code/shared/generated/stash_catalog.cpp.

A level container is an entity of class `Stash`, driven by
`Scripts/Entities/WH/Stash/AnimStash.lua`. The server has no world to look in,
so without this table it cannot address a chest the level places, lock it, or
be asked before one is opened. This walks the level paks and records, per
container:

  - the level `EntityGuid`, in the encoding `IEntity::GetGuid()` returns
  - where it stands, from `Pos`, and which way it faces, from `Rotate` --
    AnimStash.lua's IsUsable only offers it to a body behind that axis
  - its `Properties.Lock` block: authored lock state, lockpickability,
    dynamic-key eligibility and the key's item class
  - `fUseDistance`, the reach AnimStash.lua's IsUsable allows
  - the stash it shares an inventory with, through a `masterStash` link --
    `C_Stash::GetMasterInventoryWuid` opens that one's inventory instead of
    its own, so both have to be one container to the server

Only the `Stash` class: `Nest`, `CartStash`, `StashCorpse` and `DestroStash`
are other scripts with other interactions. Every attribute a placement leaves
out takes the default AnimStash.lua declares, read out of `Scripts.pak` rather
than typed here.

Usage:
    python tools/generate_stash_catalog.py [--game <install dir>]
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
OUTPUT = os.path.join(REPO_ROOT, "code", "shared", "generated", "stash_catalog.cpp")

STASH_SCRIPT = "scripts/entities/wh/stash/animstash.lua"

# The Lock properties this catalog records. Their defaults come from the Lua.
LOCK_FIELDS = ("bLocked", "bCanLockPick", "bCanUnlockWithDynamicKey", "guidItemClassId")


def lua_defaults(game):
    """The `Properties.Lock` defaults and `fUseDistance` AnimStash.lua declares."""
    with zipfile.ZipFile(os.path.join(game, "Data", "Scripts.pak")) as archive:
        member = next(name for name in archive.namelist() if name.lower().replace("\\", "/") == STASH_SCRIPT)
        text = zread(archive, member).decode("utf8", "replace")

    block = re.search(r"\bLock\s*=\s*\{(.*?)\}", text, re.S)
    if block is None:
        raise ValueError("AnimStash.lua declares no Properties.Lock table")

    defaults = {}
    for name, value in re.findall(r"(\w+)\s*=\s*([^,\n]+)", block.group(1)):
        value = value.split("--")[0].strip()
        if value in ("true", "false"):
            defaults[name] = value == "true"
        elif value.startswith('"'):
            defaults[name] = value.strip('"')
    missing = [field for field in LOCK_FIELDS if field not in defaults]
    if missing:
        raise ValueError("AnimStash.lua Properties.Lock has no default for {}".format(", ".join(missing)))

    distance = re.search(r"\bfUseDistance\s*=\s*([0-9.]+)", text)
    if distance is None:
        raise ValueError("AnimStash.lua declares no fUseDistance")
    defaults["fUseDistance"] = float(distance.group(1))
    return defaults


def lua_bool(text):
    return text.strip() not in ("0", "false", "")


def forward_of(rotate):
    """The entity's forward axis, `GetDirectionVector()`, from `Rotate`.

    CryEngine reads the attribute as w,x,y,z, and the forward axis is column 1
    of the rotation matrix -- the one Stash:IsUsable dots the player's offset
    against. A container tipped onto its back has no horizontal forward, and
    the engine's own check then offers it from nowhere; it keeps +Y here.
    """
    w, x, y, z = rotate
    forward = (2.0 * (x * y - w * z), 1.0 - 2.0 * (x * x + z * z))
    length = math.hypot(forward[0], forward[1])
    if length < 1e-6:
        return 0.0, 1.0
    return forward[0] / length, forward[1] / length


def parse_vector(text, count, default):
    if text is None:
        return default
    parts = text.split(",")
    if len(parts) != count:
        raise ValueError("expected {} components in {!r}".format(count, text))
    return tuple(float(part) for part in parts)


def stashes_in_level(archive, defaults):
    """Every Stash a level places, keyed by its runtime GUID."""
    stashes = {}
    for member in archive.namelist():
        if not member.lower().endswith(".xml"):
            continue
        try:
            blob = zread(archive, member)
        except Exception:
            continue
        if b'"Stash"' not in blob:
            continue

        for entity in ET.fromstring(blob).iter("Entity"):
            if entity.get("EntityClass") != "Stash":
                continue
            guid = entity_guid(entity.get("EntityGuid", ""))
            if guid == 0:
                raise ValueError("{}: Stash {!r} has no usable EntityGuid".format(member, entity.get("Name")))
            if guid in stashes:
                raise ValueError("{}: Stash GUID {:#x} is placed twice".format(member, guid))

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

            use_distance = defaults["fUseDistance"]
            if properties is not None and properties.get("fUseDistance") is not None:
                use_distance = float(properties.get("fUseDistance"))

            master = 0
            links = entity.find("EntityLinks")
            if links is not None:
                for link in links:
                    if link.get("Name") == "masterStash":
                        master = entity_guid(link.get("TargetGuid", ""))

            stashes[guid] = {
                "name": entity.get("Name", ""),
                "position": parse_vector(entity.get("Pos"), 3, (0.0, 0.0, 0.0)),
                "forward": forward_of(parse_vector(entity.get("Rotate"), 4, (1.0, 0.0, 0.0, 0.0))),
                "lock": lock,
                "use_distance": use_distance,
                "master": master,
            }

    # A link to nothing, or to something that is not a Stash in this level,
    # opens the stash's own inventory: GetMasterInventoryWuid finds no master.
    # A master that itself has a master is not followed by the engine either.
    for stash in stashes.values():
        master = stashes.get(stash["master"])
        if master is None or stash["master"] == 0:
            stash["master"] = 0
    for guid, stash in stashes.items():
        if stash["master"] != 0 and stashes[stash["master"]]["master"] != 0:
            raise ValueError("Stash {:#x} links a master that has a master of its own".format(guid))
    return stashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default=DEFAULT_GAME)
    arguments = parser.parse_args()

    if not os.path.isdir(arguments.game):
        print("Game directory not found: {}".format(arguments.game), file=sys.stderr)
        return 1

    defaults = lua_defaults(arguments.game)
    print("AnimStash.lua defaults: {}".format(defaults))

    levels_root = os.path.join(arguments.game, "Data", "Levels")
    levels = {}
    for level in sorted(os.listdir(levels_root)):
        path = os.path.join(levels_root, level, "level.pak")
        if not os.path.isfile(path):
            continue
        with zipfile.ZipFile(path) as archive:
            levels[level] = stashes_in_level(archive, defaults)
        stashes = levels[level]
        print("  {}: {} stash(es), {} start locked, {} keyed, {} not lockpickable, {} share a master's inventory".format(
            level, len(stashes), sum(1 for s in stashes.values() if s["lock"]["bLocked"]), sum(1 for s in stashes.values() if s["lock"]["guidItemClassId"]),
            sum(1 for s in stashes.values() if not s["lock"]["bCanLockPick"]), sum(1 for s in stashes.values() if s["master"] != 0)))

    total = sum(len(stashes) for stashes in levels.values())
    if total == 0:
        print("No Stash placements found -- refusing to write an empty catalog.", file=sys.stderr)
        return 1

    lines = []
    lines.append('#include "shared/game/stash_catalog.h"')
    lines.append("")
    lines.append("// Generated by tools/generate_stash_catalog.py. Do not edit by hand.")
    lines.append("//")
    lines.append("// Every Stash placed in the shipped levels, with its Properties.Lock")
    lines.append("// block resolved against the defaults AnimStash.lua declares.")
    lines.append("")
    lines.append("namespace KCDC::Shared::Game {")
    lines.append("    const std::vector<StashPlacement> g_StashPlacements = {")
    for level, stashes in levels.items():
        for guid, stash in sorted(stashes.items()):
            lock = stash["lock"]
            lines.append('        {{"{}", {:#018x}ULL, "{}", {:.4f}F, {:.4f}F, {:.4f}F, {:.5f}F, {:.5f}F, {:.2f}F, "{}", {:#018x}ULL, {}, {}, {}}},'.format(
                escape(level), guid, escape(stash["name"]), stash["position"][0], stash["position"][1], stash["position"][2], stash["forward"][0], stash["forward"][1], stash["use_distance"], escape(lock["guidItemClassId"]), stash["master"],
                "true" if lock["bLocked"] else "false", "true" if lock["bCanLockPick"] else "false", "true" if lock["bCanUnlockWithDynamicKey"] else "false"))
    lines.append("    };")
    lines.append("} // namespace KCDC::Shared::Game")
    lines.append("")

    with open(OUTPUT, "w", encoding="utf8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print("Wrote {} stash(es) to {}".format(total, OUTPUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Generate the KCDC horse gear table from an installed Kingdom Come: Deliverance II.

A horse wears items the way a person wears clothes. Every piece of horse gear
is a plain `<Armor>` item class -- almost all of them in
`Libs/Tables/item/item__horse.xml` -- whose `Clothing` attribute names a node
under the `Horse` component of `Libs/Tables/Character/CharacterComponent.xml`:

    <Component Name="Horse" Race="Horse">
      <DerivedComponents>
        <Component Name="Clothing" FilePath="animals/horse/clothing/">
          <DerivedComponents>
            <Clothing Name="Saddle" ArmorType="Saddle" ArmorArchetypeId="75">
              <DerivedComponents>
                <Clothing Name="BasicSaddle01_m01" ... />

The slot a piece goes into comes from the `ArmorArchetypeId` it inherits down
that tree, and the game's horse inventory screen has exactly four of them
(`equipment_slot.xml`: horse_saddle, horse_head, horse_torso, horse_shoe):

    75 HorseSaddle          saddle (the saddlebags are part of the saddle model)
    77 HorseBridle          head
    76 HorseArmorHeadNeck   head (a chanfron)
    74 HorseTrappingsFull   torso (caparisons and padded trappings)
    87 HorseShoe           shoe

So the table is every `<Armor>` item class in any `item*.xml` whose clothing
resolves under the horse's tree, with the slot that archetype names.

The game dresses its horses from clothing presets (`clothing_preset*.xml`),
handed out by inventory presets on the level's horse entities. A named horse's
tack shares its soul's name (`kgru_buresHorse`, reached through
`inventory_kgru_buresHorse`), and the generic horses draw one child of
`equipment_horse_common` (`InventoryPreset__horse_equipment.xml`,
`Mode="OneChild"`). So the file also carries every clothing preset made of
horse gear alone, and that list of children.

Emitted as `code/shared/generated/horse_gear_catalog.cpp`: the server validates a
rider's report against it, dresses a spawned horse from it and scripts name gear
from it, and it has no game to read.

Usage:
    python tools/generate_horse_gear_catalog.py <game directory>
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET
import zipfile

TABLES_PAK = os.path.join("Data", "Tables.pak")
ITEM_TABLE_RE = re.compile(r"^Libs/Tables/item/item(__[a-zA-Z_]+)?\.xml$", re.IGNORECASE)
COMPONENT_TABLE = "Libs/Tables/Character/CharacterComponent.xml"

# `ArmorArchetypeId` -> the `HorseGearSlot` enumerator it fills.
ARCHETYPE_SLOTS = {
    "75": "Saddle",
    "77": "Head",
    "76": "Head",
    "74": "Torso",
    "87": "Shoe",
}

# The order the enum declares them in, which is also the order the gear is
# put on in: a caparison needs a saddle under it (`RequiresFilledSlot`).
SLOT_ORDER = ["Saddle", "Head", "Torso", "Shoe"]

CLOTHING_PRESET_RE = re.compile(r"^Libs/Tables/item/clothing_preset(__[a-zA-Z_]+)?\.xml$", re.IGNORECASE)
HORSE_EQUIPMENT_TABLE = "Libs/Tables/item/InventoryPreset__horse_equipment.xml"

# The inventory preset a generic horse of the game draws its tack from.
COMMON_GROUP = "equipment_horse_common"

DEFAULT_OUTPUT = os.path.join("code", "shared", "generated", "horse_gear_catalog.cpp")


def find_horse(root):
    for component in root.iter("Component"):
        if component.get("Name") == "Horse":
            return component
    raise SystemExit("error: no Horse component in %s" % COMPONENT_TABLE)


def read_clothing_archetypes(root):
    """Every clothing node under the horse, mapped to the archetype it inherits."""
    archetypes = {}

    def walk(node, archetype):
        for child in node:
            if child.tag == "DerivedComponents":
                walk(child, archetype)
            elif child.tag in ("Component", "Clothing"):
                inherited = child.get("ArmorArchetypeId", archetype)
                if child.tag == "Clothing" and child.get("Name"):
                    archetypes[child.get("Name")] = inherited
                walk(child, inherited)

    walk(find_horse(root), None)
    return archetypes


def read_gear(tables, archetypes):
    gear = {}
    for entry in sorted(tables.namelist()):
        if not ITEM_TABLE_RE.match(entry):
            continue
        root = ET.fromstring(tables.read(entry))
        classes = root if root.tag == "ItemClasses" else root.find("ItemClasses")
        if classes is None:
            continue
        for item in classes:
            if item.tag != "Armor":
                continue
            slot = ARCHETYPE_SLOTS.get(archetypes.get(item.get("Clothing", ""), ""))
            if slot is None or not item.get("Id") or not item.get("Name"):
                continue
            gear.setdefault(item.get("Id").lower(), (item.get("Name"), slot))
    return sorted(((name, guid, slot) for guid, (name, slot) in gear.items()), key=lambda row: (SLOT_ORDER.index(row[2]), row[0].lower()))


def read_presets(tables, gear):
    """Every clothing preset whose items are all horse gear: (name, [item guids])."""
    known = {guid for _name, guid, _slot in gear}
    presets = {}
    for entry in sorted(tables.namelist()):
        if not CLOTHING_PRESET_RE.match(entry):
            continue
        for preset in ET.fromstring(tables.read(entry)).iter("clothing_preset"):
            name = preset.get("clothing_preset_name")
            guids = [(node.text or "").strip().lower() for node in preset.iter("Guid")]
            if name and guids and all(guid in known for guid in guids):
                presets.setdefault(name, guids)
    return sorted(presets.items(), key=lambda row: row[0].lower())


def read_common_group(tables, presets):
    """The clothing presets `equipment_horse_common` draws one of, in table order."""
    names = {name for name, _guids in presets}
    for group in ET.fromstring(tables.read(HORSE_EQUIPMENT_TABLE)).iter("InventoryPreset"):
        if group.get("Name") == COMMON_GROUP:
            children = [ref.get("Name") for ref in group.iter("ClothingPresetRef")]
            missing = [child for child in children if child not in names]
            if missing:
                raise SystemExit("error: %s names presets that are not all horse gear: %s" % (COMMON_GROUP, ", ".join(missing)))
            return children
    raise SystemExit("error: no %s in %s" % (COMMON_GROUP, HORSE_EQUIPMENT_TABLE))


def cpp_string(value):
    return '"%s"' % value.replace("\\", "\\\\").replace('"', '\\"')


def render(gear, presets, common):
    lines = [
        "// Generated by tools/generate_horse_gear_catalog.py from Data/Tables.pak.",
        "// Do not edit by hand.",
        "",
        '#include "shared/game/horse_gear_catalog.h"',
        "",
        "namespace KCDC::Shared::Game {",
        "    namespace {",
        "        // Constant arrays rather than std::vector initializer lists: at this",
        "        // size those compile into per-element dynamic initialisation.",
        "        constexpr HorseGearEntry kHorseGear[] = {",
    ]
    for name, guid, slot in gear:
        lines.append("            {%s, %s, HorseGearSlot::%s}," % (cpp_string(name), cpp_string(guid), slot))
    lines += [
        "        };",
        "",
        "        constexpr std::string_view kPresetItems[] = {",
    ]
    for _, guids in presets:
        lines.append("            %s," % ", ".join(cpp_string(guid) for guid in guids))
    lines += [
        "        };",
        "",
        "        constexpr HorseGearPresetEntry kHorseGearPresets[] = {",
    ]
    offset = 0
    for name, guids in presets:
        lines.append("            {%s, {kPresetItems + %d, %d}}," % (cpp_string(name), offset, len(guids)))
        offset += len(guids)
    lines += [
        "        };",
        "",
        "        constexpr std::string_view kCommonHorseGearPresets[] = {%s};" % ", ".join(cpp_string(name) for name in common),
        "    } // namespace",
        "",
        "    const std::span<const HorseGearEntry> g_HorseGear = kHorseGear;",
        "    const std::span<const HorseGearPresetEntry> g_HorseGearPresets = kHorseGearPresets;",
        "    const std::span<const std::string_view> g_CommonHorseGearPresets = kCommonHorseGearPresets;",
        "} // namespace KCDC::Shared::Game",
        "",
    ]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("game_dir", help="Kingdom Come: Deliverance II installation directory")
    parser.add_argument("-o", "--output", default=DEFAULT_OUTPUT, help="where to write the generated table")
    args = parser.parse_args()

    with zipfile.ZipFile(os.path.join(args.game_dir, TABLES_PAK)) as tables:
        archetypes = read_clothing_archetypes(ET.fromstring(tables.read(COMPONENT_TABLE)))
        gear = read_gear(tables, archetypes)
        presets = read_presets(tables, gear)
        common = read_common_group(tables, presets)

    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(render(gear, presets, common))

    counts = {slot: sum(1 for row in gear if row[2] == slot) for slot in SLOT_ORDER}
    print("Wrote %d pieces of horse gear to %s (%s), %d presets, %d common" % (len(gear), args.output, ", ".join("%s %d" % item for item in counts.items()), len(presets), len(common)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3
"""Compile authoritative item property constraints from shipped KCD2 tables.

Property rules and defaults are documented in docs/inventory-properties.md.
This data contains class constraints, not native randomized creation outcomes.
No runtime read of an installed game's files is required.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile

ITEM_TABLE = re.compile(r"^Libs/Tables/item/item(__[a-zA-Z_]+)?\.xml$", re.I)
ARMOR = {"Armor", "Helmet", "Hood", "QuickSlotContainer"}
WEAPONS = {"MeleeWeapon", "MissileWeapon"}
CONSUMABLES = {"Food", "Herb", "Poison", "Ointment"}
IMMUTABLE = {"AlchemyBase", "Ammo", "CraftingMaterial", "DiceBadge", "Die", "Document", "Key", "KeyRing", "Misc", "MiscItem", "Money", "NPCTool", "PickableItem"}
DIVISIBLE = {"CraftingMaterial", "Die", "Food", "Herb", "KeyRing", "Misc", "MiscItem", "Money", "Ointment", "Poison"}


def extract(pak):
    items = {}
    for name in sorted(pak.namelist()):
        if not ITEM_TABLE.fullmatch(name):
            continue
        parent = ET.fromstring(pak.read(name)).find("ItemClasses")
        if parent is None:
            raise ValueError(f"Missing ItemClasses in {name}")
        for item in parent:
            key = item.attrib["Id"]
            if key in items:
                raise ValueError(f"Duplicate item {key}")
            items[key] = item
    armor_types = {row.attrib["Name"]: row.get("CanHealthChange", "true") == "true" for row in ET.fromstring(pak.read("Libs/Tables/item/armor_type.xml")).iter("armor_type")}
    clothing = {}

    def visit(node, inherited=None):
        armor_type = node.get("ArmorType", inherited)
        if node.tag == "Clothing" and node.get("Name"):
            key = node.attrib["Name"]
            if key in clothing and clothing[key] != armor_type:
                raise ValueError(f"Conflicting Clothing type for {key}")
            clothing[key] = armor_type
        for child in node:
            visit(child, armor_type)

    visit(ET.fromstring(pak.read("Libs/Tables/Character/CharacterComponent.xml")))
    resolved = {}

    def resolve(key, visiting=()):
        if key in resolved:
            return resolved[key]
        if key in visiting:
            raise ValueError(f"ItemAlias cycle: {key}")
        item = items[key]
        kind = item.tag
        if kind == "ItemAlias":
            if any(name in item.attrib for name in ("MaxQuality", "Clothing", "IsBreakable", "CanHealthChange", "IsDivisible")):
                raise ValueError(f"Unsupported ItemAlias property override: {key}")
            value = resolve(item.attrib["SourceItemId"], visiting + (key,))[:4]
        elif kind in ARMOR:
            component = item.get("Clothing")
            if component and component not in clothing:
                raise ValueError(f"Unknown Clothing {component} for {key}")
            armor_type = clothing.get(component)
            if armor_type and armor_type not in armor_types:
                raise ValueError(f"Unknown ArmorType {armor_type} for {key}")
            mutable = armor_types.get(armor_type, True)
            value = (int(item.get("MaxQuality", "3")) if mutable else 1, mutable, True, False)
        elif kind in WEAPONS:
            value = (int(item.get("MaxQuality", "4")), True, True, item.attrib["IsBreakable"] == "true")
        elif kind in CONSUMABLES:
            value = (1, True, False, False)
        elif kind in IMMUTABLE:
            value = (1, False, kind == "Ammo", False)
        else:
            raise ValueError(f"Unsupported native category {kind}")
        if not 1 <= value[0] <= 4:
            raise ValueError(f"Invalid native maximum quality for {key}: {value}")
        # IsQuestItem is registered on both PlayerItem (+0xA8, RTTR
        # 0x180FC9FDC) and ItemAlias (RTTR 0x181557D10), so an alias answers
        # it for itself and does not inherit it. Every shipped alias authors
        # it; one that does not is rejected rather than given a default.
        if kind == "ItemAlias" and "IsQuestItem" not in item.attrib:
            raise ValueError(f"ItemAlias without IsQuestItem: {key}")
        if item.get("IsQuestItem", "false") not in ("true", "false"):
            raise ValueError(f"Unexpected IsQuestItem for {key}")
        # Native class slot 7 controls CreateItems' one-stack vs one-item loop.
        # Ammo returns true; divisible classes read IsDivisible (+0xB0), whose
        # constructor default is false. Other concrete classes return false.
        if item.get("IsDivisible", "false") not in ("true", "false"):
            raise ValueError(f"Unexpected IsDivisible for {key}")
        stackable = resolve(item.attrib["SourceItemId"], visiting + (key,))[5] if kind == "ItemAlias" else kind == "Ammo" or (kind in DIVISIBLE and item.get("IsDivisible", "false") == "true")
        value = (*value[:4], item.get("IsQuestItem", "false") == "true", stackable)
        resolved[key] = value
        return value

    return [(key, *resolve(key)) for key in sorted(items)]


def extract_repair_kits(pak):
    items = {item.attrib['Id']: item for name in pak.namelist() if ITEM_TABLE.fullmatch(name)
             for item in ET.fromstring(pak.read(name)).find('ItemClasses')}

    def is_kit(key, visiting=()):
        if key in visiting:
            raise ValueError(f'ItemAlias cycle: {key}')
        item = items[key]
        if item.tag == 'ItemAlias':
            return is_kit(item.attrib['SourceItemId'], visiting + (key,))
        return item.tag == 'Ointment' and item.get('Type') == '1'

    return sorted(key for key in items if is_kit(key))


def extract_sharpenable(pak):
    items = {item.attrib['Id']: item for name in pak.namelist() if ITEM_TABLE.fullmatch(name)
             for item in ET.fromstring(pak.read(name)).find('ItemClasses')}
    zones = {row.attrib['id']: int(row.get('sharpening_zone_count', '0')) for row in ET.fromstring(pak.read('Libs/Tables/item/weapon_class.xml')).iter('MeleeWeaponClass')}

    def sharpenable(key, visiting=()):
        if key in visiting:
            raise ValueError(f'ItemAlias cycle: {key}')
        item = items[key]
        if item.tag == 'ItemAlias':
            if 'Class' in item.attrib or 'SubClass' in item.attrib:
                raise ValueError(f'Unsupported ItemAlias weapon class override: {key}')
            return sharpenable(item.attrib['SourceItemId'], visiting + (key,))
        # The grindstone's selection (SharpeningSelectionFilter 0x181F280D0):
        # a melee weapon whose class has sharpening zones (+0x90), and not a
        # training or stub sub class (5, 6).
        return item.tag == 'MeleeWeapon' and zones.get(item.get('Class', '-1'), 0) > 0 and item.get('SubClass', '-1') not in ('5', '6')

    return sorted(key for key in items if sharpenable(key))


def generate(rows, destination, repair_kits=(), sharpenable=()):
    text = ['// Generated by tools/generate_inventory_properties.py; do not edit.', '#include "shared/game/inventory_property_classes.h"', '#include "shared/game/custom_item_registry.h"', '#include "shared/game/mod_item_tables.h"', '', '#include <algorithm>', '', 'namespace KCDC::Shared::Game {', '    namespace {',
            '        // Constant arrays rather than std::vector initializer lists: at this',
            '        // size those compile into per-element dynamic initialisation.',
            '        constexpr InventoryPropertyClass kInventoryPropertyClasses[] = {']
    for key, maximum, mutable, equippable, breakable, quest, stackable in rows:
        text.append('            {"%s", %d, %s, %s, %s, %s, %s},' % (key, maximum, str(mutable).lower(), str(equippable).lower(), str(breakable).lower(), str(quest).lower(), str(stackable).lower()))
    text += ['        };', '', '        constexpr std::string_view kRepairKitClasses[] = {'] + [f'            "{key}",' for key in repair_kits] + ['        };']
    text += ['', '        constexpr std::string_view kSharpenableClasses[] = {'] + [f'            "{key}",' for key in sharpenable] + ['        };', '    } // namespace', '']
    text += ['    const std::span<const InventoryPropertyClass> g_InventoryPropertyClasses = kInventoryPropertyClasses;',
             '    const std::span<const std::string_view> g_RepairKitClasses               = kRepairKitClasses;',
             '    const std::span<const std::string_view> g_SharpenableClasses             = kSharpenableClasses;', '']
    text += ['    const InventoryPropertyClass *FindShippedInventoryPropertyClass(std::string_view guid) {', '        const auto found = std::lower_bound(g_InventoryPropertyClasses.begin(), g_InventoryPropertyClasses.end(), guid, [](const InventoryPropertyClass &entry, std::string_view value) { return entry.guid < value; });', '        return found != g_InventoryPropertyClasses.end() && found->guid == guid ? &*found : nullptr;', '    }', '']
    text += ['    const InventoryPropertyClass *FindInventoryPropertyClass(std::string_view guid) {', '        if (const auto *custom = CustomItemRegistry::Get().Find(guid)) {', '            return &custom->properties;', '        }', '        if (const auto *shipped = FindShippedInventoryPropertyClass(guid)) {', '            return shipped;', '        }', '        return ModItemRegistry::Get().FindProperties(guid);', '    }']
    text += ['', '    bool IsRepairKitClass(std::string_view guid) {',
        '        return std::binary_search(g_RepairKitClasses.begin(), g_RepairKitClasses.end(), guid);', '    }']
    text += ['', '    bool IsSharpenableClass(std::string_view guid) {',
        '        return std::binary_search(g_SharpenableClasses.begin(), g_SharpenableClasses.end(), guid);', '    }']
    text += ['} // namespace KCDC::Shared::Game', '']
    Path(destination).write_text('\n'.join(text), encoding='utf-8', newline='\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game_directory', type=Path)
    parser.add_argument('--output', default='code/shared/generated/inventory_property_classes.cpp')
    args = parser.parse_args()
    with zipfile.ZipFile(args.game_directory / 'Data/Tables.pak') as pak:
        rows = extract(pak)
        repair_kits = extract_repair_kits(pak)
        sharpenable = extract_sharpenable(pak)
    generate(rows, args.output, repair_kits, sharpenable)
    print(f'Generated {len(rows)} authoritative property classes')

if __name__ == '__main__':
    main()

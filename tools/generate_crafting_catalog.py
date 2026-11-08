#!/usr/bin/env python3
"""Compile shipped crafting recipes and placed stations into the shared catalog.

Only the production recipe tables are input. Autotest recipes are excluded.
DLC 8 stays identifiable in the table and is never offered by runtime policy.
Game archives are read only; generated output lives in this repository.
"""

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import uuid
import xml.etree.ElementTree as ET
import zipfile

from generate_door_catalog import forward_of, parse_vector
from generate_gate_catalog import DEFAULT_GAME, entity_guid, zread


def quote(value):
    return json.dumps(str(value), ensure_ascii=True)


def generate(game):
    recipes, stations, books, tools, grindstones, preservers, prepared = [], [], [], [], [], [], []
    book_classes = set()
    material_rules, product_classes = {}, {}
    digest = hashlib.sha256()
    with zipfile.ZipFile(game / "Data/Tables.pak") as archive:
        for kind, path in (("Alchemy", "AlchemyRecipe.xml"), ("Smithing", "BlacksmithRecipes.xml")):
            data = zread(archive, "Libs/Tables/minigame/" + path)
            digest.update(path.encode() + data)
            for row in ET.fromstring(data)[0]:
                alchemy = kind == "Alchemy"
                name = row.get("UIName", "") if alchemy else row.find("UIName").get("StringName", row.find("UIName").get("Text", ""))
                ingredients = [(r.get("IngredientItemId" if alchemy else "ItemClassId"), int(r.get("Quantity" if alchemy else "Amount"))) for r in row.find("Ingredients")]
                products = [(r.get("ProductItemId" if alchemy else "ItemClassId"), int(r.get("Quality" if alchemy else "MinimalQuality")), int(r.get("MaxYield", "1"))) for r in row.find("Products")]
                if not ingredients or not products or any(n <= 0 for _, n in ingredients) or len({item for item, _ in ingredients}) != len(ingredients):
                    raise ValueError("Invalid recipe " + str(row.attrib))
                recipes.append((kind, row.get("RecipeId" if alchemy else "Id"), name, int(row.get("DlcId", "0")), int(row.get("MinSkillLevel", "0")), row.get("PerkId", ""), row.get("BaseMaterial", ""), ingredients, products))
        smithing_inputs = {item for row in recipes if row[0] == "Smithing" for item, _ in row[7]}
        smithing_outputs = {item for row in recipes if row[0] == "Smithing" for item, _, _ in row[8]}
        items = {}
        for name in sorted(archive.namelist()):
            if not re.fullmatch(r"Libs/Tables/item/item(?:__[^/]+)?\.xml", name.replace("\\", "/")):
                continue
            for row in ET.fromstring(zread(archive, name))[0]:
                if row.tag == "Document" and row.get("EntityScript") == "RecipesBook":
                    book_classes.add(row.get("Id"))
                if row.get("Id") in items:
                    raise ValueError("Duplicate item class " + row.get("Id"))
                items[row.get("Id")] = row
        for item in sorted(smithing_inputs | smithing_outputs):
            row = items.get(item)
            while row is not None and row.tag == "ItemAlias":
                if any(field in row.attrib for field in ("IsQuestItem", "IsDivisible", "Class")):
                    raise ValueError("Unverified smithing alias override: " + item)
                row = items.get(row.get("SourceItemId"))
            if row is None or row.tag not in ("CraftingMaterial", "MiscItem", "Armor", "MeleeWeapon"):
                raise ValueError("Unverified smithing item category: " + item)
            if any(row.get(field, "false") not in ("true", "false") for field in ("IsQuestItem", "IsDivisible")):
                raise ValueError("Unexpected smithing item flag: " + item)
            # ConsumeRecipeMaterials 0x1812C1950 skips IsQuestItem (+0xA8, RTTR
            # 0x180FC9FDC) and on failure debits only classes whose slot 7 is
            # true: IsDivisible (+0xB0, RTTR 0x1801324B7) for crafting
            # materials and misc items (0x181A81F20), constant false for armor
            # and melee weapons (0x180838AE0).
            quest = row.get("IsQuestItem", "false") == "true"
            if item in smithing_inputs:
                material_rules[item] = (quest, row.tag in ("CraftingMaterial", "MiscItem") and row.get("IsDivisible", "false") == "true")
            if item in smithing_outputs:
                # RPGMinigames slot 76 reads the authored MeleeWeapon Class.
                product_classes[item] = (row.tag, quest, int(row.get("Class")) if row.tag == "MeleeWeapon" else -1)
        digest.update(json.dumps([sorted(material_rules.items()), sorted(product_classes.items())]).encode())
        # What a drying rack, smokehouse or fire makes of an item: the Herb's
        # DriedItemId (C_DryHerbEffect::GetPreparedItemClassId 0x182CADD44)
        # or the Food's DriedItemId, SmokedItemId or CookedItemId
        # (C_PrepareFoodEffect's, 0x182CADD70). An alias takes its source's.
        fields = (("Drying", "DriedItemId"), ("Smoking", "SmokedItemId"), ("Cooking", "CookedItemId"))
        for item in sorted(items):
            row = items[item]
            if row.tag == "ItemAlias" and any(field in row.attrib for _, field in fields):
                raise ValueError("Unverified preparation alias override: " + item)
            while row is not None and row.tag == "ItemAlias":
                row = items.get(row.get("SourceItemId"))
            for kind, field in fields:
                product = row.get(field) if row is not None else None
                if not product:
                    continue
                if row.tag not in ("Herb", "Food") or (row.tag == "Herb" and kind != "Drying") or items.get(product) is None or items[product].tag != row.tag:
                    raise ValueError("Unverified preparation of " + item)
                prepared.append((item, kind, product, row.tag == "Herb"))
        digest.update(json.dumps(prepared).encode())
        # Drying racks and smokehouses are one smart object type, so_smokehouse;
        # its tree reads the holder's `isSmokehouse` script property.
        smokehouse = ET.fromstring(zread(archive, "Libs/Tables/ai/smartEntity/SmartEntity__so_smokehouse.xml")).find(".//SmartEntityTemplate").get("DatabaseId")
    for path in sorted((game / "Data/Levels").glob("*/level.pak")):
        with zipfile.ZipFile(path) as archive:
            for name in sorted(archive.namelist()):
                if not name.lower().endswith(".xml"):
                    continue
                data = zread(archive, name)
                if b'"AlchemyTable"' not in data and b'"Smithery"' not in data and b'"Grindstone"' not in data and smokehouse.encode() not in data:
                    continue
                entities = list(ET.fromstring(data).iter("Entity"))
                by_guid = {entity_guid(row.get("EntityGuid", "")): row for row in entities}
                # A prefab's own links name their targets by EntityId alone.
                by_id = {row.get("EntityId"): row for row in entities if row.get("EntityId")}
                for row in entities:
                    if row.get("EntityClass") == "Grindstone":
                        guid = entity_guid(row.get("EntityGuid", ""))
                        if not guid:
                            raise ValueError("Grindstone without stable GUID")
                        # C_Sharpening::SetGrindstone reads the wheel's
                        # Properties.fSpeedMultiplier, 1 by default in
                        # Grindstone.lua, and takes 0 outside (-10, 10).
                        properties = row.find("Properties")
                        multiplier = float(properties.get("fSpeedMultiplier", "1")) if properties is not None else 1.0
                        multiplier = multiplier if -10.0 < multiplier < 10.0 else 0.0
                        grindstones.append((path.parent.name, guid, *parse_vector(row.get("Pos"), 3, (0, 0, 0)), multiplier))
                        continue
                    properties = row.find("Properties")
                    if properties is not None and properties.get("guidSmartObjectType") == smokehouse:
                        guid = entity_guid(row.get("EntityGuid", ""))
                        script = properties.find("Script")
                        kind = {"isSmokehouse:true": "Smoking", "isSmokehouse:false": "Drying"}.get(script.get("Misc") if script is not None else None)
                        if not guid or kind is None:
                            raise ValueError("Unverified drying rack or smokehouse " + row.get("Name", ""))
                        preservers.append((f"{path.parent.name}:{guid:016x}", path.parent.name, kind, *parse_vector(row.get("Pos"), 3, (0, 0, 0))))
                        continue
                    if row.get("EntityClass") not in ("AlchemyTable", "Smithery"):
                        continue
                    guid = entity_guid(row.get("EntityGuid", ""))
                    if not guid:
                        raise ValueError("Station without stable GUID")
                    level = path.parent.name
                    kind = "Alchemy" if row.get("EntityClass") == "AlchemyTable" else "Smithing"
                    position = parse_vector(row.get("Pos"), 3, (0, 0, 0))
                    forward = forward_of(parse_vector(row.get("Rotate"), 4, (1, 0, 0, 0)))
                    stations.append((f"{level}:{guid:016x}", level, guid, kind, row.get("EditorLayer", ""), *position, *forward))
                    for link in row.findall("./EntityLinks/Link"):
                        target_guid = entity_guid(link.get("TargetGuid", ""))
                        target = by_guid.get(target_guid) if target_guid else by_id.get(link.get("TargetId"))
                        if target is None or target.get("EntityClass") != "ItemSlot":
                            continue
                        slot_guid = entity_guid(target.get("EntityGuid", ""))
                        if not slot_guid:
                            raise ValueError("Station item slot without stable GUID")
                        properties = target.find("Properties")
                        item = properties.get("guidItemClassId", "") if properties is not None else ""
                        # Every slot a smithery links is a tool the minigame
                        # hands the smith (C_BlacksmithingSlotToolAttachment
                        # resolves it by the link's name: tongs, hammer).
                        if kind == "Smithing" and item:
                            low, high = struct.unpack("<QQ", uuid.UUID(item).bytes_le)
                            tools.append((f"{level}:{guid:016x}", slot_guid, low, high))
                            continue
                        if item not in book_classes:
                            continue
                        low, high = struct.unpack("<QQ", uuid.UUID(item).bytes_le)
                        books.append((f"{level}:{guid:016x}", slot_guid, low, high))
    if len({(r[0], r[1]) for r in recipes}) != len(recipes) or len({s[0] for s in stations}) != len(stations) or len({s[0] for s in preservers}) != len(preservers) or len({(g[0], g[1]) for g in grindstones}) != len(grindstones):
        raise ValueError("Duplicate crafting identity")
    digest.update(json.dumps(sorted(stations)).encode())
    digest.update(json.dumps(sorted(books)).encode())
    digest.update(json.dumps(sorted(tools)).encode())
    digest.update(json.dumps(sorted(grindstones)).encode())
    digest.update(json.dumps(sorted(preservers)).encode())
    lines = ['#include "shared/game/crafting_catalog.h"', '#include "shared/game/dlc_catalog.h"', '', '// Generated by tools/generate_crafting_catalog.py. Do not edit by hand.', '', 'namespace KCDC::Shared::Game {', f'    const std::string_view g_CraftCatalogHash = "{digest.hexdigest()}";', '    const std::vector<CraftRecipe> g_CraftRecipes = {']
    for kind, key, name, dlc, skill, perk, base, ingredients, products in recipes:
        mats = ", ".join(f"{{{quote(item)}, {amount}}}" for item, amount in ingredients)
        outputs = ", ".join(f"{{{quote(item)}, {threshold}, {amount}}}" for item, threshold, amount in products)
        prefix = f'        {{CraftKind::{kind}, {quote(key)}, {quote(name)}, {dlc}, {skill}, {quote(perk)}, {quote(base)},'
        line = prefix + f' {{{mats}}}, {{{outputs}}}}},'
        if len(line) <= 280:
            lines.append(line)
        else:
            lines.append(prefix)
            for values, suffix in ((mats, ','), (outputs, '},')):
                line = '            {' + values + '}' + suffix
                if len(line) <= 280:
                    lines.append(line)
                else:
                    lines.append('            {')
                    entries = values.split('}, {')
                    for i, entry in enumerate(entries):
                        lines.append('                ' + ('{' if i else '') + entry + ('},' if i + 1 < len(entries) else ''))
                    lines.append('            }' + suffix)
    lines += ['    };', '', '    const std::vector<CraftStation> g_CraftStations = {']
    for key, level, guid, kind, layer, *coords in sorted(stations):
        numbers = ", ".join(f"{v:.6f}F" for v in coords)
        lines.append(f'        {{{quote(key)}, {quote(level)}, 0x{guid:016x}ULL, CraftKind::{kind}, {quote(layer)}, {numbers}}},')
    lines += ['    };', '', '    const std::vector<CraftStationSlot> g_CraftStationBooks = {']
    for station, slot, low, high in sorted(books):
        lines.append(f'        {{{quote(station)}, 0x{slot:016x}ULL, 0x{low:016x}ULL, 0x{high:016x}ULL}},')
    lines += ['    };', '', '    const std::vector<CraftStationSlot> g_CraftStationTools = {']
    for station, slot, low, high in sorted(tools):
        lines.append(f'        {{{quote(station)}, 0x{slot:016x}ULL, 0x{low:016x}ULL, 0x{high:016x}ULL}},')
    lines += ['    };', '', '    const std::vector<SharpeningStation> g_SharpeningStations = {']
    for level, guid, x, y, z, multiplier in sorted(grindstones):
        lines.append(f'        {{{quote(level)}, 0x{guid:016x}ULL, ' + ", ".join(f"{v:.6f}F" for v in (x, y, z, multiplier)) + '},')
    lines += ['    };', '', '    const std::vector<PreparationStation> g_PreparationStations = {']
    for key, level, kind, *coords in sorted(preservers):
        lines.append(f'        {{{quote(key)}, {quote(level)}, PreparationKind::{kind}, ' + ", ".join(f"{v:.6f}F" for v in coords) + '},')
    lines += ['    };', '', '    const std::vector<PreparedItem> g_PreparedItems = {']
    for item, kind, product, herb in prepared:
        lines.append(f'        {{{quote(item)}, PreparationKind::{kind}, {quote(product)}, {str(herb).lower()}}},')
    lines += ['    };', '', '    const std::vector<SmithingMaterialRule> g_SmithingMaterialRules = {']
    for item, (quest, divisible) in sorted(material_rules.items()):
        lines.append(f'        {{{quote(item)}, {str(quest).lower()}, {str(divisible).lower()}}},')
    lines += ['    };', '', '    const std::vector<SmithingProductClass> g_SmithingProductClasses = {']
    for item, (kind, quest, weapon_class) in sorted(product_classes.items()):
        lines.append(f'        {{{quote(item)}, SmithingProductKind::{kind}, {str(quest).lower()}, {weapon_class}}},')
    lines += ['    };', '', '    const CraftRecipe *FindCraftRecipe(CraftKind kind, std::string_view id) {', '        for (const auto &recipe : g_CraftRecipes) {', '            if (recipe.kind == kind && recipe.id == id) return &recipe;', '        }', '        return nullptr;', '    }', '', '    const CraftStation *FindCraftStation(std::string_view id) {', '        for (const auto &station : g_CraftStations) {', '            if (station.id == id) return &station;', '        }', '        return nullptr;', '    }', '', '    const SharpeningStation *FindSharpeningStation(std::string_view level, std::uint64_t guid) {', '        for (const auto &station : g_SharpeningStations) {', '            if (station.guid == guid && station.level == level) return &station;', '        }', '        return nullptr;', '    }', '', '    const PreparedItem *FindPreparedItem(PreparationKind kind, std::string_view item) {', '        for (const auto &prepared : g_PreparedItems) {', '            if (prepared.kind == kind && prepared.item == item) return &prepared;', '        }', '        return nullptr;', '    }', '', '    const SmithingMaterialRule *FindSmithingMaterialRule(std::string_view item) {', '        for (const auto &rule : g_SmithingMaterialRules) {', '            if (rule.item == item) return &rule;', '        }', '        return nullptr;', '    }', '', '    const SmithingProductClass *FindSmithingProductClass(std::string_view item) {', '        for (const auto &product : g_SmithingProductClasses) {', '            if (product.item == item) return &product;', '        }', '        return nullptr;', '    }', '', '    bool CraftDLCEnabled(std::uint8_t dlc, std::uint16_t required) {', '        if (dlc == 0 || dlc == 2 || dlc == 3 || dlc == 4) return true;', '        if (dlc == 6 || dlc == 8 || dlc > kMaxDLCId) return false;', '        return (required & DLCBit(dlc)) != 0;', '    }', '} // namespace KCDC::Shared::Game', '']
    output = Path(__file__).resolve().parents[1] / "code/shared/generated/crafting_catalog.cpp"
    output.write_text("\n".join(lines))
    print(f"Wrote {len(recipes)} recipes, {len(stations)} stations, {len(books)} books, {len(tools)} tools, {len(grindstones)} grindstones, {len(preservers)} drying racks and smokehouses and {len(prepared)} dried, smoked or cooked items; SHA256 {digest.hexdigest()}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path(DEFAULT_GAME))
    generate(parser.parse_args().game)

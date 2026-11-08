#!/usr/bin/env python3
"""Generate the KCDC melee pricing tables from an installed Kingdom Come: Deliverance II.

The server prices every blow one player lands on another the way the game's
own hit calculation does (`C_HitCalculation`, WHGame.dll 0x18072704C), so it
needs the tables that calculation reads:

- `Libs/Tables/item/item*.xml` -- every `MeleeWeapon` (its attack, the three
  per-type attack modifiers, the block defence, the stat requirements, class
  and subclass), and every `Armor`, `Helmet` and `Hood` (the three defences
  and the `Clothing` the piece is drawn as).
- `Libs/Tables/item/weapon_class.xml` -- the skill each weapon class trains.
- `Libs/Tables/Character/CharacterComponent.xml` -- the armour archetype of a
  `Clothing`, which is the nearest ancestor-or-self that names one. The
  descriptor reads it at its clothing record +0x128 (0x1805CDABC).
- `Libs/Tables/item/armor_archetype2body_subpart.xml`,
  `Libs/Tables/item/body_subpart.xml`, `Libs/Tables/item/body_part.xml` --
  which subparts an archetype covers, how much of the body that is
  (`BuildArchetypeCoverage` 0x181381BF0) against the race's whole body
  (`C_BodySubpartDatabase::OnLoaded` 0x180723A3C), and the damage coefficient
  of each subpart's limb.
- `Libs/Tables/combat/combat_attack_type.xml` and
  `Libs/Tables/combat/combat_damage_type_mapping.xml` -- the damage passes an
  attack runs, its stamina cost, and whether it may kill.
- `Libs/Tables/rpg/soul_archetype.xml` -- the two player archetypes' base
  body armour and unarmed attack.

Emitted offline as `code/shared/generated/combat_catalog.cpp` because the server
has no game module to read the tables from.

Usage:
    python tools/generate_combat_catalog.py <game directory>
"""

from __future__ import annotations

import argparse
import os
import re
import sys
import uuid
import xml.etree.ElementTree as ET
import zipfile

TABLES_PAK = os.path.join("Data", "Tables.pak")
ITEM_TABLE_RE = re.compile(r"^Libs/Tables/item/item(__[a-zA-Z_]+)?\.xml$", re.IGNORECASE)
WEAPON_CLASS_TABLE = "Libs/Tables/item/weapon_class.xml"
COMPONENT_TABLE = "Libs/Tables/Character/CharacterComponent.xml"
COVERAGE_TABLE = "Libs/Tables/item/armor_archetype2body_subpart.xml"
BODY_SUBPART_TABLE = "Libs/Tables/item/body_subpart.xml"
BODY_PART_TABLE = "Libs/Tables/item/body_part.xml"
ATTACK_TYPE_TABLE = "Libs/Tables/combat/combat_attack_type.xml"
DAMAGE_MAPPING_TABLE = "Libs/Tables/combat/combat_damage_type_mapping.xml"
SOUL_ARCHETYPE_TABLE = "Libs/Tables/rpg/soul_archetype.xml"

# `soul__player.xml` names 13; the female player swaps to 15.
PLAYER_ARCHETYPES = {13: "Hero", 15: "Hero_female"}
HUMAN_RACE = 0

DEFAULT_OUTPUT = os.path.join("code", "shared", "generated", "combat_catalog.cpp")


def item_class_halves(guid):
    """The two halves `ItemClassIdFromGuid` makes of a GUID: the wire's binary class."""
    raw = uuid.UUID(guid).bytes_le
    return int.from_bytes(raw[0:8], "little"), int.from_bytes(raw[8:16], "little")


def read_table(pak, path, row_tag):
    root = ET.fromstring(pak.read(path).decode("utf-8-sig"))
    return list(root.iter(row_tag))


def number(element, name, default=0.0):
    value = element.get(name)
    return float(value) if value not in (None, "") else default


def read_items(pak):
    weapons, armour = {}, {}
    for name in sorted(pak.namelist()):
        if not ITEM_TABLE_RE.match(name):
            continue
        root = ET.fromstring(pak.read(name).decode("utf-8-sig"))
        for classes in root.iter("ItemClasses"):
            for item in classes:
                guid = (item.get("Id") or "").lower()
                if not guid:
                    continue
                if item.tag == "MeleeWeapon":
                    weapons[guid] = {
                        "name": item.get("Name", ""),
                        "attack": number(item, "Attack"),
                        "mods": [number(item, "AttackModStab"), number(item, "AttackModSlash"), number(item, "AttackModSmash")],
                        "defense": number(item, "Defense"),
                        "str": int(number(item, "StrReq")),
                        "agi": int(number(item, "AgiReq")),
                        "cls": int(number(item, "Class", -1)),
                        "sub": int(number(item, "SubClass", -1)),
                    }
                elif item.tag in ("Armor", "Helmet", "Hood"):
                    armour[guid] = {
                        "name": item.get("Name", ""),
                        "defense": [number(item, "DefenseStab"), number(item, "DefenseSlash"), number(item, "DefenseSmash")],
                        "clothing": item.get("Clothing", ""),
                    }
    return weapons, armour


def read_archetypes(pak):
    """Clothing name to its armour archetype: the nearest ancestor-or-self `Clothing` that names one."""
    root = ET.fromstring(pak.read(COMPONENT_TABLE).decode("utf-8-sig"))
    archetypes = {}

    def walk(element, inherited):
        own = element.get("ArmorArchetypeId") if element.tag == "Clothing" else None
        current = int(own) if own not in (None, "") else inherited
        if element.tag == "Clothing" and element.get("Name"):
            archetypes[element.get("Name")] = current
        for child in element:
            walk(child, current)

    walk(root, -1)
    return archetypes


def read_rows(game_dir):
    with zipfile.ZipFile(os.path.join(game_dir, TABLES_PAK)) as pak:
        weapons, armour = read_items(pak)
        archetypes = read_archetypes(pak)
        classes = read_table(pak, WEAPON_CLASS_TABLE, "MeleeWeaponClass")
        coverage_rows = read_table(pak, COVERAGE_TABLE, "armor_archetype2body_subpart")
        subparts = read_table(pak, BODY_SUBPART_TABLE, "body_subpart")
        parts = read_table(pak, BODY_PART_TABLE, "body_part")
        attack_types = read_table(pak, ATTACK_TYPE_TABLE, "combat_attack_type")
        mappings = read_table(pak, DAMAGE_MAPPING_TABLE, "combat_damage_type_mapping")
        souls = read_table(pak, SOUL_ARCHETYPE_TABLE, "soul_archetype")

    part_rows = {int(p.get("body_part_id")): p for p in parts}
    subpart_rows = []
    importance = {}
    race_norm = 0.0
    for subpart in subparts:
        subpart_id = int(subpart.get("body_subpart_id"))
        part = part_rows.get(int(subpart.get("body_part_id")))
        weight = number(subpart, "body_subpart_importance")
        importance[subpart_id] = weight
        if part is not None and int(part.get("race_id", "0")) == HUMAN_RACE:
            race_norm += weight
        subpart_rows.append(
            {
                "id": subpart_id,
                # A subpart whose limb has no row prices at zero, as the
                # engine's zero-filled table does (0x180723CCC).
                "coef": number(part, "body_damage_coeff") if part is not None else 0.0,
                "head": part is not None and part.get("body_part_name") == "head",
            }
        )
    subpart_rows.sort(key=lambda row: row["id"])

    # Every row counts, all layers, unknown subparts skipped (0x181381BF0).
    coverage = {}
    covers = set()
    for row in coverage_rows:
        archetype = int(row.get("armor_archetype_id"))
        subpart = int(row.get("body_subpart_id"))
        covers.add((archetype, subpart))
        if subpart in importance:
            coverage[archetype] = coverage.get(archetype, 0.0) + importance[subpart]

    weapon_classes = sorted((int(c.get("id")), c.get("skill", "")) for c in classes)

    armour_rows = []
    for guid, row in armour.items():
        row["archetype"] = archetypes.get(row["clothing"], -1)
        armour_rows.append((guid, row))
    armour_rows.sort(key=lambda entry: item_class_halves(entry[0]))
    weapon_rows = sorted(weapons.items(), key=lambda entry: item_class_halves(entry[0]))

    attack_rows = sorted(
        (
            int(a.get("combat_attack_type_id")),
            number(a, "relative_stamina_cost", 1.0),
            int(number(a, "rpg_damage_type_id")),
            a.get("cannot_kill", "false").lower() == "true",
        )
        for a in attack_types
    )
    mapping_rows = [
        (int(m.get("combat_attack_type_id")), int(m.get("r_weapon_class_id")), int(m.get("r_weapon_sub_class_id")), int(m.get("rpg_damage_type_id"))) for m in mappings
    ]
    soul_rows = {int(s.get("soul_archetype_id")): s for s in souls}

    return {
        "weapons": weapon_rows,
        "armour": armour_rows,
        "classes": weapon_classes,
        "subparts": subpart_rows,
        "coverage": sorted(coverage.items()),
        "covers": sorted(covers),
        "race_norm": race_norm,
        "attacks": attack_rows,
        "mappings": mapping_rows,
        "souls": soul_rows,
    }


def cpp_float(value):
    text = repr(float(value))
    if "e" not in text and "." not in text:
        text += ".0"
    return text + "F"


def cpp_string(value):
    return '"%s"' % value.replace("\\", "\\\\").replace('"', '\\"')


def cpp_comment(name):
    """The item's name after its row, so a game patch's diff of the table reads."""
    return " // %s" % name if name else ""


def cpp_class(guid):
    low, high = item_class_halves(guid)
    return "{0x%016XULL, 0x%016XULL}" % (low, high)


def render(data):
    lines = [
        "// Generated by tools/generate_combat_catalog.py from Data/Tables.pak.",
        "// Do not edit by hand.",
        "",
        '#include "shared/game/combat_catalog.h"',
        "",
        "namespace KCDC::Shared::Game {",
    ]
    # (type, name, comment, rows). Constant arrays rather than std::vector
    # initializer lists: at this size those compile into per-element dynamic
    # initialisation. Each is exposed as a span.
    tables = [
        ("CombatWeaponEntry", "CombatWeapons", "`MeleeWeapon` rows of Libs/Tables/item/item*.xml, sorted by binary class.", [
            "{%s, %s, {%s, %s, %s}, %s, %d, %d, %d, %d},%s"
            % (cpp_class(guid), cpp_float(w["attack"]), cpp_float(w["mods"][0]), cpp_float(w["mods"][1]), cpp_float(w["mods"][2]), cpp_float(w["defense"]), w["str"], w["agi"], w["cls"], w["sub"], cpp_comment(w["name"]))
            for guid, w in data["weapons"]
        ]),
        ("CombatArmorEntry", "CombatArmor", "`Armor`, `Helmet` and `Hood` rows, sorted by binary class; the archetype through `Clothing`.", [
            "{%s, {%s, %s, %s}, %d},%s" % (cpp_class(guid), cpp_float(a["defense"][0]), cpp_float(a["defense"][1]), cpp_float(a["defense"][2]), a["archetype"], cpp_comment(a["name"]))
            for guid, a in data["armour"]
        ]),
        ("CombatWeaponClassEntry", "CombatWeaponClasses", "Libs/Tables/item/weapon_class.xml, sorted by id.", ["{%d, %s}," % (class_id, cpp_string(skill)) for class_id, skill in data["classes"]]),
        ("CombatSubpartEntry", "CombatSubparts", "Libs/Tables/item/body_subpart.xml, sorted by id, with its limb's coefficient.", [
            "{%d, %s, %s}," % (row["id"], cpp_float(row["coef"]), "true" if row["head"] else "false") for row in data["subparts"]
        ]),
        ("CombatArchetypeCoverage", "CombatArchetypeCoverage", "Body importance each armour archetype covers, summed over every row.", ["{%d, %s}," % (archetype, cpp_float(value)) for archetype, value in data["coverage"]]),
        ("CombatArchetypeSubpart", "CombatArchetypeSubparts", "(archetype, subpart) pairs of Libs/Tables/item/armor_archetype2body_subpart.xml, sorted.", ["{%d, %d}," % pair for pair in data["covers"]]),
        ("CombatAttackTypeEntry", "CombatAttackTypes", "Libs/Tables/combat/combat_attack_type.xml, sorted by id.", [
            "{%d, %s, %d, %s}," % (attack_id, cpp_float(cost), damage, "true" if cannot_kill else "false") for attack_id, cost, damage, cannot_kill in data["attacks"]
        ]),
        ("CombatDamageTypeMapping", "CombatDamageTypeMappings", "Libs/Tables/combat/combat_damage_type_mapping.xml, in table order.", ["{%d, %d, %d, %d}," % mapping for mapping in data["mappings"]]),
    ]
    lines.append("    namespace {")
    for index, (kind, name, comment, rows) in enumerate(tables):
        if index:
            lines.append("")
        lines += ["        // " + comment, "        constexpr %s k%s[] = {" % (kind, name)]
        lines += ["            " + row for row in rows]
        lines.append("        };")
    lines += ["    } // namespace", ""]
    lines += ["    const std::span<const %s> g_%s = k%s;" % (kind, name, name) for kind, name, _, _ in tables]
    lines += ["", "    // The player archetypes of Libs/Tables/rpg/soul_archetype.xml."]
    for archetype_id, name in sorted(PLAYER_ARCHETYPES.items()):
        soul = data["souls"][archetype_id]
        lines.append(
            "    const CombatSoulArchetype g_Combat%sArchetype = {%d, %s, %s};" % (name.replace("_female", "Female"), archetype_id, cpp_float(number(soul, "body_base_armor")), cpp_float(number(soul, "unarmed_attack_base")))
        )
    lines += ["", "    // Sum of `body_subpart_importance` over every human subpart.", "    const float g_CombatHumanBodyImportance = %s;" % cpp_float(data["race_norm"]), "} // namespace KCDC::Shared::Game", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("game_dir", help="Kingdom Come: Deliverance II installation directory")
    parser.add_argument("-o", "--output", default=DEFAULT_OUTPUT, help="where to write the generated table")
    args = parser.parse_args()

    data = read_rows(args.game_dir)
    if not data["weapons"] or not data["armour"] or not data["attacks"]:
        print("error: the combat tables carried no rows", file=sys.stderr)
        return 1
    # The engine's own norm for a human body; a patch that moves it moves
    # every armour weight, and has to be noticed here.
    if abs(data["race_norm"] - 42.0) > 1e-3:
        print("error: the human body importance is %s, not 42" % data["race_norm"], file=sys.stderr)
        return 1
    missing = [a["name"] for _, a in data["armour"] if a["archetype"] < 0 and any(a["defense"])]
    if missing:
        print("warning: %d defending armour pieces resolve to no archetype, e.g. %s" % (len(missing), missing[:5]), file=sys.stderr)

    with open(args.output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(render(data))
    print("Wrote %d weapons, %d armour pieces, %d archetypes to %s" % (len(data["weapons"]), len(data["armour"]), len(data["coverage"]), args.output))
    return 0


if __name__ == "__main__":
    sys.exit(main())

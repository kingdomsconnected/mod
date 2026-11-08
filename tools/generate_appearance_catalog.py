#!/usr/bin/env python3
"""Generate the KCDC character-appearance catalog from an installed KCD2.

A character's look is five nodes of the game's character-component tree at
`Libs/Tables/Character/CharacterComponent.xml`. Four of them carry identity
and are mined here; `Underwear` names a Clothing component and clothing
already replicates as equipped items. The tree has sixteen roots keyed by
race and gender, of which the two human ones matter.

Every named node under a kind's root is selectable, not just the leaves:
the engine resolves inheritance, so `x_hair_01` is a style with its authored
colour and `m_hair_001_black` is the same style recoloured. A node's parent
is emitted as its group, so a chooser can nest the colours under the style.

Beards are the one part a head restricts, and the table does not say so --
the file layout does. A skin element loads from `characters/` + the owning
component's `FilePath` + the element's `Model`, but every beard declares
`FilePath="humans/male/beard/"` with `Model="beards/m_beard_NN.skin"` and
that directory is in no pak: the skins live under each head instead, at
`head/<head>/beards/<beard>.skin`. So a beard renders on exactly the heads
it was modelled for, and asking for one on any other is a silent no-op.
That matrix is mined here too, by resolving every (head, beard) pair
against the pak file listing.

Usage:
    python tools/generate_appearance_catalog.py <game directory>
        [-o code/shared/generated/appearance_catalog.cpp]
"""

from __future__ import annotations

import argparse
import os
import sys
import xml.etree.ElementTree as ET
import zipfile

DATA_DIR = "Data"
TABLES_PAK = os.path.join(DATA_DIR, "Tables.pak")
COMPONENT_TABLE = "Libs/Tables/Character/CharacterComponent.xml"
CLOTHING_CONFIG_TABLE = "Libs/Tables/Character/ClothingConfig.xml"

# Every skin path the engine builds starts here.
ASSET_ROOT = "objects/characters/"

# In the native's part-index order. Underwear (index 4) is deliberately absent.
PARTS = ("Body", "Head", "Hair", "Beard")

# A kind root is found structurally -- the outermost node of its tag --
# because the names differ per gender ("Hair" against "F_Hair").
HUMAN_ROOTS = {"Male": "Male", "Female": "Female"}


class Component:
    """One named node of the tree, with what it takes to build its paths."""

    def __init__(self, name: str, group: str, file_path: str, models: list):
        self.name = name
        self.group = group
        # Inherited from the nearest ancestor that declares one.
        self.file_path = file_path
        # Every SkinElement `Model`, relative to `file_path`.
        self.models = models


def read_table(game_dir: str, path: str, element: str) -> ET.Element:
    pak = os.path.join(game_dir, TABLES_PAK)
    if not os.path.isfile(pak):
        raise SystemExit(f"{pak} not found; pass the game's install directory")

    with zipfile.ZipFile(pak) as tables:
        raw = tables.read(path).decode("utf-8", "replace")

    found = ET.fromstring(raw).find(element)
    if found is None:
        raise SystemExit(f"{path} has no {element} element")
    return found


def read_skin_index(game_dir: str) -> set:
    """Every `.skin` in the shipped paks, lowercased, for existence checks."""
    data = os.path.join(game_dir, DATA_DIR)
    if not os.path.isdir(data):
        raise SystemExit(f"{data} not found; pass the game's install directory")

    skins = set()
    for entry in sorted(os.listdir(data)):
        if not entry.lower().endswith(".pak"):
            continue
        with zipfile.ZipFile(os.path.join(data, entry)) as pak:
            for name in pak.namelist():
                lowered = name.lower()
                if lowered.endswith(".skin"):
                    skins.add(lowered)

    if not skins:
        raise SystemExit(f"no .skin files under {data}; the pak layout may have changed")
    return skins


def read_default_heads(game_dir: str) -> dict:
    """What a soul with no head of its own wears, per gender."""
    defaults = {}
    for config in read_table(game_dir, CLOTHING_CONFIG_TABLE, "ClothingConfigs"):
        gender = config.attrib.get("Gender") or ""
        head = config.attrib.get("DefaultHead") or ""
        if config.attrib.get("Race") != "Human" or gender not in HUMAN_ROOTS or not head:
            continue
        if defaults.setdefault(gender, head) != head:
            raise SystemExit(f"{CLOTHING_CONFIG_TABLE} gives {gender} two different DefaultHead values")

    missing = [gender for gender in HUMAN_ROOTS if gender not in defaults]
    if missing:
        raise SystemExit(f"{CLOTHING_CONFIG_TABLE} has no human DefaultHead for {', '.join(missing)}")
    return defaults


def collect(node: ET.Element, part: str, seen_root: bool, group: str, file_path: str, out: list):
    """Walk one root, emitting every node of `part` below that part's root."""
    for child in node:
        tag = child.tag
        if tag == "DerivedComponents":
            collect(child, part, seen_root, group, file_path, out)
            continue
        if tag in ("Elements", "Features"):
            continue

        name = child.attrib.get("Name") or ""
        if tag != part:
            # A structural Component node; keep descending without consuming
            # the root marker.
            collect(child, part, seen_root, group, file_path, out)
            continue

        inherited = child.attrib.get("FilePath", file_path)
        if not seen_root:
            # This is the kind's root: abstract, never selectable.
            collect(child, part, True, "", inherited, out)
            continue

        if name:
            models = [element.attrib["Model"] for element in child.findall("Elements/SkinElement") if element.attrib.get("Model")]
            out.append(Component(name, group, inherited, models))
        collect(child, part, True, name or group, inherited, out)


def read_components(components: ET.Element) -> dict:
    """The tree as `{(part, gender): [Component]}`, deduplicated by name."""
    found = {}
    for top in components:
        name = top.attrib.get("Name") or ""
        if top.attrib.get("Race") != "Human" or name not in HUMAN_ROOTS:
            continue
        gender = HUMAN_ROOTS[name]

        for part in PARTS:
            walked: list = []
            collect(top, part, False, "", "", walked)

            kept = []
            seen = set()
            for component in walked:
                if component.name in seen:
                    continue
                seen.add(component.name)
                kept.append(component)
            found[(part, gender)] = sorted(kept, key=lambda component: component.name)
    return found


def read_options(tree: dict):
    rows = []
    for (part, gender), components in tree.items():
        for component in components:
            rows.append((part, gender, component.name, component.group))

    # Stable, and grouped the way a chooser reads them.
    rows.sort(key=lambda row: (PARTS.index(row[0]), row[1], row[2]))
    return rows


def read_beard_sets(tree: dict, skins: set):
    """
    Resolve every (head, beard) pair against the shipped skins.

    Returns the distinct beard lists and one `(head, gender, index)` row per
    head; the 164 generic faces share one list, so it is stored once.
    """
    sets: list = []
    index_of: dict = {}
    heads: list = []

    for gender in sorted(HUMAN_ROOTS):
        beards = tree.get(("Beard", gender), [])
        for head in tree.get(("Head", gender), []):
            wearable = []
            for beard in beards:
                # `Model="dummy"` is the clean-shaven entry: no geometry to
                # find, and valid on every head.
                if beard.models and beard.models[0] == "dummy":
                    wearable.append(beard.name)
                    continue
                if not beard.models:
                    continue
                if ASSET_ROOT + (head.file_path.rstrip("/") + "/" + beard.models[0]).lower() in skins:
                    wearable.append(beard.name)

            key = tuple(wearable)
            if key not in index_of:
                index_of[key] = len(sets)
                sets.append(wearable)
            heads.append((head.name, gender, index_of[key]))

    return sets, heads


def escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def emit(rows, sets, heads, defaults, out_path: str):
    counts = {}
    for part, gender, _, _ in rows:
        counts[(part, gender)] = counts.get((part, gender), 0) + 1

    census = ", ".join(f"{gender} {part} {n}" for (part, gender), n in sorted(counts.items()))
    widest = max((len(entries) for entries in sets), default=0)
    bare = sum(1 for _, _, index in heads if not sets[index])

    lines = [
        "// Generated by tools/generate_appearance_catalog.py from an installed KCD2.",
        "// Do not edit by hand.",
        "",
        '#include "shared/game/appearance_catalog.h"',
        "",
        "#include <algorithm>",
        "",
        "namespace KCDC::Shared::Game {",
        f"    // {len(rows)} options: {census}.",
        "    const std::vector<AppearanceOption> g_AppearanceOptions = {",
    ]

    for part, gender, name, group in rows:
        lines.append(f'        {{"{escape(name)}", "{escape(group)}", AppearancePart::{part}, AppearanceGender::{gender}}},')

    lines += [
        "    };",
        "",
        f"    // {len(sets)} distinct lists over {len(heads)} heads; the widest carries {widest}.",
        "    const std::vector<std::vector<std::string_view>> g_AppearanceBeardSets = {",
    ]

    for entries in sets:
        if not entries:
            lines.append("        {},")
            continue
        lines.append("        {")
        for entry in entries:
            lines.append(f'            "{escape(entry)}",')
        lines.append("        },")

    lines += [
        "    };",
        "",
        f"    // {bare} of {len(heads)} heads wear no beard at all.",
        "    const std::vector<AppearanceHeadBeards> g_AppearanceHeadBeards = {",
    ]

    for name, gender, index in heads:
        lines.append(f'        {{"{escape(name)}", AppearanceGender::{gender}, {index}}},')

    lines += [
        "    };",
        "",
    ]

    for gender in sorted(HUMAN_ROOTS):
        lines.append(f'    const std::string_view kDefault{gender}Head = "{escape(defaults[gender])}";')

    lines += [
        "",
        "    const AppearanceOption *FindAppearanceOption(AppearancePart part, AppearanceGender gender, std::string_view name) {",
        "        if (name.empty()) {",
        "            return nullptr;",
        "        }",
        "        const auto match = std::find_if(g_AppearanceOptions.begin(), g_AppearanceOptions.end(), [&](const AppearanceOption &option) {",
        "            return option.part == part && option.gender == gender && option.name == name;",
        "        });",
        "        return match != g_AppearanceOptions.end() ? &*match : nullptr;",
        "    }",
        "",
        "    const std::vector<std::string_view> &AppearanceBeardsForHead(AppearanceGender gender, std::string_view head) {",
        "        static const std::vector<std::string_view> kNone;",
        "",
        "        const std::string_view wanted = head.empty() ? DefaultAppearanceHead(gender) : head;",
        "        const auto match = std::find_if(g_AppearanceHeadBeards.begin(), g_AppearanceHeadBeards.end(), [&](const AppearanceHeadBeards &entry) {",
        "            return entry.gender == gender && entry.head == wanted;",
        "        });",
        "        return match != g_AppearanceHeadBeards.end() ? g_AppearanceBeardSets[match->beardSet] : kNone;",
        "    }",
        "} // namespace KCDC::Shared::Game",
        "",
    ]

    with open(out_path, "w", encoding="utf-8", newline="\n") as handle:
        handle.write("\n".join(lines))

    print(f"{out_path}: {len(rows)} options ({census})")
    print(f"{out_path}: {len(sets)} beard sets over {len(heads)} heads, {bare} of them bare, widest {widest}")


def main(argv) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("game_dir", help="Kingdom Come: Deliverance II install directory")
    parser.add_argument("-o", "--output", default=os.path.join("code", "shared", "generated", "appearance_catalog.cpp"))
    args = parser.parse_args(argv)

    tree = read_components(read_table(args.game_dir, COMPONENT_TABLE, "CharacterComponents"))
    rows = read_options(tree)
    if not rows:
        raise SystemExit("no human appearance options found; the table layout may have changed")

    sets, heads = read_beard_sets(tree, read_skin_index(args.game_dir))
    if not any(sets):
        raise SystemExit("no beard resolved for any head; the asset layout may have changed")

    emit(rows, sets, heads, read_default_heads(args.game_dir), args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

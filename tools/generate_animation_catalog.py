"""Generate the KCD2 human animation and hand-prop catalogs from Tables.pak.

Animations are the rows of `Libs/Tables/action/actor_anim_action.xml` for the
two human actor classes: every fragment and tag combination the game itself
plays on a man or a woman, with the table's own duration, one-shot and
full-body columns, and the item manipulation each hand needs (`r_man_id`,
`l_man_id`, spelled through `ItemManipulationType.xml`).

Two things about an animation the table does not say are recovered from
elsewhere. Whether it also moves a second character comes from the body's
Mannequin controller definition: a fragment whose scopes for that tag
combination include `SlaveChar` animates whatever character the game enslaves
to the body's controller. The rest -- which fragments a pair of actors play
against each other, and which make up one in/loop/out sequence -- the game
never records: it is in the fragment names alone (`TiedUpOut_Master` against
`TiedUpOut_Slave`, `BakerMillIn`, `BakerMillLoop`, `BakerMillOut`), and is read
from them.

Props are the items in `Libs/Tables/item/item.xml` with a manipulation and a
`.cgf` model, plus the default in-hand transform `ItemManipulationType.xml`
gives that manipulation. A tool carries its `ManipulationType` on its own row;
a weapon row carries none, and takes it from its class's
`item_manipulation_type` in `weapon_class.xml`. Rows drawn with the
preproduction traffic cone or a `nodraw` primitive show nothing in the hand
and are left out.

Stance exits are the way back to standing from each `Stance` tag a request can
put a body in (`sittingNoTable`, `lyingGround`): the fragment of the table's
own transition rows that start in a sitting or lying stance, end standing and
carry that tag -- `StandUp` or `GetUp`. The `Stance` group comes from the
human tag dictionaries in Animations.pak.
"""

import argparse
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile


# actor_class_hash of the two human actor classes. Every other hash in the
# table is an animal, a horse or a scripted asset.
BODIES = (("1578932418", 1), ("-116224680", 2))

# Manipulation ids that mean "nothing in that hand" rather than an item.
EMPTY_HANDS = {"-1", "0", "1", "28"}

# The human tag dictionaries that declare the global `Stance` group.
STANCE_TAG_FILES = ("Animations/Mannequin/ADB/kcd_male_tags.xml", "Animations/Mannequin/ADB/wh_female_tags.xml")

# Where the item table's unfinished rows point their model.
PLACEHOLDER_MODELS = "objects/preproduction/"

# The Mannequin controller definition of each body in `BODIES`.
CONTROLLER_DEFS = {1: "Animations/Mannequin/ADB/kcd_male_controllerdefs.xml", 2: "Animations/Mannequin/ADB/wh_female_controllerdefs.xml"}

# A trailing role or side a sequence's fragments all carry after their phase
# (`TiedUpIn_Slave`, `Guest_EatingOut_Left`), which keeps it a sequence of its
# own.
SEQUENCE_ROLE = re.compile(r"(_Left|_Right|_?Slave|_?Master)$")
SEQUENCE_PHASES = (("In", "in"), ("Out", "out"), ("Loop", "loop"), ("Idle", "loop"))
SEQUENCE_VARIATION = re.compile(r"(_?VAR|_?Var\d*)$")

# E_StanceCategory: standing, and the two a stance exit leaves -- lying and
# sitting. Horse and cart stances have their own replication.
STANDING = "1"
EXIT_STANCES = {"2", "3"}


def cpp_string(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def cpp_float(value):
    return f"{value:.6f}F"


def parse_vector(text):
    if not text:
        return (0.0, 0.0, 0.0)
    parts = [float(part) for part in text.split(",")]
    if len(parts) != 3:
        raise ValueError(f"Expected three components: {text}")
    return tuple(parts)


def load_manipulations(pak):
    root = ET.fromstring(pak.read("Libs/Tables/item/ItemManipulationType.xml"))
    tags = {}
    transforms = {}
    for data in root.iter("ItemManipulationData"):
        tags[data.get("Id")] = data.get("Tag")
        # Only the variation without a SlotTag is the default one; the others
        # are for particular quest slots.
        for variation in data.findall("ItemManipulationVariation"):
            transform = variation.find("ItemTransformation")
            if variation.get("SlotTag") is None and transform is not None:
                transforms[data.get("Tag")] = (parse_vector(transform.get("Position")), parse_vector(transform.get("Rotation")))
    return tags, transforms


def hand(tags, identifier):
    return "" if identifier in EMPTY_HANDS else tags.get(identifier, "")


def split_tags(text):
    return frozenset(tag.lower() for tag in (text or "").split("+") if tag)


def load_slave_scopes(animations_pak):
    """Per body, each fragment's scope choices as (tags, drives SlaveChar), most specific first."""
    bodies = {}
    for body, path in CONTROLLER_DEFS.items():
        fragments = {}
        for definition in ET.fromstring(animations_pak.read(path)).find("FragmentDefs"):
            choices = [(frozenset(), definition.get("scopes", ""))]
            choices += [(split_tags(override.get("tags")) | split_tags(override.get("fragTags")), override.get("scopes", "")) for override in definition.findall("Override")]
            # Mannequin takes the most specific override whose tags the request
            # carries all of (`TTagSortedList::GetBestMatch`), the fragment's
            # own scopes when none fits.
            choices.sort(key=lambda choice: -len(choice[0]))
            fragments[definition.tag.lower()] = [(choiceTags, "SlaveChar" in scopes.split("+")) for choiceTags, scopes in choices]
        bodies[body] = fragments
    return bodies


def drives_slave(slave_scopes, bodies, fragment, tags):
    requested = split_tags(tags)
    for body, fragments in slave_scopes.items():
        if not bodies & body:
            continue
        for choiceTags, slave in fragments.get(fragment.lower(), []):
            if choiceTags <= requested:
                if slave:
                    return True
                break
    return False


def sequence_part(name):
    """The sequence a fragment would belong to and its phase in it, from its name alone."""
    role = SEQUENCE_ROLE.search(name)
    stem = name[:role.start()] if role else name
    phase = "loop"
    for suffix, candidate in SEQUENCE_PHASES:
        if stem.endswith(suffix):
            stem, phase = stem[:-len(suffix)], candidate
            break
    else:
        variation = SEQUENCE_VARIATION.search(stem)
        if variation:
            stem, phase = stem[:variation.start()], "variation"
    return stem.rstrip("_") + (role.group(1) if role else ""), phase


def find_sequences(names):
    """Fragment -> (sequence, phase) for every sequence with an in or an out and some other part."""
    sequences = {}
    for name in names:
        key, phase = sequence_part(name)
        if key:
            sequences.setdefault(key.lower(), []).append((name, key, phase))
    found = {}
    for members in sequences.values():
        phases = {phase for _, _, phase in members}
        if len(members) < 2 or len(phases) < 2 or not phases & {"in", "out"}:
            continue
        # The key as the loop spells it, else the in: a stem shared in
        # different cases reads the way the fragment it is named after does.
        key = next((key for _, key, phase in members if phase == "loop"), members[0][1])
        for name, _, phase in members:
            found[name] = (key, phase)
    return found


def find_partners(names):
    """Fragment -> the fragment the other actor of a Master/Slave pair plays."""
    partners = {}
    for name in sorted(names):
        index = name.rfind("Slave")
        if index < 0:
            continue
        candidates = (name[:index] + "Master" + name[index + 5:], name[:index].rstrip("_") + name[index + 5:])
        master = next((candidate for candidate in candidates if candidate in names and candidate != name), None)
        if master is not None and master not in partners:
            partners[name] = master
            partners[master] = name
    return partners


def generate_animations(pak, tags, slave_scopes):
    rows = ET.fromstring(pak.read("Libs/Tables/action/actor_anim_action.xml")).iter("actor_anim_action")
    bodies = dict(BODIES)
    entries = {}
    for row in rows:
        body = bodies.get(row.get("actor_class_hash"))
        if body is None:
            continue
        key = (row.get("mn_fragment_id"), row.get("mn_tags", ""))
        entry = entries.get(key)
        # Option zero is the variant every peer plays, so its row is the one
        # whose duration is reported when a combination has several.
        preferred = row.get("mn_option_index") == "0"
        if entry is None or (preferred and not entry["preferred"]):
            bodyMask = body | (entry["bodies"] if entry else 0)
            entries[key] = {
                "fragment": key[0],
                "tags": key[1],
                "duration": float(row.get("animation_duration")),
                "oneShot": row.get("isOneShot") == "true",
                "fullBody": row.get("isfullbody") == "true",
                "rightHand": hand(tags, row.get("r_man_id")),
                "leftHand": hand(tags, row.get("l_man_id")),
                "bodies": bodyMask,
                "preferred": preferred,
            }
        else:
            entry["bodies"] |= body
    names = {entry["fragment"] for entry in entries.values()}
    sequences = find_sequences(names)
    partners = find_partners(names)
    for entry in entries.values():
        entry["sequence"], entry["phase"] = sequences.get(entry["fragment"], ("", ""))
        entry["partner"] = partners.get(entry["fragment"], "")
        entry["slaveBody"] = drives_slave(slave_scopes, entry["bodies"], entry["fragment"], entry["tags"])
    return sorted(entries.values(), key=lambda entry: (entry["fragment"].lower(), entry["tags"].lower()))


def load_weapon_manipulations(pak):
    text = pak.read("Libs/Tables/item/weapon_class.xml").decode("utf-8", errors="replace")
    manipulations = {}
    for match in re.finditer(r"<(?:Melee|Missile)WeaponClass ([^>]*)>", text):
        attributes = dict(re.findall(r'(\w+)="([^"]*)"', match.group(1)))
        manipulations[attributes["id"]] = attributes["item_manipulation_type"]
    return manipulations


def generate_props(pak, tags, transforms):
    text = pak.read("Libs/Tables/item/item.xml").decode("utf-8", errors="replace")
    weapons = load_weapon_manipulations(pak)
    props = {}
    for match in re.finditer(r"<(\w+) ([^>]*)>", text):
        attributes = dict(re.findall(r'(\w+)="([^"]*)"', match.group(2)))
        # A weapon row names its class, and the class carries the manipulation.
        if match.group(1) in ("MeleeWeapon", "MissileWeapon"):
            manipulation = hand(tags, weapons.get(attributes.get("Class", ""), "-1"))
        elif "ManipulationType" in attributes:
            manipulation = hand(tags, attributes["ManipulationType"])
        else:
            continue
        model = attributes.get("Model", "").replace("\\", "/").lower()
        name = attributes.get("Name", "")
        if not manipulation or not model or not name or name in props:
            continue
        if not model.endswith(".cgf"):
            if "." in model.rsplit("/", 1)[-1]:
                continue  # a .cdf is an animated asset, not a static prop
            model += ".cgf"
        if not model.startswith("objects/"):
            model = "objects/" + model
        if model.startswith(PLACEHOLDER_MODELS) or "nodraw" in model:
            continue  # a stand-in traffic cone or an invisible primitive draws nothing to hold
        position, rotation = transforms.get(manipulation, ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0)))
        props[name] = {"name": name, "model": model, "manipulation": manipulation, "position": position, "rotation": rotation}
    return sorted(props.values(), key=lambda prop: prop["name"].lower())


def load_stance_tags(animations_pak):
    names = set()
    for path in STANCE_TAG_FILES:
        root = ET.fromstring(animations_pak.read(path))
        for group in root.iter("Group"):
            if group.get("name") == "Stance":
                names.update(tag.get("name") for tag in group.iter("Tag"))
    if not names:
        raise SystemExit("No Stance tag group in the human tag dictionaries")
    return names


def generate_stance_exits(pak, stance_tags):
    bodies = dict(BODIES)
    counts = {}
    for row in ET.fromstring(pak.read("Libs/Tables/action/actor_anim_action.xml")).iter("actor_anim_action"):
        if row.get("actor_class_hash") not in bodies:
            continue
        if row.get("actor_start_stance_id") not in EXIT_STANCES or row.get("actor_end_stance_id") != STANDING:
            continue
        for tag in set(filter(None, row.get("mn_tags", "").split("+"))) & stance_tags:
            fragments = counts.setdefault(tag, {})
            fragments[row.get("mn_fragment_id")] = fragments.get(row.get("mn_fragment_id"), 0) + 1
    # The fragment most of a tag's transitions use: the generic one, not a
    # quest's scripted variant.
    exits = [{"tag": tag, "fragment": max(sorted(fragments), key=lambda name: fragments[name])} for tag, fragments in counts.items()]
    return sorted(exits, key=lambda entry: entry["tag"].lower())


def generate(game):
    with zipfile.ZipFile(game / "Data/Tables.pak") as pak, zipfile.ZipFile(game / "Data/Animations.pak") as animations_pak:
        tags, transforms = load_manipulations(pak)
        animations = generate_animations(pak, tags, load_slave_scopes(animations_pak))
        props = generate_props(pak, tags, transforms)
        exits = generate_stance_exits(pak, load_stance_tags(animations_pak))
    lines = [
        "// Generated by tools/generate_animation_catalog.py from Tables.pak.",
        '#include "shared/game/animation_catalog.h"', "", "namespace KCDC::Shared::Game {",
        "    namespace {",
        "        // Constant arrays rather than std::vector initializer lists: at this",
        "        // size those compile into per-element dynamic initialisation.",
        "        constexpr AnimationEntry kAnimations[] = {",
    ]
    for entry in animations:
        lines.append(
            f"            {{{cpp_string(entry['fragment'])}, {cpp_string(entry['tags'])}, {cpp_float(entry['duration'])}, "
            f"{'true' if entry['oneShot'] else 'false'}, {'true' if entry['fullBody'] else 'false'}, "
            f"{cpp_string(entry['rightHand'])}, {cpp_string(entry['leftHand'])}, {entry['bodies']}, "
            f"{cpp_string(entry['sequence'])}, {cpp_string(entry['phase'])}, {cpp_string(entry['partner'])}, {'true' if entry['slaveBody'] else 'false'}}},")
    lines += ["        };", "", "        constexpr AnimationPropEntry kAnimationProps[] = {"]
    for prop in props:
        position = ", ".join(cpp_float(value) for value in prop["position"])
        rotation = ", ".join(cpp_float(value) for value in prop["rotation"])
        lines.append(f"            {{{cpp_string(prop['name'])}, {cpp_string(prop['model'])}, {cpp_string(prop['manipulation'])}, {{{position}}}, {{{rotation}}}}},")
    lines += ["        };", "", "        constexpr AnimationStanceExitEntry kAnimationStanceExits[] = {"]
    for entry in exits:
        lines.append(f"            {{{cpp_string(entry['tag'])}, {cpp_string(entry['fragment'])}}},")
    lines += [
        "        };",
        "    } // namespace",
        "",
        "    const std::span<const AnimationEntry> g_Animations                     = kAnimations;",
        "    const std::span<const AnimationPropEntry> g_AnimationProps             = kAnimationProps;",
        "    const std::span<const AnimationStanceExitEntry> g_AnimationStanceExits = kAnimationStanceExits;",
        "} // namespace KCDC::Shared::Game",
        "",
    ]
    target = Path(__file__).resolve().parents[1] / "code/shared/generated/animation_catalog.cpp"
    target.write_text("\n".join(lines), encoding="utf-8")
    print(f"Generated {len(animations)} animations, {len(props)} props and {len(exits)} stance exits: {target}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    generate(parser.parse_args().game)

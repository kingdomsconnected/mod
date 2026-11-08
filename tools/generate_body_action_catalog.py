"""Generate the KCD2 body action catalog from Tables.pak.

A body action is an animation a player's body plays that other players have
to see and that no other replication already carries:

- **Stance transitions** -- `SitDown`, `StandUp`, `LieDown`, `GetUp` and their
  variants: every `actor_anim_action` row for the two human classes whose
  start and end stance differ and are both standing, lying or sitting
  (`E_StanceCategory` 1-3). Cart, horse, crouch and kneel changes have their
  own replication.
- **Interactions** -- the interaction fragments the game resolves for every
  actor in `ResolveItemHandlingFragmentIds` (0x180D81378), minus the ones
  another feature replicates or that only make sense with something else
  replicated alongside (see `EXCLUDED`).
- **Trigger animations** -- every `sAction` a level's action trigger plays on
  the player as `esActionType="Animation"`: `ActionTrigger.lua` hands it to
  `PlayerStateHandler.PlayAnimationAction`, which queues it like any other
  interaction. Eating from a pot, drinking from a barrel, washing, praying.
  Paired ones (`JoinedAnimation` anywhere) need their partner and are left out.
- **Smart object animations** -- every `AnimationAction` a world smart
  object's `player_*` behavior (`Scripts.pak`, `AI/world/`) plays on the
  player: drying at a rack and smoking at a smokehouse (`so_smokehouse`),
  cooking at a fire (`so_fireplace`). The behavior tree node (0x180AA2E40)
  builds the same NPC state animation action as `PlayAnimationAction` does
  (0x180869FE8), so it is queued like the trigger animations.

`looped` comes from `anim_fragment.xml`: a fragment with any looping variant
for a human class has to be stopped on receivers when the owner's ends.
"""

import argparse
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile
import zlib

from generate_gate_catalog import zread


# actor_class_hash of the two human actor classes.
BODIES = {"1578932418", "-116224680"}

# E_StanceCategory: standing, lying, sitting.
STANCES = {"1", "2", "3"}

# The game's own interaction fragments, in `ResolveItemHandlingFragmentIds`'
# order, that are replicated as body actions.
INTERACTIONS = [
    "Picking", "Placing", "PlacingFromInventory", "PickFrom", "PlaceTo", "Drop",
    "PickingHerbs", "Digging",
    "SaddleBondingPat", "SaddleBondingCaress", "StandBondingPat", "StandBondingCaress",
    "ShortButchering",
    "DecoyDraw", "DecoyPlace", "DecoyThrow",
]

# The rest of that list, and why each is not a body action.
EXCLUDED = {
    "Mount": "horse", "Dismount": "horse", "HorseRear": "horse", "ForcedRearing": "horse", "HorseFastStop": "horse", "Charge": "horse",
    "LadderClimb": "ladder", "LadderGetOn": "ladder", "LadderGetOff": "ladder",
    "DrawWeapon": "hand items", "DrawWeaponFull": "hand items", "HolsterWeapon": "hand items", "DrawFromInventory": "hand items",
    "HolsterToInventory": "hand items", "HoldItemLeft": "hand items", "HoldItemRight": "hand items",
    "HoldItemLeftBlendOut": "hand items", "HoldItemRightBlendOut": "hand items", "SwapWeapon": "hand items",
    "ledgeGrab": "ledge",
    "MoveToIdle": "locomotion", "IdleToMove": "locomotion",
    "DogPetting": "the dog praise, a paired animation replayed through the dog",
    "CorpseGrab": "needs the carried body", "CorpsePut": "needs the carried body",
    "CarryItemPickup": "needs the carried item", "CarryItemPlace": "needs the carried item",
    "Sleep": "the sleep system is disabled",
    "WallLookIn": "the battlement lean", "WallLook": "the battlement lean", "WallLookOut": "the battlement lean",
}


# The base levels whose triggers are mined, as the other level catalogs read them.
LEVELS = ("klaster", "kutnohorsko", "trosecko")


def trigger_actions(game):
    """The fragments level triggers play on the player as a one-shot `Animation`."""
    single, joined = set(), set()
    for level in LEVELS:
        with zipfile.ZipFile(game / "Data/Levels" / level / "level.pak") as archive:
            for member in archive.namelist():
                if not member.lower().endswith(".xml"):
                    continue
                blob = zread(archive, member)
                if b"esActionType" not in blob:
                    continue
                for properties in ET.fromstring(blob).iter("Properties"):
                    for key in ("Click", "Hold"):
                        action = properties.find(key)
                        name = action.get("sAction") if action is not None else None
                        if not name:
                            continue
                        if action.get("esActionType") == "Animation":
                            single.add(name)
                        elif action.get("esActionType") == "JoinedAnimation":
                            joined.add(name)
    return single - joined


def smart_object_actions(game):
    """The fragments a world smart object's player behavior plays on the player."""
    fragments = set()
    with zipfile.ZipFile(game / "Data/Scripts.pak") as archive:
        for member in archive.namelist():
            if not (member.startswith("AI/world/") and member.endswith(".xml")):
                continue
            for tree in ET.fromstring(zread(archive, member)).iter("BehaviorTree"):
                if tree.get("name", "").startswith("player_"):
                    # The node's fragment is a quoted expression: 'Drying'.
                    fragments |= {node.get("fragment").strip("'") for node in tree.find("Root").iter("AnimationAction")}
    return fragments


def generate(game):
    scripted = trigger_actions(game) | smart_object_actions(game)
    with zipfile.ZipFile(game / "Data/Tables.pak") as pak:
        fragments = set(INTERACTIONS) | scripted
        for row in ET.fromstring(pak.read("Libs/Tables/action/actor_anim_action.xml")).iter("actor_anim_action"):
            start, end = row.get("actor_start_stance_id"), row.get("actor_end_stance_id")
            if row.get("actor_class_hash") in BODIES and start != end and start in STANCES and end in STANCES:
                fragments.add(row.get("mn_fragment_id"))

        present, looped = set(), set()
        for row in ET.fromstring(pak.read("Libs/Tables/animation/anim_fragment.xml")).iter("anim_fragment"):
            name = row.get("mn_fragment_id")
            if row.get("actor_class_hash") in BODIES and name in fragments:
                present.add(name)
                if row.get("looped") == "true":
                    looped.add(name)

    missing = sorted(set(INTERACTIONS) - present)
    if missing:
        raise SystemExit(f"Interaction fragments the human classes do not have: {missing}")
    # A trigger or a smart object can name a fragment only another body class
    # has; such a one never plays on a player and is not sent.
    fragments = (fragments - scripted) | (scripted & present)

    # Mannequin keys a fragment by the CRC-32 of its lowercase name.
    entries = sorted((zlib.crc32(name.lower().encode("ascii")) & 0xFFFFFFFF, name) for name in fragments)
    lines = [
        "// Generated by tools/generate_body_action_catalog.py from Tables.pak, the base levels and Scripts.pak.",
        '#include "shared/game/body_action_catalog.h"', "", "namespace KCDC::Shared::Game {",
        "    const std::vector<BodyActionEntry> g_BodyActions = {",
    ]
    for crc, name in entries:
        lines.append(f'        {{0x{crc:08X}U, "{name}", {"true" if name in looped else "false"}}},')
    lines += ["    };", "} // namespace KCDC::Shared::Game", ""]
    target = Path(__file__).resolve().parents[1] / "code/shared/generated/body_action_catalog.cpp"
    target.write_text("\n".join(lines), encoding="utf-8")
    print(f"Generated {len(entries)} body actions ({len(looped)} looped): {target}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    generate(parser.parse_args().game)

#!/usr/bin/env python3
"""Mines the game's siege engines into code/shared/generated/siege_catalog.cpp.

KCD2 has no siege-engine gameplay. The trebuchet is a skinned character
(`trebuchet.chr`) that only cutscenes animate, and the cannon is a static
mesh. Their stones are separate `GeomEntity` objects a TrackView sequence
flies. So nothing in the running game can be asked how long a clip runs or
where the stone leaves the sling. This walks the paks for it:

  1. the trebuchet's chrparams and DBA, for its four clips and their lengths
  2. the `projectile` bone's track in the fire clip -- the stone's path while
     it is still in the sling, and the key at which it is let go
  3. the crew's `Trebuchet` Mannequin events, for when the crew lays the
     stone in the sling during the load
  4. each mesh's own bounding box, for the projectiles' radii and the
     cannon's front face

The values that cannot be mined -- which sounds and effects to play, the
cannon's muzzle speed and reload, the default damage -- are declared below,
each with the reason it is that value.

Usage:
    python tools/generate_siege_catalog.py [--game <install dir>]
"""

import argparse
import math
import os
import re
import struct
import sys
import zlib

import generate_gate_catalog as gates

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(REPO_ROOT, "code", "shared", "generated", "siege_catalog.cpp")

TREBUCHET = "objects/characters/assets/trebuchet/trebuchet.chr"
TREBUCHET_STONE = "objects/characters/assets/trebuchet/trebuchet_stone.cgf"
CANNON = "objects/manmade/task_specific_props/combat/cannon.cgf"
CANNON_BALL = "objects/manmade/task_specific_props/combat/cannon_ammo.cgf"

# The clips `trebuchet.dba` ships besides two cutscene takes. `default` holds
# the arm down, `pull_back` winches it down to cocked, `load` holds it cocked
# while the crew lays the stone, `fire` lets it go and swings out.
TREBUCHET_CLIPS = {
    "idle": "quest_trebuchet_default",
    "draw": "quest_trebuchet_pull_back",
    "load": "quest_trebuchet_load",
    "fire": "quest_trebuchet_fire",
}

# The bone the cutscenes fly the stone along. Its parent is the root, so its
# track is in the model's own space.
PROJECTILE_BONE = "projectile"

# Sling motion turns the stone's direction of travel through about 90
# degrees in a third of a second; once it is loose the direction changes by
# a fraction of a degree a key. The first key under this is the release.
RELEASE_TURN_DEGREES = 1.0

# Metres a second under which the stone is taken to be lying in the sling.
MOVING_SPEED = 5.0

# The sounds. `c_trebuchet_stone_shoot` and `c_trebuchet_stone_impact` are
# declared in `Libs/GameAudio/combat.xml` with no FMOD event behind them, so
# they play nothing; the cutscene's own impact (`q_M50_trebuchet_impact`) and
# the Malesov cannon's shot are the sounds the game actually has.
TREBUCHET_RELEASE_SOUND = ""
TREBUCHET_IMPACT_SOUND = "q_M50_trebuchet_impact"
CANNON_RELEASE_SOUND = "q_M44b_cannon_shot"

# How far each sound carries, in metres. Both triggers were authored for a
# single set piece: `distance_culling="65535"` and an FMOD event with no
# distance attenuation, so as shipped they play at full volume at any range.
# The client hands these to the audio system as the triggers load, which
# culls past the range and fades the volume out toward it.
CANNON_RELEASE_SOUND_RANGE = 300.0
IMPACT_SOUND_RANGE = 150.0

# The particle effects, by their names in the vfx catalog.
TREBUCHET_IMPACT_EFFECT = "WH_Particels.other.trebuchet_hit"
CANNON_RELEASE_EFFECT = "cinematics.arrows_and_firearms.cannon"

# The cannon is a static mesh with no clips, so its cycle is ours: a crew
# reload of about the length the game's own cannon cutscene gives it, and
# long enough after the shot for its smoke to clear.
CANNON_LOAD_MS = 12000
CANNON_FIRE_MS = 2500

# A bombard of this size threw its ball at a few tens of metres a second, so
# the shot is flat and the range comes from the elevation, as it did.
CANNON_MUZZLE_SPEED = 80.0
CANNON_MAX_ELEVATION = 30.0

# How far the trebuchet can be made to throw. The mined release angle with
# the stone's speed solved for the target; the bounds are the throws a
# counterweight engine of this size managed.
TREBUCHET_MIN_RANGE = 40.0
TREBUCHET_MAX_RANGE = 350.0
CANNON_MIN_RANGE = 10.0
CANNON_MAX_RANGE = 600.0

# How far an engine throws when it is fired without a target -- a player
# pressing its native prompt fires along their facing this far. Well inside
# each engine's range, at a distance a crew would have laid it for.
TREBUCHET_DEFAULT_RANGE = 150.0
CANNON_DEFAULT_RANGE = 120.0

# Defaults a script can change per engine: health taken at the point of
# impact (a player has 100) and how far from it anything is hurt.
TREBUCHET_DAMAGE = 150.0
TREBUCHET_DAMAGE_RADIUS = 6.0
CANNON_DAMAGE = 120.0
CANNON_DAMAGE_RADIUS = 4.0

# A cannon's barrel is laid by raising the gun about the back end of its
# bed, which stays on the ground: the pivot is the mesh's rearmost point
# along the engine's forward, at the bottom of its bounds. A trebuchet
# throws at its one mined angle and has no pivot.
CANNON_PIVOT_LIFT = 0.05

# How far a shot kicks the cannon back along its axis, in metres, before it
# eases home -- a gun of this size on a sledge bed rolled back a hand's
# breadth or two.
CANNON_RECOIL = 0.35

# Where the gunner's match meets the powder, in the engine's frame: the top
# of the barrel above the breech. The fuse's spark and fizz play there as
# the shot starts.
CANNON_TOUCH_HOLE = (0.0, -0.55, 0.95)
CANNON_FUSE_EFFECT = "firearms.hand_cannon.topParticleEmit"
CANNON_FUSE_SOUND = "c_f_gunpowder_flash_burn"

# The Malesov assault's ball passing overhead, played where a ball flies by
# within `WHIZ_DISTANCE` metres of the listener.
CANNON_WHIZ_SOUND = "q_M44b_cannon_ball_whiz_by"

# The cannon's own impact: the cutscene trebuchet boom read wrong on a
# ball, so it throws up dust with the Nebakov cinematic's impact instead.
CANNON_IMPACT_SOUND = "cin_m1140t_SFX_25_impact_boom"
CANNON_IMPACT_EFFECT = "WH_Particels.other.explosion_dust"

# What a wrecked engine looks like. The game ships `cannon_broken.cgf`; it
# has no broken trebuchet, so a wrecked one leans and settles into the
# ground instead. Both throw up the cinematic's dust and debris as they go.
CANNON_BROKEN = "objects/manmade/task_specific_props/combat/cannon_broken.cgf"
TREBUCHET_WRECK_TILT = (5.7, 3.4)
TREBUCHET_WRECK_SINK = 0.5
WRECK_EFFECT = "cinematics.explosion.dust_explosion"
WRECK_SOUND = "cin_m1140t_SFX_40_debris"

# How far each of these carries, for the same reason as the impact: the
# cinematic and quest events have no distance attenuation of their own.
FUSE_SOUND_RANGE = 40.0
WHIZ_SOUND_RANGE = 40.0
WRECK_SOUND_RANGE = 200.0

# The local player's camera shakes near a blast: its strength right at the
# point (the game's `CameraShake` amount) and how far it reaches, fading
# with the square of the distance. The muzzle shakes the gun crew too.
TREBUCHET_IMPACT_SHAKE = (4.0, 90.0)

# The trebuchet's two winch places, where the crew's pull clips stand a
# man to work the rear winch: (x, y, z) in the engine's frame, the way he
# faces in degrees off the engine's own, and the `Trebuchet` fragment's tags
# for that place (`kcd_male_database.adb`). The skeleton has no winch bones
# and the clips align by a procedural layer that needs a target the mod
# does not give it, so the two places are declared: the ends of the winch
# drum, behind the frame. A man there plays his place's clip for as long as
# the arm is winched down.
TREBUCHET_CREW = [
    ((1.38, -6.56, 0.0), 10.1, "trebuchetPullA+trebuchet"),
    ((-1.08, -7.12, 0.0), -53.1, "trebuchetPullB"),
]
CREW_FRAGMENT = "Trebuchet"
# Crew places per engine: `SiegeEngineEntity::kCrewPlaces` on the C++ side.
CREW_PLACES = 2

# The fragment's `trebuchetFire` is not played on the operator: its clip is
# the whole 45 s cutscene firing, and the fragment holds the player's input
# and camera for all of it.
CANNON_IMPACT_SHAKE = (3.0, 60.0)
CANNON_RELEASE_SHAKE = (2.5, 40.0)



def crc(name):
    return zlib.crc32(name.encode("ascii")) & 0xFFFFFFFF


def mesh_boxes(paks, path):
    """Every mesh chunk's bounding box (MESH_CHUNK_DESC_0801), as (low, high)."""
    blob = paks.read(path)
    if blob is None or blob[:4] != b"CrCh":
        raise ValueError("{} is not a chunk file".format(path))
    _version, chunk_count, table = struct.unpack_from("<III", blob, 4)
    boxes = []
    for index in range(chunk_count):
        chunk_type, chunk_version, _id, _size, offset = struct.unpack_from("<HHIII", blob, table + 16 * index)
        if chunk_type != 0x1000 or chunk_version not in (0x0800, 0x0801):
            continue
        # nFlags, nFlags2, nVerts, nIndices, nSubsets, nSubsetsChunkId,
        # nVertAnimID, nStreamChunkID[16], nPhysicsDataChunkId[4]
        at = offset + 4 * (7 + 16 + 4)
        boxes.append((struct.unpack_from("<3f", blob, at), struct.unpack_from("<3f", blob, at + 12)))
    if not boxes:
        raise ValueError("{} has no mesh chunk".format(path))
    return boxes


def mesh_bounds(paks, path):
    """The union of every mesh chunk's bounding box."""
    boxes = mesh_boxes(paks, path)
    low = [min(box[0][axis] for box in boxes) for axis in range(3)]
    high = [max(box[1][axis] for box in boxes) for axis in range(3)]
    return low, high


def cannon_barrel(paks):
    """Where the cannon's muzzle is, and which way the mesh faces.

    The barrel is the mesh's narrowest part -- the carriage and the stone
    proxies are wider -- and it sticks out further at its muzzle than at its
    breech. On `cannon.cgf` that is -Y: the mesh faces backwards from the
    engine's own forward, so it is turned half a circle when it is placed,
    and the muzzle, at the centre of the barrel's height, lands on +Y.
    """
    barrel_low, barrel_high = min(mesh_boxes(paks, CANNON), key=lambda box: box[1][0] - box[0][0])
    faces_back = -barrel_low[1] > barrel_high[1]
    reach = -barrel_low[1] if faces_back else barrel_high[1]
    height = (barrel_low[2] + barrel_high[2]) * 0.5
    return (0.0, reach, height), 180.0 if faces_back else 0.0


def radius_of(paks, path):
    low, high = mesh_bounds(paks, path)
    return max((h - l) * 0.5 for l, h in zip(low, high))


def sample_track(times, values, frame):
    """The track's position at `frame`, linearly between its keys."""
    if frame <= times[0]:
        return values[0]
    for index in range(1, len(times)):
        if frame <= times[index]:
            span = times[index] - times[index - 1]
            weight = (frame - times[index - 1]) / span if span else 1.0
            return tuple(a + (b - a) * weight for a, b in zip(values[index - 1], values[index]))
    return values[-1]


def release_of(clip):
    """The key at which the stone leaves the sling, and its direction then.

    Read off the stone's own path: in the sling its direction of travel
    swings through a right angle in a fraction of a second, and once it is
    free it changes by a fraction of a degree a key. The release is the first
    key after the sling's fastest swing at which the turn per key drops under
    `RELEASE_TURN_DEGREES`.
    """
    times, values = clip.positions[crc(PROJECTILE_BONE)]

    def direction(index):
        (y0, z0), (y1, z1) = values[index - 1][1:], values[index + 1][1:]
        return math.degrees(math.atan2(z1 - z0, y1 - y0))

    def speed(index):
        (y0, z0), (y1, z1) = values[index - 1][1:], values[index + 1][1:]
        return math.hypot(y1 - y0, z1 - z0) / ((times[index + 1] - times[index - 1]) / gates.KEYS_PER_SECOND)

    # A stone lying still has no direction to turn, so only keys where it is
    # moving count -- before the swing starts every key would read as noise.
    moving = [index for index in range(1, len(times) - 2) if speed(index) > MOVING_SPEED and speed(index + 1) > MOVING_SPEED]
    turns = [(index, abs(direction(index + 1) - direction(index))) for index in moving]
    # The cutscene lands the stone with a kink of its own, so the swing is
    # looked for before the top of the flight only.
    apex = max(range(len(times)), key=lambda index: values[index][2])
    fastest = max((entry for entry in turns if entry[0] < apex), key=lambda entry: entry[1])[0]
    for index, turn in turns:
        if index > fastest and turn < RELEASE_TURN_DEGREES:
            return times[index], values[index], direction(index)
    raise ValueError("the projectile never leaves the sling in {}".format(clip.name))


def load_lay_frame(paks):
    """When, into the load clip, the crew lets go of the stone in the sling.

    The crew's `Trebuchet` fragment with `trebuchetLoad` carries an `Attach`
    when a crewman picks the stone up and a `Detach` when he lays it in the
    sling (`Libs/Tables/animation/anim_fragment_events.xml`). Both clips are
    the same length, so the event's normalized time is the stone's.
    """
    text = paks.read("libs/tables/animation/anim_fragment_events.xml").decode("utf8", "replace")
    for row in re.finditer(r"<anim_fragment_events ([^>]*)/>", text):
        attributes = dict(re.findall(r'(\w+)="([^"]*)"', row.group(1)))
        if attributes.get("mn_fragment_id") == "Trebuchet" and attributes.get("event_name") == "Detach" and "trebuchetLoad" in attributes.get("mn_tags", ""):
            return float(attributes["norm_event_time"])
    raise ValueError("no Detach event on the crew's trebuchetLoad fragment")


def trebuchet(paks):
    clips = gates.clips_for_character(paks, TREBUCHET)
    for role, name in TREBUCHET_CLIPS.items():
        if name not in clips:
            raise ValueError("{} has no {} clip {!r}; it has {}".format(TREBUCHET, role, name, list(clips)))
    fire = clips[TREBUCHET_CLIPS["fire"]]
    release_frame, release_at, elevation = release_of(fire)

    # The stone's place from the start of the fire clip to its release, a key
    # every 30th of a second, so a client can keep it in the sling.
    times, values = fire.positions[crc(PROJECTILE_BONE)]
    sling = [sample_track(times, values, frame) for frame in range(0, int(math.ceil(release_frame)) + 1)]

    lay = load_lay_frame(paks)
    load_ms = clips[TREBUCHET_CLIPS["load"]].length_ms
    return {
        "kind": "Trebuchet",
        "name": "trebuchet",
        "model": TREBUCHET,
        "clips": [TREBUCHET_CLIPS[role] for role in ("idle", "draw", "load", "fire")],
        "durations": [clips[TREBUCHET_CLIPS[role]].length_ms for role in ("idle", "draw", "load", "fire")],
        "release_ms": int(round(release_frame / gates.KEYS_PER_SECOND * 1000.0)),
        "release_offset": release_at,
        "model_yaw": 0.0,
        "elevation": elevation,
        "muzzle_speed": 0.0,
        "min_range": TREBUCHET_MIN_RANGE,
        "max_range": TREBUCHET_MAX_RANGE,
        "default_range": TREBUCHET_DEFAULT_RANGE,
        "stone_laid_ms": int(round(lay * load_ms)),
        "sling": sling,
        "projectile": TREBUCHET_STONE,
        "projectile_radius": radius_of(paks, TREBUCHET_STONE),
        "release_sound": TREBUCHET_RELEASE_SOUND,
        "impact_sound": TREBUCHET_IMPACT_SOUND,
        "release_sound_range": 0.0,
        "impact_sound_range": IMPACT_SOUND_RANGE,
        "release_effect": "",
        "impact_effect": TREBUCHET_IMPACT_EFFECT,
        "damage": TREBUCHET_DAMAGE,
        "damage_radius": TREBUCHET_DAMAGE_RADIUS,
        "pivot": (0.0, 0.0, 0.0),
        "broken": "",
        "wreck_tilt": TREBUCHET_WRECK_TILT,
        "wreck_sink": TREBUCHET_WRECK_SINK,
        "recoil": 0.0,
        "touch_hole": (0.0, 0.0, 0.0),
        "fuse_effect": "",
        "fuse_sound": "",
        "whiz_sound": "",
        "impact_shake": TREBUCHET_IMPACT_SHAKE,
        "release_shake": (0.0, 0.0),
        "crew": TREBUCHET_CREW,
        "note": "fire clip releases the stone at key {:.0f}, heading {:.1f} degrees above forward".format(release_frame, elevation),
    }


def cannon_pivot(paks, model_yaw):
    """The back end of the cannon's bed, in the engine's frame."""
    low, high = mesh_bounds(paks, CANNON)
    back = high[1] if model_yaw == 180.0 else -low[1]
    return (0.0, -back, low[2] + CANNON_PIVOT_LIFT)


def cannon(paks):
    muzzle, model_yaw = cannon_barrel(paks)
    return {
        "kind": "Cannon",
        "name": "cannon",
        "model": CANNON,
        "clips": ["", "", "", ""],
        "durations": [0, 0, CANNON_LOAD_MS, CANNON_FIRE_MS],
        "release_ms": 0,
        "release_offset": muzzle,
        "model_yaw": model_yaw,
        "elevation": 0.0,
        "muzzle_speed": CANNON_MUZZLE_SPEED,
        "min_range": CANNON_MIN_RANGE,
        "max_range": CANNON_MAX_RANGE,
        "default_range": CANNON_DEFAULT_RANGE,
        "stone_laid_ms": 0,
        "sling": [],
        "projectile": CANNON_BALL,
        "projectile_radius": radius_of(paks, CANNON_BALL),
        "release_sound": CANNON_RELEASE_SOUND,
        "impact_sound": CANNON_IMPACT_SOUND,
        "release_sound_range": CANNON_RELEASE_SOUND_RANGE,
        "impact_sound_range": IMPACT_SOUND_RANGE,
        "release_effect": CANNON_RELEASE_EFFECT,
        "impact_effect": CANNON_IMPACT_EFFECT,
        "damage": CANNON_DAMAGE,
        "damage_radius": CANNON_DAMAGE_RADIUS,
        "pivot": cannon_pivot(paks, model_yaw),
        "broken": CANNON_BROKEN,
        "wreck_tilt": (0.0, 0.0),
        "wreck_sink": 0.0,
        "recoil": CANNON_RECOIL,
        "touch_hole": CANNON_TOUCH_HOLE,
        "fuse_effect": CANNON_FUSE_EFFECT,
        "fuse_sound": CANNON_FUSE_SOUND,
        "whiz_sound": CANNON_WHIZ_SOUND,
        "impact_shake": CANNON_IMPACT_SHAKE,
        "release_shake": CANNON_RELEASE_SHAKE,
        "crew": [],
        "note": "static mesh turned {:.0f} degrees; muzzle {:.2f} m ahead of the pivot, {:.2f} m up".format(model_yaw, muzzle[1], muzzle[2]),
    }


def number(value):
    return "{:.4f}F".format(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default=gates.DEFAULT_GAME)
    arguments = parser.parse_args()

    if not os.path.isdir(arguments.game):
        print("Game directory not found: {}".format(arguments.game), file=sys.stderr)
        return 1

    print("Indexing paks under {} ...".format(arguments.game))
    paks = gates.PakSet(arguments.game)

    engines = [trebuchet(paks), cannon(paks)]
    for engine in engines:
        print("  {}: {}".format(engine["name"], engine["note"]))
        print("      clips {} {}ms, release {}ms at {}, stone laid at {}ms, {} sling keys".format(
            engine["clips"], engine["durations"], engine["release_ms"], tuple(round(v, 3) for v in engine["release_offset"]), engine["stone_laid_ms"], len(engine["sling"])))

    lines = []
    lines.append('#include "shared/game/siege_catalog.h"')
    lines.append("")
    lines.append("// Generated by tools/generate_siege_catalog.py. Do not edit by hand.")
    lines.append("//")
    lines.append("// Clip lengths are the key ranges the DBA stores, at 30 keys a second;")
    lines.append("// the release and the sling path are the `projectile` bone's track in")
    lines.append("// the fire clip; radii are the meshes' own bounds. The sounds, effects,")
    lines.append("// ranges and damage are the tool's declared choices.")
    lines.append("")
    lines.append("namespace KCDC::Shared::Game {")
    lines.append("    const std::vector<SiegeEngineAsset> g_SiegeEngineAssets = {")
    for engine in engines:
        lines.append("        // {}".format(engine["note"]))
        lines.append("        {{SiegeEngineKind::{}, \"{}\", \"{}\", {},".format(engine["kind"], engine["name"], engine["model"], number(engine["model_yaw"])))
        lines.append("            {{{}}},".format(", ".join('"{}"'.format(clip) for clip in engine["clips"])))
        lines.append("            {{{}}},".format(", ".join("{}U".format(ms) for ms in engine["durations"])))
        lines.append("            {}U, {{{}}}, {}, {}, {}, {}, {}, {}U,".format(
            engine["release_ms"], ", ".join(number(v) for v in engine["release_offset"]), number(engine["elevation"]), number(engine["muzzle_speed"]), number(engine["min_range"]), number(engine["max_range"]), number(engine["default_range"]),
            engine["stone_laid_ms"]))
        if engine["sling"]:
            points = ["{{{}}}".format(", ".join(number(v) for v in point)) for point in engine["sling"]]
            lines.extend(gates.packed("            {", "                ", points, "},"))
        else:
            lines.append("            {},")
        lines.append("            \"{}\", {},".format(engine["projectile"], number(engine["projectile_radius"])))
        lines.append("            \"{}\", \"{}\", \"{}\", \"{}\",".format(engine["release_sound"], engine["impact_sound"], engine["release_effect"], engine["impact_effect"]))
        lines.append("            {}, {},".format(number(engine["release_sound_range"]), number(engine["impact_sound_range"])))
        lines.append("            {}, {},".format(number(engine["damage"]), number(engine["damage_radius"])))
        lines.append("            {{{}}}, \"{}\", {}, {}, {}, {},".format(", ".join(number(v) for v in engine["pivot"]), engine["broken"], number(engine["wreck_tilt"][0]), number(engine["wreck_tilt"][1]), number(engine["wreck_sink"]), number(engine["recoil"])))
        lines.append("            {{{}}}, \"{}\", \"{}\", \"{}\", \"{}\", \"{}\",".format(", ".join(number(v) for v in engine["touch_hole"]), engine["fuse_effect"], engine["fuse_sound"], engine["whiz_sound"], WRECK_EFFECT, WRECK_SOUND))
        lines.append("            {}, {}, {},".format(number(FUSE_SOUND_RANGE), number(WHIZ_SOUND_RANGE), number(WRECK_SOUND_RANGE)))
        lines.append("            {}, {}, {}, {},".format(number(engine["impact_shake"][0]), number(engine["impact_shake"][1]), number(engine["release_shake"][0]), number(engine["release_shake"][1])))
        places = ["{{{{{}}}, {}, \"{}\"}}".format(", ".join(number(v) for v in at), number(yaw), tags) for at, yaw, tags in engine["crew"]]
        assert len(places) <= CREW_PLACES, "{} has more crew places than the entity replicates".format(engine["name"])
        while len(places) < CREW_PLACES:
            places.append("{{0.0000F, 0.0000F, 0.0000F}, 0.0000F, \"\"}")
        lines.append("            {{{{{}}}}}, \"{}\"}},".format(", ".join(places), CREW_FRAGMENT if engine["crew"] else ""))
    lines.append("    };")
    lines.append("} // namespace KCDC::Shared::Game")
    lines.append("")

    with open(OUTPUT, "w", encoding="utf8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print("Wrote {}".format(OUTPUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())

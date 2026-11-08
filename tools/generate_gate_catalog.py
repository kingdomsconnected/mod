#!/usr/bin/env python3
"""Mines the game's animated gate assets into code/shared/generated/gate_catalog.cpp.

A "gate" here is a piece of castle architecture the game animates as a
skinned character rather than moving as a rigid body: a drawbridge or a
portcullis. The game ships no entity class for them. Each one is placed as a
plain `GeomEntity` whose `Geometry` property is a `.cdf`, and the shipped
motion comes from a TrackView sequence that keys clip names on that entity.

So nothing in the running game enumerates them, and there is no table to read
at runtime. This walks the paks instead:

  1. every `GeomEntity` placement in a level whose Geometry is a `.cdf`
  2. that `.cdf`'s `.chrparams`, for the `$TracksDatabase` glob
  3. the `.dba` behind that glob, for the clip names and their lengths

and keeps the assets whose path or clips use gate vocabulary. Everything the
catalog records past that point -- the character path, the clip names, which
clip opens and which closes -- is read out of the files, not typed here.

Some castle gates are not animated at all: the level draws them open and
shut as two layers of static brushes, and the quests swap one for the other.
Those are named in SWAPPED_GATES by the GameProfiles the quests use, and
their layers, pivots and keys are read out of the level from there.

Usage:
    python tools/generate_gate_catalog.py [--game <install dir>]
"""

import argparse
import fnmatch
import os
import re
import struct
import sys
import zipfile
import xml.etree.ElementTree as ET

DEFAULT_GAME = r"D:/SteamLibrary/steamapps/common/KingdomComeDeliverance2"

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUTPUT = os.path.join(REPO_ROOT, "code", "shared", "generated", "gate_catalog.cpp")

# What makes an animated character a gate rather than a door, a cart or a
# crossbow. Matched against the character path and against the clip names.
GATE_VOCABULARY = ("drawbridge", "gate_bars", "portcullis")

# Gates the level swaps between layers instead of animating: static meshes,
# one layer per state, switched by the quests through GameProfiles. Each is
# named by those profiles (`profile:`); the Ruthard palace gate has none of
# its own, so its layers are named directly (`layer:`, a name prefix).
#
# `cleared` is every other version of the same gateway -- barricaded,
# damaged, broken, a second pair of leaves -- and the quest barriers on its
# line. They stay out of the world while the gate is in it.
#
# A profile also switches layers that hold only the quest's NPCs and props,
# and those are not the gate, so only a layer that draws brushes is taken.
# The entities that share a gate's layer are its own: a light through the
# gateway, a navigation seed for the side it closes off.
SWAPPED_GATES = (
    {
        "name": "Ratborsch fortress gate",
        "level": "kutnohorsko",
        "open": ("profile:ratborFortress_gate_open",),
        "closed": ("profile:ratborFortress_gate_closed",),
        "cleared": ("profile:setkanivratbori2_gateBarrier",),
    },
    {
        "name": "Nebakov fortress gate",
        "level": "trosecko",
        "open": ("profile:nebakovFortress_gateOpened",),
        "closed": ("profile:nebakovFortress_gateClosed",),
        "cleared": ("profile:nebakovFortress_gateClosedBarricade", "profile:nebakovFortress_gateClosedDamaged", "profile:nebakovFortress_gateOpenedBroken"),
    },
    # Its editor folders are `series_stablesGate/open` and `/openFull`. The
    # first, the one the story shows, swings a single leaf and leaves its
    # other leaf's collider where the shut gate's is: about 1.2 m clear, too
    # narrow for a horse. `openFull` swings both, about 2.1 m clear.
    {
        "name": "Ruthard stables gate",
        "level": "kutnohorsko",
        "open": ("layer:_enviro_5de784db",),
        "closed": ("layer:_enviro_f7150b3b",),
        "cleared": ("layer:_enviro_0463fd7e",),
    },
    # The main gate into the courtyard, about 18 m north of the stables gate:
    # `series_gate/gateOpen`, and a shut leaf with no folder. The third version,
    # `series_gate/gateClosedDoorsOpen`, keeps it shut with a wicket door.
    {
        "name": "Ruthard palace gate",
        "level": "kutnohorsko",
        "open": ("layer:_enviro_c016c439",),
        "closed": ("layer:_enviro_b47eca21",),
        "cleared": ("layer:_common_ec1df05e",),
    },
)

# How far from a swapped gate's pivot each of its open and closed layers must
# place a mesh, so a renamed layer cannot quietly switch something elsewhere.
SWAPPED_GATE_REACH = 5.0

BACKSLASH = chr(92)


def zread(archive, name):
    """Reads one member, tolerating the separator the pak was written with.

    The paks are not consistent: some store `a/b.cgf` in the central
    directory and `a\\b.cgf` in the local header, which makes zipfile refuse
    the member outright.
    """
    info = archive.NameToInfo[name]
    original = info.orig_filename
    for candidate in (name, name.replace("/", BACKSLASH), name.replace(BACKSLASH, "/")):
        info.orig_filename = candidate
        try:
            return archive.read(info)
        except zipfile.BadZipFile:
            continue
        finally:
            info.orig_filename = original
    raise RuntimeError("cannot read {} from {}".format(name, archive.filename))


class PakSet:
    """Every pak, indexed once by lowercased forward-slashed member name."""

    def __init__(self, game):
        self._archives = []
        self._index = {}
        roots = [os.path.join(game, "Data"), os.path.join(game, "Data", "Levels"), os.path.join(game, "Engine")]
        paths = []
        for root in roots:
            for directory, _, files in os.walk(root):
                paths.extend(os.path.join(directory, f) for f in files if f.lower().endswith(".pak"))
        for path in sorted(set(paths)):
            try:
                archive = zipfile.ZipFile(path)
            except Exception as error:
                print("  skipping {}: {}".format(os.path.basename(path), error), file=sys.stderr)
                continue
            self._archives.append(archive)
            for member in archive.namelist():
                key = member.lower().replace(BACKSLASH, "/")
                self._index.setdefault(key, (archive, member))

    def read(self, path):
        entry = self._index.get(path.lower().replace(BACKSLASH, "/"))
        return zread(entry[0], entry[1]) if entry is not None else None

    def glob(self, pattern):
        pattern = pattern.lower().replace(BACKSLASH, "/")
        return sorted(name for name in self._index if fnmatch.fnmatch(name, pattern))

    def level_paks(self):
        return [a for a in self._archives if os.path.basename(a.filename).lower() == "level.pak"]


def placed_characters(paks):
    """Every `.cdf` a level places on a GeomEntity, with the layers it sits in."""
    placements = {}
    entity = re.compile(rb"<Entity ([^>]*)>")
    for archive in paks.level_paks():
        level = os.path.basename(os.path.dirname(archive.filename))
        for member in archive.namelist():
            if not member.lower().endswith(".xml"):
                continue
            try:
                blob = zread(archive, member)
            except Exception:
                continue
            if b"<Entity" not in blob:
                continue
            for match in entity.finditer(blob):
                attributes = match.group(1).decode("utf8", "replace")
                geometry = re.search(r'Geometry="([^"]+)"', attributes)
                if geometry is None or not geometry.group(1).lower().endswith(".cdf"):
                    continue
                entity_class = re.search(r'EntityClass="([^"]+)"', attributes)
                if entity_class is None or entity_class.group(1) != "GeomEntity":
                    continue
                editor_layer = re.search(r'EditorLayer="([^"]+)"', attributes)
                record = placements.setdefault(geometry.group(1).lower(), {"levels": set(), "layers": set(), "instances": {}})
                record["levels"].add(level)
                if editor_layer is not None:
                    record["layers"].add(editor_layer.group(1))
                guid_attr = re.search(r'EntityGuid="([^"]+)"', attributes)
                name_attr = re.search(r'Name="([^"]*)"', attributes)
                layer_attr = re.search(r'Layer="([^"]*)"', attributes)
                if guid_attr is not None:
                    guid = entity_guid(guid_attr.group(1))
                    if guid:
                        # Keyed by (level, guid): the same entity turns up in
                        # several layer files, and a cinematic copy of it is a
                        # different entity with its own guid.
                        #
                        # The runtime layer name is the `Layer` attribute, the
                        # same string leveldata.xml registers with AddLayer and
                        # that IsLayerEnabled / EnableLayer take. These gates
                        # sit in quest-switched layers, so a client has to be
                        # able to ask whether the layer is even on.
                        layer = layer_attr.group(1) if layer_attr else ""
                        # Cinematic layers hold duplicate copies of the same
                        # prop, placed for one cutscene. Driving one would put
                        # a second drawbridge in the world, and enabling its
                        # layer would be worse, so they are not placements.
                        if layer.lower().startswith("cin_"):
                            continue
                        pos_attr = re.search(r'Pos="([^"]*)"', attributes)
                        position = (0.0, 0.0, 0.0)
                        if pos_attr is not None:
                            parts = pos_attr.group(1).split(",")
                            if len(parts) == 3:
                                try:
                                    position = tuple(float(x) for x in parts)
                                except ValueError:
                                    position = (0.0, 0.0, 0.0)
                        existing = record["instances"].get((level, guid))
                        if existing is None or (not existing[1] and layer):
                            record["instances"][(level, guid)] = (name_attr.group(1) if name_attr else "", layer, position)
    return placements


def entity_guid(text):
    """The uint64 `IEntity::GetGuid()` returns for a level EntityGuid string.

    Not a dash-stripped hex read. `ParseEntityGuidString` (WHGame.dll
    0x18071A868) scans "%8x-%4hx-%4hx" and recombines the groups as
    `group0 | ((group1 | (group2 << 16)) << 32)`, so the first group becomes
    the low half and the third-then-second the high half. Reading it the
    obvious way matches nothing at runtime.
    """
    parts = text.split("-")
    if len(parts) != 3:
        try:
            return int(text, 16)
        except ValueError:
            return 0
    try:
        group0, group1, group2 = (int(p, 16) for p in parts)
    except ValueError:
        return 0
    return group0 | ((group1 | (group2 << 16)) << 32)


# CA_ASSET_ADDITIVE (CryCharAnimationParams.h): an additive clip's first key
# is its reference pose, so the engine starts it one key later.
CA_ASSET_ADDITIVE = 0x001

# The only rate the engine plays a DBA at (ANIMATION_30Hz).
KEYS_PER_SECOND = 30.0

# How many evenly spaced points of a motion's openness the catalog carries.
# Must match `kGateCurveSamples` in gate_catalog.h.
CURVE_SAMPLES = 65

# The key-time formats a DBA stores (EKeyTimesFormat, ControllerPQ.h), in the
# order the chunk counts them.
KEY_TIME_F32, KEY_TIME_U16, KEY_TIME_U8, KEY_TIME_F32_START_STOP, KEY_TIME_U16_START_STOP, KEY_TIME_U8_START_STOP, KEY_TIME_BITSET = range(7)

# ECompressionInformation (QuatQuantization.h): positions are only ever
# stored uncompressed, three floats a key.
NO_COMPRESS_VEC3 = 2

NO_TRACK = 0xFFFFFFFF


class Clip:
    """One animation in a DBA: its length, and the position keys of every bone it moves."""

    def __init__(self, name, length_ms, first_key, last_key, positions):
        self.name = name
        self.length_ms = length_ms
        self.first_key = first_key
        self.last_key = last_key
        self.positions = positions  # controller id -> (key times, [(x, y, z)])


def format_of(index, counts):
    """Which format a track is stored in, from the chunk's per-format counts."""
    first = 0
    for fmt, count in enumerate(counts):
        if index < first + count:
            return fmt
        first += count
    raise ValueError("track {} is past every format's range".format(index))


def parse_dba(blob):
    """Every clip a DBA holds, read the way CInternalDatabaseInfo::ReadController905 does.

    The file is a `CrCh` chunk file with one controller chunk, type 0x100D
    version 0x0905: track
    sizes and formats, track offsets, then per clip its CAF path, a
    `MotionParams905`, foot plants and a `CControllerInfo` per bone. The
    lengths are `(m_nEnd - m_nStart) / 30` seconds, starting one key later on
    an additive clip -- exactly what `IAnimationSet::GetDuration_sec` hands
    back at runtime.
    """
    if blob[:4] != b"CrCh":
        raise ValueError("not a chunk file")
    _version, chunk_count, table = struct.unpack_from("<III", blob, 4)
    chunk = None
    for index in range(chunk_count):
        chunk_type, chunk_version, _id, size, offset = struct.unpack_from("<HHIII", blob, table + 16 * index)
        if chunk_type == 0x100D and chunk_version == 0x0905:
            chunk = (offset, offset + size)
    if chunk is None:
        raise ValueError("no CONTROLLER_CHUNK_DESC_0905")
    offset, chunk_end = chunk

    num_pos, num_rot, num_times, num_anims = struct.unpack_from("<IIII", blob, offset)
    cursor = offset + 16
    base = cursor

    def take(fmt, count):
        nonlocal cursor
        values = struct.unpack_from("<{}{}".format(count, fmt), blob, cursor)
        cursor += struct.calcsize("<{}{}".format(count, fmt))
        return values

    time_sizes = take("H", num_times)
    time_formats = take("I", KEY_TIME_BITSET + 1)
    pos_sizes = take("H", num_pos)
    pos_formats = take("I", 9)
    take("H", num_rot)
    take("I", 9)
    time_offsets = take("i", num_times)
    pos_offsets = take("i", num_pos)
    rot_offsets = take("i", num_rot + 1)

    # A negative last offset marks a file prepared for streaming in place:
    # every offset is then relative to the end of the chunk.
    in_place = rot_offsets[num_rot] < 0
    padding = take("I", 1)[0] if in_place else 0
    cursor += (4 - (cursor - base) % 4) % 4
    track_bytes = (abs(rot_offsets[num_rot]) + 3) // 4 * 4
    if in_place:
        storage = chunk_end
    else:
        storage = cursor
        cursor += track_bytes
    cursor += padding

    def key_times(track):
        count = time_sizes[track]
        at = storage + time_offsets[track]
        fmt = format_of(track, time_formats)
        if fmt in (KEY_TIME_F32, KEY_TIME_U16, KEY_TIME_U8):
            code = {KEY_TIME_F32: "f", KEY_TIME_U16: "H", KEY_TIME_U8: "B"}[fmt]
            return [float(value) for value in struct.unpack_from("<{}{}".format(count, code), blob, at)]
        if fmt in (KEY_TIME_F32_START_STOP, KEY_TIME_U16_START_STOP, KEY_TIME_U8_START_STOP):
            code = {KEY_TIME_F32_START_STOP: "f", KEY_TIME_U16_START_STOP: "H", KEY_TIME_U8_START_STOP: "B"}[fmt]
            first, last = struct.unpack_from("<2" + code, blob, at)
            return [float(first + step) for step in range(int(last - first) + 1)]
        # CKeyTimesInformationBitSet: {m_Start, m_End, m_Size}, then one bit
        # per frame from m_Start.
        first, last, size = struct.unpack_from("<3H", blob, at)
        words = struct.unpack_from("<{}H".format(count - 3), blob, at + 6)
        times = [float(first + 16 * word + bit) for word, bits in enumerate(words) for bit in range(16) if (bits >> bit) & 1]
        if len(times) != size or times[0] != first or times[-1] != last:
            raise ValueError("bitset key times do not agree with their header")
        return times

    clips = {}
    block = cursor
    for _ in range(num_anims):
        length = struct.unpack_from("<H", blob, cursor)[0]
        cursor += 2
        path = blob[cursor:cursor + length].decode("ascii").lower()
        cursor += length
        flags, _compression, _ticks, _seconds, first, last = struct.unpack_from("<IIifii", blob, cursor)
        cursor += 132  # sizeof(MotionParams905)
        cursor += 2 + struct.unpack_from("<H", blob, cursor)[0]
        controllers = struct.unpack_from("<H", blob, cursor)[0]
        cursor += 2
        infos_at = cursor - block
        if in_place:
            infos_at = struct.unpack_from("<i", blob, cursor)[0]
            cursor += 4
        else:
            cursor += 20 * controllers

        positions = {}
        for index in range(controllers):
            controller, pos_times, pos_track, _rot_times, _rot_track = struct.unpack_from("<5I", blob, block + infos_at + 20 * index)
            if pos_track == NO_TRACK or pos_times == NO_TRACK:
                continue
            if format_of(pos_track, pos_formats) != NO_COMPRESS_VEC3:
                raise ValueError("position track stored in an unexpected format")
            at = storage + pos_offsets[pos_track]
            keys = [struct.unpack_from("<3f", blob, at + 12 * key) for key in range(pos_sizes[pos_track])]
            positions[controller] = (key_times(pos_times), keys)

        if flags & CA_ASSET_ADDITIVE:
            first += 1
        name = os.path.splitext(os.path.basename(path))[0]
        clips[name] = Clip(name, int(round(max(last - first, 0) / KEYS_PER_SECOND * 1000.0)), first, max(last, first), positions)
    return clips


def clips_for_character(paks, character):
    """The clips a character can play, via its chrparams and DBA."""
    chrparams = paks.read(os.path.splitext(character)[0] + ".chrparams")
    if chrparams is None:
        return {}
    text = chrparams.decode("utf8", "replace")
    tracks = re.search(r'name="\$TracksDatabase"\s+path="([^"]+)"', text)
    if tracks is None:
        return {}
    clips = {}
    for dba in paks.glob(tracks.group(1)):
        blob = paks.read(dba)
        if blob is not None:
            clips.update(parse_dba(blob))
    return dict(sorted(clips.items()))


def sample(times, values, frame):
    """A key track's value at one frame, interpolated the way the engine does between keys."""
    if frame <= times[0]:
        return values[0]
    for index in range(1, len(times)):
        if frame <= times[index]:
            span = times[index] - times[index - 1]
            weight = (frame - times[index - 1]) / span if span > 0 else 1.0
            return values[index - 1] + (values[index] - values[index - 1]) * weight
    return values[-1]


def motion_curves(clips, roles):
    """How open the gate is along each motion, at CURVE_SAMPLES evenly spaced points.

    A motion that borrows the other direction's clip is that clip run
    backwards, so its curve is the other one mirrored, and reversing between
    them by time is exact whatever the curve is. Two separate clips are not
    mirrors of each other -- the Suchdol portcullis slams shut in 1.3s of its
    7s close and winches open over 12.8s of its 17s open -- so between those
    a reversal has to match the pose, not the time. That pose is read off the
    bone both clips move furthest: its travel between where the close ends
    and where the open ends is 0 to 1.
    """
    linear = [index / (CURVE_SAMPLES - 1) for index in range(CURVE_SAMPLES)]
    if not roles["open"] or not roles["close"]:
        # One clip for both ways: its own pose curve is not needed to reverse
        # it, and a rotation-only rig has no travel to read one from.
        return list(linear), [1.0 - value for value in linear], "one clip both ways; openness is linear in its time"

    opening, closing = clips[roles["open"]], clips[roles["close"]]
    best = None
    for controller in set(opening.positions) & set(closing.positions):
        for axis in range(3):
            values = [key[axis] for key in opening.positions[controller][1] + closing.positions[controller][1]]
            span = max(values) - min(values)
            if best is None or span > best[0]:
                best = (span, controller, axis)
    if best is None or best[0] < 0.01:
        raise ValueError("{} / {}: no bone travels in both clips".format(opening.name, closing.name))
    _span, controller, axis = best

    def curve(clip):
        times, keys = clip.positions[controller]
        values = [key[axis] for key in keys]
        return [sample(times, values, clip.first_key + (clip.last_key - clip.first_key) * point) for point in linear]

    raw_open, raw_close = curve(opening), curve(closing)
    shut, raised = raw_close[-1], raw_open[-1]
    if abs(raised - shut) < 0.01:
        raise ValueError("{} / {}: the two clips end in the same pose".format(opening.name, closing.name))

    def normalize(values):
        return [min(1.0, max(0.0, (value - shut) / (raised - shut))) for value in values]

    return normalize(raw_open), normalize(raw_close), "bone {:08x} axis {} travels {:.3f} between the clips' ends".format(controller, "xyz"[axis], raised - shut)


def classify(character, clips):
    """Which clip opens, which closes, and which hold each settled pose.

    Open always means passable: a portcullis raised, a drawbridge lowered.
    That is why a drawbridge's `lifting` clip is the *closing* one -- it
    raises the deck out of the way and takes the crossing with it.
    """
    result = {"open": "", "close": "", "opened_idle": "", "closed_idle": ""}
    for clip in clips:
        if "opened_idle" in clip or "open_idle" in clip:
            result["opened_idle"] = result["opened_idle"] or clip
        elif "closed_idle" in clip or "close_idle" in clip:
            result["closed_idle"] = result["closed_idle"] or clip
        elif clip.endswith("_open") or "_opening" in clip:
            result["open"] = result["open"] or clip
        elif clip.endswith("_close") or "_closing" in clip:
            result["close"] = result["close"] or clip
        elif "lifting" in clip:
            # A drawbridge lifts to close. It ships no lowering clip; the
            # client plays this one backwards for the opposite direction.
            result["close"] = result["close"] or clip
        elif clip.endswith("_loop"):
            # The held pose the lift runs into.
            result["closed_idle"] = result["closed_idle"] or clip
    kind = "Portcullis" if "gate_bars" in character or "portcullis" in character else "Drawbridge"
    return kind, result


def is_gate(character, clips, layers):
    haystack = " ".join([character] + list(clips) + list(layers)).lower()
    return any(word in haystack for word in GATE_VOCABULARY)


def escape(text):
    return text.replace(BACKSLASH, BACKSLASH * 2).replace('"', BACKSLASH + '"')


def terrain_blobs(game, levels):
    """Each gate level's `terrain/terrain.dat`, read once: it is several hundred MB."""
    blobs = {}
    for level in sorted(levels):
        terrain = os.path.join(game, "Data", "Levels", level, "terrain.pak")
        if not os.path.isfile(terrain):
            continue
        with zipfile.ZipFile(terrain) as archive:
            blobs[level] = zread(archive, "terrain/terrain.dat")
    return blobs


# One static object's path in terrain.dat's table (CTerrain's name chunk).
STAT_OBJECT_ENTRY = 0x100
STAT_OBJECT_PATH = re.compile(rb"[%A-Za-z][\x21-\x7e]{3,250}\.(?:cgf|cga)\x00", re.IGNORECASE)


def stat_object_table(blob):
    """The level's static object paths, in the order a brush references them.

    terrain.dat stores them as a u32 count followed by that many 256-byte,
    NUL-padded paths; an unused slot is all zeros. A layer's
    `physics/layer<id>.dat` names its brushes by index into this list. The
    table is found by walking out from any path in it and is accepted only
    when the count in front of it agrees with the run of entries that
    follows, so the vegetation table (a different stride) is never taken
    for it.
    """
    def entry(offset):
        chunk = blob[offset:offset + STAT_OBJECT_ENTRY]
        if len(chunk) != STAT_OBJECT_ENTRY:
            return False
        if not any(chunk):
            return True
        path = chunk.split(b"\0", 1)[0]
        return b"." in path and all(0x20 < byte < 0x7F for byte in path) and not any(chunk[len(path):])

    rejected = []
    for match in STAT_OBJECT_PATH.finditer(blob):
        start = match.start()
        if any(first <= start < last for first, last in rejected) or not entry(start):
            continue
        while start >= STAT_OBJECT_ENTRY + 4 and entry(start - STAT_OBJECT_ENTRY):
            start -= STAT_OBJECT_ENTRY
        count = 0
        while entry(start + count * STAT_OBJECT_ENTRY):
            count += 1
        if struct.unpack_from("<I", blob, start - 4)[0] == count:
            return [blob[start + index * STAT_OBJECT_ENTRY:start + (index + 1) * STAT_OBJECT_ENTRY].split(b"\0", 1)[0].decode("ascii") for index in range(count)]
        rejected.append((start, start + count * STAT_OBJECT_ENTRY))
    raise ValueError("terrain.dat holds no static object table")


# One record of a layer's `physics/layer<id>.dat`: the brush's world
# Matrix34, then its index into the static object table, then state the
# catalog does not need.
LAYER_PHYSICS_RECORD = 72


class LevelPhysics:
    """Every layer physics record of one level, by layer name, read once.

    The runtime keeps a layer's colliders in record order, one per record,
    so a brush's collider is named by its layer, its index and the record
    count that proves the list is the one this was read from.
    """

    def __init__(self, archive, table):
        self.archive = archive
        self.layers = {}
        for node in ET.fromstring(zread(archive, "leveldata.xml")).iter("Layer"):
            name = "physics/layer{}.dat".format(node.attrib["Id"])
            if name not in archive.namelist():
                continue
            blob = zread(archive, name)
            if len(blob) % LAYER_PHYSICS_RECORD != 0:
                raise ValueError("{} is not a whole number of brush records".format(name))
            records = []
            for offset in range(0, len(blob), LAYER_PHYSICS_RECORD):
                matrix = struct.unpack_from("<12f", blob, offset)
                index = struct.unpack_from("<I", blob, offset + 48)[0]
                if index >= len(table):
                    raise ValueError("{} names static object {} of {}".format(name, index, len(table)))
                records.append((table[index], matrix))
            self.layers[node.attrib["Name"]] = records

    def colliders_of(self, path, position, reach):
        """Every (layer, index, count) that physicalizes `path` within `reach` of `position`."""
        found = []
        for layer, records in sorted(self.layers.items()):
            for index, (record_path, matrix) in enumerate(records):
                at = (matrix[3], matrix[7], matrix[11])
                if record_path.lower() == path.lower() and all(abs(at[axis] - position[axis]) <= reach for axis in range(3)):
                    found.append((layer, index, len(records)))
        return found


def npc_only_surfaces(paks):
    """Surface types that collide with NPCs alone -- horses included, the player not."""
    root = ET.fromstring(paks.read("libs/materialeffects/surfacetypes.xml"))
    return {node.attrib["name"].lower() for node in root.iter("SurfaceType") if any(physics.attrib.get("npc_only") == "1" for physics in node.findall("Physics"))}


def brush_chunks(paks, archive, path):
    """A brush's `.cgf` blob and its chunks as {id: (type, version, offset)}, or None."""
    if path.lower().startswith("%level%/"):
        members = {member.lower().replace(BACKSLASH, "/"): member for member in archive.namelist()}
        member = members.get(path[len("%level%/"):].lower())
        blob = zread(archive, member) if member is not None else None
    else:
        blob = paks.read(path)
    if blob is None or blob[:4] != b"CrCh":
        return None
    _version, chunk_count, table = struct.unpack_from("<III", blob, 4)
    chunks = {}
    for index in range(chunk_count):
        chunk_type, chunk_version, chunk_id, _size, offset = struct.unpack_from("<HHIII", blob, table + 16 * index)
        chunks[chunk_id] = (chunk_type, chunk_version, offset)
    return blob, chunks


def brush_surfaces(paks, archive, path):
    """Every surface type the brush's material assigns, or None when it cannot be read."""
    read = brush_chunks(paks, archive, path)
    if read is None:
        return None
    blob, chunks = read
    material = None
    for chunk_type, chunk_version, offset in chunks.values():
        # MTL_NAME_CHUNK_DESC_0802 opens with the material's name.
        if chunk_type == 0x1014 and chunk_version == 0x0802:
            material = blob[offset:offset + 128].split(b"\0", 1)[0].decode("ascii")
    mtl = paks.read(material + ".mtl") if material else None
    if mtl is None:
        return None
    return {node.attrib["SurfaceType"].lower() for node in ET.fromstring(mtl).iter("Material") if node.attrib.get("SurfaceType")}


def brush_bounds_centre(paks, archive, path, matrix):
    """The centre of a brush's world bounds, placed by its layer record's Matrix34.

    This, not the pivot, is what a lookup has to aim at: the octree keeps a
    node when its world bounds overlap the query box, and a stand-in's mesh
    can sit metres from its pivot -- the Suchdol bars are ten.

    Every mesh chunk counts, placed through its node's transform and the
    transforms of that node's parents (NODE_CHUNK_DESC_0824,
    MESH_CHUNK_DESC_0801).
    """
    read = brush_chunks(paks, archive, path)
    if read is None:
        raise ValueError("cannot read the mesh {}".format(path))
    blob, chunks = read
    nodes = {}
    for chunk_id, (chunk_type, chunk_version, offset) in chunks.items():
        if chunk_type == 0x100B and chunk_version in (0x0823, 0x0824):
            object_id, parent_id = struct.unpack_from("<ii", blob, offset + 64)
            nodes[chunk_id] = (object_id, parent_id, struct.unpack_from("<16f", blob, offset + 84))

    def to_object(node_id, point):
        while node_id in nodes:
            _object, parent, tm = nodes[node_id]
            point = tuple(point[0] * tm[column] + point[1] * tm[4 + column] + point[2] * tm[8 + column] + tm[12 + column] for column in range(3))
            node_id = parent
        return point

    corners = []
    for node_id, (object_id, _parent, _tm) in nodes.items():
        mesh = chunks.get(object_id)
        if mesh is None or mesh[0] != 0x1000 or mesh[1] not in (0x0800, 0x0801):
            continue
        low = struct.unpack_from("<3f", blob, mesh[2] + 108)
        high = struct.unpack_from("<3f", blob, mesh[2] + 120)
        for corner in range(8):
            corners.append(to_object(node_id, tuple(high[axis] if corner >> axis & 1 else low[axis] for axis in range(3))))
    if not corners:
        raise ValueError("{} has no mesh bounds".format(path))
    world = [tuple(matrix[4 * row] * point[0] + matrix[4 * row + 1] * point[1] + matrix[4 * row + 2] * point[2] + matrix[4 * row + 3] for row in range(3)) for point in corners]
    return tuple((min(point[axis] for point in world) + max(point[axis] for point in world)) / 2.0 for axis in range(3))


# How far a record may stand from where the catalog expects its brush. The
# stand-in shares the gate's pivot to the centimetre.
BRUSH_REACH = 0.05


def displaced_brushes(paks, blobs, gates):
    """The level brushes each gate placement puts out of the world while it is bound.

    Two kinds, both mined rather than listed:

    - the gate's static stand-in, `<asset>_opened.cgf`, at the gate's pivot,
      with every layer record that physicalizes it;
    - NPC-only collision the gate's own layer brings in with it. The Suchdol
      portcullis layer is not entity-only: it also physicalizes a designer
      brush on the bars line with `collision_proxy_npc_only`, which the
      battle quest uses to pen its NPCs in. Horses are NPCs, so once the
      layer is claimed for the gate a raised portcullis stops every rider
      while people on foot walk through. Only a brush whose every surface is
      NPC-only is taken, so nothing visible is hidden this way.

    Each brush is (mesh, centre of its world bounds, [(layer, record index,
    record count)]).
    """
    surfaces = npc_only_surfaces(paks)
    archives = {os.path.basename(os.path.dirname(archive.filename)): archive for archive in paks.level_paks()}
    physics = {}
    found = {}
    for gate in gates:
        for (level, guid), (_name, layer, position) in gate["instances"].items():
            if level not in archives or level not in blobs:
                continue
            if level not in physics:
                physics[level] = LevelPhysics(archives[level], stat_object_table(blobs[level]))
            level_physics = physics[level]
            brushes = []
            if gate["stand_in"]:
                colliders = level_physics.colliders_of(gate["stand_in"], position, BRUSH_REACH)
                if not colliders:
                    raise ValueError("{}: no layer physicalizes the stand-in {}".format(level, gate["stand_in"]))
                first_layer, first_record, _count = colliders[0]
                matrix = level_physics.layers[first_layer][first_record][1]
                brushes.append((gate["stand_in"], brush_bounds_centre(paks, archives[level], gate["stand_in"], matrix), colliders))
            records = level_physics.layers.get(layer, [])
            for index, (path, matrix) in enumerate(records):
                assigned = brush_surfaces(paks, archives[level], path)
                if assigned and assigned <= surfaces:
                    brushes.append((path, brush_bounds_centre(paks, archives[level], path, matrix), [(layer, index, len(records))]))
            found[(level, guid)] = brushes
    return found


def game_profiles(archive):
    """The level's GameProfiles, name -> layer names, from the `whdata_1` member of level.pak."""
    members = {member.lower().replace(BACKSLASH, "/"): member for member in archive.namelist()}
    blob = zread(archive, members["whdata_1"]).decode("ascii", "replace")
    profiles = {}
    for name, body in re.findall(r"<GameProfile>\s*<Name>([^<]*)</Name>.*?<GameLayers>(.*?)</GameLayers>", blob, re.S):
        profiles[name] = re.findall(r"<GameLayer>([^<]*)</GameLayer>", body)
    return profiles


def layer_entities(archive, layer):
    """The editor folder and the entity classes a layer's `layers/<name>.xml` places, for the review printout."""
    members = {member.lower().replace(BACKSLASH, "/"): member for member in archive.namelist()}
    member = members.get("layers/{}.xml".format(layer.lower()))
    blob = zread(archive, member).decode("utf8", "replace") if member is not None else ""
    folder = re.search(r'EditorLayer="([^"]*)"', blob)
    return (folder.group(1) if folder else ""), sorted(set(re.findall(r'EntityClass="([^"]*)"', blob)))


def layer_guid(layer):
    """The key a swapped gate goes by: its layer's GUID through the same shuffle as an entity's."""
    match = re.search(r"([0-9a-f]{8})-([0-9a-f]{4})-([0-9a-f]{4})-[0-9a-f]{4}-[0-9a-f]{12}$", layer, re.IGNORECASE)
    if match is None:
        raise ValueError("layer {} carries no GUID".format(layer))
    return entity_guid("-".join(match.groups()))


def swapped_gates(paks, blobs):
    """Resolves SWAPPED_GATES against each level's profiles, layers and layer physics."""
    archives = {os.path.basename(os.path.dirname(archive.filename)): archive for archive in paks.level_paks()}
    resolved = []
    physics = {}
    profiles = {}
    for spec in SWAPPED_GATES:
        level = spec["level"]
        archive = archives[level]
        if level not in physics:
            physics[level] = LevelPhysics(archive, stat_object_table(blobs[level]))
            profiles[level] = game_profiles(archive)
        layers = physics[level].layers

        def resolve(entries, role):
            names = []
            for entry in entries:
                source, _, key = entry.partition(":")
                if source == "profile":
                    if key not in profiles[level]:
                        raise ValueError("{}: {} names no GameProfile of {}".format(spec["name"], entry, level))
                    candidates = profiles[level][key]
                else:
                    candidates = [layer for layer in layers if layer.startswith(key)]
                    if len(candidates) != 1:
                        raise ValueError("{}: {} matches {} brush layers".format(spec["name"], entry, len(candidates)))
                for layer in candidates:
                    if layer not in layers:
                        print("    {} {}: skipping {} -- it draws no brush, so it is the quest's and not the gate's".format(spec["name"], role, layer))
                        continue
                    if layer not in names:
                        names.append(layer)
            if not names:
                raise ValueError("{}: no {} layer is left".format(spec["name"], role))
            return names

        open_layers = resolve(spec["open"], "open")
        closed_layers = resolve(spec["closed"], "closed")
        cleared_layers = resolve(spec["cleared"], "cleared")
        everything = open_layers + closed_layers + cleared_layers
        if len(set(everything)) != len(everything):
            raise ValueError("{}: a layer is in more than one set".format(spec["name"]))

        # The pivot is the closed mesh's: the first record that is not an
        # invisible designer brush.
        pivot = None
        for path, matrix in layers[closed_layers[0]]:
            if "/brush/designer_" not in path.lower():
                pivot = (matrix[3], matrix[7], matrix[11])
                break
        if pivot is None:
            raise ValueError("{}: the closed layer draws no mesh".format(spec["name"]))
        for layer in open_layers + closed_layers:
            if not any(all(abs(matrix[3 + 4 * axis] - pivot[axis]) <= SWAPPED_GATE_REACH for axis in range(3)) for _path, matrix in layers[layer]):
                raise ValueError("{}: {} places nothing within {} m of the gate".format(spec["name"], layer, SWAPPED_GATE_REACH))

        resolved.append({
            "name": spec["name"],
            "level": level,
            "guid": layer_guid(closed_layers[0]),
            "position": pivot,
            "open": open_layers,
            "closed": closed_layers,
            "cleared": cleared_layers,
            "meshes": {layer: sorted({path.split("/")[-1] for path, _matrix in layers[layer]}) for layer in everything},
            "entities": {layer: layer_entities(archive, layer) for layer in everything},
        })
    return resolved


def stand_ins(blobs, gates):
    """The static mesh the level shows in each gate's place when no quest runs.

    Both gates exist twice. The animated `.cdf` is alone in a quest layer the
    level does not load; what the world shows every other time is a plain
    brush, `<asset>_opened.cgf`, in the level's static objects
    (`terrain/terrain.dat`, which stores each brush's mesh path in the clear).
    A client that brings the animated gate in has to hide that brush, or two
    gates stand in one gateway.

    Returns the path as the level spells it, which is what the render node
    reports through `IRenderNode::GetName`.
    """
    found = {}
    wanted = {gate["character"]: re.compile(re.escape(os.path.splitext(os.path.basename(gate["character"]))[0] + "_opened.cgf").encode("ascii"), re.IGNORECASE) for gate in gates}
    for _level, blob in sorted(blobs.items()):
        for character, pattern in wanted.items():
            match = pattern.search(blob)
            if match is None:
                continue
            start = match.start()
            while start > 0 and 0x20 < blob[start - 1] < 0x7F:
                start -= 1
            found[character] = blob[start:match.end()].decode("ascii")
    return found


# .clang-format's ColumnLimit. The catalog is emitted already formatted, so
# running the formatter over the repository leaves a generated file alone.
COLUMN_LIMIT = 280


def packed(first, continuation, items, closing):
    """Items packed into as few lines as fit the column limit, the way clang-format bin-packs a braced list."""
    lines = []
    line = first
    for index, item in enumerate(items):
        piece = item + (closing if index == len(items) - 1 else ",")
        joined = piece if line in (first, continuation) else " " + piece
        if len(line) + len(joined) > COLUMN_LIMIT and line not in (first, continuation):
            lines.append(line)
            line = continuation + piece
        else:
            line += joined
    lines.append(line)
    return lines


def curve_lines(values):
    """One curve as an initializer, wrapped as clang-format wraps it."""
    return packed("            {", "                ", ["{:.4f}F".format(value) for value in values], "},")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", default=DEFAULT_GAME)
    arguments = parser.parse_args()

    if not os.path.isdir(arguments.game):
        print("Game directory not found: {}".format(arguments.game), file=sys.stderr)
        return 1

    print("Indexing paks under {} ...".format(arguments.game))
    paks = PakSet(arguments.game)

    print("Scanning level layers for placed characters ...")
    placements = placed_characters(paks)
    print("  {} distinct .cdf placed on a GeomEntity".format(len(placements)))

    gates = []
    for character, record in sorted(placements.items()):
        clips = clips_for_character(paks, character)
        if not clips or not is_gate(character, clips, record["layers"]):
            continue
        kind, roles = classify(character, clips)
        if not roles["open"] and not roles["close"]:
            print("  skipping {}: no open or close clip among {}".format(character, list(clips)))
            continue
        # A direction with no clip of its own is the other one run backwards,
        # so it takes the same time.
        open_ms = clips[roles["open"] or roles["close"]].length_ms
        close_ms = clips[roles["close"] or roles["open"]].length_ms
        if open_ms == 0 or close_ms == 0:
            raise ValueError("{}: a motion clip has no length in its DBA".format(character))
        open_curve, close_curve, curve_source = motion_curves(clips, roles)
        gates.append({
            "character": character,
            "kind": kind,
            "levels": sorted(record["levels"]),
            "clips": list(clips),
            "instances": record["instances"],
            "open_ms": open_ms,
            "close_ms": close_ms,
            "opened_idle_ms": clips[roles["opened_idle"]].length_ms if roles["opened_idle"] else 0,
            "closed_idle_ms": clips[roles["closed_idle"]].length_ms if roles["closed_idle"] else 0,
            "open_curve": open_curve,
            "close_curve": close_curve,
            "curve_source": curve_source,
            **roles,
        })

    if not gates:
        print("No gate assets found -- refusing to write an empty catalog.", file=sys.stderr)
        return 1

    print("Looking for each gate's static stand-in in the levels' static objects ...")
    blobs = terrain_blobs(arguments.game, {level for gate in gates for level in gate["levels"]} | {spec["level"] for spec in SWAPPED_GATES})
    standing = stand_ins(blobs, gates)
    for gate in gates:
        gate["stand_in"] = standing.get(gate["character"], "")

    print("Looking for the brushes each placement displaces and the layers that physicalize them ...")
    displaced = displaced_brushes(paks, blobs, gates)

    print("Found {} gate asset(s):".format(len(gates)))
    for gate in gates:
        print("  {} [{}] open={!r} {}ms close={!r} {}ms openedIdle={!r} {}ms closedIdle={!r} {}ms".format(
            gate["character"], gate["kind"], gate["open"], gate["open_ms"], gate["close"], gate["close_ms"], gate["opened_idle"], gate["opened_idle_ms"], gate["closed_idle"], gate["closed_idle_ms"]))
        print("      curves: {}".format(gate["curve_source"]))
        print("      stand-in: {}".format(gate["stand_in"] or "none"))
        for (level, guid), (name, layer, position) in sorted(gate["instances"].items()):
            print("      {} {:#018x} {}  at {:.1f},{:.1f},{:.1f}  layer={!r}".format(level, guid, name, position[0], position[1], position[2], layer))
            for path, at, colliders in displaced.get((level, guid), []):
                print("        displaces {} at {:.2f},{:.2f},{:.2f}".format(path, at[0], at[1], at[2]))
                for collider_layer, record, count in colliders:
                    print("          collider: layer {!r} record {} of {}".format(collider_layer, record, count))
    placements = sum(len(g["instances"]) for g in gates)
    if placements == 0:
        print("No gate placements found -- the client would have nothing to look up.", file=sys.stderr)
        return 1
    print("Found {} placement(s).".format(placements))

    print("Resolving the gates the level swaps between layers ...")
    swapped = swapped_gates(paks, blobs)
    for gate in swapped:
        print("  {} {} {:#018x} at {:.1f},{:.1f},{:.1f}".format(gate["name"], gate["level"], gate["guid"], *gate["position"]))
        for role in ("open", "closed", "cleared"):
            for layer in gate[role]:
                folder, classes = gate["entities"][layer]
                print("      {:<7} {}: {}".format(role, layer, ", ".join(gate["meshes"][layer])))
                print("              {} {}".format(folder or "(no entities)", ", ".join(classes)))

    lines = []
    lines.append('#include "shared/game/gate_catalog.h"')
    lines.append("")
    lines.append("// Generated by tools/generate_gate_catalog.py. Do not edit by hand.")
    lines.append("//")
    lines.append("// Mined from the level layers, chrparams and animation databases of the")
    lines.append("// shipped paks. Every field below was read out of the game's own files;")
    lines.append("// clip lengths are the key ranges the DBA stores, at 30 keys a second.")
    lines.append("")
    lines.append("namespace KCDC::Shared::Game {")
    lines.append("    const std::vector<GateAsset> g_GateAssets = {")
    for gate in gates:
        lines.append("        // {} -- levels: {}".format(gate["kind"], ", ".join(gate["levels"])))
        lines.append("        // clips in the DBA: {}".format(", ".join(gate["clips"])))
        lines.append("        // curves: {}".format(gate["curve_source"]))
        head = ['"{}"'.format(escape(gate[field])) for field in ("character", "open", "close", "opened_idle", "closed_idle")]
        head += [str(gate[field]) for field in ("open_ms", "close_ms", "opened_idle_ms", "closed_idle_ms")]
        lines.extend(packed("        {", "            ", head, ","))
        lines.extend(curve_lines(gate["open_curve"]))
        lines.extend(curve_lines(gate["close_curve"]))
        lines.append("            GateKind::{}}},".format(gate["kind"]))
    # The one row every swapped gate shares. A layer switch has no length,
    # so neither does either motion, and openness is the line between ends.
    swapped_index = len(gates)
    lines.append("        // Doors -- every gate the level swaps between layers rather than animates")
    lines.append("        // curves: a layer switch has no length, so openness is the line between the ends")
    lines.extend(packed("        {", "            ", ['""'] * 5 + ["0"] * 4, ","))
    lines.extend(curve_lines([index / (CURVE_SAMPLES - 1) for index in range(CURVE_SAMPLES)]))
    lines.extend(curve_lines([1.0 - index / (CURVE_SAMPLES - 1) for index in range(CURVE_SAMPLES)]))
    lines.append("            GateKind::Doors},")
    lines.append("    };")
    lines.append("")
    lines.append("    const std::vector<GatePlacement> g_GatePlacements = {")
    for index, gate in enumerate(gates):
        for (level, guid), (name, layer, position) in sorted(gate["instances"].items()):
            fields = ['"{}"'.format(escape(level)), '"{}"'.format(escape(layer)), "{:#018x}ULL".format(guid)]
            fields += ["{:.4f}F".format(value) for value in position]
            fields += [str(index)]
            # One brush per line: with its colliders, a brush is most of the
            # column limit on its own.
            lines.append("        // {}".format(escape(name or gate["kind"])))
            lines.append("        {{{},".format(", ".join(fields)))
            brushes = displaced.get((level, guid), [])
            if not brushes:
                lines[-1] += " {}},"
                continue
            lines.append("            {")
            for path, at, colliders in brushes:
                lines.append('                {{"{}", {:.4f}F, {:.4f}F, {:.4f}F,'.format(escape(path), at[0], at[1], at[2]))
                records = ['{{"{}", {}U, {}U}}'.format(escape(collider_layer), record, count) for collider_layer, record, count in colliders]
                lines.extend(packed("                    {", "                        ", records, "}},"))
            lines.append("            }},")
    for gate in swapped:
        fields = ['"{}"'.format(escape(gate["level"])), '""', "{:#018x}ULL".format(gate["guid"])]
        fields += ["{:.4f}F".format(value) for value in gate["position"]]
        fields += [str(swapped_index), "{}"]
        lines.append("        // {}".format(gate["name"]))
        lines.append("        {{{},".format(", ".join(fields)))
        for role in ("open", "closed"):
            lines.extend(packed("            {", "                ", ['"{}"'.format(escape(layer)) for layer in gate[role]], "},"))
        lines.extend(packed("            {", "                ", ['"{}"'.format(escape(layer)) for layer in gate["cleared"]], "}},"))
    lines.append("    };")
    lines.append("} // namespace KCDC::Shared::Game")
    lines.append("")

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w", encoding="utf8", newline="\n") as handle:
        handle.write("\n".join(lines))
    print("Wrote {}".format(OUTPUT))
    return 0


if __name__ == "__main__":
    sys.exit(main())

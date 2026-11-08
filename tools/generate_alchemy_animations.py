#!/usr/bin/env python3
"""Compile authored alchemy clip durations and fragment mappings.

Animations.img stores CryCh 0x746 GlobalAnimationHeaderCAF 0x971 chunks.
Timing is at byte300: start, end and total seconds; it is NOT Blend.Duration.
This generator validates the complete chunk directory before selecting clips.
"""
from __future__ import annotations
import argparse
import json
import math
from pathlib import Path
import struct
import xml.etree.ElementTree as ET
import zipfile

IMG_PATH = 'Animations/Animations.img'
ADB_PATH = 'Animations/Mannequin/ADB/male_alchemy.adb'
EVENTS_PATH = 'Animations/humans/male/male.animevents'
KETTLE_EVENTS_PATH = 'Animations/assets/alchemy_kettle/alchemy_kettle.animevents'
KETTLE_CLIP_PREFIX = 'animations/assets/alchemy_kettle/'
# These CRC event names drive the alchemy callbacks.
MECHANICAL_EVENTS = {'attach', 'detach', 'detachresource', 'use', 'actionend', 'openingbook', 'closingbook', 'attachpot', 'detachpot', 'detachflask', 'potionpouring', 'attachmortar', 'detachmortar', 'attachpestle', 'detachpestle', 'attachsack', 'detachsack', 'attachretort', 'detachretort'}
PLAYER_CLIP_PREFIX = 'animations/humans/male/alchemy/'
CHUNK_HEADER_SIZE = 404
TIME_OFFSET = 300


def extract(image, adb):
    if len(image) < 16 or image[:4] != b'CrCh':
        raise ValueError('Animations.img does not have a CryCh header')
    version, count, directory = struct.unpack_from('<III', image, 4)
    if version != 0x746 or directory + count * 16 > len(image):
        raise ValueError('Unsupported or truncated animation chunk directory')
    clips = {}
    kettle_clips = {}
    for index in range(count):
        kind, version, identity, size, offset = struct.unpack_from('<HHIII', image, directory + index * 16)
        if kind != 0x3007 or version != 0x971 or size != CHUNK_HEADER_SIZE or offset + size > len(image):
            raise ValueError(f'Unsupported animation chunk {index}: {kind:x}/{version:x}/{size}')
        path = image[offset + 4:offset + 260].split(b'\0', 1)[0].decode('ascii')
        if not path.startswith((PLAYER_CLIP_PREFIX, KETTLE_CLIP_PREFIX)):
            continue
        start, end, duration = struct.unpack_from('<fff', image, offset + TIME_OFFSET)
        if not all(math.isfinite(x) for x in (start, end, duration)) or start < 0 or end <= start or abs(end - start - duration) > 0.00001:
            raise ValueError(f'Invalid clip time: {path}')
        name = Path(path).stem
        selected = clips if path.startswith(PLAYER_CLIP_PREFIX) else kettle_clips
        if name in selected:
            raise ValueError(f'Duplicate animation clip: {path}')
        selected[name] = (path, duration)
    fragments = []
    tree = ET.fromstring(adb)
    for entry in tree.find('FragmentList'):
        if not entry.tag.startswith('Alchemy'):
            continue
        for fragment in entry:
            if fragment.attrib.get('Tags', '').startswith('Scope_'):
                continue
            for layer_index, layer in enumerate(fragment.findall('AnimLayer')):
                for animation in layer.findall('Animation'):
                    name = animation.attrib['name']
                    if name not in clips:
                        raise ValueError(f'Missing player clip: {name}')
                    # Native ADB may eventually author speed/start offsets;
                    # do not silently erase these if the source changes.
                    unknown = set(animation.attrib) - {'name', 'flags'}
                    if unknown:
                        raise ValueError(f'Unsupported animation attributes: {unknown}')
                    fragments.append((entry.tag, fragment.attrib.get('Tags', ''), fragment.attrib.get('FragTags', ''), name, layer_index, 'Loop' in animation.attrib.get('flags', '').split('+')))
    return clips, fragments, kettle_clips


def extract_events(source, clips):
    events = []
    for animation in ET.fromstring(source):
        path = animation.attrib['name'].lower()
        if not path.startswith(PLAYER_CLIP_PREFIX):
            continue
        clip = Path(path).stem
        if clip not in clips:
            # male.animevents retains obsolete clips (e.g. alchemy_in)
            # absent from the shipped animation header catalog. No ADB
            # action can use them: extract() requires every referenced clip.
            continue
        for event in animation:
            name = event.attrib['name'].lower()
            if name not in MECHANICAL_EVENTS:
                continue
            normalized = float(event.attrib['time'])
            if not math.isfinite(normalized) or not 0 <= normalized <= 1:
                raise ValueError(f'Invalid normalized animation event time: {path}/{name}')
            events.append((clip, name, normalized))
    # Duplicate authored animation entries exist. Exact duplicate events add
    # no timing information; distinct same-name times remain explicit and
    # cannot be selected through the unique-event lookup.
    return sorted(set(events))



def extract_kettle_motions(source, clips):
    motions = []
    root = ET.fromstring(source)
    for on_fire, name in ((False, 'alchemy_pot_move_up'), (True, 'alchemy_pot_move_down')):
        if name not in clips:
            raise ValueError(f'Missing kettle motion clip: {name}')
        path, duration = clips[name]
        times = {'moving': set(), 'done': set()}
        for animation in root:
            if animation.attrib['name'].lower() != path:
                continue
            for event in animation:
                kind = event.attrib['name'].lower()
                if kind not in times:
                    continue
                value = float(event.attrib['time'])
                if not math.isfinite(value) or not 0 <= value <= 1:
                    raise ValueError(f'Invalid kettle event time: {path}/{kind}')
                times[kind].add(value)
        if any(len(values) != 1 for values in times.values()):
            raise ValueError(f'Missing or ambiguous kettle movement events: {name}')
        moving, done = next(iter(times['moving'])), next(iter(times['done']))
        if not moving < done:
            raise ValueError(f'Unordered kettle movement events: {name}')
        motions.append((on_fire, path, duration, moving, done))
    return motions

def generate(clips, fragments, output, events=(), kettle_motions=()):
    lines = ['// Generated by tools/generate_alchemy_animations.py; do not edit.', '#include "shared/game/alchemy_animations.h"', '', '#include <algorithm>', '', 'namespace KCDC::Shared::Game {', '    const std::vector<AlchemyAnimationClip> g_AlchemyAnimationClips = {']
    for name, (path, duration) in sorted(clips.items()):
        lines.append(f'        {{{json.dumps(name)}, {json.dumps(path)}, {duration!r}F}},')
    lines += ['    };', '', '    const std::vector<AlchemyAnimationFragment> g_AlchemyAnimationFragments = {']
    for fragment, tags, fragment_tags, clip, layer, loop in sorted(set(fragments)):
        lines.append('        {%s, %s, %s, %s, %dU, %s},' % (json.dumps(fragment), json.dumps(tags), json.dumps(fragment_tags), json.dumps(clip), layer, str(loop).lower()))
    lines += ['    };', '', '    const std::vector<AlchemyAnimationEvent> g_AlchemyAnimationEvents = {']
    for clip, name, normalized in events:
        lines.append(f'        {{{json.dumps(clip)}, {json.dumps(name)}, {normalized!r}F}},')
    lines += ['    };', '', '    const std::vector<AlchemyKettleMotion> g_AlchemyKettleMotions = {']
    for on_fire, path, duration, moving, done in kettle_motions:
        lines.append(f'        {{{str(on_fire).lower()}, {json.dumps(path)}, {duration!r}F, {moving!r}F, {done!r}F}},')
    lines += ['    };', '', '    const AlchemyKettleMotion *FindAlchemyKettleMotion(bool onFire) {', '        for (const auto &motion : g_AlchemyKettleMotions)', '            if (motion.onFire == onFire) return &motion;', '        return nullptr;', '    }', ]
    lines += ['', '    const AlchemyAnimationEvent *FindAlchemyAnimationEvent(std::string_view clip, std::string_view name) {', '        const AlchemyAnimationEvent *result = nullptr;', '        for (const auto &event : g_AlchemyAnimationEvents) {', '            if (event.clip != clip || event.name != name) continue;', '            if (result != nullptr) return nullptr;', '            result = &event;', '        }', '        return result;', '    }', '', '    const AlchemyAnimationClip *FindAlchemyAnimationClip(std::string_view name) {', '        const auto found = std::lower_bound(g_AlchemyAnimationClips.begin(), g_AlchemyAnimationClips.end(), name, [](const AlchemyAnimationClip &clip, std::string_view key) { return clip.name < key; });', '        return found != g_AlchemyAnimationClips.end() && found->name == name ? &*found : nullptr;', '    }', '} // namespace KCDC::Shared::Game', '']
    output.write_text('\n'.join(lines), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game_directory', type=Path)
    parser.add_argument('--output', type=Path, default=Path('code/shared/generated/alchemy_animations.cpp'))
    args = parser.parse_args()
    with zipfile.ZipFile(args.game_directory / 'Data/Animations.pak') as pak:
        paths = [path for path in pak.namelist() if path.lower().endswith('/male_alchemy.adb')]
        if len(paths) != 1:
            raise ValueError(f'Expected exactly one male_alchemy.adb, found {paths}')
        clips, fragments, kettle_clips = extract(pak.read(IMG_PATH), pak.read(paths[0]))
        events = extract_events(pak.read(EVENTS_PATH), clips)
        kettle_motions = extract_kettle_motions(pak.read(KETTLE_EVENTS_PATH), kettle_clips)
    generate(clips, fragments, args.output, events, kettle_motions)
    print(f'Generated {len(clips)} player alchemy clip durations, {len(set(fragments))} fragment mappings and {len(events)} mechanical event times')

if __name__ == '__main__':
    main()

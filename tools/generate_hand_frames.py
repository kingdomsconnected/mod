#!/usr/bin/env python3
"""Generate the KCDC table that puts a woman's hands on a man's joint frames.

`male.chr` and `female.chr` name their hand joints alike, so every male clip
binds to a woman's hands by name CRC. Their joint frames are not alike: the
female hand is rolled about 105 degrees from the male one and every finger
inherits it. A clip stores each joint's absolute local rotation, so a male
clip on her rotates her hand around the wrong axes.

The client puts her hand joints on his frames as her skeleton loads and
converts the clips authored on her skeleton to match, so both play natively.
This table is the per-joint frame change it applies: for a joint J,

    C(J) = inverse(female world bind rotation) * male world bind rotation

and a rotation authored on her skeleton becomes inverse(C(parent)) * q * C(J).

- Every skinned joint under `LeftHand`/`RightHand` takes its own C.
- `<Side>Hand_IKTarget` takes its hand's C: animation-driven IK copies the
  target's world rotation onto the hand, so the target must use the hand's
  frame.
- `<Side>Hand_IKBlend` is verbatim: its translation X carries the IK weight,
  so nothing may rotate it.
- `<Side>HandSlot` keeps its own world frame (an identity C), so the CDF's
  weapon attachments and the male clips' weapon grip stay as authored; only
  its parent's frame change reaches it.

Each row also names the joint's parent on her skeleton, which is what a
clip's track needs to find C(parent).

Usage:
    python tools/generate_hand_frames.py <game directory>
        [-o code/shared/generated/hand_frames.cpp]
"""

from __future__ import annotations

import argparse
import math
import os
import struct
import sys
import zipfile

DATA_DIR = "Data"
MALE_CHR = "objects/characters/humans/male/skeleton/male.chr"
FEMALE_CHR = "objects/characters/humans/female/skeleton/female.chr"

# ChunkType_CompiledBones in a 0x746 chunk file.
COMPILED_BONES = 0x2000
# Per file record: controller id, two BONE_PHYSICS_COMP, mass, W2B, B2W, name.
BONE_RECORD = 584
B2W_OFFSET = 264
NAME_OFFSET = 312
PARENT_OFFSET = 572


def read_pak_file(game_dir: str, path: str) -> bytes:
    data_dir = os.path.join(game_dir, DATA_DIR)
    for name in sorted(os.listdir(data_dir)):
        if not name.lower().endswith(".pak"):
            continue
        with zipfile.ZipFile(os.path.join(data_dir, name)) as pak:
            for entry in pak.namelist():
                if entry.lower() == path:
                    return pak.read(entry)
    raise SystemExit(f"{path} is in no pak under {data_dir}")


def read_bones(blob: bytes) -> list[dict]:
    signature, version, count, table = struct.unpack_from("<4sIII", blob, 0)
    if signature != b"CrCh" or version != 0x746:
        raise SystemExit("not a 0x746 chunk file")
    for index in range(count):
        kind, _, _, size, offset = struct.unpack_from("<HHIII", blob, table + index * 16)
        if kind == COMPILED_BONES:
            break
    else:
        raise SystemExit("no CompiledBones chunk")
    bones = []
    for index in range((size - 32) // BONE_RECORD):
        record = offset + 32 + index * BONE_RECORD
        crc = struct.unpack_from("<I", blob, record)[0]
        matrix = struct.unpack_from("<12f", blob, record + B2W_OFFSET)
        name = blob[record + NAME_OFFSET : record + NAME_OFFSET + 256].split(b"\0")[0].decode()
        parent = struct.unpack_from("<i", blob, record + PARENT_OFFSET)[0]
        bones.append({"crc": crc, "name": name, "rotation": quat_from_rows(matrix), "parent": index + parent if parent else -1})
    return bones


def quat_from_rows(m) -> tuple:
    """Quaternion (x, y, z, w) of a row-major Matrix34's rotation, as Quat(Matrix33)."""
    r = [[m[0], m[1], m[2]], [m[4], m[5], m[6]], [m[8], m[9], m[10]]]
    trace = r[0][0] + r[1][1] + r[2][2]
    if trace > 0:
        s = math.sqrt(trace + 1.0) * 2
        return ((r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s, (r[1][0] - r[0][1]) / s, s / 4)
    if r[0][0] > r[1][1] and r[0][0] > r[2][2]:
        s = math.sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2
        return (s / 4, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s, (r[2][1] - r[1][2]) / s)
    if r[1][1] > r[2][2]:
        s = math.sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2
        return ((r[0][1] + r[1][0]) / s, s / 4, (r[1][2] + r[2][1]) / s, (r[0][2] - r[2][0]) / s)
    s = math.sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2
    return ((r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, s / 4, (r[1][0] - r[0][1]) / s)


def multiply(a, b) -> tuple:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx, aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz)


def conjugate(q) -> tuple:
    return (-q[0], -q[1], -q[2], q[3])


def canonical(q) -> tuple:
    length = math.sqrt(sum(c * c for c in q))
    q = tuple(c / length for c in q)
    return q if q[3] >= 0 else tuple(-c for c in q)


def under_hand(bones: list[dict], index: int) -> str | None:
    while index >= 0:
        if bones[index]["name"] in ("LeftHand", "RightHand"):
            return bones[index]["name"]
        index = bones[index]["parent"]
    return None


def build_rows(male: list[dict], female: list[dict]) -> list[tuple]:
    male_by_name = {bone["name"]: bone for bone in male}
    female_by_name = {bone["name"]: bone for bone in female}

    def frame_change(name: str) -> tuple:
        return canonical(multiply(conjugate(female_by_name[name]["rotation"]), male_by_name[name]["rotation"]))

    identity = (0.0, 0.0, 0.0, 1.0)

    def parent_crc(bone: dict) -> int:
        return female[bone["parent"]]["crc"] if bone["parent"] >= 0 else 0

    rows = []
    for index, bone in enumerate(female):
        name = bone["name"]
        hand = under_hand(female, index)
        if hand is None or name not in male_by_name:
            continue
        if name.endswith("_IKBlend"):
            rows.append((name, bone["crc"], parent_crc(bone), "Verbatim", identity))
        elif name.endswith("HandSlot"):
            rows.append((name, bone["crc"], parent_crc(bone), "Frame", identity))
        else:
            rows.append((name, bone["crc"], parent_crc(bone), "Frame", frame_change(name)))
    for side in ("Left", "Right"):
        bone = female_by_name[side + "Hand_IKTarget"]
        rows.append((bone["name"], bone["crc"], parent_crc(bone), "Frame", frame_change(side + "Hand")))
    return rows


def emit(rows: list[tuple]) -> str:
    lines = [
        "// Generated by tools/generate_hand_frames.py from male.chr and female.chr.",
        '#include "shared/game/hand_frames.h"',
        "",
        "namespace KCDC::Shared::Game {",
        "    const std::vector<HandFrameEntry> g_HandFrames = {",
    ]
    for name, crc, parent, kind, (x, y, z, w) in rows:
        lines.append(f'        {{"{name}", 0x{crc:08X}U, 0x{parent:08X}U, HandFrameKind::{kind}, {{{x:.9f}F, {y:.9f}F, {z:.9f}F, {w:.9f}F}}}},')
    lines += ["    };", "} // namespace KCDC::Shared::Game", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("game_dir")
    parser.add_argument("-o", "--output", default=os.path.join("code", "shared", "generated", "hand_frames.cpp"))
    args = parser.parse_args()

    male = read_bones(read_pak_file(args.game_dir, MALE_CHR))
    female = read_bones(read_pak_file(args.game_dir, FEMALE_CHR))
    rows = build_rows(male, female)
    with open(args.output, "w", newline="\n") as out:
        out.write(emit(rows))
    print(f"{len(rows)} joints -> {args.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())

"""Encode the AVM1 actions and SWF tags used by native UI hosts.

Encoding reference: ruffle-rs/ruffle swf/src/avm1/write.rs and swf/src/write.rs.
"""

import struct


def action(opcode, payload=b""):
    return bytes([opcode]) + (struct.pack("<H", len(payload)) if opcode >= 128 else b"") + payload


def push(*values):
    payload = b""
    for value in values:
        if isinstance(value, str):
            payload += b"\0" + value.encode("ascii") + b"\0"
        elif isinstance(value, bool):
            payload += b"\x05" + bytes([value])
        elif isinstance(value, int):
            payload += b"\x07" + struct.pack("<i", value)
        else:
            raise TypeError(type(value))
    return action(0x96, payload)


def root_member(name, value_code):
    return push("_root") + action(0x1C) + push(name) + value_code + action(0x4F)


def when(condition, body):
    return condition + action(0x12) + action(0x9D, struct.pack("<h", len(body))) + body


def function(name, parameters, body):
    # DefineFunction's action length excludes its nested code bytes.
    header = name.encode("ascii") + b"\0" + struct.pack("<H", len(parameters))
    header += b"".join(p.encode("ascii") + b"\0" for p in parameters)
    return action(0x9B, header + struct.pack("<H", len(body))) + body


def tag(code, payload=b""):
    size = len(payload)
    return struct.pack("<H", (code << 6) | min(size, 63)) + (struct.pack("<I", size) if size >= 63 else b"") + payload

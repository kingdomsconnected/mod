"""Lay a KCD2 mod out as a server resource that streams it.

    python tools/import_kcd2_mod.py <mod folder or .zip> <resources>/<name>

A KCD2 mod is `<modid>/mod.manifest` beside `data/*.pak` and
`localization/<Language>_xml.pak`. The resource gets:

- `replace/`: every file of the data paks a server may ship -- models,
  materials, textures, clips' databases, sounds at their stock paths, and the
  table patches `libs/tables/.../<table>__<id>.xml`;
- `localization/<language>/`: the string patches of each `<Language>_xml.pak`;
- `level/levels/<level>/`: the files of each `data/levels/<level>/*.pak`, later
  paks winning as they do for an installed mod;
- `script/scripts/...`: the mod's Lua, which the client runs in a restricted
  environment;
- `package.json`, naming the mod.

Everything else is left out and listed: quests, Storm, AI trees, UI movies,
`mod.cfg`. Both ends refuse a pak carrying any of it, so the tool drops it here
rather than have the server refuse the whole lane. The rules mirror
`code/shared/streamed_assets/streamed_asset_policy.cpp`; the server checks every
entry again when it builds the paks.
"""

import argparse
import io
import json
import re
import sys
import zipfile
from collections import Counter
from pathlib import Path, PurePosixPath

ASSET_EXTENSIONS = (".cgf", ".cgfm", ".chr", ".chrm", ".skin", ".skinm", ".cga", ".cdf", ".mtl", ".dds", ".dds.a", ".adb", ".animevents", ".ogg")
REPLACE_DENIED_ROOTS = {"scripts", "libs", "engine", "shaders", "levels", "localization", "editor"}
SPLIT_TEXTURE_CHUNK = re.compile(r"\.dds\.[1-9][0-9]?a?")
PATCH_FILE = re.compile(r"[a-z0-9_-]+__[a-z0-9_]+\.xml")
LANGUAGE_PAK = re.compile(r"([a-z]+)_xml\.pak")
LEVEL_PAK = re.compile(r"data/levels/([a-z0-9_]+)/[^/]+\.pak")
LEVEL_FOLDERS = {"terrain", "physics", "recast", "hlods", "cestool", "brush", "layers"}
LEVEL_EXTENSIONS = (".dat", ".lst", ".ctc", ".xml", ".cgf", ".cgfm", ".hdr", ".nav")
LEVEL_ENTITY_FILE = re.compile(r"(objects|mission)_[^/]*\.xml")
SCRIPT_COMMAND = re.compile(rb"scriptcommand(?!=\"\")", re.IGNORECASE)
ESCAPED_CLASS = re.compile(rb"class=\"[^\"]*&", re.IGNORECASE)


def level_reason(path, data):
    """Why the level lane would refuse `path` relative to its level folder, or None when it ships."""
    normalized = path.replace("\\", "/").lower()
    segments = normalized.split("/")
    name = segments[-1]
    if re.search(r"[^a-z0-9_./-]", normalized) or any(not segment or segment in (".", "..") for segment in segments):
        return "a level path outside [A-Za-z0-9_./-], or an empty or relative segment"
    if any("." in segment for segment in segments[:-1]):
        return "a dot in a level folder name"
    if len(segments) == 1:
        allowed = name == "occluder.ocm" or re.fullmatch(r"whdata_[0-9]+", name) or LEVEL_ENTITY_FILE.fullmatch(name)
    elif normalized == "tables/ai/scheduler.xml":
        allowed = True
    else:
        allowed = segments[0] in LEVEL_FOLDERS and name[name.find("."):] in LEVEL_EXTENSIONS and "." in name
    if not allowed:
        return f"{name} is not a file a level edit may replace"
    if (len(segments) == 1 and LEVEL_ENTITY_FILE.fullmatch(name)) or (segments[0] == "layers" and name.endswith(".xml")):
        if SCRIPT_COMMAND.search(data) or ESCAPED_CLASS.search(data):
            return f"{name} runs a script"
    return None


def replace_reason(path):
    """Why the replace lane would refuse `path`, or None when it ships."""
    normalized = path.replace("\\", "/").lower()
    segments = normalized.split("/")
    if re.search(r"[^a-z0-9_./-]", normalized):
        return "characters outside [A-Za-z0-9_./-]"
    if len(segments) < 2 or any(not segment or segment in (".", "..") for segment in segments):
        return "no folder, or an empty or relative segment"
    if any("." in segment for segment in segments[:-1]):
        return "a dot in a folder name"
    if normalized.startswith("libs/tables/"):
        return None if PATCH_FILE.fullmatch(segments[-1]) else "a table can only be patched (<table>__<id>.xml)"
    if segments[0] in REPLACE_DENIED_ROOTS:
        return f"{segments[0]}/ is code or data, not an asset"
    if segments[1] == "kcdc":
        return "the kcdc namespace belongs to the stream lane"
    name = segments[-1]
    extension = name[name.find("."):] if "." in name else ""
    if extension == ".caf":
        return "stock animations cannot be replaced"
    if extension in ASSET_EXTENSIONS or SPLIT_TEXTURE_CHUNK.fullmatch(extension):
        return None
    return f"{extension or 'no extension'} is not an asset the engine loads by path"


def is_script(path):
    normalized = path.replace("\\", "/").lower()
    return normalized.startswith("scripts/") and normalized.endswith(".lua")


def script_reason(path, data):
    """Why the script lane would refuse `path`, or None when it ships."""
    normalized = path.replace("\\", "/").lower()
    segments = normalized.split("/")
    if re.search(r"[^a-z0-9_./-]", normalized) or any(not segment or segment in (".", "..") for segment in segments):
        return "a script path outside [A-Za-z0-9_./-], or an empty or relative segment"
    if len(segments) < 3 or any("." in segment for segment in segments[:-1]):
        return "a script outside scripts/.../<name>.lua"
    if data.startswith(b"\x1b") or b"\0" in data:
        return "a precompiled script"
    return None


def mod_files(source):
    """(relative path, bytes) for every file of a mod folder or zip, the mod's own folder stripped."""
    if source.is_dir():
        for path in sorted(source.rglob("*")):
            if path.is_file():
                yield path.relative_to(source).as_posix(), path.read_bytes()
        return
    with zipfile.ZipFile(source) as archive:
        names = [info.filename for info in archive.infolist() if not info.is_dir()]
        manifests = [name for name in names if PurePosixPath(name).name.lower() == "mod.manifest"]
        root = str(PurePosixPath(manifests[0]).parent) + "/" if manifests and "/" in manifests[0] else ""
        for name in names:
            if name.startswith(root):
                yield name[len(root):], archive.read(name)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("mod", type=Path, help="the mod's folder (holding mod.manifest) or a zip of it")
    parser.add_argument("resource", type=Path, help="the resource folder to write, e.g. bin/resources/my-mod")
    args = parser.parse_args()

    name = args.resource.name
    if not re.fullmatch(r"[a-z0-9_-]{1,64}", name):
        sys.exit(f"'{name}' is not a resource name: lowercase letters, digits, '-' and '_'")
    if args.resource.exists() and any(args.resource.iterdir()):
        sys.exit(f"{args.resource} is not empty")

    shipped = Counter()
    dropped = Counter()
    manifest = None
    for relative, data in mod_files(args.mod):
        lowered = relative.lower()
        if lowered == "mod.manifest":
            manifest = data.decode("utf-8", "replace")
            continue
        language = LANGUAGE_PAK.fullmatch(PurePosixPath(lowered).name) if lowered.startswith("localization/") else None
        level = LEVEL_PAK.fullmatch(lowered)
        if level:
            with zipfile.ZipFile(io.BytesIO(data)) as pak:
                for entry in pak.infolist():
                    if entry.is_dir():
                        continue
                    contents = pak.read(entry)
                    reason = level_reason(entry.filename, contents)
                    if reason:
                        dropped[reason] += 1
                        continue
                    target = args.resource / "level" / "levels" / level.group(1) / entry.filename.replace("\\", "/").lower()
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(contents)
                    shipped["level"] += 1
        elif lowered.startswith("data/") and lowered.endswith(".pak"):
            with zipfile.ZipFile(io.BytesIO(data)) as pak:
                for entry in pak.infolist():
                    if entry.is_dir():
                        continue
                    if is_script(entry.filename):
                        contents = pak.read(entry)
                        reason = script_reason(entry.filename, contents)
                        if reason:
                            dropped[reason] += 1
                            continue
                        target = args.resource / "script" / entry.filename.replace("\\", "/").lower()
                        target.parent.mkdir(parents=True, exist_ok=True)
                        target.write_bytes(contents)
                        shipped["script"] += 1
                        continue
                    reason = replace_reason(entry.filename)
                    if reason:
                        dropped[reason] += 1
                        continue
                    target = args.resource / "replace" / entry.filename
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(pak.read(entry))
                    shipped["replace"] += 1
        elif language:
            with zipfile.ZipFile(io.BytesIO(data)) as pak:
                for entry in pak.infolist():
                    if entry.is_dir():
                        continue
                    if "/" in entry.filename or not PATCH_FILE.fullmatch(entry.filename.lower()):
                        dropped["a string file that is not a <file>__<id>.xml patch"] += 1
                        continue
                    target = args.resource / "localization" / language.group(1) / entry.filename
                    target.parent.mkdir(parents=True, exist_ok=True)
                    target.write_bytes(pak.read(entry))
                    shipped["localization"] += 1
        else:
            dropped[f"{PurePosixPath(lowered).name}: not a data or <Language>_xml pak"] += 1

    if manifest is None:
        sys.exit(f"{args.mod} holds no mod.manifest")
    title = re.search(r"<name>([^<]*)</name>", manifest)
    version = re.search(r"<version>([^<]*)</version>", manifest)
    package = {
        "name": name,
        "version": version.group(1) if version else "1.0.0",
        "description": f"Streams the KCD2 mod {title.group(1) if title else name}",
        "mafiahub": {"serverScripts": [], "clientScripts": [], "files": []},
    }
    args.resource.mkdir(parents=True, exist_ok=True)
    (args.resource / "package.json").write_text(json.dumps(package, indent=2) + "\n", encoding="utf-8")

    print(f"{name}: {shipped['replace']} replace file(s), {shipped['localization']} string patch(es), {shipped['level']} level file(s), {shipped['script']} script(s)")
    for reason, count in dropped.most_common():
        print(f"  left out {count}: {reason}")


if __name__ == "__main__":
    main()

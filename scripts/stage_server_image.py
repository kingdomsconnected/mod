"""Create an allowlisted Docker context from a Linux release artifact."""

import argparse
import pathlib
import re
import shutil
import subprocess

from render_pterodactyl_egg import render

# Resources the image seeds an empty /home/container/resources with on first run.
DEFAULT_RESOURCES = ["kcdc-gamemode"]
NOTICES = ["LICENSE.txt", "NOTICE.txt", "licenses/Framework-LICENSE.txt"]

# The published side of a resource, beside its `src` tree.
DEFAULT_RESOURCE_FILES = ["package.json", "package-lock.json", "README.md", "tsconfig.json", "LICENSE.txt", "NOTICE.txt"]


def verify_elf(path):
    with path.open("rb") as binary:
        if binary.read(5) != b"\x7fELF\x02":
            raise ValueError(f"Expected a 64-bit ELF runtime: {path.name}")
    sections = subprocess.check_output(["readelf", "--wide", "--sections", str(path)], text=True)
    if re.search(r"\.(?:z?debug\w*|symtab|gdb_index|gnu_debug\w*)\b", sections):
        raise ValueError(f"Debug information in public runtime: {path.name}")


def stage(bundle, destination, image=None):
    bundle = bundle.resolve(strict=True)
    # Never merge with stale files on a runner.
    destination.mkdir(parents=True, exist_ok=False)
    runtime = destination / "runtime"
    runtime.mkdir()
    libnode = sorted(p.name for p in bundle.iterdir() if re.fullmatch(r"libnode\.so(?:\.\d+)*", p.name))
    if not libnode:
        raise ValueError("Linux bundle is missing libnode.so")
    for name in ["KCDCServer", "crashpad_handler"] + libnode:
        source = (bundle / name).resolve(strict=True)
        if source.parent != bundle or not source.is_file():
            raise ValueError(f"Runtime must resolve inside the bundle: {name}")
        target = runtime / name
        shutil.copyfile(source, target)
        # Preserve tested server bytes on promotion; strip vendor helpers only.
        if name != "KCDCServer":
            subprocess.run(["strip", "--strip-all", str(target)], check=True)
        verify_elf(target)
    for name in NOTICES:
        source = (bundle / name).resolve()
        if bundle not in source.parents or not source.is_file():
            raise ValueError(f"Required notice must resolve inside the bundle: {name}")
        shutil.copyfile(source, ensure_parent(runtime / name))
    repository = pathlib.Path(__file__).resolve().parents[1]
    for source, name in [(repository / "deploy/Dockerfile", "Dockerfile"), (repository / "deploy/.dockerignore", ".dockerignore"), (repository / "deploy/entrypoint.sh", "entrypoint.sh")]:
        (destination / name).write_text(source.read_text(), encoding="utf-8", newline="\n")
    stage_default_resources(bundle, destination)
    render(image or "kcdc-server:local", destination / "egg-kcdc.json")


def stage_default_resources(bundle, destination):
    """Copy the default gamemode out of the bundle, so a fresh container is seeded with it.

    From the bundle, not the repository: it is the same `resources/` the tested
    server ran and the zip ships, and the image job checks out only the
    packaging scripts anyway.

    The resource travels as source -- an operator reads it, changes it and
    builds it where it runs -- so what is staged is its TypeScript and what
    builds it, never a `dist/`. A bundle's `resources/` is otherwise
    unrestricted, so this is an allowlist rather than a copy: the directories
    named in DEFAULT_RESOURCES, and within each only the files named in
    DEFAULT_RESOURCE_FILES and the TypeScript under `src/`.
    """
    for name in DEFAULT_RESOURCES:
        source = (bundle / "resources" / name).resolve()
        if not source.is_dir() or bundle.resolve() not in source.parents:
            raise ValueError(f"Default resource is missing from the bundle: resources/{name}")
        target = destination / "default-resources" / name

        sources = sorted(source.joinpath("src").rglob("*.ts")) + sorted(source.joinpath("src").rglob("tsconfig*.json"))
        if not sources:
            raise ValueError(f"Default resource carries no TypeScript: resources/{name}/src")
        for relative in DEFAULT_RESOURCE_FILES:
            copy_inside(source, source / relative, target)
        for script in sources:
            copy_inside(source, script, target)


def copy_inside(root, path, target):
    """Copy one file, refusing anything that resolves outside the resource it claims to be in."""
    resolved = path.resolve()
    if not resolved.is_file() or root not in resolved.parents:
        raise ValueError(f"Default resource file must resolve inside it: {path}")
    shutil.copyfile(resolved, ensure_parent(target / resolved.relative_to(root)))


def ensure_parent(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=pathlib.Path)
    parser.add_argument("destination", type=pathlib.Path)
    parser.add_argument("--image", help="Exact image tag embedded in the egg (default: kcdc-server:local)")
    arguments = parser.parse_args()
    stage(arguments.bundle, arguments.destination, arguments.image)

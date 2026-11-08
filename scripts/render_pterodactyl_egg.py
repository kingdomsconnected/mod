"""Render an importable PTDL_v2 egg for an exact KCDC image tag."""

import argparse
import json
import pathlib
import re


def render(image, destination):
    if not re.fullmatch(r"[a-z0-9][a-z0-9./_-]*:[A-Za-z0-9_][A-Za-z0-9_.-]{0,127}", image):
        raise ValueError("Expected an explicit Docker image:tag, not a URL or shell command")
    repository = pathlib.Path(__file__).resolve().parents[1]
    egg = json.loads((repository / "deploy/pterodactyl/egg-kcdc.template.json").read_text(encoding="utf-8"))
    egg["_comment"] = "KCDC server egg. Contains only public deployment metadata."
    egg["docker_images"] = {"KCDC Linux amd64": image}
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(egg, indent=4) + "\n", encoding="utf-8", newline="\n")
    return egg


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args()
    render(args.image, args.out)

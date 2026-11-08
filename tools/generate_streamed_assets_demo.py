"""Build a throwaway server resource that exercises streamed assets end to end.

The resource carries both lanes:

- `replace/` swaps the stock bucket's diffuse texture for a generated magenta
  checker, so every stock bucket in the world turns magenta.
- `stream/` adds a new model at `objects/kcdc/<resource>/demo_bucket.cgf`: the
  stock bucket's geometry with its material name rewritten to a new `.mtl`
  beside it, whose diffuse is a generated green checker. It also adds a clip
  (`animations/kcdc/<resource>/demo_argue.caf`), a voice line
  (`sounds/kcdc/<resource>/demo_line.ogg`), ATL controls naming one new
  trigger over a stock UI event (`sounds/kcdc/<resource>/atl/demo.xml`) and a
  particle library (`libs/particles/kcdc_<resource>_sparks.xml`, a copy of the
  stock woodcutting library under its own name).
- FMOD banks: FMOD's own example project as compiled for the FMOD GDExtension
  demo (MIT), fetched from a pinned commit -- master and strings banks, plus
  Vehicles, SFX and Music, about 32 MB, which is also enough for the joining
  screen's progress bar to show. Built at bank version 141, which the game's
  FMOD 2.02.21 loads. A second ATL trigger plays one of their events.

  FMOD loads a second project's master bank beside the game's only when its
  master bus carries the game's GUID -- the rule FMOD sets for user content --
  so the example project's master bus GUID is rewritten to kcd.bank's in the
  master and strings banks. Without it FMOD refuses the master bank (error
  28) and every event of the project stops the moment it starts.

Joining spawns nothing: the assets are only mounted. The server console command
`assetdemo` puts the showcase up for every player -- the stock bucket in the
left hand and the new one in the right, both buckets as props, a streamed
effect, and an NPC playing the clip through `playAnimation({ clip })` -- and the
client script then checks the trigger, the effect and the model resolve on the
player's machine.

Game files are only read, from the local install, and the output goes to a
`zz-` resource that is meant to be deleted after the test. Nothing here is
committed.
"""

import argparse
import re
import struct
import urllib.request
import zipfile
from pathlib import Path

DEFAULT_GAME = Path(r"D:\SteamLibrary\steamapps\common\KingdomComeDeliverance2")
DEFAULT_OUT = Path(__file__).resolve().parents[3].parent / "builds" / "build-64" / "bin" / "resources"
RESOURCE = "zz-asset-demo"
LIBRARY = f"kcdc_{RESOURCE}_sparks"
TRIGGER = "kcdc_asset_demo_cue"
BANK_TRIGGER = "kcdc_asset_demo_engine"

# FMOD's example project banks, as the FMOD GDExtension demo (MIT) ships them.
FMOD_BANKS_URL = "https://raw.githubusercontent.com/utopia-rise/fmod-gdextension/b74dd111eeaa62fd583f37578af5cc02f61c8fbb/demo/assets/Banks/"
FMOD_BANKS = {"Master.bank": "master", "Master.strings.bank": "master.strings", "Vehicles.bank": "vehicles", "SFX.bank": "sfx", "Music.bank": "music"}
FMOD_EVENT = "event:/Vehicles/Car Engine"

# Master bus GUIDs for the example project and the game's kcd.bank.
EXAMPLE_MASTER_BUS = bytes.fromhex("7a029daf1f3aa849a9ef4cbe20673632")
GAME_MASTER_BUS = bytes.fromhex("2f0f688df19d6a47b7fb7f654c8a2db1")

STOCK_BUCKET_DIR = "Objects/manmade/common_furniture/bucket/"
STOCK_BUCKET_MATERIAL = b"objects/manmade/common_furniture/bucket/bucket_a"
STOCK_BUCKET_MTL = STOCK_BUCKET_DIR + "bucket_a.mtl"
STOCK_BUCKET_PARTS = ["bucket_a_a.cgf", "bucket_a_a.cgfm", "bucket_a_a_lod1.cgf", "bucket_a_a_lod1.cgfm", "bucket_a_a_lod2.cgf", "bucket_a_a_lod2.cgfm"]
STOCK_CLIP = "Animations/humans/male/cinematics/universal/cin_standing_arguing_mf_mf_male1.caf"
STOCK_PARTICLES = "Libs/Particles/particles_woodcutting.xml"
STOCK_TRIGGER_FILE = "Libs/GameAudio/ui.xml"
STOCK_TRIGGER = "ui_helmet_up"


def dxt1_checker(size, square, color_a, color_b):
    """A BC1 DDS with a full mip chain; every 4x4 block is one solid colour."""

    def rgb565(color):
        r, g, b = color
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)

    mips = []
    level, sq = size, square
    while level >= 1:
        blocks = max(1, level // 4)
        out = bytearray()
        for by in range(blocks):
            for bx in range(blocks):
                cell = ((bx * 4) // max(sq, 4) + (by * 4) // max(sq, 4)) % 2
                c = rgb565(color_a if cell == 0 else color_b)
                # c0 == c1 selects the three-colour mode; index 0 is c0 everywhere.
                out += struct.pack("<HHI", c, c, 0)
        mips.append(bytes(out))
        level //= 2
        sq = max(1, sq // 2)

    header = bytearray(128)
    header[0:4] = b"DDS "
    flags = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000  # caps, height, width, pixelformat, mipcount, linearsize
    struct.pack_into("<IIIIIII", header, 4, 124, flags, size, size, len(mips[0]), 0, len(mips))
    struct.pack_into("<II4s", header, 76, 32, 0x4, b"DXT1")  # pixel format: size, FOURCC, code
    struct.pack_into("<I", header, 108, 0x1000 | 0x8 | 0x400000)  # texture, complex, mipmap
    return bytes(header) + b"".join(mips)


def read_from_paks(paths, wanted):
    """Read entries out of the game's paks, case-insensitively, without touching them."""
    found = {}
    lowered = {name.lower(): name for name in wanted}
    for pak in paths:
        with zipfile.ZipFile(pak) as archive:
            for info in archive.infolist():
                key = info.filename.lower()
                if key in lowered and lowered[key] not in found:
                    found[lowered[key]] = archive.read(info)
        if len(found) == len(wanted):
            break
    missing = [name for name in wanted if name not in found]
    if missing:
        raise SystemExit(f"not found in the game's paks: {missing}")
    return found


def retarget_material(data):
    """Point a CGF's embedded material name at the demo's own .mtl, keeping the field's length."""
    new = f"objects/kcdc/{RESOURCE}/demo_bucket".encode()
    assert len(new) < len(STOCK_BUCKET_MATERIAL)
    needle = STOCK_BUCKET_MATERIAL + b"\0"
    if needle not in data:
        return data
    return data.replace(needle, new + b"\0" * (len(needle) - len(new)))


def fetch(url, cache):
    """Downloads once into the scratch cache beside the output, which is deleted with it."""
    if not cache.exists():
        cache.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url) as response:
            cache.write_bytes(response.read())
    return cache.read_bytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", type=Path, default=DEFAULT_GAME)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    data_dir = args.game / "Data"
    root = args.out / RESOURCE
    stream = root / "stream"
    replace = root / "replace"

    objects = sorted(data_dir.glob("IPL_Objects-part*.pak")) + sorted(data_dir.glob("Objects-part*.pak"))
    bucket = read_from_paks(objects, [STOCK_BUCKET_DIR + part for part in STOCK_BUCKET_PARTS] + [STOCK_BUCKET_MTL])
    clip = read_from_paks([data_dir / "Cinematics.pak"], [STOCK_CLIP])[STOCK_CLIP]
    game_data = read_from_paks([data_dir / "IPL_GameData.pak"], [STOCK_PARTICLES, STOCK_TRIGGER_FILE])

    voice_pak = sorted((args.game / "Localization").glob("IPL_*.pak"))[0]
    with zipfile.ZipFile(voice_pak) as archive:
        lines = sorted((info for info in archive.infolist() if info.filename.lower().endswith(".ogg") and 40_000 < info.file_size < 120_000), key=lambda info: info.filename)
        voice = archive.read(lines[0])

    # replace/: the stock bucket's diffuse, magenta.
    stock_diffuse = replace / "objects/manmade/common_furniture/bucket/bucket_a_diff.dds"
    stock_diffuse.parent.mkdir(parents=True, exist_ok=True)
    stock_diffuse.write_bytes(dxt1_checker(256, 32, (255, 0, 255), (40, 0, 40)))

    # stream/: a new bucket with its own material and texture.
    model_dir = stream / f"objects/kcdc/{RESOURCE}"
    model_dir.mkdir(parents=True, exist_ok=True)
    for part in STOCK_BUCKET_PARTS:
        name = part.replace("bucket_a_a", "demo_bucket")
        (model_dir / name).write_bytes(retarget_material(bucket[STOCK_BUCKET_DIR + part]))

    texture = f"textures/kcdc/{RESOURCE}/demo_checker.dds"
    material = bucket[STOCK_BUCKET_MTL].decode("ascii")
    material = material.replace('File="./bucket_a_diff.tif"', f'File="{texture}"')
    material = material.replace('File="./', f'File="{STOCK_BUCKET_DIR.lower()}')
    (model_dir / "demo_bucket.mtl").write_text(material, encoding="ascii")

    texture_path = stream / texture
    texture_path.parent.mkdir(parents=True, exist_ok=True)
    texture_path.write_bytes(dxt1_checker(256, 32, (0, 255, 0), (255, 255, 255)))

    clip_path = stream / f"animations/kcdc/{RESOURCE}/demo_argue.caf"
    clip_path.parent.mkdir(parents=True, exist_ok=True)
    clip_path.write_bytes(clip)

    voice_path = stream / f"sounds/kcdc/{RESOURCE}/demo_line.ogg"
    voice_path.parent.mkdir(parents=True, exist_ok=True)
    voice_path.write_bytes(voice)

    # ATL: one new trigger playing a stock UI event, parsed at the level's scope.
    stock_triggers = game_data[STOCK_TRIGGER_FILE].decode("utf-8")
    event = re.search(r'<ATLTrigger atl_name="%s">\s*(<FmodEvent[^>]*/>)' % STOCK_TRIGGER, stock_triggers).group(1)
    atl_path = stream / f"sounds/kcdc/{RESOURCE}/atl/demo.xml"
    atl_path.parent.mkdir(parents=True, exist_ok=True)
    bank_event = re.sub(r'fmod_name="[^"]*"', 'fmod_name="%s"' % FMOD_EVENT, event)
    atl_path.write_text(
        '<?xml version="1.0" encoding="us-ascii"?>\n<ATLConfig atl_name="%s">\n  <AudioTriggers>\n    <ATLTrigger atl_name="%s">\n      %s\n    </ATLTrigger>\n    <ATLTrigger atl_name="%s">\n      %s\n    </ATLTrigger>\n  </AudioTriggers>\n</ATLConfig>\n' % (RESOURCE, TRIGGER, event, BANK_TRIGGER, bank_event),
        encoding="ascii",
    )

    # FMOD: the banks go where the game's own are, under the resource's prefix.
    bank_dir = stream / "sounds/fmod/build/pc"
    bank_dir.mkdir(parents=True, exist_ok=True)
    for source, name in FMOD_BANKS.items():
        data = fetch(FMOD_BANKS_URL + source, args.out.parent / ".streamed-assets-demo-cache" / source)
        if source.startswith("Master"):
            assert EXAMPLE_MASTER_BUS in data, source
            data = data.replace(EXAMPLE_MASTER_BUS, GAME_MASTER_BUS)
        (bank_dir / f"kcdc_{RESOURCE}_{name}.bank").write_bytes(data)

    # Particles: the stock woodcutting library under a name of its own.
    particles = game_data[STOCK_PARTICLES].decode("ascii")
    particles = particles.replace('<ParticleLibrary Name="particles_woodcutting"', '<ParticleLibrary Name="%s"' % LIBRARY, 1)
    library_path = stream / f"libs/particles/{LIBRARY}.xml"
    library_path.parent.mkdir(parents=True, exist_ok=True)
    library_path.write_text(particles, encoding="ascii")

    (root / "package.json").write_text(
        """{
  "name": "%s",
  "version": "1.0.0",
  "author": "KCDC",
  "description": "Throwaway streamed-asset demo; delete after the test",
  "mafiahub": {
    "serverScripts": ["index.js"],
    "clientScripts": ["client.js"],
    "files": ["client.js"]
  }
}
"""
        % RESOURCE,
        encoding="ascii",
    )
    (root / "index.js").write_text(
        """"use strict";
const STOCK = "objects/manmade/common_furniture/bucket/bucket_a_a.cgf";
const STREAMED = "objects/kcdc/%(resource)s/demo_bucket.cgf";
const CLIP = "animations/kcdc/%(resource)s/demo_argue.caf";
const EFFECT = "%(library)s.cutting.wood_split";
const BOTH = [{ model: STOCK, hand: "left" }, { model: STREAMED, hand: "right" }];

function ahead(player, distance, side) {
    const facing = player.rotation.rotateVector(new Vector3(0, 1, 0));
    const length = Math.hypot(facing.x, facing.y) || 1;
    const fx = facing.x / length, fy = facing.y / length;
    const p = player.position;
    return new Vector3(p.x + fx * distance - fy * side, p.y + fy * distance + fx * side, p.z);
}

function facingPlayer(player) {
    const facing = player.rotation.rotateVector(new Vector3(0, 1, 0));
    return Quaternion.fromAxisAngle(new Vector3(0, 0, 1), Math.atan2(facing.x, -facing.y));
}

function show(player) {
    const held = player.playAnimation("", { props: BOTH });
    const stockProp = Prop.spawn(STOCK, ahead(player, 2.0, -0.8), undefined, 1.5, "static");
    const streamedProp = Prop.spawn(STREAMED, ahead(player, 2.0, 0.8), undefined, 1.5, "static");
    console.log(`[asset-demo] streamed prop ${streamedProp.id}: model ${streamedProp.model}, name ${streamedProp.modelName}, group ${streamedProp.modelGroup}`);
    const effect = Vfx.spawn(EFFECT, ahead(player, 2.5, 0), { scale: 2 });
    console.log(`[asset-demo] streamed effect ${effect.id}: ${effect.effect} (library ${effect.library})`);
    const npc = Npc.create({ soul: "guard", position: ahead(player, 3.5, 1.2), rotation: facingPlayer(player), name: "", interactable: false, nametag: false });
    setTimeout(() => {
        const clip = npc.playAnimation("", { clip: CLIP, loop: true, props: [{ model: STREAMED, hand: "right" }] });
        console.log(`[asset-demo] npc ${npc.id} plays the streamed clip holding the streamed bucket: ${clip}`);
        player.emit("zz-asset-demo:play", "{}");
    }, 4000);
    console.log(`[asset-demo] ${player.nickname} holds both buckets: ${held}; stock prop ${stockProp ? stockProp.id : "refused"}`);
}

Events.on("playerAnimationEnd", (player, fragment, reason) => {
    console.log(`[asset-demo] ${player.nickname} animation ${fragment} ended: ${reason}`);
});

Events.on("consoleCommand", (command) => {
    if (command === "assetdemo") {
        for (const player of Player.all()) {
            show(player);
        }
    }
});
"""
        % {"resource": RESOURCE, "library": LIBRARY},
        encoding="ascii",
    )

    (root / "client.js").write_text(
        """"use strict";
const STREAMED = "objects/kcdc/%(resource)s/demo_bucket.cgf";
const LINE = "sounds/kcdc/%(resource)s/demo_line";
const TRIGGER = "%(trigger)s";
const BANK_TRIGGER = "%(bank_trigger)s";
const EFFECT = "%(library)s.cutting.wood_split";

Events.on("zz-asset-demo:play", () => {
    if (!LocalPlayer) {
        return;
    }
    console.log(`[asset-demo] model mounted: ${Assets.has(STREAMED)}, trigger parsed: ${Audio.hasTrigger(TRIGGER)}, effect resolves: ${Vfx.has(EFFECT)}`);
    console.log(`[asset-demo] trigger played: ${Audio.play(TRIGGER)}`);
    console.log(`[asset-demo] bank trigger parsed: ${Audio.hasTrigger(BANK_TRIGGER)}, global: ${Audio.play(BANK_TRIGGER)}, on the player: ${Audio.playOnEntity(BANK_TRIGGER, LocalPlayer.entityId)}`);
    console.log(`[asset-demo] control, stock trigger on the player: ${Audio.playOnEntity(TRIGGER, LocalPlayer.entityId)} (entity ${LocalPlayer.entityId})`);
    const near = World.entitiesInRadius(LocalPlayer.position, 8, { class: "NPC", max: 4 });
    for (const entity of near) {
        console.log(`[asset-demo] entity ${entity.entityId}: line ${Audio.playFile(LINE, entity.entityId, "50")}`);
    }
});
"""
        % {"resource": RESOURCE, "library": LIBRARY, "trigger": TRIGGER, "bank_trigger": BANK_TRIGGER},
        encoding="ascii",
    )

    print(f"wrote {root}")


if __name__ == "__main__":
    main()

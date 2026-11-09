# Server-streamed custom assets

A KCDC server streams new assets and replacements of stock ones -- models,
materials, textures, animation clips, sounds, ATL audio controls, FMOD banks
and particle libraries -- to its clients, which load them through the game's
own loaders.

## For server owners

A resource gets two optional folders beside its scripts:

```
resources/<resource>/
  stream/    new assets, each under <category>/kcdc/<resource>/...
  replace/   files at stock paths, which then win over the game's own
```

The server builds one deterministic pak per lane folder of each resource and
ships it with the resource, exactly like its client scripts: announced in the
resource list, downloaded by the same delta transfer, refreshed when the
resource restarts. A resource with asset folders and no client scripts ships
its assets only. Starting, restarting or stopping a resource while players are
connected reaches them in place: `start <resource>`, or `restart <resource>`
after editing its files, and a changed texture, mesh or material is reloaded
where it is resident.

### Stream lane paths

| Kind               | Path                                                                                                        |
| ------------------ | ----------------------------------------------------------------------------------------------------------- |
| Models, materials  | `objects/`, `characters/` or `materials/kcdc/<r>/...` (`.cgf .cgfm .chr .chrm .skin .skinm .cga .cdf .mtl`) |
| Textures           | `textures/kcdc/<r>/...` (`.dds`, split mips `.dds.N`, `.dds.a`, `.dds.Na`)                                  |
| Clips              | `animations/kcdc/<r>/...caf` (also `.adb`, `.animevents`)                                                   |
| Sounds             | `sounds/kcdc/<r>/...ogg`                                                                                    |
| ATL controls       | `sounds/kcdc/<r>/atl/<name>.xml`                                                                            |
| FMOD banks         | `sounds/fmod/build/pc/kcdc_<r>_<name>[.strings\|.assets\|.streams].bank`                                    |
| Particle libraries | `libs/particles/kcdc_<r>_<name>.xml`                                                                        |

Paths are lowercased and may hold only `A-Z a-z 0-9 _ . / -`. Folder names
may not contain a dot (CMatMan keys materials by the name cut at the first
one). The replace lane takes the model, material, texture, `.adb`,
`.animevents` and `.ogg` extensions above -- no `.xml` and no `.bank`, so ATL
controls, FMOD banks and particle libraries cannot be replaced -- at any
stock path except `scripts/ libs/ engine/ shaders/ levels/ localization/
editor/` or `<root>/kcdc/`, and never a stock `.caf` (the animation image
keeps its metadata). Both ends enforce these rules; a pak that breaks one is
refused whole.

### KCD2 mods

A server ships KCD2 mods through the same resources; a player's own installed
mods are never loaded by KCDC. The game still finds everything under `mods/`
and reads `mod_order.txt`, but the client empties its enabled list right after
that scan (`C_ModManager`, run once from `CSystem::Init`), so no mod pak is
mounted and no `mod.cfg`, `scripts/mods/*.lua` or localization patch runs.
Nothing under `mods/` is touched; the log says
`Ignoring local mods: N installed, M enabled`.

To ship a mod, lay it out as a resource with the import tool:

```
python tools/import_kcd2_mod.py <mod folder or .zip> <server>/resources/<resource>
```

```
resources/<resource>/
  replace/        what the mod's data/*.pak carry that a server may ship
  localization/
    <language>/   the string patches of its Localization/<Language>_xml.pak
  package.json
```

It leaves out, and lists, whatever a server never ships -- Lua, quests, Storm,
AI trees, UI movies, `mod.cfg` -- because the server refuses a whole lane over
one such file.

| Mod content                                          | Where it goes                 | What the client does                                                                                                                                                                                                                                          |
| ---------------------------------------------------- | ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Models, materials, textures, sounds, `.adb`          | `replace/` at the mod's paths | Replace lane, as above                                                                                                                                                                                                                                        |
| Table patches `libs/tables/.../<table>__<id>.xml`    | `replace/` at the mod's paths | Each `<id>` is reported to the game as an enabled mod, so every database's own loader merges the file row by row -- items, souls, shops, presets and AI brains as much as plain tables. Every database but the AI scheduler's is read again at the level edge |
| String patches `<file>__<id>.xml`                    | `localization/<language>/`    | Mounted at the game's `Localization` root and merged by the game's own mod-patch loader; the language in use wins, English is the fallback. Retired patches are undone by reading the stock file again                                                        |
| `scripts/mods/*.lua`, `mod.cfg`                      | --                            | Refused: a server never runs Lua or sets console variables                                                                                                                                                                                                    |
| `data/quests/<id>.xml`, `libs/storm/storm__<id>.xml` | --                            | Refused: the game merges them only at boot                                                                                                                                                                                                                    |
| Level edits, `data/levels/<level>/*.pak`             | `level/levels/<level>/`       | Mounted over the game's level files before the level loads, each file replacing the game's whole. See [Level edits](#level-edits)                                                                                                                             |

A table can only be patched, never replaced whole, and a patch's `<id>` is
lowercase letters, digits and `_`, as the game's mod ids are.

The item classes a mod adds (`libs/tables/item/item__<id>.xml`) join the
server's item catalog while the resource runs, and each client's while the pak
is mounted, read from the same file on both ends. `Inventory.add`,
`Inventory.set`, `player.giveItem` and the rest take them by GUID or table
name like any shipped class, and `Inventory.classes()` lists them. Their
quality range and repairability follow the authored attributes and category
by the rules the compiled catalog was generated with; a piece whose clothing
component carries one of the game's immutable armor types (spurs, belts,
pouches, horse tack) never changes condition, whether the component is the
game's or one the mod's own `CharacterComponent__<id>.xml` defines. A string patch
is merged for the language the game ran with when the patch was applied.

The recipes a mod adds or changes (`libs/tables/minigame/alchemyrecipe__<id>.xml`
and `blacksmithrecipes__<id>.xml`) join the server's crafting the same way, and
the server brews and forges them by the game's rules; see
[custom-recipes.md](custom-recipes.md).

A resource's classes join the catalog once it has started, after its own
scripts and `resourceStart` handlers have run. A resource whose scripts name a
mod's classes while they start lists the mod's resource as a dependency, which
starts first. Two resources that define the same GUID settle it the same way on
the server and every client: the resource whose name sorts first keeps it. A
mod's row for a class the game already ships changes the game's copy, but the
catalog keeps the shipped entry for it. An `ItemAlias` may name a class from
any running resource, or later in its own file.

Table patches are applied and retired at the level edge, between the old
level's unload and the new one's load: every item, soul and actor in a level
points into the databases' rows, which a reload frees. Joining is one such
edge, so a session's patches are in place before its level loads; patches a
resource ships mid-session apply at the next level load. A retired pak carrying
table patches is closed there rather than disabled, because the table loader
finds patches through CryPak's file listing, which still lists a disabled pak,
and fails a whole table on a listed file it cannot read. One that `ClosePack`
refuses stays readable, its patches in use, until a later level edge closes it.
String patches apply as soon as their pak is mounted, mid-session included.

### Level edits

A resource's `level/` folder changes the level itself: terrain, navigation,
physics, far-view meshes, the level's object list and its daily schedule.

```
resources/<resource>/
  level/
    levels/<level>/
      terrain/terrain.dat
      recast/rdnavmesh_1.nav
      objects_mission0.xml
```

It takes these files inside `levels/<level>/`, and nothing else:

| Where           | Files                                                |
| --------------- | ---------------------------------------------------- |
| The folder root | `objects_*.xml`, `mission_*.xml`, `occluder.ocm`, `whdata_<n>` |
| `tables/ai/`    | `scheduler.xml`                                      |
| `terrain/ physics/ recast/ hlods/ cestool/ brush/ layers/` | `.dat .lst .ctc .xml .cgf .cgfm .hdr .nav` |

A level's info and data files stay closed, because they name a script the
load runs. Entity XML (`objects_*.xml`, `mission_*.xml`, `layers/*.xml`) is
refused if a trigger or flow node in it carries a script command; the server
leaves such a file out, and a client refuses a pak that still has one.

Each file replaces the game's whole file, so two resources shipping the same
file do not combine, and a game update that changes it can leave the edit
stale. A level edit applies from the next join: one changed while players are
connected waits, because the level has already read its files. These files
are large, so expect a download prompt.

### Content rules the engine sets

- **Clips** must be authored for the human skeleton and in place: they play
  through Mannequin with MovementControlMethod letting the clip drive the
  root, so root motion moves the body.
- **FMOD banks** follow FMOD's rule for user content: the project's master
  bank must use the game's master bus GUID (`kcd.bank`'s `bus:/`,
  `{8d680f2f-9df1-476a-b7fb-7f654c8a2db1}`) and a GUID of its own.
  FMOD otherwise refuses the master bank (`FMOD_ERR_INTERNAL`, 28) and every
  event of the project stops the moment it starts. Banks must be built with
  FMOD Studio 2.02 or earlier (the game runs 2.02.21; bank format 142).
  Event paths must not collide with the game's. Name the master bank
  `kcdc_<r>_master.bank` and its strings bank `kcdc_<r>_master.strings.bank`:
  the client loads only `*_master.bank` and `*.strings.bank` directly, before
  the rest, and registers every other bank as an event bank.
- **ATL triggers** name FMOD events by path (`fmod_name`); `fmod_id` is
  ignored. Trigger names should carry a resource prefix: the first parse of
  a name wins.
- **Particle effects** are named `kcdc_<r>_<library>.<group>.<effect>`; the
  part before the first dot is the library file.

### Scripting

| API                                        | Side                   | What it takes                                                                      |
| ------------------------------------------ | ---------------------- | ---------------------------------------------------------------------------------- |
| `Prop.spawn(path, ...)`                    | server                 | a streamed `objects/kcdc/<r>/...cgf` path (only `.cgf`), as well as catalog meshes |
| `player.setDisguise(path)`                 | server                 | the same                                                                           |
| `playAnimation("", { clip })`              | server, player and NPC | `animations/kcdc/<r>/x.caf`                                                        |
| `Vfx.spawn/burst/burstOn(name, ...)`       | server                 | `kcdc_<r>_<lib>.<group>.<effect>`                                                  |
| `Vfx.spawn/attach`, `Vfx.has`              | client                 | the same                                                                           |
| `Audio.play*`, `Audio.hasTrigger`          | client                 | a streamed ATL trigger name                                                        |
| `Audio.playFile(path, entityId, purpose?)` | client                 | `sounds/kcdc/<r>/x`, with or without `.ogg`                                        |
| `Assets.has(path)`                         | client                 | whether a mounted stream pak carries the path                                      |

A streamed prop's `modelGroup` reads `kcdc/<r>`. `Audio.playFile`'s
`purpose` picks the game's voice host event that carries the file: `inner`,
`10`, `20`, `35`, `50` (default), `100`, `200` (a bark heard up to that many
metres), `sfp_dialog`, `sfp_cutscene` or `sfp_music`; an entity takes one
file per 0.1 seconds (the game's `wh_snd_playfile_cooldown`). See the
[animation system](animation-system.md) for `playAnimation` and the
[prop placer](prop-placer.md) for ghosts and placer parts.

Ghost and placer parts (`models`) take mounted streamed meshes too. The server
refuses a streamed name no running resource ships. A joining player has every
pak mounted before the level loads; one connected while a resource starts
holds a prop, disguise or effect whose pak it has not mounted yet and builds it
as soon as it has. A resource started at runtime gets its paks built right
after its script first runs, so a spawn of its own streamed model belongs a
moment later (a timer or an event), not in the script's first lines.

## For players

**Ask before server downloads**, a row on the game's own Game settings page
kept in the player profile: always ask, above 100 MB, above 500 MB (default),
above 2 GB, never ask. A first download whose missing asset paks exceed it
waits on the joining screen with Download and Leave; a download that takes
longer than 400 ms shows its progress there. What a resource ships while the
player is connected is fetched without asking: joining was the decision, and
there is no screen to ask on in the world.

Asset paks are cached with the server's resource packages, in
`<mod>/cache/servers/<server>/`; a pak the server no longer ships is removed
on the next join.

## How it works

### Layering

Asset paks ride the Framework's resource package pipeline, which knows nothing
about KCD2, so other MafiaHub mods reuse it unchanged:

| Piece                                              | Where                                                                                             |
| -------------------------------------------------- | ------------------------------------------------------------------------------------------------- |
| Policy interface, pak writer and validator         | `Framework/utils/streamed_assets/`                                                                |
| Pak builder, staged beside the `.fwpak`s           | `Framework/integrations/server/asset_paks/`                                                       |
| Announcement                                       | `ResourceInfo::assetPaks` in `Framework/networking/rpc/server_resources.h`                        |
| Download, consent, pak list                        | `Framework/integrations/client/instance.*` (`GetResourceAssetPaks`, `OnResourceAssetPaksChanged`) |
| KCDC policy (lanes, paths, kinds)                  | `code/shared/streamed_assets/`                                                                    |
| KCDC client (mount, engine registration, settings) | `code/client/src/core/streamed_assets/`                                                           |
| Join gate and card                                 | `core/states/session_connection.cpp`, `core/ui/multiplayer_frontend.*`, `ui/`                     |

A mod supplies a `Policy` (which lanes exist, how an entry path is
normalised and checked) to the server, and on the client mounts the paks the
Framework hands it.

### Transport

`InitAssetStreamer` builds each running resource's lane folders into
`<resource>.<lane>.<hash>.pak` in the package staging directory, beside the
encrypted `.fwpak`s, and registers them with the same `DirectoryDeltaTransfer`.
An unchanged lane keeps its pak without being rebuilt or rehashed. The paks are
plain ZIPs -- the engine reads them in place, so they are never encrypted --
and the hash in the name makes a changed pak a new file rather than one
rewritten under a mounted archive. Each resource's `ResourceInfo` carries its
paks (lane, SHA-256, size), and a hot reload's `ResourceRefresh` carries them
again.

The client fetches what its cache lacks over HTTP: the server's `download-url`
mirrors first when any are set, then `/files/<name>` on the game port over TCP
with a per-connection token. Every served file has a `<name>.chunks` list
beside it (one SHA-256 per 4 MiB), and the client assembles a pak from it:
chunks the previous version of the lane pak, the stale copy being replaced or
another server's copy already hold are taken from disk, the rest come by
`Range` over four connections, and the assembly survives a cut or a restart.
So a `restart <resource>` after a one-texture edit costs each player one
chunk, not the lane. The whole file is still verified with SHA-256 before it
is moved into place. A busy server answers 503 with its queue position and the
client waits its turn rather than failing; the Framework's own stall watch
gives up only on a download that does nothing observable for a minute. The
Framework's `docs/asset_download.md` is the full account. The client then
checks each announced pak is on disk at its announced size before handing the
list to the mod. The loading hint and the admission card show the rate and,
while queued, how many joins are ahead.

The Framework can also let a lane's pak arrive after the join
(`Policy::LaneMayDefer`) and admit downloading players beside the `maxplayers`
slots (`max-downloading` in the server config). KCDC keeps every lane blocking
for now: the replace lane must be in before the level load, and the stream
lane carries clips and models the spawn may need. `max-downloading` is a
server operator's choice; the Pterodactyl egg leaves it at 0.

### Mount lifecycle

The client checks every pak against the content policy before mounting it.
Join assets mount before the level loads. Mid-session updates mount one pak
a frame and reload changed assets; a resource's new pak supersedes its old
one. Paks the session no longer names are disabled at the next level change.

## Verified in game on `kutnohorsko` with one client

The demo resource is generated by `tools/generate_streamed_assets_demo.py`:
both lanes, a new bucket mesh with its own material and texture, a cinematic
clip, a voice line, ATL controls, a particle library and FMOD's own example
banks (Vehicles, SFX, Music; about 32 MB) with the master bus rewritten to
the game's.

- Join: the paks come down with the resource packages and are mounted before
  the level; a rejoin sends only what changed.
- Replace lane: every stock bucket draws the replacement texture. Stream
  lane: the new mesh, material and texture, as a prop, held in the hands and
  scaled.
- Clips: the database rebuild adds the clip as an option; an NPC plays it
  through `playAnimation("", { clip })` with no refusal.
- Audio: the ATL controls parse, `Audio.hasTrigger` sees the new triggers and
  the bank's trigger executes on the player; the `.ogg` plays on an NPC. FMOD
  reports all three banks linked with their 24 event paths.
- Particles: the streamed effect builds as an emitter.
- Mid-session: a resource added and started while connected sends only its
  own pak, which is applied in place -- database rebuilt with its clip, prop
  and clip shown. Restarting a resource after editing a replaced texture sends
  only the changed pak, and the texture changes on screen.
- Consent and progress: a 662 MB pak waits on the card at the default
  threshold; Download shows the bar to completion (about 37 MB/s over
  loopback) and the game's memory stays flat while it downloads.
- Retire: rejoining a server that ships nothing disables both paks at the
  level edge, rebuilds the database without clips and restores the stock
  texture.

## Release notes for maintainers

- MafiaNet 0.22.1: `FileListTransfer::SetReferencePushWindow`, and two
  `DirectoryDeltaTransfer` fixes the resource packages needed too --
  `AddFile` uploads are compared by hash (every sync used to re-send every
  package), and streamed downloads are written as they arrive.
- The resource list and the prop, VFX and animation-playback wire formats
  differ from earlier releases: client and server must both run 1.6.0.

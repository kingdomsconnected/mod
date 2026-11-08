# Playground

An optional test resource for the sample game mode. Press **F2** to open its
window. The native **Playground** action hint shows the key, which can be
rebound in the game's controls. Choose a tool in the window; character
creation has a button there too. Close the window with Escape or **Close**.

## Enable it

Use matching client and server builds that include the playground APIs and
keybind. The resource depends on `kcdc-gamemode`.

In a release bundle:

1. In `resources/optional/kcdc-playground`, run `npm ci`
   and `npm run build`.
2. Move that folder to `resources/kcdc-playground`, beside `kcdc-gamemode`.
3. Start the server. Both resources load automatically.

The server discovers resources directly inside `resources`. Leaving this
folder inside `optional` keeps it disabled. To disable it again, stop the
server and move the folder back into `optional`.

When working from this repository, install and build in the original folder
so its link to `scripting-api` resolves. Copy the built resource to the test
server's `resources/kcdc-playground` folder. A local native build also stages
the optional folder when its dependencies are installed; it does not enable it.

By default, every player can use these cheats on a server that enables the
resource. Change `config.allowed` in `src/server/config.ts` to use your access
rule, then rebuild. The server checks that rule for each request and trader
interaction. Inventory grants, recipe knowledge and accepted appearances stay
on the character according to the server's usual persistence rules.

## Tools

- **Item spawner:** search all equippable item classes by their game
  table name or GUID, or filter by category. Choose the class's supported
  quality and 1–20 items, then grant them to your inventory or drop them at
  the cursor. Aim at ground within 15 metres before pressing F2; opening the
  window captures that point. The server checks the ground before spawning.
- **Locations:** settlements and points of interest from the current level,
  plus this resource's traders. The dice button chooses the nearest table
  without a sample-mode lobby, invitation, match or another nearby player in
  your world. Quest-layer tables may require that area to be loaded. Dismount
  and leave crafting or dice before teleporting.
- **Crafting:** learn a selected recipe or all listed recipes, and grant
  ingredients for 1–10 batches. This uses normal blacksmithing and alchemy
  recipes, excluding recipes with quest ingredients. Skill requirements still
  apply; craft at a normal station.
- **Companions:** spawn up to four test NPCs, standing still or following you.
  Spawn your horse and dog here too. Existing owned companions are kept.
  **Remove my test spawns** removes your playground NPCs, horse, dog and
  remaining ground drops. Inventory items are kept.
- **Character creator:** change your appearance through the game's character
  creator. Accept to apply it; cancel to keep your previous appearance.

The resource places two stationary traders in the loaded level. Trosky uses
the Tachov blacksmith and Troskowitz trader placements; Kuttenberg uses the
Enderlin and Lacek placements. Sedletz uses two authored monastery NPC
placements as test traders. They are separate server NPCs with random
equipment stock, bread and apples. Use **Locations** to find them, then
interact with them to trade. Each starts with 10,000 groschen.

Companions and drops belong to the player who spawned them. Death,
respawning, disconnecting or stopping the resource removes those test
entities. Stopping the resource also removes its traders and shops. The
default limit is 40 uncollected ground items per player.

## Development

Run `npm test` to compile both scripts and check request validation, cursor
grants, recipe unlocks, entity cleanup, creator ownership and the client UI
lifecycle. The game mode's dice tests cover table selection and occupancy.

The server uses compiled game catalogs through `Inventory.classes()`,
`World.pointsOfInterest()` and `Npc.placements()`. NPC placements are authored
positions from each level's `objects_mission0.xml`, matched to the existing
soul catalog; they are not the NPC's current schedule or a live-game lookup.
Regenerate that catalog from an installed game with:

```powershell
python tools/generate_npc_placement_catalog.py "D:/SteamLibrary/steamapps/common/KingdomComeDeliverance2"
```

The UI uses local HTML and CSS, with parchment, red and gold styling. It does
not bundle game fonts or textures.

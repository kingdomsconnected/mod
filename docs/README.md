# Documentation

Guides for players, server operators, resource authors and contributors.

The scripting API itself is documented by its declarations; see
[`scripting-api/README.md`](../scripting-api/README.md). The default
gamemode's commands are in
[`resources/kcdc-gamemode/README.md`](../resources/kcdc-gamemode/README.md).

## Guides

- [Building KCDC](building.md) covers prerequisites and local builds.
- [World Builder MCP](world-builder-mcp.md) -- local setup, authoring coverage,
  operation handling and contract maintenance.

### Hosting

- [Server configuration](server-configuration.md) covers `server.json`,
  ports and level selection.
- [Public server container](server-container.md) -- the Docker image, its
  arguments, and Compose and Fly.io setup.
- [Hosting with Pterodactyl](pterodactyl.md) -- the egg, allocations and
  upgrades.
- [Server-side navigation](navigation.md) -- loading the level's navigation
  mesh, and NPC pathfinding modes.
- [Server-streamed custom assets](asset-streaming.md) -- models, textures,
  clips and sounds a resource streams to clients.

### Players and bodies

- [Animals and horse navigation](animals.md) -- spawning mammals, authored
  spawnpoints, movement and sample respawns.

- [Scripted animations](animation-system.md) -- the game's own animations on a
  player or an NPC, with props.
- [Emotes](emote-system.md) -- the emote wheel, and laying it out from a
  client script.
- [Character creator](character-creator.md) -- `CharacterCreator.open` on the
  game's own character stage.
- [Player progression](player-progression.md) -- server XP, levels, perks and
  points.
- [Player stats and status effects](player-vitals.md) -- writable and derived
  stats, and restoring health, nourishment and effects across rejoins.
- [Carrying items](item-carrying.md) -- baskets, firewood and sacks carried in
  the arms.
- [Carrying players](player-carrying.md) -- picking up another player's body.
- [Pickpocketing](pickpocketing.md) -- one player robbing another on the
  game's own Rob entry.
- [Screen effects](screen-effects.md) -- client-local post effects on one
  player's picture.

### Inventory, containers and trade

- [Server inventory](inventory-authority.md) -- how the server holds every
  player's items and the game shows them.
- [Inventory scripting API](inventory-scripting-api.md) -- what server
  scripts can do with a player's items.
- [Inventory item properties](inventory-properties.md) -- the `metadata` an
  inventory row carries.
- [Custom inventory items](custom-inventory-items.md) -- server-defined
  resource types, personalized instances and scripted use.
- [Chest contents](stash-inventory-api.md) -- container inventories and
  virtual stashes.
- [Level containers](level-containers.md) -- the level's own chests, boxes and
  barrels, locked and gated by the server.
- [Ground items](ground-item-physics.md) -- dropped stacks, pickup and
  physics.
- [Vendors](vendor-system.md) -- server-owned price lists and purses on the
  game's shop screen.

### Crafting

- [Alchemy server API](alchemy-api.md) -- brewing at the game's own tables.
- [Smithing server API](smithing-api.md) -- smithing at the game's own
  smitheries.
- [Custom recipes](custom-recipes.md) -- alchemy and smithing recipes from
  scripts and streamed mods.
- [Crafting refund policy](crafting-refunds.md) -- refunding an unfinished
  craft.
- [Server alchemy: how it works](authoritative-alchemy.md) -- what the server
  rules on at an alchemy table.
- [Server smithing: how it works](authoritative-smithing.md) -- what the
  server owns at a smithery.
- [Character inputs](authoritative-soul.md) -- how the server builds the
  character a crafted result is computed from.

### World, NPCs and building

- [Server-spawned NPCs](npc-system.md) -- the NPC system as built, its
  scripting API and its limits.
- [Siege engines](siege-engines.md) -- building, loading and firing
  trebuchets and cannons.
- [Siege defences](siege-defences.md) -- ladders to storm a wall and push
  off it, and battlement piles of hurling stones.
- [The map editor](map-editor.md) -- `MapEditor.open` and the world builder,
  with per-player multiplayer access and F7 to open it.
- [`PropGhost` and `PropPlacer`](prop-placer.md) -- previews and placement
  from a client resource.
- [World objects](world-objects.md) -- hanging doors, placing seats, beds,
  dice tables and crafting stations, taking the level's own out, and
  shipping them in a world resource.
- [Rigid props and weapon hits](prop-physics.md) -- pushing, stopping and
  resetting a prop that falls, hearing where a weapon strikes the world, and
  felling a tree.
- [Client debug gizmos](gizmos.md) -- world-space debug geometry from a client
  resource.

### Client UI

- [Native UI from a client resource](native-ui-scripting.md) -- `NativeUI`
  next to `Web`, and when to use which.
- [Books from a client resource](book-scripting.md) -- opening a composed book
  in the player's hands.

# kcdc-gamemode

The default Kingdoms Connected gamemode: every `/` command a bare server answers, and the CEF
debug panel its clients open over them. Both halves are written in TypeScript
against the public scripting API.

`KCDC::Server::OnChatCommand` does one thing -- raise `playerCommand` -- so
this is the whole command surface of a default server, and turning it off takes
the commands with it. Nothing here is privileged: every call it makes is one a
resource beside it can make, which is what makes it the worked example as well
as the default.

## Quick start

You need [Node.js](https://nodejs.org) 22 or newer; npm comes with it.

```sh
npm ci          # install the toolchain and the scripting declarations
npm run dev     # compile both halves and recompile on every save
```

The server loads `dist/`, so restart the resource (or the server) after a
change. `npm test` builds and runs every test in `tests/`. In this repository,
`buildsuild.bat KCDCServer 64` also compiles the gamemode and stages it
beside the server it builds, so you rarely need to copy anything by hand.

## Food pots

Use `/pot` beside a food pot to inspect its meal and remaining portions.
`/pot recipes` lists ingredient costs. `/pot refill porridge`,
`/pot refill vegetable`, and `/pot refill meat` spend inventory food to
refill an empty pot. The recipes in `src/server/cook-pots.ts` are examples
that server owners can change.

`/pot poisons` lists your poison rows. `/pot poison <row>` spends one dose
and poisons future player meals until the pot is freshly refilled. The
native poisoning prompt remains hidden. These commands do not poison NPCs
or trigger the base game's crime system. See [the pot API guide](../../docs/cook-pots.md).

Run `npm run test:pots` for the command tests.

## Ending a fight

Hold U for one second to offer peace to the opponent named in the action
hint. They hold U to accept. Both players must agree; after your vote, the
hint says it is waiting for the other player. Attacking withdraws the offer.
You can rebind U under **Multiplayer** in the controls menu.

Combat also ends after neither player attacks for 45 seconds. Change
`COMBAT_TIMEOUT_MS` in `src/server/combat.ts` to choose another duration in
milliseconds; `0` disables this timer. This is a gamemode rule, implemented
with `playerAttack`, client consent events and `player.cancelCombat(opponent)`.

Run `npm run test:combat` for the negotiation and hint tests.

## Woodcutting

Hit a beech, a linden or a pine three times with an axe to fell it. The tree
gives way to a stump of its own kind, and the trunk topples away from you and
lies where it falls. Every player nearby sees the same fall. Birches, oaks and
the other trees have no stump in the game, so an axe leaves them standing.
`/give axeWork01` hands you a woodcutter's axe.

A felled tree grows back after 10 minutes, and `/trees regrow` brings every
felled tree back at once. The numbers are at the top of
`src/server/woodcutting.ts`: how many blows fell a tree, how long it takes to
grow back (`0` keeps it felled), and how heavy the trunk is.

This is a gamemode rule, built on `playerWeaponHit`, which names the tree an
axe struck, `LevelEdit.apply`, which swaps the tree for its stump, and a
`rigid` prop whose fall the nearest player's game simulates. See
[Rigid props and weapon hits](../../docs/prop-physics.md).

Run `npm run test:woodcutting` for its tests.

## Commands

Every command a default server answers, with its forms; `/help <command>`
prints them in game. Nothing checks who is asking: chat commands are open to
every player.

| Command | What it does |
| --- | --- |
| `/help [command]` | Lists the commands this server answers, or every form of one. |
| `/anim list <search>`, `/anim next\|prev\|<n>`, `/anim play <fragment> [tags\|-] [loop] [prop] [left]`, `/anim hold <prop>`, `/anim props [search]`, `/anim npc <fragment> [tags\|-] [loop] [prop]`, `/anim stop [standup]`, `/anim npcstop [standup]` | Browses and plays the game's own animations, with the props they hold, on you or the nearest NPC. |
| `/appearance [info]`, `/appearance who`, `/appearance list <head\|hair\|beard\|body> [filter] [male\|female]`, `/appearance set <head\|hair\|beard\|body> <name\|->`, `/appearance gender <male\|female>`, `/appearance random [male\|female]`, `/appearance reset` | Reads and changes what a body looks like. |
| `/outfit random`, `/outfit <preset>`, `/outfit list [filter]` | Dresses you in one of the game's own outfits. |
| `/area [here]`, `/area near [radius]`, `/area doors [id]` | Lists the level's areas and the script areas around you, and the doors an area links. |
| `/blip [type] [label]`, `/blip me [type] [label]`, `/blip set <id> type\|label\|state\|compass\|map <value>`, `/blip move <id>`, `/blip types [prefix]`, `/blip list`, `/blip remove <id>`, `/blip clear` | Puts a point on everybody's compass and map screen, or on yours alone. |
| `/blipdemo`, `/blipdemo clear` | Places one of every kind of blip around you, plus one that circles. |
| `/book [book\|plainBook\|letter]`, `/book parchment`, `/book fade`, `/book legibility <0..1>`, `/book close`, `/book status` | Opens a demo book in your hands. |
| `/build <model> [scale] [rigid\|static\|none]`, `/build stop` | Places a mesh where you aim, with a live preview. |
| `/camera`, `/camera third [distance]`, `/camera first` | Switches your view between the game's first-person camera and one behind you. |
| `/carry <item>`, `/carry prop <item>`, `/carry list`, `/carry drop [now]` | Carries an item in your arms, or lays one down for the game's pick-up prompt. |
| `/cart [blueprint]`, `/cart list`, `/cart sit [seat]`, `/cart leave`, `/cart clear` | Puts one of the game's carts or wagons, horses and all, in front of you. |
| `/crafttest <alchemy\|smithing> [rounds=1] [station=1]`, `/crafttest move <alchemy\|smithing> [station=1]`, `/crafttest <alchemy\|smithing> list`, `/crafttest check` | Checks crafting station placement and grants test ingredients and, for smithing, the recipe perks. |
| `/creator [male\|female\|both]`, `/creator stop` | Opens the character creator on the game's own character stage. |
| `/customitems [grant]`, `/customitems rename`, `/customitems defaults`, `/customitems vendor` | Registers custom wood and letters during play, grants personalized copies, updates their display and prepares custom vendor stock. |
| `/dialogue`, `/dialogue stop` | Opens the sample conversation in the game's own dialogue list. |
| `/dice <player> [target score]`, `/dice accept\|decline\|cancel` | Invites a nearby opponent to agree a target score, or leaves the table lobby. See [Dice at a table](#dice-at-a-table). |
| `/disguise <model> [@player]`, `/disguise off [@player]` | Draws you, or another player, as a prop from the catalog. |
| `/dog [soul]`, `/dog info`, `/dog name [text]`, `/dog mode <wait\|follow\|free\|aggressive\|search\|hunt\|guard\|ambush>`, `/dog give <player id>`, `/dog remove` | Gives you a dog, or reads, renames and commands the one you have. |
| `/door [near] [guid]`, `/door lock\|unlock\|open\|close [guid]`, `/door list` | Reads, locks, opens and closes the level's own doors; `Door.queryNearest` is a round trip to your own client. |
| `/drop <GUID\|"exact item name"> [amount]`, `/drop list`, `/drop remove <id>`, `/drop clear` | Lays a pickable stack of an item on the ground. |
| `/face <fragment> [tags\|-]`, `/face clear`, `/face talk [neutral\|happy\|drunk\|chew]`, `/face quiet`, `/face npc <fragment> [tags\|-]`, `/face npcclear`, `/face npctalk [style]`, `/face npcquiet`, `/face list` | Sets a facial expression and makes the mouth talk, on you or the nearest NPC. |
| `/gate [toggle] [guid]`, `/gate open\|close [guid]`, `/gate list` | Opens and closes the level's castle gates, played out from the server clock: the drawbridge and the portcullis animate, and the Ratborsch, Nebakov and Ruthard palace gates swap at once. Every gate starts open. |
| `/give <GUID\|"exact item name"> [amount] [quality 1-4]` | Puts items into your own inventory. |
| `/herbs [info]`, `/herbs time <minSeconds> [maxSeconds]`, `/herbs defaults`, `/herbs respawn [radiusMetres]`, `/herbs respawn-all` | Changes herb regrowth time or respawns harvested herbs in your virtual world; `time 0 0` disables timed regrowth. |
| `/horse [breed\|soul]`, `/horse breeds`, `/horse name [text]`, `/horse info`, `/horse ride`, `/horse give [player id]`, `/horse kill\|revive`, `/horse gear [item \| off <slot> \| bare \| preset <name> \| presets \| lock \| unlock \| items [slot]]` | Spawns a horse you own by breed, or reads, renames, rides, gives, dresses, kills or revives the one you are at. |
| `/hud hide\|show\|status [element]`, `/hud list`, `/hud reset` | Native HUD suppression on your own client; `Stats` is the default element. See [HUD visibility regression checks](#hud-visibility-regression-checks). |
| `/lookalike <player\|slot>` | Wears what another player is wearing. |
| `/marker <material> [size]`, `/marker <cylinder\|sphere\|chevron\|cube\|plane> <colour> [size] [height]`, `/marker colours`, `/marker names [prefix]`, `/marker list`, `/marker resize <id> <size> [height]`, `/marker spin <id> <degreesPerSecond>`, `/marker bob <id> <metres>`, `/marker trigger <id> on\|off`, `/marker tint <id> <r> <g> <b> [a]`, `/marker remove <id>`, `/marker clear` | Puts a marker in the world: a decal on the ground, or a mesh standing in it. |
| `/mute <player\|slot>` | Stops you hearing another player's voice. |
| `/mutes` | Lists the players you have muted. |
| `/npc [role\|soulGuid]`, `/npc roles`, `/npc list`, `/npc info`, `/npc name [text]`, `/npc follow\|patrol\|come\|hold`, `/npc say <text>`, `/npc native\|kinematic`, `/npc kill\|revive`, `/npc pin\|unpin`, `/npc remove\|clear` | Spawns server-owned NPCs and orders them about. |
| `/patrol list`, `/patrol start <route-id>`, `/patrol validate <route-id>`, `/patrol status`, `/patrol stop` | Lists named routes from world exports, validates their paths and assigns them to nearby NPCs. |
| `/npcdemo guardpost`, `/npcdemo square [size]`, `/npcdemo escort`, `/npcdemo stand [auto\|game\|server]`, `/npcdemo crowd [count]`, `/npcdemo scene`, `/npcdemo native`, `/npcdemo stats`, `/npcdemo reset` | Worked NPC arrangements: a guard post, a patrol, an escort, a crowd, a scripted scene and a native-locomotion trial. |
| `/pick` | Traces from your crosshair and says what it met, disguised players included. |
| `/prop <model> [scale] [rigid\|static\|none]`, `/prop list`, `/prop remove <id>`, `/prop clear` | Puts a mesh from the game's own object catalog into the world, built on every client. |
| `/quest give <key> "<title>" [main\|side\|activity\|event\|micro\|racing]`, `/quest desc <key> "<description>"`, `/quest step <key> "<objective text>" [optional]`, `/quest done <key> <index>`, `/quest state <key> <active\|done\|failed>`, `/quest list`, `/quest show <key>`, `/quest remove <key>`, `/quest clear` | Writes a quest into the game's own journal, and moves it on. |
| `/screenfx drunk\|injured [0-1]`, `/screenfx hit`, `/screenfx grey [0-2]`, `/screenfx raw <blur\|ghosting\|screenBlood> [0-1]`, `/screenfx clear`, `/screenfx list` | Puts screen effects on your own picture. |
| `/siege trebuchet\|cannon`, `/siege load [id]`, `/siege fire [distance] [id]`, `/siege fire at <player id> [id]`, `/siege speed <multiplier> [id]`, `/siege list`, `/siege remove <id>`, `/siege clear` | Builds, loads and fires trebuchets and cannons; an engine it builds can also be loaded and fired from the game's use hint. |
| `/stash`, `/stash virtual`, `/stash info`, `/stash clear`, `/stash near [guid]`, `/stash lock\|unlock [guid]`, `/stash pick on\|off [guid]`, `/stash key <item guid>\|clear [guid]`, `/stash goto [locked\|unlocked\|guid]` | Opens your virtual stash, spawns containers, and reads and locks the level's own. See [Virtual stash](#virtual-stash). |
| `/take <GUID\|"exact item name"> [amount]` | Takes items back out of your own inventory. |
| `/tp <player\|slot>`, `/tp <x> <y> [z]`, `/tp list` | Puts you next to another player, or at a map position; the slot is the ID the Tab scoreboard shows. |
| `/ui` | Opens the debug panel on your own screen. |
| `/unmute <player\|slot>` | Lets you hear a player you muted again. |
| `/vendor stall`, `/vendor here`, `/vendor reset` | Trades on the game's own shop screen, at prices the server sets. |
| `/vfx <effect> [scale]`, `/vfx burst <effect> [scale]`, `/vfx names [prefix]`, `/vfx list`, `/vfx remove <id>`, `/vfx clear` | Places a particle effect from the game's own libraries, or fires one off. |
| `/trees`, `/trees regrow` | Counts the trees felled with an axe, or grows them all back. See [Woodcutting](#woodcutting). |
| `/world [info]`, `/world scale <0..200>`, `/world hour <0..24>`, `/world day <day> <hour>`, `/world weather <preset> [seconds 0..21600]`, `/world rain <0..1> [amount 0..1]`, `/world wind <x> <y> [z]` | Reads and sets the shared clock and weather; wind is in metres per second, up to 50. |

## Dice at a table

Approach a dice table and look at it from within three metres. Use the
**Play dice (join lobby)** action hint on your Interact control to join.
Joining makes you ready immediately. While waiting for another player,
the hint becomes **Leave dice lobby**. Interact again, walk away, or use
`/dice cancel` to leave. There is no separate ready key or seating step.

When a second player joins, a conversation opens. The first player can
lower or raise the target score, then propose it. The other player can
agree to play or change the score and send a counteroffer back. Changing
the score clears the previous agreement. The native match seats both
players only after they agree to the same score. The default is 2000,
adjustments are 500 points, and the limits are 100 and 100000. Nothing is
wagered.

Both players always have an **I do not want to play** option, including the
player waiting for a proposal. Esc or `/dice cancel` also cancels setup.
Leaving the table's area, changing worlds, dying, disconnecting, or
replacing the conversation cancels it too. Each turn has a 60-second limit.
Cancellation and match end release the lobby; another game requires both
players to join again.

To invite a particular nearby opponent, use `/dice <player> [score]` while
both players are free and close to the same table. `/dice accept` opens
the same negotiation; it does not start a match. `/dice decline` rejects
the invitation, and either player can withdraw it with `/dice cancel`.
Invitations expire after 60 seconds.

The server creates an `Area` around each placement returned by
`Dice.tables()`, and checks that list again every two seconds. A table a
script or a world resource places gets a lobby within that time, and a
table that goes away, destroyed or removed with `SmartObject.remove`, ends
any setup waiting at it. That API lists each table's GUID, position, and
two sitting smart-object GUIDs. The position is the midpoint between
the seat alignment points. The sample reserves two lobby slots at the
chosen loaded table in the player's virtual world and checks proximity
before joining or accepting a dialogue choice. The client checks aim,
obstructions, and whether the station is loaded before showing the hint.
The catalog includes quest-layer tables. Regenerate placements with
`python tools/generate_dice_catalog.py --game <game-directory>` from the
mod repository root.

Run `npm run test:dice` here to build both halves and check interaction,
negotiation, cancellation, stale requests, invitations, and table/world
separation with mocked game APIs. In a two-client game, also check the
Interact hint while standing near and looking at the table, leaving and
rejoining, dialogue navigation, and automatic seating after agreement.
Try a counteroffer, quit from the waiting player's page, and walk away or
disconnect during setup. Neither cancellation nor a completed match should
start a rematch until both players join again.

## Virtual stash

`/stash virtual` demonstrates the server's extended stash API: create stock
with `Stash.createVirtual(player.virtualWorld)` and show the native deposit
and withdrawal screen with `stash.open(player)`. The command reopens the
same personal stock across UI closes and world changes. It destroys that
stock when the connection ends or this resource stops; scripts can choose
their own trigger and lifetime. See
[Chest contents](../../docs/stash-inventory-api.md).

## The map editor

`/builder` or `/builder on` grants yourself World Builder access; press F7
to open it. `/builder off` revokes access and closes an open editor. These
sample commands are available to everyone and affect only the caller. A
custom gamemode can use `player.setWorldBuilderEnabled(boolean)` with its
own permission checks. Access resets on disconnect. See
[The map editor](../../docs/map-editor.md).

## HUD visibility regression checks

These commands require the native HUD persistence fix. `/hud hide Stats`
calls `Hud.setElementVisible("Stats", false)` once on the caller's client.
There are no CVars, timers or clip writes in the demo. Results appear in chat
and the client console, including whether the request was accepted.

1. Run `/hud hide Stats`, then repeat it. The entire Stats bar should stay
   hidden, including its frame, while sprinting and drawing/sheathing a weapon.
   Open and close inventory and dialogue to exercise native HUD transitions.
2. Run `/hud status Stats`. `suppressed by multiplayer` reports the saved
   permission; inspect the picture separately to verify it is actually hidden.
   `/hud status QAMWeapon` and `/hud status QAMFood` should remain allowed in
   an otherwise unmodified session: those are separate elements.
3. Run `/hud hide Compass`, then `/hud show Stats`. Stats should return when
   the game's own HUD policy allows it; Compass should remain suppressed.
   `/hud reset` releases the hides issued by this demo's HUD commands.
4. To test the original spawn timing, enter `hudspawn on` in the **server
   console**, then connect or reconnect a client. The `playerSpawned` handler
   sends the Stats hide request immediately, with no retry in the script.
   Repeat the connection and gameplay checks. `hudspawn status` reports the
   toggle; `hudspawn off` disables it for future spawns. It defaults to off
   and resets when the resource restarts. Turning it off does not release
   existing clients' hides; use `/hud reset` on those clients.
5. With Stats hidden, stop `kcdc-gamemode` using the server's resource controls.
   Native resource cleanup should restore the game's visibility policy. Also
   check disconnect/reconnect with `hudspawn off`; no suppression should leak
   into the new session.

`show` releases multiplayer suppression; it cannot force an element into a
menu or situation where the game hides it. `/hud list` gives the exact,
case-sensitive names. The command also appears in `/help` and F4's Ledger.

## Whitelist

Off by default. When on, `playerConnecting` turns away anyone not listed
before they are sent anything -- no download, no level, no body -- and they
read the refusal on their menu. It is driven from the server console, since
chat commands are open to every player:

| Console | |
| --- | --- |
| `whitelist on` / `whitelist off` | Turn the check on or off. |
| `whitelist add <entry>` / `whitelist remove <entry>` | A Steam id, or `name:<nickname>`. |
| `whitelist list` | What is on it. |

Console changes last until the server stops; the list in `whitelist.ts` is
where a permanent one goes. Names are for test sessions: anyone can type one.
Neither is verified, so a whitelist that has to hold should check
`connection.ticket` -- a launch link's `&ticket=` -- against whatever issued it.

## The debug panel

`F4` opens and closes it, `/ui` opens it, **Esc** hands input back to the game.
While the panel holds the keyboard the key cannot close it -- a focused CEF view
owns input, which is what stops `Key.bind` from firing -- so the panel carries
its own Close plate.

The page is `ui/index.html`, shipped inside this resource and served from the
per-server asset cache as `fw://resources/kcdc-gamemode/ui/index.html`. It
fetches nothing.

| Tab | Draws | From |
| --- | --- | --- |
| Retinue | Condition bars, position, Mannequin tags, attributes, skills, hands, buffs | `LocalPlayer`, four times a second |
| Realm | Clock, sky, what the server has spawned | The server, twice a second |
| Company | Everyone connected, with distances and a ride-to plate | The server |
| Ledger | The command registry, and a box to speak one | `CommandRegistry` |
| Chronicle | Client-only switches, and a log of what the server said back | `Nametags`, `Compass`, `Chat`, `Voice`, `chatMessage` |

Three directions, each worth copying: the page calls `callEvent` and `Web.on`
receives it; `Web.emit` sends a whole snapshot back per channel, so a page that
reloads is correct after one tick; `Events.emitServer` and `player.emit` carry
what only the server knows, polled only while the panel is visible.

Every button that changes the world sends the `/` line a player could have
typed, through `Chat.send`. The answer comes back in chat, and the Chronicle tab
keeps a copy.

## Layout

```
src/server/
  index.ts            the entry point: builds the registry, wires the events
  command.ts          Command, CommandContext, Reply and the dispatcher
  args.ts             argument reading: quoted names, amounts, level GUIDs
  place.ts            where a command puts things, in this Z-up world
  panel.ts            the poll the debug panel's client half asks
  whitelist.ts        who may join, answered in playerConnecting
  players.ts          finding the player a chat line names
  spawns.ts           where arriving players are put
  death-drop.ts       what dying costs: the pockets fall at the body
  pickpocket.ts       the answer to one player picking another's pocket
  woodcutting.ts      felling a tree with an axe, and growing it back
  dice.ts             dice table lobbies and the score negotiation
  build.ts            the server half of /build
  commands/*.ts       the commands, one file per feature

src/client/
  index.ts            the key bind, the page bridge, the sampling tick
  panel.ts            the CEF view, and who owns input while it is up
  snapshot.ts         the local body, as the page draws it
  switches.ts         the switches that never leave this machine
  dice.ts             the dice table's join and leave hint
  interaction.ts      whether a target is in reach and under the aim
  book.ts             the client half of /book
  chronicle.ts        the texts of the /book demo
  build.ts            the client half of /build: the preview
  camera.ts           the client half of /camera
  creator.ts          the client half of /creator
  hud.ts              the client half of /hud
  pick.ts             the client half of /pick
  screen.ts           the client half of /screenfx
  tsconfig.json       the client program

ui/index.html         the panel
assets/book/          the textures the /book demo binds its pages in
tests/dice.test.cjs   the dice checks `npm run test:dice` runs
```

`dist/` is what the manifest names and what a server runs. It is neither
committed nor shipped: this resource travels as source, a release bundle
included, so whoever deploys it runs `npm ci` and `npm run build` where
it is going to run.

A local Framework build is the exception, and only so the dev loop closes: it
compiles this resource and stages the emit into `builds/build-64/bin/resources`,
so the server you just built runs the gamemode you just edited. Configure with
`-DKCDC_STAGE_RESOURCE_SOURCES=ON` -- which is what the release pipeline does --
to stage this source tree there instead.

## Building

```sh
npm ci
npm run build           # both halves
npm run dev             # rebuild both halves on every save
npm test                # build, then every test in tests/
```

The two halves are separate TypeScript programs because the generated
declarations disagree -- the server's `Player` has the connection verbs, the
client's does not -- so no configuration compiles them together. The server
program is the root `tsconfig.json`, the client program is
`src/client/tsconfig.json`.

Both compile against `@kingdomsconnected/types`, the server half naming
`@kingdomsconnected/types/server` in its `types` and the client half
`@kingdomsconnected/types/client`. Here that dependency is a link to
`scripting-api/`, so a binding and the code using it land together. A release
bundle carries no `scripting-api/`, so its copy depends on the version that
release published to npm instead (`scripts/stage_gamemode_types.mjs` makes that
one change while the release is staged), and `npm ci` is all a
deployment needs.

`builds\build.bat KCDCResources 64` compiles and syncs into the runtime
directory; `builds\build.bat KCDCServer 64` does both as part of the server
build.

Those contracts come from *running* a debug server and client. After changing a
C++ binding, run the binary, regenerate with `node
scripts/build_scripting_contract.mjs --out ./build/scripting-contract
--skip-missing`, and only then build this resource against it.

## Animal populations

Authored animal sites are populated automatically in world 0 when the server's
navigation mesh is ready. Without a mesh, no animals spawn. The sample caps
living animals and retained corpses at 256 and removes distant living animals to
free capacity as players travel. Animals roam around their home; deaths are
replaced after five to six minutes when players are sufficiently far away.

Use `/animals list` to find nearby authored spawnpoints and
`/animals start <spawnpointGuid>` to enable one in your virtual world.
`/animals stop` clears its populations until sites are enabled again or the
resource restarts. In `src/server/animals.ts`, `animalSpawnpoints` defaults to
`null` for all populated sites. Set it to selected GUIDs or `[]` to disable
automatic populations.
See [animals and horse navigation](../../docs/animals.md) for the full respawn
rules and API. `npc-spawn.ts` prepares each animal's inventory at spawn:
meat has a 100% drop chance and special parts have a 50% chance. Change
`animalDropChance` there to tune the policy. Butchering unlocks that prepared
stock without rolling it again. Run `builds\build.bat RunKCDCTests 64 Debug`
from the Framework root for the population and loot regression tests.

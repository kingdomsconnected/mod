/**
 * The default KCDC gamemode: every `/` command a bare server answers.
 *
 * Nothing here is privileged -- every call it makes is one a resource beside it
 * can make -- so it is the worked example as much as the default.
 */

import { installBuildHandlers } from "./build.js";
import { installAnimalPopulation } from "./animals.js";
import { animalsCommand } from "./commands/animals.js";
import { installCombat } from "./combat.js";
import { CommandRegistry } from "./command.js";
import { animCommand, installAnimHandlers } from "./commands/anim.js";
import { areaCommand } from "./commands/area.js";
import { appearanceCommand, lookalikeCommand } from "./commands/appearance.js";
import { outfitCommand } from "./commands/outfit.js";
import { blipCommand } from "./commands/blip.js";
import { blipDemoCommand } from "./commands/blipdemo.js";
import { bookCommand } from "./commands/book.js";
import { cameraCommand } from "./commands/camera.js";
import { screenFxCommand } from "./commands/screenfx.js";
import { carryCommand, installCarryHandlers } from "./commands/carry.js";
import { cartCommand } from "./commands/cart.js";
import { craftTestCommand } from "./commands/crafttest.js";
import { customItemsCommand, installCustomItemDemo } from "./commands/customitems.js";
import { creatorCommand, installCreatorSample } from "./commands/creator.js";
import { buildCommand } from "./commands/build.js";
import { builderCommand } from "./commands/builder.js";
import { dialogueCommand, installDialogueSample } from "./commands/dialogue.js";
import { diceCommand, installDiceHandlers } from "./commands/dice.js";
import { doorCommand } from "./commands/door.js";
import { dropCommand } from "./commands/drop.js";
import { faceCommand } from "./commands/face.js";
import { gateCommand } from "./commands/gate.js";
import { installSiegeHandlers, siegeCommand } from "./commands/siege.js";
import { giveCommand } from "./commands/give.js";
import { takeCommand } from "./commands/take.js";
import { helpCommand } from "./commands/help.js";
import { herbsCommand } from "./commands/herbs.js";
import { dogCommand } from "./commands/dog.js";
import { horseCommand } from "./commands/horse.js";
import { hudCommand, installHudHandlers } from "./commands/hud.js";
import { patrolCommand, installPatrolSample } from "./commands/patrol.js";
import { npcCommand } from "./commands/npc.js";
import { installNpcDemoHandlers, npcDemoCommand } from "./commands/npcdemo.js";
import { propCommand } from "./commands/prop.js";
import { disguiseCommand, invisibleCommand, pickCommand } from "./commands/prophunt.js";
import { installStashHandlers, stashCommand } from "./commands/stash.js";
import { teleportCommand } from "./commands/teleport.js";
import { pickSpawnPoint } from "./spawns.js";
import { uiCommand } from "./commands/ui.js";
import { installVendorSample, vendorCommand } from "./commands/vendor.js";
import { markerCommand } from "./commands/marker.js";
import { questCommand } from "./commands/quest.js";
import { vfxCommand } from "./commands/vfx.js";
import { muteCommand, mutesCommand, unmuteCommand } from "./commands/voice.js";
import { worldCommand } from "./commands/world.js";
import { installDebugPanel } from "./panel.js";
import { installDeathDrops } from "./death-drop.js";
import { installPickpocketPolicy } from "./pickpocket.js";
import { installWhitelist } from "./whitelist.js";
import { potCommand } from "./commands/pot.js";
import { installWoodcutting, treesCommand } from "./woodcutting.js";

const commands = new CommandRegistry();
commands.add(potCommand);
commands.add(animalsCommand);
installAnimalPopulation();
commands.add(herbsCommand);
commands.add(builderCommand);
commands.add(patrolCommand);
installPatrolSample();
commands.add(craftTestCommand, animCommand, faceCommand, worldCommand, horseCommand, cartCommand, npcCommand, npcDemoCommand, dogCommand, appearanceCommand, lookalikeCommand, outfitCommand, giveCommand, takeCommand, stashCommand, propCommand, vfxCommand, markerCommand, blipCommand, blipDemoCommand, questCommand, dialogueCommand, creatorCommand, vendorCommand, dropCommand, carryCommand, teleportCommand, doorCommand, areaCommand, gateCommand, siegeCommand, uiCommand, cameraCommand, screenFxCommand, disguiseCommand, invisibleCommand, pickCommand, buildCommand, bookCommand, muteCommand, unmuteCommand, mutesCommand, diceCommand);

commands.add(hudCommand);
commands.add(treesCommand);
commands.add(customItemsCommand);
installCustomItemDemo();

// Added last so it can list the ones above, including itself.
commands.add(helpCommand(commands));

// After the registry is complete: the panel lists it too.
installDebugPanel(commands);

// What a building client sends back, and the teardown when one stops.
installBuildHandlers();
installCombat();

// The sample conversation, and the two handlers that log every choice it takes.
installDialogueSample();

// The sample character creator, which puts on whatever look the player accepts.
installCreatorSample();

// The sample stall: its keeper opens the vendor on use, and every deal is logged.
installVendorSample();

// Who may join, answered before they are sent anything. Off until the console
// or the list in whitelist.ts turns it on.
installWhitelist();

// Robbing another player: a caught thief is named to both of them.
installPickpocketPolicy();
installDiceHandlers();
installStashHandlers();
installSiegeHandlers();
installCarryHandlers();

// The NPC demos are state machines driven by `npcIntentDone`, so their handlers
// have to be installed whether or not anybody has run a demo yet.
installNpcDemoHandlers();
installAnimHandlers();
installHudHandlers();

// Three blows of an axe fell a tree the game ships a stump for.
installWoodcutting();

Events.on("playerCommand", (player, command, args) => {
    if (!commands.dispatch(player, command, args)) {
        Chat.sendToPlayer(player, `Unknown command '/${command}'. /help lists the ones this server answers.`);
    }
});

Events.on("playerConnect", (player) => {
    console.log(`${player.nickname} joined as entity ${player.id}`);
});

// The server names nowhere by itself, so without this every join lands on
// the level's own start point. Nothing is seen: they are still loading.
Events.on("playerSpawning", (player) => {
    const point = pickSpawnPoint();
    if (point) {
        player.spawn(point.position, new Vector3(0, 0, point.yaw ?? 0));
    }
});

// The first moment a welcome is worth sending: playerConnect fires while
// they are still loading.
Events.on("playerSpawned", (player) => {
    Chat.sendToPlayer(player, `Welcome to KCDC, ${player.nickname}. Type /help for what this server answers, or press F4 for the debug panel.`);
    console.log(`${player.nickname} is in the world`);
});

Events.on("playerDisconnect", (player) => {
    console.log(`${player.nickname} left`);
});

// Registered first, so the pockets fall before the body gets back up.
installDeathDrops();

// No automatic respawn exists; reviving at once is the demo's answer, and the
// obvious thing a real gamemode replaces.
Events.on("playerDied", (player) => {
    Chat.sendToPlayer(player, "You died. Getting you back up.");
    player.revive();
});

// A knockout the game puts on (a brawl, fists) keeps the player in the dark,
// without menus, for minutes: single player skips time through it, and one
// player cannot skip the clock everyone shares. Waking them is the demo's
// answer, as reviving is.
//
// Passing out from drink is the same wait under its own buff,
// `unconscious_alcohol`, and a longer one: hours of game time. It carries no
// family, so `clearBuffs("unconscious")` cannot reach it and it is removed by
// name instead.
const KNOCKOUT_MS = 15_000;
Events.on("playerBuffAdded", (player, buff) => {
    if (buff === "unconscious") {
        setTimeout(() => {
            if (player.ready && player.hasBuff("unconscious")) player.clearBuffs("unconscious");
        }, KNOCKOUT_MS);
    } else if (buff === "unconscious_alcohol") {
        setTimeout(() => {
            if (player.ready && player.hasBuff("unconscious_alcohol")) player.removeBuff("unconscious_alcohol");
        }, KNOCKOUT_MS);
    }
});

console.log(`ready with ${commands.all().length} commands`);

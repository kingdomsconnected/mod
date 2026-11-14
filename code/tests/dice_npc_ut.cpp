#include "dice_npc_ut.h"

#include "core/server.h"
#include "features/dice/dice_service.h"
#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <filesystem>
#include <fstream>
#include <integrations/server/scripting/module.h>
#include <iostream>
#include <mafianet/get_time.h>
#include <networking/replication/replication_manager.h>
#include <scripting/engine.h>
#include <stdexcept>
#include <string>

// A dice match whose second seat is a server NPC: the server deals and rules
// as it does between two players, and the NPC's moves are a script's, through
// Dice.move. No client is connected, so nothing here covers the table itself;
// what is covered is who may sit, which moves are taken, and who forfeits.
int RunDiceNpcSuite() {
    KCDC::Server server;
    bool initialized = false;
    try {
        std::ofstream("server.json") << R"({"mod":{"level":"trosecko","required_dlc":[],"npcEnabled":true,"scoreboard":false,"world_resources":[]}})";
        std::filesystem::create_directories("resources/dice");
        std::ofstream("resources/dice/package.json") << R"({"name":"dice","version":"1.0.0","mafiahub":{"serverScripts":["server.js"]}})";
        std::ofstream("resources/dice/server.js") << "// Dice NPC fixture\n";
        Framework::Integrations::Server::InstanceOptions options {};
        options.modSlug          = "dice-npc-tests";
        options.modName          = "Dice NPC tests";
        options.modVersion       = "1.0.0";
        options.gameName         = "dice-npc-tests";
        options.gameVersion      = "1";
        options.bindHost         = "127.0.0.1";
        options.bindPort         = 28987;
        options.maxPlayers       = 4;
        options.bindPublicServer = false;
        options.verifyBuildToken = false;
        options.enableSignals    = false;
        options.modConfigSchema  = {{"level", Framework::Utils::ConfigFieldType::String, "trosecko"}, {"required_dlc", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}, {"npcEnabled", Framework::Utils::ConfigFieldType::Bool, true},
            {"scoreboard", Framework::Utils::ConfigFieldType::Bool, false}, {"world_resources", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}};
        char executable[]        = "dice-npc-tests";
        char *args[]             = {executable};
        options.argc             = 1;
        options.argv             = args;
        const auto started       = server.Init(options);
        if (!started) {
            throw std::runtime_error(started.GetError().message);
        }
        initialized = true;

        auto *replication = Framework::CoreModules::GetReplication();
        auto *player      = replication->CreateEntity<KCDC::Shared::Entities::HumanEntity>();
        auto *bystander   = replication->CreateEntity<KCDC::Shared::Entities::HumanEntity>();
        if (player == nullptr || bystander == nullptr) {
            throw std::runtime_error("the players' bodies could not be created");
        }
        // Where a script's NPC is created when it names no world.
        player->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        bystander->SetVirtualWorld(MafiaNet::VIRTUAL_WORLD_GLOBAL);
        player->position    = glm::vec3(10.0F, 10.0F, 10.0F);
        player->nickname    = "Henry";
        bystander->position = glm::vec3(10.5F, 10.0F, 10.0F);
        bystander->nickname = "Hans";

        auto *engine         = server.GetScriptingModule()->GetEngine();
        std::size_t sequence = 0;
        const auto check     = [&](const std::string &source) {
            const std::string path = "resources/dice/check-" + std::to_string(++sequence) + ".js";
            {
                std::ofstream file(path);
                file << "(() => {" << source << "})()";
            }
            if (!engine->ExecuteResourceFile("dice", path)) {
                throw std::runtime_error("Dice NPC assertion failed: " + engine->GetLastError());
            }
        };
        auto &dice = KCDC::Features::Dice::DiceService::Get();

        check("globalThis.PLAYER = " + std::to_string(player->GetNetworkID()) + "; globalThis.BYSTANDER = " + std::to_string(bystander->GetNetworkID()) + ";" + R"(
            globalThis.assert = (value, message) => { if (!value) throw Error(message); };
            globalThis.rejects = (fn, message) => {
                let error;
                try { fn(); } catch (caught) { error = caught; }
                assert(error !== undefined, message);
                return String(error);
            };
            globalThis.log = [];
            for (const name of ["diceTurnStart", "diceRoll", "diceMove", "diceTurnEnd"]) {
                Events.on(name, (event) => log.push({ name, ...event }));
            }
            globalThis.starts = [];
            Events.on("diceMatchStart", (match, first, second, targetScore) => starts.push({ match, first, second, targetScore }));
            globalThis.ends = [];
            Events.on("diceMatchEnd", (match, first, second, winner, reason, firstScore, secondScore) => ends.push({ match, first, second, winner, reason, firstScore, secondScore }));
            globalThis.keeper = Npc.create({ soul: "townsman", position: new Vector3(11, 10, 10) });
            // The roll a decision is due on: the last one dealt.
            globalThis.due = () => log.filter((event) => event.name === "diceRoll").at(-1);
            // Which seat throws first is drawn, so a match is started until `accept` likes its opening.
            globalThis.startUntil = (second, accept) => {
                for (let attempt = 0; attempt < 2000; attempt++) {
                    log.length = 0;
                    const match = Dice.start(PLAYER, second, { targetScore: 100000, firstName: "Henry", secondName: "Innkeeper" });
                    const roll = due();
                    assert(roll.match === match && roll.reason === "rolled", "a started match waits on a throw that scored");
                    if (accept(roll)) {
                        return match;
                    }
                    assert(Dice.stop(match), "a running match stops");
                }
                throw Error("no opening suited the test in 2000 matches");
            };
        )");

        // Who may sit.
        check(R"(
            assert(rejects(() => Dice.start(keeper.id, PLAYER), "an NPC took the first seat").includes("second seat"), "the first seat's refusal says why");
            const far = Npc.create({ soul: "townsman", position: new Vector3(40, 10, 10) });
            assert(rejects(() => Dice.start(PLAYER, far.id), "an NPC across the room was seated").includes("stand together"), "a distant NPC is refused like a distant player");
            assert(rejects(() => Dice.start(far.id, keeper.id), "two NPCs were seated").includes("second seat"), "two NPCs cannot play each other");
            far.remove();
            const hare = Npc.create({ soul: Npc.animals().find((row) => row.actorClass === "Hare").soul, position: new Vector3(11, 10, 10) });
            assert(rejects(() => Dice.start(PLAYER, hare.id), "an animal was seated").includes("living human"), "an animal is refused");
            hare.remove();
            const corpse = Npc.create({ soul: "townsman", position: new Vector3(11, 10, 10) });
            corpse.health = 0;
            assert(corpse.alive === false, "an NPC with no health is dead");
            assert(rejects(() => Dice.start(PLAYER, corpse.id), "a corpse was seated").includes("living human"), "a dead NPC is refused");
            corpse.remove();
            assert(rejects(() => Dice.start(PLAYER, 0xFFFFFF), "nobody was seated").includes("connected"), "an id naming nobody is refused");
            assert(Dice.matchOf(PLAYER) === 0 && Dice.matchOf(keeper.id) === 0 && starts.length === 0 && ends.length === 0, "a refused start leaves nothing behind");
        )");

        // The NPC's moves are checked by the rules a player's are.
        check(R"(
            const scoring = (roll) => roll.faces.findIndex((face) => face === 1 || face === 5);
            const dead = (roll) => roll.faces.findIndex((face) => face !== 1 && face !== 5);
            const match = startUntil(keeper.id, (roll) => roll.seat === 1 && scoring(roll) >= 0 && dead(roll) >= 0);
            const start = starts.at(-1);
            assert(start.match === match && start.second !== null && start.second.id === keeper.id && start.targetScore === 100000, "diceMatchStart hands the NPC over as the second seat");
            assert(Dice.matchOf(keeper.id) === match && Dice.matchOf(PLAYER) === match, "both seats are in the match");
            assert(rejects(() => Dice.start(BYSTANDER, keeper.id), "a seated NPC was seated again").includes("already playing"), "an NPC plays one match at a time");

            const roll = due();
            assert(roll.player === keeper.id && roll.seat === 1 && roll.rolledMask === 0x3F && roll.faces.length === 6, "diceRoll names the NPC and what it threw");
            const moves = () => log.filter((event) => event.name === "diceMove").length;
            assert(Dice.move(match, 0, false) === false, "scoring nothing and throwing again is refused");
            assert(Dice.move(match, 1 << dead(roll), false) === false, "setting aside a die that does not count is refused");
            assert(Dice.move(match, 1 << dead(roll), true) === false, "banking over a die that does not count is refused");
            assert(Dice.move(match, 0x40, true) === false, "a mask naming a seventh die is refused");
            assert(Dice.move(match + 1000, 0, true) === false, "a match that does not exist takes no move");
            assert(moves() === 0 && due() === roll, "a refused move changes nothing and raises nothing");

            const held = 1 << scoring(roll);
            const worth = roll.faces[scoring(roll)] === 1 ? 100 : 50;
            assert(Dice.move(match, held, false) === true, "setting aside a die that counts and throwing the rest is taken");
            const move = log.filter((event) => event.name === "diceMove").at(-1);
            assert(moves() === 1 && move.player === keeper.id && move.seat === 1 && move.reason === "continue" && move.heldMask === held && move.points === worth && move.turnScore === worth, `diceMove reports the NPC's move, got ${JSON.stringify(move)}`);
            const next = due();
            assert(next !== roll && next.seat === 1 && next.rolledMask === (0x3F & ~held), "the dice left are thrown for the NPC");
            if (next.reason === "rolled") {
                assert(Dice.move(match, held, false) === false, "a die already set aside cannot be held again");
                assert(Dice.move(match, 0, true) === true, "the NPC banks its turn");
                const ended = log.filter((event) => event.name === "diceTurnEnd").at(-1);
                assert(ended.player === keeper.id && ended.reason === "pass" && ended.points === worth && ended.totalScore === worth, `the banked turn is the NPC's, got ${JSON.stringify(ended)}`);
            }
            // Whatever followed, a seat the player holds is not the script's to move.
            const waiting = due();
            if (waiting.seat === 0 && waiting.reason === "rolled") {
                assert(Dice.move(match, 0, true) === false, "the player's turn is not played through Dice.move");
            }
            assert(Dice.stop(match) && Dice.matchOf(keeper.id) === 0, "a stopped match frees the NPC");
            const end = ends.at(-1);
            assert(end.match === match && end.reason === "stopped" && end.winner === 255 && end.second !== null && end.second.id === keeper.id, "diceMatchEnd hands the NPC over too");
            assert(Dice.move(match, 0, true) === false, "a match that is over takes no move");
        )");

        // A move made from inside the diceRoll that asks for it, as a scripted opponent would.
        check(R"(
            let answered = 0;
            const answer = (event) => {
                if (event.player === keeper.id && event.reason === "rolled" && answered === 0) {
                    answered++;
                    assert(Dice.move(event.match, 0, true) === true, "a pass from inside diceRoll is taken");
                }
            };
            Events.on("diceRoll", answer);
            let match = 0;
            for (let attempt = 0; attempt < 2000 && answered === 0; attempt++) {
                if (match !== 0) {
                    Dice.stop(match);
                }
                log.length = 0;
                match = Dice.start(PLAYER, keeper.id);
            }
            assert(answered === 1, "the NPC never threw first in 2000 matches");
            const names = log.map((event) => event.name);
            const rolled = log.findIndex((event) => event.name === "diceRoll" && event.player === keeper.id && event.reason === "rolled");
            assert(names[rolled + 1] === "diceMove" && names[rolled + 2] === "diceTurnEnd" && log[rolled + 1].player === keeper.id, `the answer follows the roll it answers, got ${names.join(",")}`);
            assert(Dice.matchOf(keeper.id) === match, "the match goes on after the answer");
            Dice.stop(match);
        )");

        // A match between two players has no seat for a script.
        check(R"(
            const match = Dice.start(PLAYER, BYSTANDER);
            assert(starts.at(-1).second !== null && starts.at(-1).second.id === BYSTANDER, "two players still start a match");
            assert(Dice.move(match, 0, true) === false, "Dice.move plays for nobody between two players");
            assert(Dice.stop(match), "the players' match stops");
        )");

        // The turn's delay runs for an NPC as for a player.
        check(R"(globalThis.silent = startUntil(keeper.id, (roll) => roll.seat === 1);)");
        dice.Sweep(MafiaNet::GetTime() + 170000);
        check(R"(assert(Dice.matchOf(keeper.id) === silent, "an NPC silent for less than the delay keeps its seat");)");
        dice.Sweep(MafiaNet::GetTime() + 181000);
        check(R"(
            const end = ends.at(-1);
            assert(Dice.matchOf(keeper.id) === 0 && end.match === silent && end.reason === "timedOut" && end.winner === 0, `a silent NPC forfeits to the player, got ${JSON.stringify(end)}`);
            const closed = log.filter((event) => event.name === "diceTurnEnd").at(-1);
            assert(closed.player === keeper.id && closed.reason === "timedOut", "the NPC's open turn is closed");
            globalThis.silent = startUntil(keeper.id, (roll) => roll.seat === 0);
        )");
        dice.Sweep(MafiaNet::GetTime() + 181000);
        check(R"(
            const end = ends.at(-1);
            assert(end.match === silent && end.reason === "timedOut" && end.winner === 1, `a silent player forfeits to the NPC, got ${JSON.stringify(end)}`);
        )");

        // An NPC that dies or is removed has left the table.
        check(R"(
            globalThis.left = Dice.start(PLAYER, keeper.id);
            keeper.health = 0;
            assert(keeper.alive === false, "the NPC at the table was killed");
        )");
        dice.Sweep(MafiaNet::GetTime());
        check(R"(
            const end = ends.at(-1);
            assert(Dice.matchOf(PLAYER) === 0 && end.match === left && end.reason === "left" && end.winner === 0, `a killed NPC leaves the match to the player, got ${JSON.stringify(end)}`);
            keeper.remove();
            const stranger = Npc.create({ soul: "townsman", position: new Vector3(11, 10, 10) });
            globalThis.left = Dice.start(PLAYER, stranger.id);
            stranger.remove();
        )");
        dice.Sweep(MafiaNet::GetTime());
        check(R"(
            const end = ends.at(-1);
            assert(Dice.matchOf(PLAYER) === 0 && end.match === left && end.reason === "left" && end.winner === 0 && end.second === null, `a removed NPC leaves the match to the player, got ${JSON.stringify(end)}`);
        )");

        replication->DestroyEntity(player);
        replication->DestroyEntity(bystander);
        server.Shutdown();
        initialized = false;
        std::cout << "Dice NPC: an NPC sits on the second seat only; Dice.move is held to the rules and to the NPC's turn; a silent seat forfeits, NPC or player; a dead or removed NPC leaves the match\n";
        return 0;
    }
    catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (initialized) {
            server.Shutdown();
        }
        return 1;
    }
}

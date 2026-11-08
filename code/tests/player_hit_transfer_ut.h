#pragma once

#include "core/server.h"
#include "features/player/player_scripting.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <integrations/server/scripting/module.h>
#include <iostream>
#include <scripting/engine.h>
#include <stdexcept>
#include <string>

// Every server resource runs in an isolate of its own, so the `hit` a
// playerHit handler is given is a copy made for that resource. What a
// handler sets on it -- `damage`, `modifiers` in place or whole -- still has
// to reach the blow the server rules on, and the next resource's handler
// has to see it.
inline int RunPlayerHitTransferSuite() {
    KCDC::Server server;
    bool initialized  = false;
    const auto expect = [](bool condition, const std::string &message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    try {
        std::ofstream("server.json") << R"({"mod":{"level":"trosecko","required_dlc":[],"npcEnabled":false,"scoreboard":false,"world_resources":[]}})";
        for (const std::string name : {"hit-halver", "hit-quarterer"}) {
            std::filesystem::create_directories("resources/" + name);
            std::ofstream("resources/" + name + "/package.json") << R"({"name":")" << name << R"(","version":"1.0.0","mafiahub":{"serverScripts":["server.js"]}})";
        }
        // Each probe is told apart by its stamina. The edits commute, so the
        // result does not depend on which resource's handler runs first --
        // only on both of them landing. The halver also poisons its own
        // Object.prototype: `hit` is built inside its runtime, and must be
        // built there without running a script's setter.
        std::ofstream("resources/hit-halver/server.js") << R"JS(
            Object.defineProperty(Object.prototype, "stamina", { set() { throw Error("a prototype setter ran"); }, configurable: true });
            Events.on("playerHit", (player, attacker, hit) => {
                if (hit.stamina === 1) hit.damage *= 0.5;
                if (hit.stamina === 2) hit.modifiers.attack *= 2;
                if (hit.stamina === 3) return false;
                if (hit.stamina === 4) { hit.damage = 15; hit.damage = 30; hit.modifiers.attack *= 2; }
            });
        )JS";
        std::ofstream("resources/hit-quarterer/server.js") << R"JS(
            globalThis.seen = [];
            Events.on("playerHit", (player, attacker, hit) => {
                seen.push({ keys: Object.keys(hit), priced: hit.priced, source: hit.source, weapon: hit.weapon, slash: hit.attack.slash, engineDamage: hit.engineDamage, blocked: hit.blocked, sameModifiers: hit.modifiers === hit.modifiers });
                if (hit.stamina === 1) hit.damage *= 0.25;
                if (hit.stamina === 2) hit.modifiers = { attack: hit.modifiers.attack * 3, defense: 0.5, block: hit.modifiers.block, health: hit.modifiers.health };
                globalThis.kept = hit;
            });
        )JS";
        Framework::Integrations::Server::InstanceOptions options {};
        options.modSlug          = "player-hit-transfer-tests";
        options.modName          = "Player hit transfer tests";
        options.modVersion       = "1.0.0";
        options.gameName         = "player-hit-transfer-tests";
        options.gameVersion      = "1";
        options.bindHost         = "127.0.0.1";
        options.bindPort         = 28985;
        options.maxPlayers       = 4;
        options.bindPublicServer = false;
        options.verifyBuildToken = false;
        options.enableSignals    = false;
        options.modConfigSchema  = {{"level", Framework::Utils::ConfigFieldType::String, "trosecko"},
                                    {"required_dlc", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()},
                                    {"npcEnabled", Framework::Utils::ConfigFieldType::Bool, false},
                                    {"scoreboard", Framework::Utils::ConfigFieldType::Bool, false},
                                    {"world_resources", Framework::Utils::ConfigFieldType::Json, nlohmann::json::array()}};
        char executable[]        = "player-hit-transfer-tests";
        char *args[]             = {executable};
        options.argc             = 1;
        options.argv             = args;
        const auto started       = server.Init(options);
        if (!started) {
            throw std::runtime_error(started.GetError().message);
        }
        initialized = true;

        // An NPC's blow: priced by the victim's game, so `damage` is the one
        // thing to change.
        KCDC::Scripting::Events::PlayerHitProposal unpriced;
        unpriced.damage       = 20.0F;
        unpriced.engineDamage = 20.0F;
        unpriced.stamina      = 1.0F;
        expect(KCDC::Scripting::Events::PlayerHit(1, 2, unpriced), "a blow nobody refused was refused");
        expect(unpriced.damageSet && std::abs(unpriced.damage - 2.5F) < 0.0001F, "both resources' damage edits should leave 2.5, left " + std::to_string(unpriced.damage) + (unpriced.damageSet ? "" : " and unset"));

        // A player's priced swing: one resource scales the modifiers in place,
        // the other replaces them whole.
        KCDC::Scripting::Events::PlayerHitProposal priced;
        priced.damage  = 30.0F;
        priced.stamina = 2.0F;
        priced.priced  = true;
        priced.weapon  = "0123456789abcdef0123456789abcdef";
        priced.attack  = {1.0F, 42.0F, 3.0F};
        expect(KCDC::Scripting::Events::PlayerHit(1, 2, priced), "a priced blow nobody refused was refused");
        expect(!priced.damageSet && priced.damage == 30.0F, "scaling the modifiers must not set the damage");
        expect(std::abs(priced.modifiers.attack - 6.0F) < 0.0001F, "attack should be scaled by both resources to 6, was " + std::to_string(priced.modifiers.attack));
        expect(std::abs(priced.modifiers.defense - 0.5F) < 0.0001F, "a replaced modifiers object should set defense to 0.5, was " + std::to_string(priced.modifiers.defense));
        expect(priced.modifiers.block == 1.0F && priced.modifiers.health == 1.0F, "untouched modifiers should stay 1");

        // A damage set and set back is not set: the modifiers still price it.
        KCDC::Scripting::Events::PlayerHitProposal restored;
        restored.damage  = 30.0F;
        restored.stamina = 4.0F;
        restored.priced  = true;
        expect(KCDC::Scripting::Events::PlayerHit(1, 2, restored), "a restored blow was refused");
        expect(!restored.damageSet && restored.damage == 30.0F, "a damage set back to its own value must not count as set");
        expect(std::abs(restored.modifiers.attack - 2.0F) < 0.0001F, "the restored blow's attack should be 2, was " + std::to_string(restored.modifiers.attack));

        // A literal false from another resource still refuses the blow.
        KCDC::Scripting::Events::PlayerHitProposal refused;
        refused.damage  = 10.0F;
        refused.stamina = 3.0F;
        expect(!KCDC::Scripting::Events::PlayerHit(1, 2, refused), "a handler's false should refuse the blow");

        // A `hit` kept past its event changes nothing, and does not throw.
        std::ofstream("resources/hit-quarterer/assertions.js") << R"JS(
            (() => {
                const assert = (condition, message) => { if (!condition) throw Error(message); };
                assert(seen.length === 4, `expected four blows, saw ${seen.length}`);
                assert(seen.every((blow) => blow.sameModifiers), "hit.modifiers should be one object per hit");
                const [unpriced, priced] = seen;
                for (const key of ["damage", "modifiers", "stamina", "priced", "source", "attack"]) assert(unpriced.keys.includes(key), `hit is missing its own key ${key}: ${unpriced.keys}`);
                assert(unpriced.priced === false && unpriced.source === "melee" && unpriced.weapon === null && unpriced.engineDamage === 20 && unpriced.blocked === false, `read-only fields arrived as ${JSON.stringify(unpriced)}`);
                assert(priced.priced === true && priced.weapon === "0123456789abcdef0123456789abcdef" && priced.slash === 42, `a priced blow's fields arrived as ${JSON.stringify(priced)}`);
                kept.damage = 99;
                kept.modifiers = { attack: 9 };
            })();
        )JS";
        auto *engine = server.GetScriptingModule()->GetEngine();
        if (!engine->ExecuteResourceFile("hit-quarterer", "resources/hit-quarterer/assertions.js")) {
            throw std::runtime_error("Player hit assertions failed: " + engine->GetLastError());
        }
        expect(refused.damage == 10.0F && !refused.damageSet && refused.modifiers.attack == 1.0F, "a hit kept past its event must not reach the blow");
        server.Shutdown();
        initialized = false;
        std::cout << "Player hit transfer: edits from every resource reach the blow\n";
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

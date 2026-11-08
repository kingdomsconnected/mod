#pragma once

#include "shared/features/player/player_custom_buff.h"
#include "shared/features/player/player_progression_restore.h"
#include "shared/game/custom_buff_registry.h"

namespace CustomBuffTests {
    inline bool Rejects(const nlohmann::json &definition) {
        try {
            KCDC::Shared::Game::CustomBuffRegistry::Get().Register("qa:invalid", definition);
            return false;
        }
        catch (const std::invalid_argument &) {
            return true;
        }
    }
    inline nlohmann::json Definition() {
        return {{"name", "Warrior's Blessing"}, {"description", "Strength and stamina."}, {"visual", "potion_marigold_decoction"}, {"duration", 300}, {"modifiers", {{"strength", 5}, {"stamina", 20}}}};
    }
} // namespace CustomBuffTests

MODULE(custom_buffs, {
    IT("registers stable identities and resolves every existing buff spelling", {
        auto &registry = KCDC::Shared::Game::CustomBuffRegistry::Get();
        registry.Reset();
        auto definition = CustomBuffTests::Definition();
        // Use a table row known to this build rather than an example alias.
        definition["visual"] = std::string(KCDC::Shared::Game::g_Buffs.front().name);
        const auto &buff     = registry.Register("qa:blessing", definition);
        EQUALS(buff.parameters, std::string("mst+20,strength+5"));
        EQUALS(KCDC::Shared::Game::ResolveBuff("qa:blessing"), &buff.entry);
        EQUALS(KCDC::Shared::Game::ResolveBuff(buff.guid), &buff.entry);
        EQUALS(KCDC::Shared::Game::FindBuffById(buff.entry.id), &buff.entry);
        EQUALS(&registry.Register("qa:blessing", definition), &buff);
        bool shadowRefused = false;
        try {
            registry.Register(buff.guid, definition);
        }
        catch (const std::invalid_argument &) {
            shadowRefused = true;
        }
        EQUALS(shadowRefused, true);
        const auto guid        = buff.guid;
        const auto native      = buff.nativeGuid;
        definition["duration"] = 42;
        bool refused           = false;
        try {
            registry.Register("qa:blessing", definition);
        }
        catch (const std::invalid_argument &) {
            refused = true;
        }
        EQUALS(refused, true);
        registry.Reset();
        const auto &changed = registry.Register("qa:blessing", definition);
        EQUALS(changed.guid, guid);
        EQUALS(changed.nativeGuid == native, false);
        registry.Reset();
        registry.Register(guid, definition);
        shadowRefused = false;
        try {
            registry.Register("qa:blessing", definition);
        }
        catch (const std::invalid_argument &) {
            shadowRefused = true;
        }
        EQUALS(shadowRefused, true);
        registry.Reset();
    });
    IT("rejects invalid definitions without leaving a partial registration", {
        auto definition      = CustomBuffTests::Definition();
        definition["visual"] = std::string(KCDC::Shared::Game::g_Buffs.front().name);
        for (const auto &change : std::vector<nlohmann::json> {{{"duration", 0}}, {{"duration", -2}}, {{"duration", nullptr}}, {{"tickInterval", 0.001}}, {{"stackable", false}, {"maxStacks", 2}}, {{"stackable", true}, {"maxStacks", 17}}, {{"unknown", true}},
                 {{"visual", "missing"}}, {{"modifiers", {{"missing", 1}}}}, {{"modifiers", {{"stamina", 1}, {"maxStamina", 2}}}}, {{"modifiers", {{"strength", {{"operation", "bogus"}, {"value", 1}}}}}}}) {
            auto invalid = definition;
            invalid.update(change);
            EQUALS(CustomBuffTests::Rejects(invalid), true);
        }
        EQUALS(KCDC::Shared::Game::CustomBuffRegistry::Get().Snapshot().empty(), true);
    });
    IT("bounds definition packets before allocation and rejects truncation", {
        KCDC::Shared::RPC::CustomBuffDefinition sent;
        sent.body = "{\"reset\":true}";
        MafiaNet::BitStream stream;
        sent.Serialize(&stream, true);
        KCDC::Shared::RPC::CustomBuffDefinition received;
        received.Serialize(&stream, false);
        EQUALS(received.valid, true);
        EQUALS(received.body, sent.body);
        MafiaNet::BitStream truncated;
        truncated.Write(std::uint32_t(12000));
        received.body.clear();
        received.Serialize(&truncated, false);
        EQUALS(received.valid, false);
        EQUALS(received.body.empty(), true);
        MafiaNet::BitStream oversized;
        oversized.Write(KCDC::Shared::RPC::CustomBuffDefinition::kMaximumBytes + 1);
        received.Serialize(&oversized, false);
        EQUALS(received.valid, false);
    });
    IT("uses ordered maximum-health modifiers with native resource bounds", {
        std::vector<KCDC::Shared::Game::CustomBuffMaximum> modifiers {{KCDC::Shared::Game::CustomBuffMaximum::Operation::Multiply, 1.5F}, {KCDC::Shared::Game::CustomBuffMaximum::Operation::Add, 20.0F}};
        EQUALS(KCDC::Shared::Game::ApplyCustomHealthMaximum(100, modifiers), 170.0F);
        modifiers.push_back({KCDC::Shared::Game::CustomBuffMaximum::Operation::Set, 50.0F});
        EQUALS(KCDC::Shared::Game::ApplyCustomHealthMaximum(100, modifiers), 50.0F);
        modifiers = {{KCDC::Shared::Game::CustomBuffMaximum::Operation::Add, -1000}};
        EQUALS(KCDC::Shared::Game::ApplyCustomHealthMaximum(100, modifiers), 1.0F);
    });
    IT("converts XP across every level boundary and canonicalizes the cap", {
        for (const auto &track : KCDC::Shared::Game::g_ProgressionTracks) {
            for (std::uint32_t level = 0; level <= track.cap; ++level) {
                const auto total = KCDC::Shared::Game::TotalXpQ7ForLevel(track, level);
                const auto state = KCDC::Shared::Game::ProgressFromTotalXpQ7(track, total);
                EQUALS(state.level, level);
                EQUALS(state.xpQ7, 0U);
                if (level != 0) {
                    const auto lower = KCDC::Shared::Game::ProgressFromTotalXpQ7(track, total - 1);
                    EQUALS(lower.level, level - 1);
                    EQUALS(lower.xpQ7, static_cast<std::uint32_t>(KCDC::Shared::Game::XpQ7ToNextLevel(track, level - 1) - 1));
                }
            }
        }
    });
    IT("validates an entire restore before any tracks can be applied", {
        nlohmann::json snapshot = {{"tracks", {{"strength", {{"level", 2}, {"xp", 0.5}}}}}, {"perks", nlohmann::json::array()}, {"perkPoints", {{"main", 0}}}};
        const auto plan         = KCDC::Shared::Game::ProgressionRestorePlan::Parse(snapshot);
        EQUALS(plan.tracks.size(), std::size_t(1));
        EQUALS(plan.replacePerks, true);
        EQUALS(plan.points.front().second, 0U);
        snapshot["perkPoints"]["main"] = -1;
        bool refused                   = false;
        try {
            KCDC::Shared::Game::ProgressionRestorePlan::Parse(snapshot);
        }
        catch (const std::invalid_argument &) {
            refused = true;
        }
        EQUALS(refused, true);
    });
    IT("restores a capped track whatever remainder it reports", {
        const auto index        = KCDC::Shared::Game::FindProgressionTrack("strength");
        const auto &track       = KCDC::Shared::Game::g_ProgressionTracks[index];
        nlohmann::json snapshot = {{"tracks", {{"strength", {{"level", track.cap}, {"xp", 12.5}}}}}};
        const auto plan         = KCDC::Shared::Game::ProgressionRestorePlan::Parse(snapshot);
        EQUALS(plan.tracks.size(), std::size_t(1));
        EQUALS(static_cast<std::uint64_t>(plan.tracks.front().second), KCDC::Shared::Game::TotalXpQ7ForLevel(track, track.cap));
    });
    IT("round trips absolute zero XP orders without accepting invalid tracks", {
        KCDC::Shared::RPC::ProgressionOrder order;
        order.sequence = 1;
        order.kind     = KCDC::Shared::RPC::ProgressionOrderKind::SetTotalXp;
        order.track    = 0;
        order.value    = 0;
        order.announce = true;
        MafiaNet::BitStream stream;
        order.Serialize(&stream, true);
        KCDC::Shared::RPC::ProgressionOrder received;
        received.Serialize(&stream, false);
        EQUALS(received.IsSane(), true);
        EQUALS(received.announce, true);
        received.track = KCDC::Shared::RPC::kProgressionTracks;
        EQUALS(received.IsSane(), false);
    });
});

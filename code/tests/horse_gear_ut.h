#pragma once

#include "horse_life_ut.h"

#include "shared/features/horse/horse_entity.h"
#include "shared/features/horse/horse_gear.h"
#include "shared/game/horse_gear_catalog.h"

#include <mafianet/bit_stream.h>

#include <string>

namespace HorseGearTests {
    /** The first catalog entry for a slot. */
    inline const KCDC::Shared::Game::HorseGearEntry *FirstFor(KCDC::Shared::Game::HorseGearSlot slot) {
        for (const auto &entry : KCDC::Shared::Game::g_HorseGear) {
            if (entry.slot == slot) {
                return &entry;
            }
        }
        return nullptr;
    }

    inline KCDC::Shared::Game::HorseGear FullSet() {
        KCDC::Shared::Game::HorseGear gear;
        for (std::size_t i = 0; i < KCDC::Shared::Game::kHorseGearSlotCount; ++i) {
            const auto slot = static_cast<KCDC::Shared::Game::HorseGearSlot>(i);
            gear.At(slot)   = FirstFor(slot);
        }
        return gear;
    }
} // namespace HorseGearTests

MODULE(horse_gear, {
    IT("has gear for every slot, and knows the game's own saddle", {
        for (std::size_t i = 0; i < KCDC::Shared::Game::kHorseGearSlotCount; ++i) {
            EQUALS(HorseGearTests::FirstFor(static_cast<KCDC::Shared::Game::HorseGearSlot>(i)) != nullptr, true);
        }
        const auto *saddle = KCDC::Shared::Game::FindHorseGear("BasicSaddle01_m01");
        EQUALS(saddle != nullptr, true);
        EQUALS(saddle->slot == KCDC::Shared::Game::HorseGearSlot::Saddle, true);
        EQUALS(KCDC::Shared::Game::FindHorseGear("12427009-3F05-45F4-81F4-C8163B3A8543") == saddle, true);
        EQUALS(KCDC::Shared::Game::FindHorseGear("CaparisonNotAThing") == nullptr, true);
    });

    IT("round-trips a full set through the wire text, in slot order", {
        const KCDC::Shared::Game::HorseGear gear = HorseGearTests::FullSet();
        const std::string packed                 = KCDC::Shared::Game::PackHorseGear(gear);
        EQUALS(packed.size(), KCDC::Shared::Game::kMaxPackedHorseGear);

        KCDC::Shared::Game::HorseGear read;
        EQUALS(KCDC::Shared::Game::UnpackHorseGear(packed, read) == KCDC::Shared::Game::HorseGearError::None, true);
        EQUALS(read == gear, true);

        // The receiver dresses in wire order, so the saddle leads.
        KCDC::Shared::RPC::ItemClassId first;
        EQUALS(KCDC::Shared::Game::ReadPackedItemClass(packed, 0, first), true);
        EQUALS(KCDC::Shared::Game::FindHorseGear(first) == gear.At(KCDC::Shared::Game::HorseGearSlot::Saddle), true);
    });

    // The server dresses every spawned horse from these; a preset that broke
    // the slot rules would spawn a horse the server itself would refuse.
    IT("turns every one of the game's tack presets into a valid set", {
        EQUALS(KCDC::Shared::Game::g_HorseGearPresets.empty(), false);
        for (const auto &preset : KCDC::Shared::Game::g_HorseGearPresets) {
            KCDC::Shared::Game::HorseGear gear;
            EQUALS(KCDC::Shared::Game::HorseGearFromPreset(preset, gear) == KCDC::Shared::Game::HorseGearError::None, true);
        }
        EQUALS(KCDC::Shared::Game::g_CommonHorseGearPresets.empty(), false);
        for (const auto name : KCDC::Shared::Game::g_CommonHorseGearPresets) {
            const auto *preset = KCDC::Shared::Game::FindHorseGearPreset(name);
            EQUALS(preset != nullptr, true);
            KCDC::Shared::Game::HorseGear gear;
            KCDC::Shared::Game::HorseGearFromPreset(*preset, gear);
            EQUALS(gear.At(KCDC::Shared::Game::HorseGearSlot::Saddle) != nullptr, true);
        }
        EQUALS(KCDC::Shared::Game::FindHorseGearPreset("HORSE_COMMON01") != nullptr, true);
        EQUALS(KCDC::Shared::Game::FindHorseGearPreset("soldier_bergov_3_05") == nullptr, true);
    });

    IT("reads an empty set as a bare horse", {
        KCDC::Shared::Game::HorseGear read;
        EQUALS(KCDC::Shared::Game::UnpackHorseGear("", read) == KCDC::Shared::Game::HorseGearError::None, true);
        EQUALS(read == KCDC::Shared::Game::HorseGear {}, true);
    });

    IT("refuses what the game would not let a horse wear", {
        KCDC::Shared::Game::HorseGear read;

        // A caparison lies on a saddle.
        KCDC::Shared::Game::HorseGear torsoOnly;
        torsoOnly.At(KCDC::Shared::Game::HorseGearSlot::Torso) = HorseGearTests::FirstFor(KCDC::Shared::Game::HorseGearSlot::Torso);
        EQUALS(KCDC::Shared::Game::UnpackHorseGear(KCDC::Shared::Game::PackHorseGear(torsoOnly), read) == KCDC::Shared::Game::HorseGearError::TorsoWithoutSaddle, true);

        // Two saddles.
        const std::string saddle = KCDC::Shared::Game::PackHorseGear(KCDC::Shared::Game::HorseGear {{HorseGearTests::FirstFor(KCDC::Shared::Game::HorseGearSlot::Saddle)}});
        EQUALS(KCDC::Shared::Game::UnpackHorseGear(saddle + saddle, read) == KCDC::Shared::Game::HorseGearError::SlotTaken, true);

        // A person's shirt is not horse gear.
        EQUALS(KCDC::Shared::Game::UnpackHorseGear(std::string(32, '1'), read) == KCDC::Shared::Game::HorseGearError::NotHorseGear, true);

        // Half a class, and something that is not hex.
        EQUALS(KCDC::Shared::Game::UnpackHorseGear(std::string(16, '1'), read) == KCDC::Shared::Game::HorseGearError::Malformed, true);
        EQUALS(KCDC::Shared::Game::UnpackHorseGear(std::string(32, 'z'), read) == KCDC::Shared::Game::HorseGearError::Malformed, true);

        // A piece keyed under the wrong slot.
        KCDC::Shared::Game::HorseGear misplaced;
        misplaced.At(KCDC::Shared::Game::HorseGearSlot::Head) = HorseGearTests::FirstFor(KCDC::Shared::Game::HorseGearSlot::Saddle);
        EQUALS(KCDC::Shared::Game::ValidateHorseGear(misplaced) == KCDC::Shared::Game::HorseGearError::SlotTaken, true);
    });

    // The rider is the simulator and owns the horse: a gear it reported and
    // the server refused must not come back up its own stream as a verdict.
    IT("never takes the gear from the simulator's upstream state", {
        KCDC::Shared::Entities::HorseEntity simulator;
        simulator.gear         = KCDC::Shared::Game::PackHorseGear(HorseGearTests::FullSet());
        simulator.gearRevision = 9;

        KCDC::Shared::Entities::HorseEntity server;
        server.gear         = "";
        server.gearRevision = 4;

        HorseLifeTests::Send(simulator, server, false);
        EQUALS(server.gear, std::string());
        EQUALS(server.gearRevision, 4U);
    });

    IT("relays the server's gear to every other copy", {
        KCDC::Shared::Entities::HorseEntity server;
        server.gear         = KCDC::Shared::Game::PackHorseGear(HorseGearTests::FullSet());
        server.gearRevision = 2;

        KCDC::Shared::Entities::HorseEntity observer;
        HorseLifeTests::Send(server, observer, true);
        EQUALS(observer.gear, server.gear);
        EQUALS(observer.gearRevision, 2U);
    });

    IT("carries the gear on the forced state, the simulator's one way to hear it", {
        KCDC::Shared::Entities::HorseEntity server;
        server.gear         = KCDC::Shared::Game::PackHorseGear(HorseGearTests::FullSet());
        server.gearRevision = 5;

        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        server.SerializeForcedState(writer);

        KCDC::Shared::Entities::HorseEntity simulator;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        simulator.SerializeForcedState(reader);
        EQUALS(simulator.gear, server.gear);
        EQUALS(simulator.gearRevision, 5U);
    });

    IT("puts a report on the wire and reads it back", {
        KCDC::Shared::RPC::HorseGearReport report;
        report.horseNetworkId  = 42;
        report.gear            = KCDC::Shared::Game::PackHorseGear(HorseGearTests::FullSet());
        report.basedOnRevision = 3;

        MafiaNet::BitStream stream;
        report.Serialize(&stream, true);

        KCDC::Shared::RPC::HorseGearReport read;
        read.Serialize(&stream, false);
        EQUALS(read.IsSane(), true);
        EQUALS(read.horseNetworkId, 42ULL);
        EQUALS(read.gear, report.gear);
        EQUALS(read.basedOnRevision, 3U);

        read.gear += "0";
        EQUALS(read.IsSane(), false);
    });
});

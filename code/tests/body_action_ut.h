#pragma once

#include "human_soul_ut.h"

#include "shared/features/player/player_action.h"
#include "shared/features/player/player_entity.h"
#include "shared/game/body_action_catalog.h"

#include <mafianet/bit_stream.h>
#include <utils/hashing.h>
#include <utils/string_utils.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

MODULE(body_actions, {
    IT("preserves a target-height pickup and clears its blend on the next action", {
        KCDC::Shared::Entities::HumanEntity owner, receiver;
        HumanSoulTests::Channel channel;
        owner.soul = HumanSoulTests::Sample();
        channel.Send(owner, receiver, true);
        owner.bodyAction.revision         = 1;
        owner.bodyAction.kind             = KCDC::Shared::Game::BodyAction::Kind::Fragment;
        owner.bodyAction.fragment         = KCDC::Shared::Game::BodyAction::kPickingFragment;
        owner.bodyAction.hasPickingMotion = true;
        owner.bodyAction.pickingMotion    = {0.8F, 0.66F, 0.4F};
        EQUALS(owner.bodyAction.IsSane(), true);
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.bodyAction == owner.bodyAction, true);
        EQUALS(channel.Send(owner, receiver), false);

        owner.bodyAction          = {};
        owner.bodyAction.revision = 2;
        owner.bodyAction.kind     = KCDC::Shared::Game::BodyAction::Kind::Stop;
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.bodyAction.hasPickingMotion, false);
        EQUALS(receiver.bodyAction.pickingMotion[0], 0.0F);
    });

    IT("rejects invalid pickup blend values and blends attached to other actions", {
        KCDC::Shared::Game::BodyAction action;
        action.revision         = 1;
        action.kind             = KCDC::Shared::Game::BodyAction::Kind::Fragment;
        action.fragment         = KCDC::Shared::Game::BodyAction::kPickingFragment;
        action.hasPickingMotion = true;
        action.pickingMotion    = {0.0F, 1.0F, 0.5F};
        EQUALS(action.IsSane(), true);
        for (float invalid : {-0.1F, 1.1F, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            action.pickingMotion[0] = invalid;
            EQUALS(action.IsSane(), false);
        }
        action.pickingMotion[0] = 0.5F;
        action.fragment         = 0x5EFE4272U;
        EQUALS(action.IsSane(), false);
    });

    IT("carries a seat's alignment target and drops it with the next action", {
        KCDC::Shared::Entities::HumanEntity owner, receiver;
        HumanSoulTests::Channel channel;
        owner.soul = HumanSoulTests::Sample();
        channel.Send(owner, receiver, true);
        owner.bodyAction.revision       = 1;
        owner.bodyAction.kind           = KCDC::Shared::Game::BodyAction::Kind::Fragment;
        owner.bodyAction.fragment       = 0x5EFE4272U;
        owner.bodyAction.hasTarget      = true;
        owner.bodyAction.targetRotation = {0.0F, 0.0F, 0.7071068F, 0.7071068F};
        owner.bodyAction.targetPosition = {512.25F, 1024.5F, 33.75F};
        EQUALS(owner.bodyAction.IsSane(), true);
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.bodyAction == owner.bodyAction, true);

        owner.bodyAction          = {};
        owner.bodyAction.revision = 2;
        owner.bodyAction.kind     = KCDC::Shared::Game::BodyAction::Kind::Stop;
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.bodyAction.hasTarget, false);
    });

    IT("rejects a non-finite alignment target and one attached to a non-fragment", {
        KCDC::Shared::Game::BodyAction action;
        action.revision  = 1;
        action.kind      = KCDC::Shared::Game::BodyAction::Kind::Fragment;
        action.fragment  = 0x5EFE4272U;
        action.hasTarget = true;
        EQUALS(action.IsSane(), true);
        action.targetPosition[2] = std::numeric_limits<float>::quiet_NaN();
        EQUALS(action.IsSane(), false);
        action.targetPosition[2] = 0.0F;
        action.targetRotation[3] = std::numeric_limits<float>::infinity();
        EQUALS(action.IsSane(), false);
        action.targetRotation[3] = 1.0F;
        action.kind              = KCDC::Shared::Game::BodyAction::Kind::Stop;
        EQUALS(action.IsSane(), false);
    });

    IT("carries a seated stance and a fragment action on the human delta channel", {
        KCDC::Shared::Entities::HumanEntity owner, receiver;
        HumanSoulTests::Channel channel;
        owner.soul = HumanSoulTests::Sample();
        channel.Send(owner, receiver, true);

        owner.bodyStance.stance   = KCDC::Shared::Game::BodyStance::Sitting;
        owner.bodyStance.side     = 2;
        owner.bodyStance.seatGuid = 0x02DCE23071D2A21AULL;
        owner.bodyStance.reading  = KCDC::Shared::Game::BodyStanceState::kReadingFromInventory;
        owner.bodyStance.holdingDecoy = true;
        owner.bodyAction.revision = 1;
        owner.bodyAction.kind     = KCDC::Shared::Game::BodyAction::Kind::Fragment;
        owner.bodyAction.fragment = 0x5EFE4272U;
        owner.bodyAction.option   = 0;
        owner.bodyAction.priority = 5;
        owner.bodyAction.tagCount = 3;
        owner.bodyAction.tags[0]  = 0x11111111U;
        owner.bodyAction.tags[1]  = 0x22222222U;
        owner.bodyAction.tags[2]  = 0x33333333U;
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.bodyStance == owner.bodyStance, true);
        EQUALS(receiver.bodyAction == owner.bodyAction, true);

        // Unchanged state costs nothing.
        EQUALS(channel.Send(owner, receiver), false);

        owner.bodyStance = {};
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.bodyStance.stance == KCDC::Shared::Game::BodyStance::Standing, true);
        EQUALS(receiver.bodyStance.seatGuid, 0ULL);
        EQUALS(receiver.bodyStance.holdingDecoy, false);
    });

    IT("carries a praise and a stop", {
        KCDC::Shared::Entities::HumanEntity owner, receiver;
        HumanSoulTests::Channel channel;
        owner.soul = HumanSoulTests::Sample();
        channel.Send(owner, receiver, true);

        owner.bodyAction          = {};
        owner.bodyAction.revision = 7;
        owner.bodyAction.kind     = KCDC::Shared::Game::BodyAction::Kind::Praise;
        owner.bodyAction.partner  = 3868;
        owner.bodyAction.variant  = 2;
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.bodyAction == owner.bodyAction, true);
        EQUALS(receiver.bodyAction.IsSane(), true);

        owner.bodyAction          = {};
        owner.bodyAction.revision = 8;
        owner.bodyAction.kind     = KCDC::Shared::Game::BodyAction::Kind::Stop;
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.bodyAction.kind == KCDC::Shared::Game::BodyAction::Kind::Stop, true);
        EQUALS(receiver.bodyAction.partner, 0ULL);
    });

    IT("reads a stance or an action kind it has no meaning for as nothing", {
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        std::uint8_t wire  = 6; // crouch: another replication's
        std::uint8_t side  = 0;
        std::uint64_t seat = 0;
        writer.Field(wire);
        writer.Field(side);
        writer.Field(seat);

        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        KCDC::Shared::Game::BodyStanceState decoded;
        decoded.stance = KCDC::Shared::Game::BodyStance::Sitting;
        decoded.Serialize(reader);
        EQUALS(decoded.stance == KCDC::Shared::Game::BodyStance::Standing, true);

        KCDC::Shared::Game::BodyAction action;
        action.revision = 1;
        action.kind     = static_cast<KCDC::Shared::Game::BodyAction::Kind>(9);
        MafiaNet::BitStream actionStream;
        Framework::Networking::Replication::FieldSerializer actionWriter(&actionStream, true);
        action.Serialize(actionWriter);
        Framework::Networking::Replication::FieldSerializer actionReader(&actionStream, false);
        KCDC::Shared::Game::BodyAction decodedAction;
        decodedAction.Serialize(actionReader);
        EQUALS(decodedAction.kind == KCDC::Shared::Game::BodyAction::Kind::None, true);
        EQUALS(decodedAction.IsSane(), false);
    });

    IT("refuses an action that cannot have been captured", {
        KCDC::Shared::Game::BodyAction action;
        EQUALS(action.IsSane(), true);
        action.revision = 1;
        EQUALS(action.IsSane(), false); // a revision of nothing
        action.kind = KCDC::Shared::Game::BodyAction::Kind::Fragment;
        EQUALS(action.IsSane(), false); // no fragment
        action.fragment = 0x5EFE4272U;
        EQUALS(action.IsSane(), true);
        action.tagCount = KCDC::Shared::Game::BodyAction::kMaxTags + 1;
        EQUALS(action.IsSane(), false);
        action          = {};
        action.revision = 2;
        action.kind     = KCDC::Shared::Game::BodyAction::Kind::Praise;
        EQUALS(action.IsSane(), false); // no dog
        EQUALS(KCDC::Shared::Game::BodyAction::Advance(0xFFFFFFFFU), 1U);
    });

    IT("carries the running lockpicking actions in order and drops the ones that ended", {
        KCDC::Shared::Entities::HumanEntity owner, receiver;
        HumanSoulTests::Channel channel;
        owner.soul = HumanSoulTests::Sample();
        channel.Send(owner, receiver, true);

        // The loop is queued ahead of the idle, both at 6, on a chest.
        owner.minigameActions.count      = 2;
        owner.minigameActions.actions[0] = {1, 0x5CCED8DFU, 1, {0xFB44B3B7U}, 6};
        owner.minigameActions.actions[1] = {2, 0x21B57024U, 1, {0xFB44B3B7U}, 6};
        EQUALS(owner.minigameActions.IsSane(), true);
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.minigameActions == owner.minigameActions, true);
        EQUALS(channel.Send(owner, receiver), false);

        // Both ended for the turn, which is posed rather than played.
        owner.minigameActions              = {};
        owner.minigameActions.count        = 1;
        owner.minigameActions.actions[0]   = {3, KCDC::Shared::Game::MinigameActions::kTurnFragment, 1, {0xFB44B3B7U}, 6};
        owner.minigameActions.turnFraction = 128;
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.minigameActions == owner.minigameActions, true);
        EQUALS(receiver.minigameActions.actions[1].id, 0);

        owner.minigameActions = {};
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.minigameActions.count, 0);

        const std::string turn = "lockpickingturn";
        EQUALS(Framework::Utils::Hashing::CalculateCRC32(turn.c_str(), turn.size()), KCDC::Shared::Game::MinigameActions::kTurnFragment);
        KCDC::Shared::Game::MinigameActions state;
        state.count = KCDC::Shared::Game::MinigameActions::kMaxActions + 1;
        EQUALS(state.IsSane(), false);
        state.count = 1;
        EQUALS(state.IsSane(), false); // no fragment
    });

    IT("carries every alchemy step with all of its tags, in the order the owner queued them", {
        KCDC::Shared::Entities::HumanEntity owner, receiver;
        HumanSoulTests::Channel channel;
        owner.soul = HumanSoulTests::Sample();
        channel.Send(owner, receiver, true);

        const auto crc = [](const char *name) {
            return Framework::Utils::Hashing::CalculateCRC32(name, std::strlen(name));
        };
        // The idle at the mortar, made as the game makes it (priority 4,
        // flags 0x6: an interrupted idle is queued again), then oil on the
        // raised pot with the bellows slaved, as AlchemyUse is tagged in the
        // catalog.
        owner.minigameActions.count      = 2;
        owner.minigameActions.actions[0] = {7, crc("alchemyidle"), 1, {crc("alchemymortar")}, 4, KCDC::Shared::Game::MinigameActions::kReplayedFlags};
        owner.minigameActions.actions[1] = {8, crc("alchemyuse"), 3, {crc("alchemyoil"), crc("alchemypotupposition"), crc("slavebellows")}, 6};
        EQUALS(owner.minigameActions.IsSane(), true);
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.minigameActions == owner.minigameActions, true);
        EQUALS(receiver.minigameActions.actions[0].flags, 0x6);
        EQUALS(receiver.minigameActions.actions[1].tagCount, 3);
        EQUALS(receiver.minigameActions.actions[1].tags[2], crc("slavebellows"));

        // The use ended; the idle is still the same action, not a new one.
        owner.minigameActions.count      = 1;
        owner.minigameActions.actions[1] = {};
        EQUALS(channel.Send(owner, receiver), true);
        EQUALS(receiver.minigameActions.count, 1);
        EQUALS(receiver.minigameActions.actions[0].id, 7);

        KCDC::Shared::Game::MinigameActions state;
        state.count               = 1;
        state.actions[0].fragment = crc("alchemytake");
        state.actions[0].tagCount = KCDC::Shared::Game::MinigameActions::kMaxTags + 1;
        EQUALS(state.IsSane(), false); // more tags than travel
        state.actions[0].tagCount = 0;
        EQUALS(state.IsSane(), true);
        state.actions[0].flags = 0x40; // a controller's state bit, never a constructor's
        EQUALS(state.IsSane(), false);
    });

    IT("keys every catalogued action by Mannequin's own name CRC", {
        // The CRCs the local controller queued in game, read off the probe.
        EQUALS(KCDC::Shared::Game::FindBodyAction(0x5EFE4272U) != nullptr, true); // SitDown
        EQUALS(KCDC::Shared::Game::FindBodyAction(0xE2671BA1U) != nullptr, true); // StandUp
        EQUALS(KCDC::Shared::Game::FindBodyAction(0x03E46E25U) != nullptr, true); // Picking
        EQUALS(KCDC::Shared::Game::FindBodyAction(0x2F23CF80U)->looped, true);    // Digging
        EQUALS(KCDC::Shared::Game::FindBodyAction(0x03E46E25U)->looped, false);
        EQUALS(KCDC::Shared::Game::FindBodyAction(0x99056183U) != nullptr, true); // DecoyDraw
        EQUALS(KCDC::Shared::Game::FindBodyAction(0xAB7479E0U) != nullptr, true); // DecoyThrow
        EQUALS(KCDC::Shared::Game::FindBodyAction(0x90FA6B1AU) != nullptr, true); // DecoyPlace
        EQUALS(KCDC::Shared::Game::FindBodyAction(0x5E42015FU) == nullptr, true); // SittingIdle is the stance, not an action
        EQUALS(KCDC::Shared::Game::FindBodyAction(0x67DF90A6U) == nullptr, true); // WeaponHolsterFull belongs to the hand items
        EQUALS(KCDC::Shared::Game::FindBodyAction(0xC7FC5EC2U) == nullptr, true); // WallLook belongs to the battlement lean
        // The world smart objects' player behaviors, which run to their end.
        EQUALS(KCDC::Shared::Game::FindBodyAction(0xD82CF22BU)->looped, false); // Drying
        EQUALS(KCDC::Shared::Game::FindBodyAction(0x3F0D92C1U)->looped, false); // SmokehouseSniff
        EQUALS(KCDC::Shared::Game::FindBodyAction(0xB57D7A97U)->looped, false); // CookingPanKitchen
        EQUALS(KCDC::Shared::Game::FindBodyAction(0xABD8BAA1U)->looped, false); // CookingPanCamp

        for (const auto &entry : KCDC::Shared::Game::g_BodyActions) {
            const std::string lower = Framework::Utils::StringUtils::ToLower(entry.fragment);
            EQUALS(Framework::Utils::Hashing::CalculateCRC32(lower.c_str(), lower.size()), entry.crc);
        }
    });
});

#pragma once

#include <utils/safe_win32.h>

#include "sdk/cryengine/action/i_mannequin_action.h"
#include "features/social/social_emote_wheel.h"
#include "shared/features/social/social_emote_sequence.h"
#include "shared/game/emote_catalog.h"

MODULE(emote_protocol, {
    IT("round trips an allowlisted gesture and rejects every truncation", {
        KCDC::Shared::RPC::Emote event;
        event.actorNetworkId = 42;
        event.sequence       = 1;
        event.emoteId        = 1;
        MafiaNet::BitStream stream;
        event.Serialize(&stream, true);
        KCDC::Shared::RPC::Emote decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.actorNetworkId, 42U);
        EQUALS(decoded.sequence, 1U);
        EQUALS(decoded.emoteId, 1U);
        EQUALS(decoded.phase, KCDC::Shared::RPC::Emote::Start);
        for (unsigned int size = 0; size < stream.GetNumberOfBytesUsed(); ++size) {
            MafiaNet::BitStream truncated(stream.GetData(), size, false);
            KCDC::Shared::RPC::Emote partial;
            partial.Serialize(&truncated, false);
            EQUALS(partial.IsSane(), false);
        }
    });

    IT("rejects unknown gestures, phases and zero sequences", {
        KCDC::Shared::RPC::Emote event;
        event.sequence = 1;
        event.emoteId  = 1;
        EQUALS(event.IsSane(), true);
        event.emoteId = 65535;
        EQUALS(event.IsSane(), false);
        event.emoteId = 1;
        event.phase   = 2;
        EQUALS(event.IsSane(), false);
        event.phase    = 0;
        event.sequence = 0;
        EQUALS(event.IsSane(), false);
    });

    IT("prevents duplicates, spam and stale stops without losing newer starts", {
        KCDC::Shared::Game::EmoteSequence gate;
        KCDC::Shared::RPC::Emote event;
        event.sequence = 1;
        event.emoteId  = 1;
        EQUALS(gate.Accept(event, 1000, true), true);
        EQUALS(gate.Accept(event, 2000, true), false);
        event.sequence = 2;
        EQUALS(gate.Accept(event, 1100, true), false);
        EQUALS(gate.Accept(event, 2000, true), true);
        event.phase    = KCDC::Shared::RPC::Emote::Stop;
        event.sequence = 1;
        EQUALS(gate.Accept(event, 2100, true), false);
        event.sequence = 2;
        event.emoteId  = 2;
        EQUALS(gate.Accept(event, 2100, true), false);
        event.emoteId = 1;
        EQUALS(gate.Accept(event, 2100, true), true);
        EQUALS(gate.Accept(event, 2100, true), false);
        gate           = {};
        event.phase    = KCDC::Shared::RPC::Emote::Start;
        event.sequence = 1;
        EQUALS(gate.Accept(event, 2200, true), true);
    });

    IT("keeps the stock gestures on the default wheel", {
        const char *const stock[] = {"Wave", "Big wave", "Nod", "Respectful nod", "Bow", "Cheer"};
        EQUALS(KCDC::Features::Social::EmoteWheelService::kDefaultSlots.size(), sizeof(stock) / sizeof(stock[0]));
        for (std::size_t slot = 0; slot < KCDC::Features::Social::EmoteWheelService::kDefaultSlots.size(); ++slot) {
            const KCDC::Shared::Game::EmoteEntry *entry = KCDC::Shared::Game::FindEmote(KCDC::Features::Social::EmoteWheelService::kDefaultSlots[slot]);
            EQUALS(entry != nullptr, true);
            EQUALS(std::string(entry != nullptr ? entry->name : ""), std::string(stock[slot]));
        }
    });
});

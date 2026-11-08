#pragma once

#include "shared/features/pickpocket/pickpocket_rpc.h"

#include <mafianet/bit_stream.h>

#include <cstdint>

MODULE(pickpocket_protocol, {
    IT("round trips a stocked verdict", {
        KCDC::Shared::RPC::PickpocketVerdict verdict;
        verdict.requestId = 9;
        verdict.refusal   = static_cast<std::uint8_t>(KCDC::Shared::RPC::PickpocketRefusal::None);
        verdict.items.push_back(KCDC::Shared::RPC::PickpocketItem {KCDC::Shared::RPC::ItemClassId {0x414AB25CDFEA5D01ULL, 0x1821F8A51169B49AULL}, 12});
        verdict.items.push_back(KCDC::Shared::RPC::PickpocketItem {KCDC::Shared::RPC::ItemClassId {1, 2}, 1});
        EQUALS(verdict.IsSane(), true);

        MafiaNet::BitStream stream;
        verdict.Serialize(&stream, true);
        KCDC::Shared::RPC::PickpocketVerdict decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.Accepted(), true);
        EQUALS(decoded.requestId, verdict.requestId);
        EQUALS(decoded.items.size(), verdict.items.size());
        EQUALS(decoded.items[0].itemClass == verdict.items[0].itemClass, true);
        EQUALS(decoded.items[0].amount, verdict.items[0].amount);
    });

    IT("refuses lines an item request could not carry", {
        KCDC::Shared::RPC::PickpocketItem empty {KCDC::Shared::RPC::ItemClassId {1, 1}, 0};
        KCDC::Shared::RPC::PickpocketItem huge {KCDC::Shared::RPC::ItemClassId {1, 1}, KCDC::Shared::RPC::kMaxGiveAmount + 1};
        KCDC::Shared::RPC::PickpocketItem classless {KCDC::Shared::RPC::ItemClassId {}, 1};
        EQUALS(empty.IsSane(), false);
        EQUALS(huge.IsSane(), false);
        EQUALS(classless.IsSane(), false);
    });

    IT("only lets a theft carry taken lines", {
        KCDC::Shared::RPC::PickpocketResult result;
        result.requestId = 3;
        result.outcome   = static_cast<std::uint8_t>(KCDC::Shared::RPC::PickpocketOutcome::Caught);
        result.taken.push_back(KCDC::Shared::RPC::PickpocketItem {KCDC::Shared::RPC::ItemClassId {1, 1}, 1});
        EQUALS(result.IsSane(), false);

        result.outcome = static_cast<std::uint8_t>(KCDC::Shared::RPC::PickpocketOutcome::Stole);
        EQUALS(result.IsSane(), true);

        MafiaNet::BitStream stream;
        result.Serialize(&stream, true);
        KCDC::Shared::RPC::PickpocketResult decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.taken.size(), static_cast<std::size_t>(1));
    });

    IT("refuses more taken lines than the loot grid has tiles", {
        KCDC::Shared::RPC::PickpocketResult result;
        result.requestId = 4;
        result.outcome   = static_cast<std::uint8_t>(KCDC::Shared::RPC::PickpocketOutcome::Stole);
        for (std::size_t i = 0; i <= KCDC::Shared::RPC::kMaxPickpocketTaken; ++i) {
            result.taken.push_back(KCDC::Shared::RPC::PickpocketItem {KCDC::Shared::RPC::ItemClassId {i + 1, 1}, 1});
        }
        EQUALS(result.IsSane(), false);
    });

    IT("refuses an unknown refusal or outcome", {
        KCDC::Shared::RPC::PickpocketVerdict verdict;
        verdict.requestId = 1;
        verdict.refusal   = 42;
        EQUALS(verdict.IsSane(), false);

        KCDC::Shared::RPC::PickpocketResult result;
        result.requestId = 1;
        result.outcome   = 42;
        EQUALS(result.IsSane(), false);
    });
});

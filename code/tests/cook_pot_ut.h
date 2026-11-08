#pragma once
#include "shared/features/cook_pot/cook_pot_entity.h"
#include "shared/features/cook_pot/cook_pot_message.h"
#include "shared/features/cook_pot/cook_pot_state.h"
#include <cstring>

MODULE(cook_pots, {
    IT("replicates a meal without disclosing server poison state", {
        KCDC::Shared::Entities::CookPotEntity sent;
        sent.guid           = 42;
        sent.stockWorld     = 7;
        sent.stock.food     = KCDC::Shared::Game::CookPotFood::Soup;
        sent.stock.portions = 2;
        sent.stock.capacity = 4;
        sent.stock.enabled  = false;
        MafiaNet::BitStream clean;
        Framework::Networking::Replication::FieldSerializer cleanWriter(&clean, true);
        sent.OnSerializeConstruction(cleanWriter);
        sent.SerializeFields(cleanWriter);

        sent.stock.poison = {123, 456};
        MafiaNet::BitStream poisoned;
        Framework::Networking::Replication::FieldSerializer poisonWriter(&poisoned, true);
        sent.OnSerializeConstruction(poisonWriter);
        sent.SerializeFields(poisonWriter);
        EQUALS(clean.GetNumberOfBitsUsed(), poisoned.GetNumberOfBitsUsed());
        EQUALS(std::memcmp(clean.GetData(), poisoned.GetData(), clean.GetNumberOfBytesUsed()), 0);

        KCDC::Shared::Entities::CookPotEntity received;
        Framework::Networking::Replication::FieldSerializer reader(&poisoned, false);
        received.OnSerializeConstruction(reader);
        received.SerializeFields(reader);
        EQUALS(reader.Good(), true);
        EQUALS(received.guid, sent.guid);
        EQUALS(received.stockWorld, sent.stockWorld);
        EQUALS(received.stock.food == sent.stock.food, true);
        EQUALS(received.stock.portions, sent.stock.portions);
        EQUALS(received.stock.capacity, sent.stock.capacity);
        EQUALS(received.stock.enabled, sent.stock.enabled);
        EQUALS(received.stock.poison.IsValid(), false);
        EQUALS(sent.stock.poison.IsValid(), true);
    });
    IT("poison delivery keeps its body identity across the wire", {
        KCDC::Shared::RPC::ApplyBuff sent;
        sent.buff        = {1, 2};
        sent.incarnation = 7;
        MafiaNet::BitStream stream;
        sent.Serialize(&stream, true);
        KCDC::Shared::RPC::ApplyBuff received;
        received.Serialize(&stream, false);
        EQUALS(received.AppliesTo(7), true);
        EQUALS(received.AppliesTo(0), false);
        EQUALS(received.AppliesTo(8), false);
        EQUALS(received.buff == sent.buff, true);

        // Ordinary script orders retain their next-available-body policy.
        sent.incarnation = 0;
        MafiaNet::BitStream unscoped;
        sent.Serialize(&unscoped, true);
        received.Serialize(&unscoped, false);
        EQUALS(received.AppliesTo(8), true);
    });
    IT("a truncated poison order cannot become an unscoped buff", {
        MafiaNet::BitStream stream;
        KCDC::Shared::RPC::BuffId buff {1, 2};
        buff.Serialize(&stream, true);
        KCDC::Shared::RPC::ApplyBuff received;
        received.Serialize(&stream, false);
        EQUALS(received.IsSane(), false);
        EQUALS(received.AppliesTo(7), false);
    });
    IT("shares four portions and refuses every use after depletion", {
        KCDC::Shared::CookPot::Stock stock;
        EQUALS(stock.Appearance() == KCDC::Shared::CookPot::State::Full, true);
        EQUALS(stock.Take(), true);
        EQUALS(stock.Take(), true);
        EQUALS(stock.Appearance() == KCDC::Shared::CookPot::State::Half, true);
        EQUALS(stock.Take(), true);
        EQUALS(stock.Take(), true);
        EQUALS(stock.Appearance() == KCDC::Shared::CookPot::State::Empty, true);
        EQUALS(stock.Take(), false);
        EQUALS(stock.portions, 0);
    });
    IT("blocks a full pot without losing or changing its stock", {
        KCDC::Shared::CookPot::Stock stock;
        stock.enabled = false;
        EQUALS(stock.Take(), false);
        EQUALS(stock.portions, 4);
        EQUALS(stock.Appearance() == KCDC::Shared::CookPot::State::Full, true);
        stock.enabled = true;
        EQUALS(stock.Take(), true);
    });
    IT("refills with bounded finite stock and an appropriate half-full point", {
        KCDC::Shared::CookPot::Stock stock;
        EQUALS(stock.Refill(1001), false);
        EQUALS(stock.portions, 4);
        EQUALS(stock.Refill(2), true);
        EQUALS(stock.Appearance() == KCDC::Shared::CookPot::State::Full, true);
        EQUALS(stock.Take(), true);
        EQUALS(stock.Appearance() == KCDC::Shared::CookPot::State::Half, true);
        EQUALS(stock.Refill(0), true);
        EQUALS(stock.Take(), false);
    });
    IT("refuses truncated requests rather than using partial identities", {
        MafiaNet::BitStream stream;
        stream.Write(std::uint64_t(1));
        KCDC::Shared::RPC::CookPotRequest request;
        request.Serialize(&stream, false);
        EQUALS(request.valid, false);
    });
    IT("round trips a meal request and its approval", {
        MafiaNet::BitStream stream;
        KCDC::Shared::RPC::CookPotRequest sent;
        sent.sequence        = 123;
        sent.guid            = 0x0a41d8e7921000e4ULL;
        sent.contextRevision = 91;
        sent.world           = 7;
        sent.incarnation     = 3;
        sent.Serialize(&stream, true);
        KCDC::Shared::RPC::CookPotRequest received;
        received.Serialize(&stream, false);
        EQUALS(received.valid, true);
        EQUALS(received.sequence, sent.sequence);
        EQUALS(received.guid, sent.guid);
        EQUALS(received.contextRevision, sent.contextRevision);
        EQUALS(received.world, sent.world);
        EQUALS(received.incarnation, sent.incarnation);
        EQUALS(received.completion, false);
        EQUALS(received.eaten, false);
        MafiaNet::BitStream approval;
        KCDC::Shared::RPC::CookPotReply reply;
        reply.sequence = sent.sequence;
        reply.guid     = sent.guid;
        reply.granted  = true;
        reply.food     = KCDC::Shared::Game::CookPotFood::Soup;
        reply.Serialize(&approval, true);
        KCDC::Shared::RPC::CookPotReply decoded;
        decoded.Serialize(&approval, false);
        EQUALS(decoded.valid, true);
        EQUALS(decoded.granted, true);
        EQUALS(decoded.sequence, sent.sequence);
        EQUALS(decoded.guid, sent.guid);
        EQUALS(decoded.food == KCDC::Shared::Game::CookPotFood::Soup, true);
    });
    IT("refill clears poison but preserves food and disabled state", {
        KCDC::Shared::CookPot::Stock stock;
        stock.food    = KCDC::Shared::Game::CookPotFood::Soup;
        stock.poison  = {1, 2};
        stock.enabled = false;
        EQUALS(stock.Refill(1001), false);
        EQUALS(stock.poison.IsValid(), true);
        EQUALS(stock.Refill(4), true);
        EQUALS(stock.poison.IsValid(), false);
        EQUALS(stock.food == KCDC::Shared::Game::CookPotFood::Soup, true);
        EQUALS(stock.enabled, false);
    });
    IT("rejects an invalid food enum in a meal approval", {
        MafiaNet::BitStream stream;
        KCDC::Shared::RPC::CookPotReply reply;
        reply.food = static_cast<KCDC::Shared::Game::CookPotFood>(255);
        reply.Serialize(&stream, true);
        KCDC::Shared::RPC::CookPotReply received;
        received.Serialize(&stream, false);
        EQUALS(received.valid, false);
    });
});

#pragma once

#include "shared/features/dice/dice_rpc.h"
#include "shared/features/dice/dice_rules.h"

#include <mafianet/bit_stream.h>

#include <cstdint>
#include <vector>

MODULE(dice_rules, {
    IT("scores the single faces and their sums", {
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({0}, false, false).score, 100u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({4}, false, false).score, 50u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({0, 4}, false, false).score, 150u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({0, 0, 4, 4}, false, false).score, 300u);
    });

    IT("refuses a selection with a die that does not count", {
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({0, 1}, false, false).scores, false);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({1}, false, false).scores, false);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({}, false, false).scores, false);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({}, false, true).scores, true);
    });

    IT("lets a throw keep dice that do not count", {
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({0, 1}, true, false).scores, true);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({0, 1}, true, false).score, 100u);
    });

    IT("doubles a run of one face for every die past three", {
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({0, 0, 0}, false, false).score, 1000u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({0, 0, 0, 0}, false, false).score, 2000u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({1, 1, 1}, false, false).score, 200u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({1, 1, 1, 1, 1, 1}, false, false).score, 1600u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({5, 5, 5, 5, 5}, false, false).score, 2400u);
    });

    IT("scores the three straights", {
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({0, 1, 2, 3, 4}, false, false).score, 500u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({1, 2, 3, 4, 5}, false, false).score, 750u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({5, 4, 3, 2, 1, 0}, false, false).score, 1500u);
    });

    IT("combines rules the way the engine's passes do", {
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({4, 4, 4, 0}, false, false).score, 600u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({2, 2, 2, 5, 5, 5}, false, false).score, 900u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({0, 0, 0, 4, 4, 4}, false, false).score, 1500u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({0, 1, 2, 3, 4, 4}, false, false).score, 550u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({0, 1, 2, 3, 4, 0}, false, false).score, 600u);
        EQUALS(KCDC::Shared::Game::Dice::Evaluate({1, 2, 3, 4, 5, 4}, false, false).score, 800u);
    });

    IT("knows a bust", {
        EQUALS(KCDC::Shared::Game::Dice::IsBust({1, 1, 2, 3, 5, 5}), true);
        EQUALS(KCDC::Shared::Game::Dice::IsBust({1, 2, 3}), true);
        EQUALS(KCDC::Shared::Game::Dice::IsBust({3}), true);
        EQUALS(KCDC::Shared::Game::Dice::IsBust({0}), false);
        EQUALS(KCDC::Shared::Game::Dice::IsBust({1, 1, 1, 2}), false);
    });
});

MODULE(dice_table, {
    IT("scores, throws the rest and wins on a pass", {
        KCDC::Shared::Game::Dice::Table table(1000, 0);
        EQUALS(table.ThrowNeeded(), true);
        EQUALS(table.DiceToThrow(), static_cast<std::size_t>(6));
        EQUALS(table.Throw({0, 0, 0, 1, 2, 3}), false);
        EQUALS(table.Move(KCDC::Shared::Game::Dice::MoveKind::Continue, 0x07).value_or(0), 1000u);
        EQUALS(table.TurnScore(), 1000u);
        EQUALS(table.DiceToThrow(), static_cast<std::size_t>(3));
        EQUALS(table.Throw({4, 1, 1}), false);
        EQUALS(table.Face(3), static_cast<std::uint8_t>(4));
        EQUALS(table.Move(KCDC::Shared::Game::Dice::MoveKind::Pass, 0x08).value_or(0), 50u);
        EQUALS(table.Over(), true);
        EQUALS(table.Winner(), static_cast<std::uint8_t>(0));
        EQUALS(table.Total(0), 1050u);
    });

    IT("hands the turn over on a bust and loses the turn's points", {
        KCDC::Shared::Game::Dice::Table table(2000, 1);
        EQUALS(table.Throw({0, 1, 1, 2, 3, 5}), false);
        EQUALS(table.Move(KCDC::Shared::Game::Dice::MoveKind::Continue, 0x01).has_value(), true);
        EQUALS(table.Throw({1, 1, 2, 2, 3}), true);
        EQUALS(table.Turn(), static_cast<std::uint8_t>(0));
        EQUALS(table.Total(1), 0u);
        EQUALS(table.TurnScore(), 0u);
        EQUALS(table.DiceToThrow(), static_cast<std::size_t>(6));
    });

    IT("banks an empty pass at what the turn already held", {
        KCDC::Shared::Game::Dice::Table table(5000, 0);
        EQUALS(table.Throw({0, 4, 1, 1, 2, 3}), false);
        EQUALS(table.Move(KCDC::Shared::Game::Dice::MoveKind::Continue, 0x01).has_value(), true);
        EQUALS(table.Throw({4, 1, 1, 2, 3}), false);
        EQUALS(table.Move(KCDC::Shared::Game::Dice::MoveKind::Pass, 0x00).value_or(1), 0u);
        EQUALS(table.Total(0), 100u);
        EQUALS(table.Turn(), static_cast<std::uint8_t>(1));
    });

    IT("throws all six again once all six are set aside", {
        KCDC::Shared::Game::Dice::Table table(10000, 0);
        EQUALS(table.Throw({0, 0, 0, 4, 4, 4}), false);
        EQUALS(table.Move(KCDC::Shared::Game::Dice::MoveKind::Continue, 0x3F).value_or(0), 1500u);
        EQUALS(table.DiceToThrow(), static_cast<std::size_t>(6));
    });

    IT("refuses moves the engine would not accept", {
        KCDC::Shared::Game::Dice::Table table(1000, 0);
        EQUALS(table.Move(KCDC::Shared::Game::Dice::MoveKind::Pass, 0).has_value(), false);
        EQUALS(table.Throw({0, 1, 1, 2, 3, 5}), false);
        EQUALS(table.Move(KCDC::Shared::Game::Dice::MoveKind::Continue, 0x03).has_value(), false);
        EQUALS(table.Move(KCDC::Shared::Game::Dice::MoveKind::Continue, 0x00).has_value(), false);
        EQUALS(table.Move(KCDC::Shared::Game::Dice::MoveKind::Pass, 0x02).has_value(), false);
        EQUALS(table.Move(KCDC::Shared::Game::Dice::MoveKind::Continue, 0x01).has_value(), true);
        EQUALS(table.Move(KCDC::Shared::Game::Dice::MoveKind::Pass, 0x00).has_value(), false);
    });
});

MODULE(dice_protocol, {
    IT("round trips a match start with its deals", {
        KCDC::Shared::RPC::DiceMatchStart start;
        start.matchId           = 7;
        start.localSeat         = 1;
        start.firstSeat         = 0;
        start.opponentNetworkId = 42;
        start.targetScore       = 2000;
        start.anchorX           = 1.5F;
        start.deals.push_back(KCDC::Shared::RPC::DiceDeal {0, {0, 1, 2, 3, 4, 5}});
        EQUALS(start.IsSane(), true);

        MafiaNet::BitStream stream;
        start.Serialize(&stream, true);
        KCDC::Shared::RPC::DiceMatchStart decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.localSeat, static_cast<std::uint8_t>(1));
        EQUALS(decoded.deals.size(), static_cast<std::size_t>(1));
        EQUALS(decoded.deals[0].faces.size(), static_cast<std::size_t>(6));
        EQUALS(decoded.deals[0].faces[5], static_cast<std::uint8_t>(5));
    });

    IT("refuses faces and masks the game does not have", {
        KCDC::Shared::RPC::DiceDeal deal {0, {6}};
        EQUALS(deal.IsSane(), false);
        KCDC::Shared::RPC::DiceDeal empty {0, {}};
        EQUALS(empty.IsSane(), false);
        KCDC::Shared::RPC::DiceMove move;
        move.matchId  = 1;
        move.heldMask = 0x40;
        EQUALS(move.IsSane(), false);
        move.heldMask = 0x3F;
        EQUALS(move.IsSane(), true);
    });

    IT("round trips a commit", {
        KCDC::Shared::RPC::DiceCommit commit;
        commit.matchId   = 3;
        commit.moveIndex = 4;
        commit.seat      = 1;
        commit.kind      = static_cast<std::uint8_t>(KCDC::Shared::Game::Dice::MoveKind::Continue);
        commit.heldMask  = 0x05;
        commit.total1    = 650;
        commit.deals.push_back(KCDC::Shared::RPC::DiceDeal {1, {2, 2, 3, 0}});

        MafiaNet::BitStream stream;
        commit.Serialize(&stream, true);
        KCDC::Shared::RPC::DiceCommit decoded;
        decoded.Serialize(&stream, false);
        EQUALS(decoded.IsSane(), true);
        EQUALS(decoded.heldMask, static_cast<std::uint8_t>(0x05));
        EQUALS(decoded.total1, 650u);
        EQUALS(decoded.deals[0].faces.size(), static_cast<std::size_t>(4));
    });
});

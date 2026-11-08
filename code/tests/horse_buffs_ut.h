#pragma once

#include "shared/features/horse/horse_buffs.h"
#include "shared/features/horse/horse_entity.h"
#include "shared/game/buff_catalog.h"

#include <string>
#include <vector>

MODULE(horse_buffs, {
    IT("round-trips a list of definitions through the wire text, dropping repeats", {
        const auto *shoe  = KCDC::Shared::Game::FindBuffByName("item_horse_shoe");
        const auto *boost = KCDC::Shared::Game::FindBuffByName("konskeZavody_horseBoost_diff3");
        EQUALS(shoe != nullptr, true);
        EQUALS(boost != nullptr, true);

        const std::vector<KCDC::Shared::RPC::BuffId> buffs = {KCDC::Shared::Game::BuffIdOf(*shoe), KCDC::Shared::Game::BuffIdOf(*boost)};
        const std::string packed                           = KCDC::Shared::Game::PackHorseBuffs(buffs);
        EQUALS(packed, std::string(shoe->guid) + "," + std::string(boost->guid));

        std::vector<KCDC::Shared::RPC::BuffId> read;
        EQUALS(KCDC::Shared::Game::UnpackHorseBuffs(packed, read), true);
        EQUALS(read == buffs, true);

        EQUALS(KCDC::Shared::Game::UnpackHorseBuffs(packed + "," + std::string(shoe->guid), read), true);
        EQUALS(read.size(), static_cast<std::size_t>(2));

        EQUALS(KCDC::Shared::Game::UnpackHorseBuffs("", read), true);
        EQUALS(read.empty(), true);
    });

    IT("refuses what a server never writes", {
        std::vector<KCDC::Shared::RPC::BuffId> read;
        EQUALS(KCDC::Shared::Game::UnpackHorseBuffs("not-a-guid", read), false);
        EQUALS(KCDC::Shared::Game::UnpackHorseBuffs("00000000-0000-0000-0000-000000000000", read), false);
        EQUALS(KCDC::Shared::Game::UnpackHorseBuffs(std::string(KCDC::Shared::Game::kMaxPackedHorseBuffs + 1, 'a'), read), false);
        EQUALS(KCDC::Shared::Entities::HorseEntity::kMaxBuffs * (KCDC::Shared::Game::kCryGuidTextLength + 1), KCDC::Shared::Game::kMaxPackedHorseBuffs);
    });
});

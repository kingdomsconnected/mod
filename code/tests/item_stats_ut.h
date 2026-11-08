#pragma once

#include "shared/features/inventory/item_stats_message.h"
#include "shared/game/item_classes.h"
#include "shared/game/item_stats.h"

#include <mafianet/bit_stream.h>

#include <string_view>

MODULE(item_stats, {
    IT("names every stat once and resolves the names back", {
        for (std::size_t i = 0; i < KCDC::Shared::Game::kItemStatCount; ++i) {
            const auto stat = static_cast<KCDC::Shared::Game::ItemStat>(i);
            EQUALS(KCDC::Shared::Game::FindItemStat(KCDC::Shared::Game::ItemStatName(stat)), static_cast<int>(i));
        }
        EQUALS(KCDC::Shared::Game::FindItemStat("swingSpeed"), -1);
    });

    IT("gives each table element the stats its reflection block carries", {
        using KCDC::Shared::Game::ItemStat;
        using KCDC::Shared::Game::ItemStatAppliesTo;
        EQUALS(ItemStatAppliesTo("MeleeWeapon", ItemStat::Attack), true);
        EQUALS(ItemStatAppliesTo("MeleeWeapon", ItemStat::DefenseStab), false);
        EQUALS(ItemStatAppliesTo("MissileWeapon", ItemStat::Power), true);
        EQUALS(ItemStatAppliesTo("MissileWeapon", ItemStat::Attack), false);
        EQUALS(ItemStatAppliesTo("Armor", ItemStat::DefenseSlash), true);
        EQUALS(ItemStatAppliesTo("Helmet", ItemStat::Noise), true);
        EQUALS(ItemStatAppliesTo("Hood", ItemStat::StrReq), true);
        EQUALS(ItemStatAppliesTo("Armor", ItemStat::AgiReq), false);
        EQUALS(ItemStatAppliesTo("Ammo", ItemStat::PowerMod), true);
        EQUALS(ItemStatAppliesTo("Ammo", ItemStat::Defense), false);
        EQUALS(KCDC::Shared::Game::ItemCategoryHasStats("CraftingMaterial"), false);
        EQUALS(KCDC::Shared::Game::ItemCategoryHasStats("Hood"), true);
    });

    IT("round-trips a table through the wire and refuses what a server never sends", {
        KCDC::Shared::RPC::ItemStats table;
        table.replace = true;
        const auto *sword = KCDC::Shared::Game::FindItemClass("7ff1bc2e-8664-4691-a8c7-0e6d9cf0e96c");
        EQUALS(sword != nullptr, true);
        EQUALS(sword->category == "MeleeWeapon", true);
        table.entries.push_back({KCDC::Shared::Game::ItemClassIdFromGuid(sword->guid), {{static_cast<std::uint8_t>(KCDC::Shared::Game::ItemStat::Attack), 500.0F}, {static_cast<std::uint8_t>(KCDC::Shared::Game::ItemStat::StrReq), 3.0F}}});
        table.entries.push_back({KCDC::Shared::Game::ItemClassIdFromGuid(sword->guid), {}});

        MafiaNet::BitStream stream;
        table.Serialize(&stream, true);
        KCDC::Shared::RPC::ItemStats read;
        read.Serialize(&stream, false);
        EQUALS(read.IsSane(), true);
        EQUALS(read.replace, true);
        EQUALS(read.entries.size(), static_cast<std::size_t>(2));
        EQUALS(read.entries[0].values.size(), static_cast<std::size_t>(2));
        EQUALS(read.entries[0].values[0].value, 500.0F);
        EQUALS(read.entries[1].values.empty(), true);

        KCDC::Shared::RPC::ItemStats bad;
        bad.entries.push_back({KCDC::Shared::Game::ItemClassIdFromGuid(sword->guid), {{static_cast<std::uint8_t>(KCDC::Shared::Game::kItemStatCount), 1.0F}}});
        MafiaNet::BitStream badStream;
        bad.Serialize(&badStream, true);
        KCDC::Shared::RPC::ItemStats badRead;
        badRead.Serialize(&badStream, false);
        EQUALS(badRead.IsSane(), false);
    });

    IT("refuses a negative requirement, which a weapon would read as four billion", {
        using KCDC::Shared::Game::ItemStat;
        using KCDC::Shared::Game::ItemStatAccepts;
        EQUALS(ItemStatAccepts(ItemStat::StrReq, 0.0F), true);
        EQUALS(ItemStatAccepts(ItemStat::StrReq, -1.0F), false);
        EQUALS(ItemStatAccepts(ItemStat::AgiReq, -0.5F), false);
        EQUALS(ItemStatAccepts(ItemStat::Attack, -5.0F), true);
        EQUALS(ItemStatAccepts(ItemStat::Attack, 100001.0F), false);

        const auto *sword = KCDC::Shared::Game::FindItemClass("7ff1bc2e-8664-4691-a8c7-0e6d9cf0e96c");
        KCDC::Shared::RPC::ItemStats table;
        table.entries.push_back({KCDC::Shared::Game::ItemClassIdFromGuid(sword->guid), {{static_cast<std::uint8_t>(ItemStat::AgiReq), -1.0F}}});
        MafiaNet::BitStream stream;
        table.Serialize(&stream, true);
        KCDC::Shared::RPC::ItemStats read;
        read.Serialize(&stream, false);
        EQUALS(read.IsSane(), false);
    });
});

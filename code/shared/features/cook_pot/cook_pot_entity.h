#pragma once

#include "cook_pot_state.h"
#include <networking/replication/network_entity.h>

namespace KCDC::Shared::Entities {
    class CookPotEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::CookPot";
        std::uint64_t guid                     = 0;
        std::uint32_t stockWorld               = 0;
        KCDC::Shared::CookPot::Stock stock;
        /** Server-only: bumped when a script replaces the stock, so an older reservation is not refunded into it. */
        std::uint64_t revision = 0;

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(guid);
            fields.Field(stockWorld);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(stock.portions);
            fields.Field(stock.capacity);
            fields.Field(stock.enabled);
            fields.Field(stock.food);
            fields.Field(stock.fire);
        }
    };
} // namespace KCDC::Shared::Entities

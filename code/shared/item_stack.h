#pragma once

#include <mafianet/bit_stream.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Shared::RPC {
    /** Cap on one give or drop request. Shared: ground items are dropped in the same amounts a body carries. */
    inline constexpr std::uint32_t kMaxGiveAmount = 10000;

    inline constexpr std::uint32_t kMaxInventoryStackAmount = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
    inline constexpr std::uint32_t kNoEquipmentSlot         = std::numeric_limits<std::uint32_t>::max();
    /** Protocol safety bound, not a native limit on authored buff sets. */
    inline constexpr std::uint8_t kMaximumOnEquipBuffs = 32;

    /** Length of an item class GUID as the game's own tables spell it. */
    inline constexpr std::size_t kItemClassGuidLength = 36;

    /** Whether `text` is a dashed GUID, which is all the receiving peer's parser will take. */
    inline bool IsItemClassGuidText(std::string_view text) {
        constexpr std::array<std::size_t, 4> hyphens {8, 13, 18, 23};
        if (text.size() != kItemClassGuidLength) {
            return false;
        }
        for (std::size_t i = 0; i < text.size(); ++i) {
            const bool hyphen = std::find(hyphens.begin(), hyphens.end(), i) != hyphens.end();
            const char c      = text[i];
            if (hyphen ? c != '-' : !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                return false;
            }
        }
        return true;
    }

    /** The engine's 16-byte S_ItemClassGUID without a client-SDK dependency. */
    struct ItemClassId {
        std::uint64_t low  = 0;
        std::uint64_t high = 0;

        bool operator==(const ItemClassId &) const = default;

        bool operator<(const ItemClassId &other) const {
            return low < other.low || (low == other.low && high < other.high);
        }

        bool IsValid() const {
            return low != 0 || high != 0;
        }

        /** The 32 hex digits used by equipment strings and script item classes. */
        void AppendPackedText(std::string &out) const {
            char text[33];
            std::snprintf(text, sizeof(text), "%016llx%016llx", static_cast<unsigned long long>(low), static_cast<unsigned long long>(high));
            out.append(text, 32);
        }

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            return bs->Serialize(write, low) && bs->Serialize(write, high);
        }
    };

    /** One owner-assigned inventory identity and its post-mutation state. */
    struct InventoryItem {
        std::uint64_t inventoryItemId = 0;
        ItemClassId itemClass;
        std::uint32_t amount         = 0;
        std::uint32_t quality        = 0;
        float health                 = -1.0F;
        float condition              = -1.0F;
        std::int32_t phase           = -1;
        std::uint64_t ownerId        = 0;
        std::uint64_t stolenFromId   = 0;
        std::uint32_t ownershipFlags = 0;
        bool equipped                = false;
        std::uint32_t equipmentSlot  = kNoEquipmentSlot;
        ItemClassId poisonBuff;
        std::uint32_t poisonCharges = 0;
        // Complete persistent on-equip definition set; independent of poison.
        // Stored in ascending binary identity order for canonical comparisons.
        std::vector<ItemClassId> onEquipBuffs;

        // data is authoritative server state and is never serialized.
        nlohmann::json custom = nlohmann::json::object();
        std::string customStackKey;

        bool operator==(const InventoryItem &) const = default;

        std::string PublicCustom() const {
            auto display = custom;
            display.erase("data");
            if (!customStackKey.empty()) {
                display["stackKey"] = customStackKey;
            }
            return display.empty() ? std::string() : display.dump();
        }

        bool ReadPublicCustom(const std::string &text) {
            custom = text.empty() ? nlohmann::json::object() : nlohmann::json::parse(text, nullptr, false);
            if (!custom.is_object() || custom.contains("data")) {
                return false;
            }
            customStackKey.clear();
            if (custom.contains("stackKey")) {
                if (!custom.at("stackKey").is_string() || custom.at("stackKey").get_ref<const std::string &>().size() != 64) {
                    return false;
                }
                customStackKey = custom.at("stackKey").get<std::string>();
                custom.erase("stackKey");
            }
            for (const auto &[key, value] : custom.items()) {
                if ((key != "name" && key != "description") || !value.is_string() || value.get_ref<const std::string &>().size() > (key == "name" ? 256 : 4096)) {
                    return false;
                }
            }
            return true;
        }

        bool IsSane() const {
            const bool validHealth    = std::isfinite(health) && health >= -1.0F && health <= 1.0F;
            const bool validCondition = std::isfinite(condition) && condition >= -1.0F && condition <= 1.0F;
            const bool validBuffs     = onEquipBuffs.size() <= kMaximumOnEquipBuffs && std::all_of(onEquipBuffs.begin(), onEquipBuffs.end(), [](const ItemClassId &buff) { return buff.IsValid(); });
            const bool canonicalBuffs = std::is_sorted(onEquipBuffs.begin(), onEquipBuffs.end()) && std::adjacent_find(onEquipBuffs.begin(), onEquipBuffs.end()) == onEquipBuffs.end();
            return inventoryItemId != 0 && itemClass.IsValid() && amount != 0 && amount <= kMaxInventoryStackAmount && validHealth && validCondition && phase >= -1 && poisonCharges <= 10000 && (poisonBuff.IsValid() || poisonCharges == 0) && validBuffs && canonicalBuffs;
        }

        bool Serialize(MafiaNet::BitStream *bs, bool write) {
            if (write && onEquipBuffs.size() > kMaximumOnEquipBuffs) {
                return false;
            }
            if (!(bs->Serialize(write, inventoryItemId) && itemClass.Serialize(bs, write) && bs->Serialize(write, amount) && bs->Serialize(write, quality) && bs->Serialize(write, health) && bs->Serialize(write, condition) && bs->Serialize(write, phase)
                    && bs->Serialize(write, ownerId) && bs->Serialize(write, stolenFromId) && bs->Serialize(write, ownershipFlags) && bs->Serialize(write, equipped) && bs->Serialize(write, equipmentSlot) && poisonBuff.Serialize(bs, write) && bs->Serialize(write, poisonCharges))) {
                return false;
            }
            std::uint8_t count = static_cast<std::uint8_t>(onEquipBuffs.size());
            if (!bs->Serialize(write, count) || count > kMaximumOnEquipBuffs) {
                return false;
            }
            if (!write) {
                onEquipBuffs.resize(count);
            }
            for (auto &buff : onEquipBuffs) {
                if (!buff.Serialize(bs, write)) {
                    return false;
                }
            }
            std::string display = write ? PublicCustom() : std::string();
            std::uint32_t size  = static_cast<std::uint32_t>(display.size());
            if (!bs->Serialize(write, size) || size > 32768 || (!write && bs->GetNumberOfUnreadBits() / 8 < size)) {
                return false;
            }
            if (!write) {
                display.resize(size);
            }
            // MafiaNet refuses zero-byte reads; an absent display has no body.
            if (size != 0 && !bs->Serialize(write, display.data(), size)) {
                return false;
            }
            return write || ReadPublicCustom(display);
        }
    };
} // namespace KCDC::Shared::RPC

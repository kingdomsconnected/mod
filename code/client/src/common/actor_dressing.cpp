#include "actor_dressing.h"

#include "core/obfuscation.h"
#include "sdk/cryengine/system/i_system.h"
#include "sdk/cryengine/system/xml_node_ref.h"
#include "sdk/types.h"
#include "sdk/wh/entitymodule/c_action_actor.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_armor_runtime_data.h"
#include "sdk/wh/entitymodule/c_clothing_preset_database.h"
#include "sdk/wh/entitymodule/c_equipment_manager.h"
#include "sdk/wh/entitymodule/c_inventory.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_item_database.h"
#include "sdk/wh/entitymodule/e_actor_gender.h"
#include "sdk/wh/entitymodule/e_weapon_equip_slot.h"
#include "sdk/wh/entitymodule/item_draw.h"
#include "sdk/wh/entitymodule/s_item_class.h"
#include "sdk/wh/rpgmodule/c_inventory_soul.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/i_inventory_soul.h"
#include "sdk/wh/rpgmodule/i_soul_system.h"
#include "shared/game/cry_guid_text.h"

#include <logging/logger.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Common {
    namespace {
        constexpr const char *kLogger = "Dressing";

        /**
         * The upper half of the first field of every preset key this module
         * derives. The soul's stack can only be asked for the id of the preset
         * in force (`GetClothingPreset`), so "is the outfit on top ours" is a
         * question about the id itself.
         */
        constexpr std::uint64_t kPresetIdMarker     = 0x7E5D0A11ULL << 32;
        constexpr std::uint64_t kPresetIdMarkerMask = 0xFFFFFFFFULL << 32;

        bool SameItemClass(const SDK::wh::entitymodule::S_ItemClassGUID &lhs, const SDK::wh::entitymodule::S_ItemClassGUID &rhs) {
            return lhs.m_low == rhs.m_low && lhs.m_high == rhs.m_high;
        }

        bool ItemClassLess(const SDK::wh::entitymodule::S_ItemClassGUID &lhs, const SDK::wh::entitymodule::S_ItemClassGUID &rhs) {
            return lhs.m_low != rhs.m_low ? lhs.m_low < rhs.m_low : lhs.m_high < rhs.m_high;
        }

        bool SameCryGuid(const SDK::CryGUID &lhs, const SDK::CryGUID &rhs) {
            return lhs.hipart == rhs.hipart && lhs.lopart == rhs.lopart;
        }

        std::string GuidText(std::uint64_t low, std::uint64_t high) {
            const std::array<char, Shared::Game::kCryGuidTextLength> text = Shared::Game::CryGuidToText(low, high);
            return std::string(text.data(), text.size());
        }

        /**
         * The key of the preset that holds exactly `armor` for a body of
         * `gender`: the same list gives the same key on every client, so each
         * one appends the same row and every copy of a body names the same
         * preset. Two FNV-1a passes with different bases fill the two halves,
         * and the marker is stamped over the top of the first.
         */
        SDK::CryGUID DerivePresetId(const std::vector<SDK::wh::entitymodule::S_ItemClassGUID> &armor, SDK::wh::entitymodule::E_ActorGender gender) {
            std::uint64_t first  = 0xCBF29CE484222325ULL;
            std::uint64_t second = 0x84222325CBF29CE4ULL;
            const auto mix       = [&](std::uint64_t value) {
                for (int shift = 0; shift < 64; shift += 8) {
                    const std::uint64_t byte = (value >> shift) & 0xFF;
                    first                    = (first ^ byte) * 0x100000001B3ULL;
                    second                   = (second ^ (byte + 0x9E)) * 0x100000001B3ULL;
                }
            };
            mix(static_cast<std::uint64_t>(gender));
            for (const auto &itemClass : armor) {
                mix(itemClass.m_low);
                mix(itemClass.m_high);
            }
            return SDK::CryGUID {(first & ~kPresetIdMarkerMask) | kPresetIdMarker, second};
        }

        const char *GenderColumn(SDK::wh::entitymodule::E_ActorGender gender) {
            return gender == SDK::wh::entitymodule::E_ActorGender::Female ? "Female" : "Male";
        }

        /**
         * One `clothing_preset` row in the table's own format, wrapped the way
         * `AppendFromXml` wants it: a `<database>` holding exactly one table.
         */
        std::string ClothingPresetXml(const std::string &presetText, SDK::wh::entitymodule::E_ActorGender gender, const std::vector<SDK::wh::entitymodule::S_ItemClassGUID> &armor) {
            std::string xml;
            xml.reserve(256 + armor.size() * 56);
            xml += "<database name=\"barbora\"><clothing_presets version=\"2\"><clothing_preset clothing_preset_id=\"";
            xml += presetText;
            xml += "\" clothing_preset_name=\"";
            xml += presetText;
            xml += "\" gender=\"";
            xml += GenderColumn(gender);
            xml += "\" prefers_hood_on=\"false\">";
            if (!armor.empty()) {
                xml += "<Items>";
                for (const auto &itemClass : armor) {
                    xml += "<Guid>";
                    xml += GuidText(itemClass.m_low, itemClass.m_high);
                    xml += "</Guid>";
                }
                xml += "</Items>";
            }
            xml += "</clothing_preset></clothing_presets></database>";
            return xml;
        }

        /**
         * Makes sure the clothing-preset database holds `presetId`, appending
         * the row through the table loader when it does not. False when the
         * database will not take it, or took something other than what was
         * written -- checked by reading the row back.
         */
        bool EnsureClothingPreset(const SDK::CryGUID &presetId, SDK::wh::entitymodule::E_ActorGender gender, const std::vector<SDK::wh::entitymodule::S_ItemClassGUID> &armor) {
            auto *database = SDK::wh::entitymodule::C_ClothingPresetDatabase::GetInstance();
            if (database == nullptr) {
                return false;
            }
            if (database->FindByGuid(presetId) != nullptr) {
                return true;
            }

            auto *system = SDK::ISystem::GetInstance();
            if (system == nullptr) {
                return false;
            }

            const std::string presetText = GuidText(presetId.hipart, presetId.lopart);
            const std::string xml        = ClothingPresetXml(presetText, gender, armor);
            SDK::XmlNodeRef document     = system->LoadXmlFromBuffer(xml.data(), xml.size(), false, false);
            if (!document.IsValid()) {
                Framework::Logging::GetLogger(kLogger)->error("Outfit {} did not parse as a clothing-preset table", presetText);
                return false;
            }

            // Replacing a duplicate key cannot hurt: the key is derived from the
            // content, so a row already under it says the same thing.
            if (!database->AppendFromXml(document, nullptr, true)) {
                Framework::Logging::GetLogger(kLogger)->error("The clothing-preset database refused outfit {}", presetText);
                return false;
            }
            database->RebuildIndex();

            const SDK::wh::entitymodule::S_ClothingPreset *row = database->FindByGuid(presetId);
            if (row == nullptr || row->m_items.Size() != armor.size()) {
                Framework::Logging::GetLogger(kLogger)->error("Outfit {} was appended as {} of {} pieces; not dressing anybody in it", presetText, row != nullptr ? row->m_items.Size() : 0, armor.size());
                return false;
            }

            Framework::Logging::GetLogger(kLogger)->info("Registered outfit {} with {} piece(s)", presetText, armor.size());
            return true;
        }

        /** Whether `presetId` is one this module derived. */
        bool IsOurPreset(const SDK::CryGUID *presetId) {
            return presetId != nullptr && (presetId->hipart & kPresetIdMarkerMask) == kPresetIdMarker;
        }

        /**
         * A stack entry for `presetId`. The tag is built by the engine because
         * the native it is handed to destroys it.
         */
        SDK::wh::rpgmodule::I_InventorySoul::S_PresetOverride MakeOverride(const SDK::CryGUID &presetId, const char *tag) {
            SDK::wh::rpgmodule::I_InventorySoul::S_PresetOverride preset {};
            preset.presetId      = presetId;
            preset.tag           = SDK::ConstructCryString(tag);
            preset.unknown18     = 1;
            preset.priorityItems = 1;
            return preset;
        }

        bool IsArmorClass(const SDK::wh::entitymodule::C_ItemDatabase *items, const SDK::wh::entitymodule::S_ItemClassGUID &itemClass) {
            const SDK::wh::entitymodule::S_ItemClass *known = items->FindItemClass(itemClass);
            return known != nullptr && known->IsOfClass(SDK::wh::entitymodule::E_ItemClassId::Armor);
        }

        /**
         * The soul's clothes: our outfit on top of its stack, or ours taken
         * off and the soul reset to what it owns. Returns whether anything
         * changed.
         */
        bool ApplyClothingOutfit(SDK::wh::rpgmodule::C_InventorySoul *runtime, SDK::wh::entitymodule::E_ActorGender gender, std::vector<SDK::wh::entitymodule::S_ItemClassGUID> armor, bool restoreOwnOutfit, bool &dressed) {
            const std::string tag(std::string_view(KCDC_HIDE("REPLICATED_OUTFIT")));
            const SDK::CryGUID *top = runtime->GetClothingPreset();
            const bool oursOnTop    = IsOurPreset(top);

            if (restoreOwnOutfit) {
                dressed = true;
                if (!oursOnTop) {
                    return false;
                }
                // Ours comes off, and the soul's inventory goes back to what its
                // own presets say it owns -- the reset the soul itself runs. A
                // pop alone would not do: with nothing pushed, the outfit
                // application only expands the override stack, so clothes that
                // came from the base or the inventory presets never come back.
                runtime->RemovePreset(MakeOverride(*top, tag.c_str()), SDK::wh::rpgmodule::I_InventorySoul::E_PresetItemMode::Delete, 0, false, true);
                runtime->ResetInventory(true, true);
                return true;
            }

            std::sort(armor.begin(), armor.end(), ItemClassLess);
            const SDK::CryGUID presetId = DerivePresetId(armor, gender);
            if (!EnsureClothingPreset(presetId, gender, armor)) {
                dressed = false;
                return false;
            }
            dressed = true;
            if (oursOnTop && SameCryGuid(*top, presetId)) {
                return false;
            }

            // One entry of ours at most: the previous one comes off first,
            // without making the soul's own clothes it would otherwise fall
            // back to, and its items go with it.
            if (oursOnTop) {
                runtime->RemovePreset(MakeOverride(*top, tag.c_str()), SDK::wh::rpgmodule::I_InventorySoul::E_PresetItemMode::Delete, 0, false, true);
            }
            runtime->PushClothingPreset(MakeOverride(presetId, tag.c_str()), SDK::wh::rpgmodule::I_InventorySoul::E_PresetItemMode::Delete, true);
            return true;
        }

        /**
         * Everything in the list that is not armor, equipped directly as a
         * difference against what is worn. Returns whether anything moved.
         */
        bool ApplyOtherEquipment(SDK::wh::entitymodule::C_Actor *actor, SDK::wh::rpgmodule::C_InventorySoul *runtime, const SDK::wh::entitymodule::C_ItemDatabase *items, const std::vector<SDK::wh::entitymodule::S_ItemClassGUID> &wanted, const DressOptions &options,
            bool &pendingWeapons) {
            auto *inventory        = runtime->GetInventory();
            auto *equipmentManager = runtime->GetEquipmentManager();
            auto *torchItem = equipmentManager->GetWeaponSlotItem(SDK::wh::entitymodule::E_WeaponEquipSlot::Torch);
            std::vector<SDK::wh::entitymodule::C_Item *> worn;
            const std::size_t count = options.reconcileWeaponSlots ? equipmentManager->GetCarriedCount() : equipmentManager->GetEquippedCount();
            for (std::size_t i = 0; i < count; ++i) {
                auto *item = equipmentManager->GetCarriedAt(i);
                if (item != nullptr && item != torchItem) {
                    worn.push_back(item);
                }
            }

            // Clear unwanted slots before equipping replacements. A drawn
            // weapon belongs to the hand action until its holster completes;
            // a dropping weapon belongs to the ground transfer until adopted.
            bool moved = false;
            for (auto *item : worn) {
                const auto itemClass = item->InvokeGetItemClassRaw();
                if (IsArmorClass(items, itemClass) || std::any_of(wanted.begin(), wanted.end(), [&](const auto &candidate) {
                        return SameItemClass(candidate, itemClass);
                    })) {
                    continue;
                }
                if (equipmentManager->IsInWeaponSlot(item)) {
                    if (actor->GetActorModel() == nullptr || SDK::wh::entitymodule::IsItemDrawn(actor, SDK::wh::framework::WUID {item->GetId()}) || (options.deferWeaponChange && options.deferWeaponChange(itemClass))) {
                        pendingWeapons = true;
                        continue;
                    }
                    equipmentManager->UnequipWeapon(item);
                }
                else {
                    equipmentManager->InvokeUnequipItemRaw(item);
                }
                moved = true;
            }

            for (const auto &itemClass : wanted) {
                // Re-read the slots: clearing equipment can notify listeners
                // that update other slots too. Never reuse a removed item.
                bool equipped                  = false;
                const std::size_t currentCount = options.reconcileWeaponSlots ? equipmentManager->GetCarriedCount() : equipmentManager->GetEquippedCount();
                for (std::size_t i = 0; i < currentCount; ++i) {
                    auto *item = equipmentManager->GetCarriedAt(i);
                    equipped   = equipped || (item != nullptr && SameItemClass(item->InvokeGetItemClassRaw(), itemClass));
                }
                if (equipped) {
                    continue;
                }
                const auto *known = items->FindItemClass(itemClass);
                const bool weapon = known != nullptr && known->IsOfClass(SDK::wh::entitymodule::E_ItemClassId::WeaponEquip);
                if (options.reconcileWeaponSlots && weapon && (pendingWeapons || (options.deferWeaponChange && options.deferWeaponChange(itemClass)))) {
                    pendingWeapons = true;
                    continue;
                }

                // Holstered hand shadows remain inventory-owned. Reuse one
                // before creating another copy of the same equipped weapon.
                auto *item = inventory->InvokeFindItemByClassRaw(itemClass);
                if (item == nullptr) {
                    inventory->CreateItems(itemClass, 1, false);
                    item = inventory->InvokeFindItemByClassRaw(itemClass);
                }
                if (item != nullptr) {
                    moved = equipmentManager->InvokeEquipItemRaw(item) || moved;
                }
            }
            return moved;
        }

        /** `actor`'s equipment manager, or null before its inventory is up. */
        SDK::wh::entitymodule::C_EquipmentManager *EquipmentManagerOf(SDK::wh::entitymodule::C_Actor *actor) {
            auto *runtime = actor != nullptr ? actor->GetInventorySoulRuntime() : nullptr;
            return runtime != nullptr && runtime->IsInitialisedFor(actor->GetSoul()) ? runtime->GetEquipmentManager() : nullptr;
        }

        /**
         * The piece of `itemClass` among what `manager` has on, or null. The
         * worn list rather than the inventory: the owner can carry a second
         * piece of the same class in its bag.
         */
        SDK::wh::entitymodule::C_Item *FindWornItem(const SDK::wh::entitymodule::C_EquipmentManager *manager, const SDK::wh::entitymodule::S_ItemClassGUID &itemClass) {
            for (std::size_t i = 0; i < manager->GetEquippedCount(); ++i) {
                auto *item = manager->GetEquippedAt(i);
                if (item != nullptr && SameItemClass(item->InvokeGetItemClassRaw(), itemClass)) {
                    return item;
                }
            }
            return nullptr;
        }
    } // namespace

    void AppendPackedItemClass(std::string &out, const SDK::wh::entitymodule::S_ItemClassGUID &itemClass) {
        char text[kPackedItemClassWidth + 1];
        std::snprintf(text, sizeof(text), "%016llx%016llx", static_cast<unsigned long long>(itemClass.m_low), static_cast<unsigned long long>(itemClass.m_high));
        out.append(text, kPackedItemClassWidth);
    }

    bool ReadPackedItemClass(const std::string &packed, std::size_t offset, SDK::wh::entitymodule::S_ItemClassGUID &out) {
        if (offset + kPackedItemClassWidth > packed.size()) {
            return false;
        }
        const std::string low  = packed.substr(offset, 16);
        const std::string high = packed.substr(offset + 16, 16);
        if (low.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos || high.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) {
            return false;
        }
        out.m_low  = std::strtoull(low.c_str(), nullptr, 16);
        out.m_high = std::strtoull(high.c_str(), nullptr, 16);
        return true;
    }

    DressResult ApplyActorEquipment(SDK::wh::entitymodule::C_Actor *actor, const std::string &packedEquipment, const DressOptions &options) {
        DressResult result;
        if (actor == nullptr) {
            return result;
        }

        // Install the native weapon/equipment listener before equipment events.
        if (auto *actions = options.weaponListener ? actor->GetActionActor() : nullptr) {
            actions->GetShootingExpansion();
        }

        auto *actorSoul = actor->GetSoul();
        auto *items     = SDK::wh::entitymodule::C_ItemDatabase::GetInstance();
        if (actorSoul == nullptr || items == nullptr) {
            return result;
        }

        // Inventory and outfit state belong to the actor's own soul, even when
        // appearance came from a catalog soul.
        SDK::wh::rpgmodule::EnsureSoulInventory(actorSoul);
        auto *runtime = actor->GetInventorySoulRuntime();
        if (runtime == nullptr || !runtime->IsInitialisedFor(actorSoul) || runtime->GetInventory() == nullptr || runtime->GetEquipmentManager() == nullptr) {
            return result;
        }
        result.ready = true;

        std::vector<SDK::wh::entitymodule::S_ItemClassGUID> armor;
        std::vector<SDK::wh::entitymodule::S_ItemClassGUID> other;
        std::size_t unknownClasses = 0;
        for (std::size_t offset = 0; offset + kPackedItemClassWidth <= packedEquipment.size(); offset += kPackedItemClassWidth) {
            SDK::wh::entitymodule::S_ItemClassGUID itemClass {};
            if (!ReadPackedItemClass(packedEquipment, offset, itemClass)) {
                continue;
            }
            const SDK::wh::entitymodule::S_ItemClass *known = items->FindItemClass(itemClass);
            if (known == nullptr) {
                ++unknownClasses;
            }
            else if (known->IsOfClass(SDK::wh::entitymodule::E_ItemClassId::Armor)) {
                armor.push_back(itemClass);
            }
            else {
                other.push_back(itemClass);
            }
        }
        result.armorPieces = armor.size();
        if (unknownClasses != 0) {
            Framework::Logging::GetLogger(kLogger)->warn("{} item class(es) on this body are not in this build's item tables and were left out", unknownClasses);
        }

        const bool restoreOwnOutfit = options.emptyRestoresOwnOutfit && armor.empty() && other.empty();
        const bool clothesChanged   = ApplyClothingOutfit(runtime, actorSoul->GetGender(), std::move(armor), restoreOwnOutfit, result.dressed);
        const bool othersMoved      = restoreOwnOutfit ? false : ApplyOtherEquipment(actor, runtime, items, other, options, result.pendingWeapons);

        result.changed = clothesChanged || othersMoved;
        if (result.changed) {
            // The clothing object only redraws when asked.
            actor->RebuildClothing();
        }
        return result;
    }

    std::string CaptureEquipmentWear(SDK::wh::entitymodule::C_Actor *actor, const std::string &packedEquipment) {
        auto *manager = EquipmentManagerOf(actor);
        if (manager == nullptr) {
            return {};
        }

        std::string packed;
        packed.reserve(packedEquipment.size() / kPackedItemClassWidth * kPackedWearWidth);
        for (std::size_t offset = 0; offset + kPackedItemClassWidth <= packedEquipment.size(); offset += kPackedItemClassWidth) {
            SDK::wh::entitymodule::S_ItemClassGUID itemClass {};
            auto *item  = ReadPackedItemClass(packedEquipment, offset, itemClass) ? FindWornItem(manager, itemClass) : nullptr;
            auto *armor = item != nullptr ? item->GetOrCreateArmorRuntimeData() : nullptr;
            if (armor == nullptr) {
                packed.append(kPackedWearWidth, '0');
                continue;
            }
            const auto &blood = armor->GetBlood();
            char text[kPackedWearWidth + 1];
            std::snprintf(text, sizeof(text), "%02x%02x%02x%02x%02x%02x%02x%02x%08x", armor->GetDirt(), blood[0], blood[1], blood[2], blood[3], blood[4], blood[5], std::min(item->InvokeGetQualityRaw(), 0xFFU), std::bit_cast<std::uint32_t>(item->GetHealth()));
            packed.append(text, kPackedWearWidth);
        }
        return packed;
    }

    void ApplyEquipmentWear(SDK::wh::entitymodule::C_Actor *actor, const std::string &packedEquipment, const std::string &packedWear) {
        auto *manager = EquipmentManagerOf(actor);
        if (manager == nullptr) {
            return;
        }

        const std::size_t pieces = std::min(packedEquipment.size() / kPackedItemClassWidth, packedWear.size() / kPackedWearWidth);
        for (std::size_t piece = 0; piece < pieces; ++piece) {
            // Dirt, the six blood zones and quality, then the health's bits.
            const char *text = packedWear.data() + piece * kPackedWearWidth;
            std::array<std::uint8_t, 8> bytes {};
            std::uint32_t healthBits = 0;
            bool parsed              = true;
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                parsed = parsed && std::from_chars(text + i * 2, text + i * 2 + 2, bytes[i], 16).ptr == text + i * 2 + 2;
            }
            parsed = parsed && std::from_chars(text + 16, text + kPackedWearWidth, healthBits, 16).ptr == text + kPackedWearWidth;

            // Quality 0 is the owner saying it does not wear this as armour.
            SDK::wh::entitymodule::S_ItemClassGUID itemClass {};
            if (!parsed || bytes[7] == 0 || !ReadPackedItemClass(packedEquipment, piece * kPackedItemClassWidth, itemClass)) {
                continue;
            }
            auto *item  = FindWornItem(manager, itemClass);
            auto *armor = item != nullptr ? item->GetOrCreateArmorRuntimeData() : nullptr;
            if (armor == nullptr) {
                continue;
            }

            // The setter stores int(dirt * 255); every byte survives the trip.
            armor->SetDirt(bytes[0] / 255.0F);
            armor->SetBlood({bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6]});

            // Quality before health, as the inventory projection writes them:
            // health is clamped into the range the quality allows, and the
            // scratches are drawn from the condition the two make. The wire is
            // another client's word, so a quality above what the class allows
            // and a health that is not finite are not passed on.
            const std::uint32_t quality = bytes[7];
            if (quality <= item->InvokeGetMaxQualityRaw() && quality != item->InvokeGetQualityRaw()) {
                item->SetQuality(quality);
            }
            if (const float health = std::bit_cast<float>(healthBits); std::isfinite(health) && health != item->GetHealth()) {
                item->InvokeSetItemHealthRaw(health);
            }
        }
    }

    bool EquipClothingPreset(SDK::wh::entitymodule::C_Actor *actor, std::string_view presetGuid) {
        SDK::CryGUID presetId {};
        if (actor == nullptr || !Shared::Game::CryGuidFromText(presetGuid, presetId.lopart, presetId.hipart)) {
            return false;
        }
        auto *actorSoul = actor->GetSoul();
        if (actorSoul == nullptr) {
            return false;
        }
        SDK::wh::rpgmodule::EnsureSoulInventory(actorSoul);
        auto *runtime = actor->GetInventorySoulRuntime();
        if (runtime == nullptr || !runtime->IsInitialisedFor(actorSoul) || runtime->GetInventory() == nullptr) {
            return false;
        }
        const std::string tag(std::string_view(KCDC_HIDE("CART_HARNESS")));
        runtime->PushClothingPreset(MakeOverride(presetId, tag.c_str()), SDK::wh::rpgmodule::I_InventorySoul::E_PresetItemMode::Delete, true);
        return true;
    }
} // namespace KCDC::Common

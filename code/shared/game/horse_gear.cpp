#include "horse_gear_catalog.h"

#include "cry_guid_text.h"

#include <utils/string_utils.h>

#include <algorithm>
#include <utility>

namespace KCDC::Shared::Game {
    namespace {
        int HexDigit(char digit) {
            if (digit >= '0' && digit <= '9') {
                return digit - '0';
            }
            if (digit >= 'a' && digit <= 'f') {
                return digit - 'a' + 10;
            }
            if (digit >= 'A' && digit <= 'F') {
                return digit - 'A' + 10;
            }
            return -1;
        }

        bool ReadHex64(std::string_view text, std::uint64_t &out) {
            out = 0;
            for (const char digit : text) {
                const int value = HexDigit(digit);
                if (value < 0) {
                    return false;
                }
                out = (out << 4) | static_cast<std::uint64_t>(value);
            }
            return true;
        }

        bool ClassLess(const RPC::ItemClassId &lhs, const RPC::ItemClassId &rhs) {
            return lhs.low != rhs.low ? lhs.low < rhs.low : lhs.high < rhs.high;
        }

        /**
         * Every entry's binary class, parsed once and kept two ways: in catalog
         * order, for packing, and sorted, for the lookup every copy of a horse
         * makes for each item it wears, every frame.
         */
        struct GearClasses {
            std::vector<RPC::ItemClassId> byEntry;
            std::vector<std::pair<RPC::ItemClassId, const HorseGearEntry *>> sorted;
        };

        const GearClasses &Classes() {
            static const GearClasses classes = [] {
                GearClasses built;
                built.byEntry.resize(g_HorseGear.size());
                built.sorted.reserve(g_HorseGear.size());
                for (std::size_t i = 0; i < g_HorseGear.size(); ++i) {
                    RPC::ItemClassId &itemClass = built.byEntry[i];
                    if (CryGuidFromText(g_HorseGear[i].guid, itemClass.low, itemClass.high)) {
                        built.sorted.emplace_back(itemClass, &g_HorseGear[i]);
                    }
                }
                std::sort(built.sorted.begin(), built.sorted.end(), [](const auto &lhs, const auto &rhs) {
                    return ClassLess(lhs.first, rhs.first);
                });
                return built;
            }();
            return classes;
        }
    } // namespace

    std::string_view HorseGearSlotName(HorseGearSlot slot) {
        switch (slot) {
        case HorseGearSlot::Saddle: return "saddle";
        case HorseGearSlot::Head: return "head";
        case HorseGearSlot::Torso: return "torso";
        case HorseGearSlot::Shoe: return "shoe";
        }
        return "";
    }

    bool ParseHorseGearSlot(std::string_view name, HorseGearSlot &out) {
        for (std::size_t i = 0; i < kHorseGearSlotCount; ++i) {
            const auto slot = static_cast<HorseGearSlot>(i);
            if (Framework::Utils::StringUtils::EqualsIgnoreCase(name, HorseGearSlotName(slot))) {
                out = slot;
                return true;
            }
        }
        return false;
    }

    const HorseGearEntry *FindHorseGear(const RPC::ItemClassId &itemClass) {
        const auto &sorted = Classes().sorted;
        const auto found   = std::lower_bound(sorted.begin(), sorted.end(), itemClass, [](const auto &candidate, const RPC::ItemClassId &wanted) {
            return ClassLess(candidate.first, wanted);
        });
        return found != sorted.end() && found->first == itemClass ? found->second : nullptr;
    }

    const HorseGearEntry *FindHorseGear(std::string_view nameOrGuid) {
        for (const HorseGearEntry &entry : g_HorseGear) {
            if (Framework::Utils::StringUtils::EqualsIgnoreCase(entry.name, nameOrGuid) || Framework::Utils::StringUtils::EqualsIgnoreCase(entry.guid, nameOrGuid)) {
                return &entry;
            }
        }
        return nullptr;
    }

    std::string_view HorseGearErrorText(HorseGearError error) {
        switch (error) {
        case HorseGearError::None: return "";
        case HorseGearError::Malformed: return "the gear list is malformed";
        case HorseGearError::NotHorseGear: return "an item is not horse gear";
        case HorseGearError::SlotTaken: return "two items are for the same slot";
        case HorseGearError::TorsoWithoutSaddle: return "a caparison needs a saddle under it";
        }
        return "";
    }

    std::string PackHorseGear(const HorseGear &gear) {
        std::string packed;
        packed.reserve(kMaxPackedHorseGear);
        for (const HorseGearEntry *entry : gear.slots) {
            if (entry == nullptr) {
                continue;
            }
            const RPC::ItemClassId &itemClass = Classes().byEntry[static_cast<std::size_t>(entry - g_HorseGear.data())];
            itemClass.AppendPackedText(packed);
        }
        return packed;
    }

    bool ReadPackedItemClass(std::string_view packed, std::size_t offset, RPC::ItemClassId &out) {
        if (offset + kPackedHorseGearWidth > packed.size()) {
            return false;
        }
        return ReadHex64(packed.substr(offset, 16), out.low) && ReadHex64(packed.substr(offset + 16, 16), out.high);
    }

    HorseGearError UnpackHorseGear(std::string_view packed, HorseGear &out) {
        out = {};
        if (packed.size() % kPackedHorseGearWidth != 0 || packed.size() > kMaxPackedHorseGear) {
            return HorseGearError::Malformed;
        }
        for (std::size_t offset = 0; offset < packed.size(); offset += kPackedHorseGearWidth) {
            RPC::ItemClassId itemClass;
            if (!ReadPackedItemClass(packed, offset, itemClass)) {
                return HorseGearError::Malformed;
            }
            const HorseGearEntry *entry = FindHorseGear(itemClass);
            if (entry == nullptr) {
                return HorseGearError::NotHorseGear;
            }
            if (out.At(entry->slot) != nullptr) {
                return HorseGearError::SlotTaken;
            }
            out.At(entry->slot) = entry;
        }
        return ValidateHorseGear(out);
    }

    const HorseGearPresetEntry *FindHorseGearPreset(std::string_view name) {
        for (const HorseGearPresetEntry &preset : g_HorseGearPresets) {
            if (Framework::Utils::StringUtils::EqualsIgnoreCase(preset.name, name)) {
                return &preset;
            }
        }
        return nullptr;
    }

    HorseGearError HorseGearFromPreset(const HorseGearPresetEntry &preset, HorseGear &out) {
        out = {};
        for (const std::string_view guid : preset.itemGuids) {
            const HorseGearEntry *entry = FindHorseGear(guid);
            if (entry == nullptr) {
                return HorseGearError::NotHorseGear;
            }
            if (out.At(entry->slot) != nullptr) {
                return HorseGearError::SlotTaken;
            }
            out.At(entry->slot) = entry;
        }
        return ValidateHorseGear(out);
    }

    HorseGearError ValidateHorseGear(const HorseGear &gear) {
        for (std::size_t i = 0; i < kHorseGearSlotCount; ++i) {
            if (gear.slots[i] != nullptr && gear.slots[i]->slot != static_cast<HorseGearSlot>(i)) {
                return HorseGearError::SlotTaken;
            }
        }
        if (gear.At(HorseGearSlot::Torso) != nullptr && gear.At(HorseGearSlot::Saddle) == nullptr) {
            return HorseGearError::TorsoWithoutSaddle;
        }
        return HorseGearError::None;
    }
} // namespace KCDC::Shared::Game

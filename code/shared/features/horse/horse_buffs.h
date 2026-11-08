#pragma once

#include "shared/features/player/player_buff.h"
#include "shared/game/cry_guid_text.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Game {
    /** The longest `HorseEntity::buffs` a server writes: canonical GUIDs and the commas between them. */
    inline constexpr std::size_t kMaxPackedHorseBuffs = 16 * (kCryGuidTextLength + 1);

    /** `HorseEntity::buffs` from the definitions, in the order given. */
    inline std::string PackHorseBuffs(const std::vector<RPC::BuffId> &buffs) {
        std::string packed;
        packed.reserve(buffs.size() * (kCryGuidTextLength + 1));
        for (const RPC::BuffId &buff : buffs) {
            if (!packed.empty()) {
                packed += ',';
            }
            const auto text = CryGuidToText(buff.low, buff.high);
            packed.append(text.data(), text.size());
        }
        return packed;
    }

    /**
     * The definitions in a `HorseEntity::buffs` string. False, with `out`
     * holding what parsed before the fault, for a string a server never
     * writes: a malformed GUID, or more entries than a horse may carry.
     */
    inline bool UnpackHorseBuffs(std::string_view packed, std::vector<RPC::BuffId> &out) {
        out.clear();
        if (packed.empty()) {
            return true;
        }
        if (packed.size() > kMaxPackedHorseBuffs) {
            return false;
        }
        std::size_t at = 0;
        while (at <= packed.size()) {
            const std::size_t end = std::min(packed.find(',', at), packed.size());
            RPC::BuffId buff;
            if (end - at != kCryGuidTextLength || !CryGuidFromText(packed.substr(at, kCryGuidTextLength), buff.low, buff.high) || !buff.IsValid()) {
                return false;
            }
            if (std::find(out.begin(), out.end(), buff) == out.end()) {
                out.push_back(buff);
            }
            at = end + 1;
        }
        return true;
    }
} // namespace KCDC::Shared::Game

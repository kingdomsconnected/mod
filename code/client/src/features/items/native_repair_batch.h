#pragma once

#include "shared/features/inventory/inventory_repair.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Features::Items {
    /** One distinct native unit, before the selection and after all combines. */
    struct NativeRepairPart {
        std::uint64_t nativeId = 0;
        std::string rowId;
        nlohmann::json before;
        nlohmann::json after;
        bool spent    = false;
        bool equipped = false;
    };

    /** All repair-kit combines between updates, committed as one selection. */
    class NativeRepairBatch final {
      public:
        template <typename ReadStart>
        void Observe(std::uint64_t kitId, std::uint64_t equipmentId, bool equipped, bool canSpend, const ReadStart &readStart) {
            // Even a refused capture must suppress wear for every unit the
            // native repair changes, so rollback restores the kits as well.
            for (const auto id : {kitId, equipmentId}) {
                if (std::find(_touched.begin(), _touched.end(), id) == _touched.end()) {
                    _touched.push_back(id);
                }
            }
            _refused = _refused || !canSpend;
            if (_refused) {
                return;
            }
            const auto capture = [&](std::uint64_t id, std::vector<NativeRepairPart> &parts, std::size_t maximum, bool worn) {
                if (std::any_of(parts.begin(), parts.end(), [id](const NativeRepairPart &part) {
                        return part.nativeId == id;
                    })) {
                    // The same kit can pay for several selected items. Its
                    // original properties and row remain the starting point.
                    return true;
                }
                NativeRepairPart part;
                if (parts.size() == maximum || !readStart(id, parts, part)) {
                    return false;
                }
                part.equipped = worn;
                parts.push_back(std::move(part));
                return true;
            };
            _refused = !capture(equipmentId, _equipment, KCDC::Shared::Inventory::kMaximumRepairEquipment, equipped) || !capture(kitId, _kits, KCDC::Shared::Inventory::kMaximumRepairKits, false);
        }

        template <typename ReadResult>
        void CaptureResult(std::uint64_t kitId, std::uint64_t equipmentId, const ReadResult &readResult) {
            if (_refused || Empty()) {
                return;
            }
            // CombineItems can merge and destroy either unit immediately
            // after ApplyRepairKit returns. Read them while both still live.
            const auto capture = [&](std::uint64_t id, std::vector<NativeRepairPart> &parts, bool kit) {
                const auto part = std::find_if(parts.begin(), parts.end(), [id](const NativeRepairPart &entry) {
                    return entry.nativeId == id;
                });
                return part != parts.end() && readResult(kit, *part);
            };
            _refused = !capture(equipmentId, _equipment, false) || !capture(kitId, _kits, true);
        }

        bool Complete() const {
            if (_refused || _equipment.empty() || _kits.empty()) {
                return false;
            }
            for (const auto &part : _equipment) {
                if (!part.after.contains("health") || part.after.at("health") <= part.before.at("health")) {
                    return false;
                }
            }
            for (const auto &part : _kits) {
                if (!part.after.contains("health")) {
                    return false;
                }
            }
            return true;
        }

        nlohmann::json Encode() const {
            nlohmann::json equipment = nlohmann::json::array();
            nlohmann::json kits      = nlohmann::json::array();
            for (const auto &part : _equipment) {
                equipment.push_back({{"id", part.rowId}, {"before", part.before}, {"after", part.after}, {"equipped", part.equipped}});
            }
            for (const auto &part : _kits) {
                kits.push_back({{"id", part.rowId}, {"before", part.before}, {"after", part.after}, {"spent", part.spent}});
            }
            return {{"kind", "repair"}, {"equipment", std::move(equipment)}, {"kits", std::move(kits)}};
        }

        bool Empty() const {
            return _touched.empty();
        }
        bool Refused() const {
            return _refused;
        }
        const std::vector<std::uint64_t> &Touched() const {
            return _touched;
        }

      private:
        std::vector<NativeRepairPart> _equipment;
        std::vector<NativeRepairPart> _kits;
        std::vector<std::uint64_t> _touched;
        bool _refused = false;
    };
} // namespace KCDC::Features::Items

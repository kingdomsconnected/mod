#include "level_edit_module.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/virtual_world.h>

#include <algorithm>

namespace KCDC::Features::LevelEdit {
    namespace {
        // Whether a resolved edit still takes `guid` out: the id could be stale, or minted again for another edit.
        bool Removes(const Shared::Entities::LevelEditEntity *edit, std::uint64_t guid) {
            return edit != nullptr && edit->kind == Shared::Game::LevelEditKind::Entity && edit->guid == guid && (edit->hidden || !edit->model.empty());
        }
    } // namespace

    LevelEditModule &LevelEditModule::Get() {
        static LevelEditModule module;
        return module;
    }

    void LevelEditModule::Note(Shared::Entities::LevelEditEntity &edit) {
        ++_revision;
        if (edit.kind != Shared::Game::LevelEditKind::Entity || edit.guid == 0) {
            return;
        }
        std::vector<std::uint64_t> &ids = _byGuid[edit.guid];
        if (std::find(ids.begin(), ids.end(), edit.GetNetworkID()) == ids.end()) {
            ids.push_back(edit.GetNetworkID());
        }
    }

    void LevelEditModule::Forget(Shared::Entities::LevelEditEntity &edit) {
        ++_revision;
        const auto found = _byGuid.find(edit.guid);
        if (found == _byGuid.end()) {
            return;
        }
        std::erase(found->second, edit.GetNetworkID());
        if (found->second.empty()) {
            _byGuid.erase(found);
        }
    }

    Shared::Entities::LevelEditEntity *LevelEditModule::RemovalOf(std::uint64_t guid, std::uint32_t virtualWorld) const {
        const auto found  = _byGuid.find(guid);
        auto *replication = Framework::CoreModules::GetReplication();
        if (found == _byGuid.end() || replication == nullptr) {
            return nullptr;
        }
        for (const std::uint64_t networkId : found->second) {
            // A stopped resource destroys its edits without telling anyone: an id that no longer resolves is that edit gone.
            auto *edit = replication->GetEntity<Shared::Entities::LevelEditEntity>(networkId);
            if (Removes(edit, guid) && (edit->GetVirtualWorld() == virtualWorld || edit->GetVirtualWorld() == MafiaNet::VIRTUAL_WORLD_GLOBAL)) {
                return edit;
            }
        }
        return nullptr;
    }

    void LevelEditModule::Prune() const {
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return;
        }
        std::erase_if(_byGuid, [replication](auto &entry) {
            std::erase_if(entry.second, [replication](std::uint64_t networkId) {
                return replication->GetEntity<Shared::Entities::LevelEditEntity>(networkId) == nullptr;
            });
            return entry.second.empty();
        });
    }

    std::vector<Shared::Entities::LevelEditEntity *> LevelEditModule::Removals() const {
        Prune();
        std::vector<Shared::Entities::LevelEditEntity *> edits;
        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return edits;
        }
        for (const auto &[guid, ids] : _byGuid) {
            for (const std::uint64_t networkId : ids) {
                auto *edit = replication->GetEntity<Shared::Entities::LevelEditEntity>(networkId);
                if (Removes(edit, guid)) {
                    edits.push_back(edit);
                }
            }
        }
        return edits;
    }
} // namespace KCDC::Features::LevelEdit

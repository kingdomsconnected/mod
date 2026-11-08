#include "level_edit_service.h"

#include "level_edit.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::LevelEdit {
    void LevelEditService::NativeContextChanged() {
        ++_nativeContext;
    }

    void LevelEditService::QueueApply(std::uint64_t networkId) {
        std::scoped_lock lock(_pendingMutex);
        _pendingApplies.insert(networkId);
    }

    void LevelEditService::QueueRestore(std::uint64_t networkId) {
        std::scoped_lock lock(_pendingMutex);
        _pendingRestores.push_back(networkId);
    }

    void LevelEditService::Reset() {
        _objects.RestoreAll();
        _applied.clear();
        _observedContext = 0;

        std::scoped_lock lock(_pendingMutex);
        _pendingApplies.clear();
        _pendingRestores.clear();
    }

    void LevelEditService::ObserveNativeContext() {
        const std::uint64_t context = _nativeContext.load();
        if (_observedContext == context) {
            return;
        }
        _observedContext = context;

        // The objects belonged to the world that is gone, so there is nothing
        // to put back: forget them, and apply every edit again to the new one.
        _objects.Forget();
        _applied.clear();
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<KCDC::Features::LevelEdit::LevelEdit>([this](KCDC::Features::LevelEdit::LevelEdit *edit) {
                QueueApply(edit->GetNetworkID());
            });
        }
    }

    void LevelEditService::Restore(std::uint64_t networkId) {
        if (const auto found = _applied.find(networkId); found != _applied.end()) {
            _objects.Restore(found->second);
            _applied.erase(found);
        }
    }

    void LevelEditService::Apply(std::uint64_t networkId) {
        // A change is a fresh record: the object goes back as the level had
        // it, and the edit is made again from what the wire says now. It is
        // rare, and it is the one way to undo half of an edit -- a move taken
        // back while the object stays out.
        Restore(networkId);

        auto *replication = Framework::CoreModules::GetReplication();
        auto *edit        = replication != nullptr ? replication->GetEntity<KCDC::Features::LevelEdit::LevelEdit>(networkId) : nullptr;
        if (edit == nullptr || (!edit->hidden && !edit->moved && edit->model.empty())) {
            return;
        }
        if (const std::uint64_t id = _objects.ApplyDocument(edit->Describe()); id != 0) {
            _applied[networkId] = id;
        }
    }

    void LevelEditService::Update() {
        ObserveNativeContext();

        std::unordered_set<std::uint64_t> applies;
        std::vector<std::uint64_t> restores;
        {
            std::scoped_lock lock(_pendingMutex);
            applies.swap(_pendingApplies);
            restores.swap(_pendingRestores);
        }

        // Restores first: an edit that arrived and went in one tick is then
        // never applied, since its replica no longer resolves.
        for (const std::uint64_t networkId : restores) {
            Restore(networkId);
        }
        for (const std::uint64_t networkId : applies) {
            Apply(networkId);
        }

        _objects.Refresh();
    }
} // namespace KCDC::Features::LevelEdit

#include "smart_object_module.h"
#include "smart_object.h"

#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/entitysystem/i_entity_system.h"
#include "sdk/cryengine/physics/c_physical_world.h"

#include <core_modules.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::SmartObject {
    SmartObjectModule &SmartObjectModule::Get() {
        static SmartObjectModule module;
        return module;
    }

    void SmartObjectModule::NativeContextChanged() {
        ++_nativeContext;
    }

    void SmartObjectModule::QueueRelease(std::vector<SDK::EntityId> parts, std::vector<std::pair<SDK::IRenderNode *, SDK::IPhysicalEntity *>> brushes) {
        std::scoped_lock lock(_releaseMutex);
        _pendingParts.insert(_pendingParts.end(), parts.begin(), parts.end());
        _pendingBrushes.insert(_pendingBrushes.end(), brushes.begin(), brushes.end());
    }

    void SmartObjectModule::AddFixture(std::uint64_t guid) {
        std::scoped_lock lock(_fixtureMutex);
        _fixtures.insert(guid);
    }

    void SmartObjectModule::RemoveFixture(std::uint64_t guid) {
        std::scoped_lock lock(_fixtureMutex);
        _fixtures.erase(guid);
    }

    bool SmartObjectModule::IsFixture(std::uint64_t guid) const {
        std::scoped_lock lock(_fixtureMutex);
        return _fixtures.contains(guid);
    }

    void SmartObjectModule::ObserveNativeContext() {
        const std::uint64_t context = _nativeContext.load();
        if (_observedContext == context) {
            return;
        }
        _observedContext = context;

        // The entity ids and the brushes named the world that went: nothing
        // queued is there to remove, and every object builds afresh.
        {
            std::scoped_lock lock(_releaseMutex);
            _pendingParts.clear();
            _pendingBrushes.clear();
        }
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<SmartObject>([](SmartObject *object) {
                object->Unbind();
            });
        }
    }

    void SmartObjectModule::DrainReleases() {
        std::vector<SDK::EntityId> parts;
        std::vector<std::pair<SDK::IRenderNode *, SDK::IPhysicalEntity *>> brushes;
        {
            std::scoped_lock lock(_releaseMutex);
            parts.swap(_pendingParts);
            brushes.swap(_pendingBrushes);
        }
        if (auto *entitySystem = SDK::IEntitySystem::GetInstance()) {
            for (const SDK::EntityId part : parts) {
                if (part != 0) {
                    entitySystem->RemoveEntity(part, false);
                }
            }
        }
        for (const auto &brush : brushes) {
            KCDC::Common::Spawn::StaticBrush::DestroyReleased(brush.first, brush.second);
        }
    }

    void SmartObjectModule::Update() {
        ObserveNativeContext();
        DrainReleases();
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<SmartObject>([](SmartObject *object) {
                object->Frame();
            });
        }
    }

    void SmartObjectModule::Reset() {
        // The replicas go with the session and hand their natives over as
        // they do; this drains what they left on the next tick of the next one.
        std::scoped_lock lock(_fixtureMutex);
        _fixtures.clear();
    }
} // namespace KCDC::Features::SmartObject

#pragma once

#include "../../types.h"
#include "../../cryengine/entitysystem/i_entity_system_sink.h"
#include "../animationmodule/i_scene_listener.h"
#include "c_combat_lod_manager.h"
#include "i_combat_scene.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::combatmodule {
    /** RTTI fixes four base subobjects; constructor 0x180A46140 fills 0x78. */
    class C_CombatScene
        : public I_CombatScene
        , public wh::animationmodule::I_SceneListener
        , public SDK::IEntitySystemSink
        , public wh::game::I_GameSideEffectCallback {
      public:
        C_CombatLodManager *GetLodManager() const {
            return m_pLodManager;
        }

      private:
        // Constructor 0x180A46140 zeroes every member and reserves 100
        // entries in +0x20. Slot 2 of the primary table
        // (RebuildCombatGroups) reads +0x48 and +0x60.
        SDK::StdVector<void *> m_objects;                // 0x20, combat objects, each asked through its vtable +0x48 (sub_180C5962C)
        C_CombatLodManager *m_pLodManager;               // 0x38
        std::uint64_t m_value0040;                       // 0x40, 0 by default
        SDK::StdVector<void *> m_groupLocks;             // 0x48, one recursive SRW lock per combat group, indexed by C_CombatActor +0x384
        SDK::StdVector<std::uint32_t> m_groupEntityIds;  // 0x60, entity ids of the fighters whose group index was assigned

        static void AssertLayout() {
            static_assert(offsetof(C_CombatScene, m_objects) == 0x20, "C_CombatScene::m_objects offset mismatch");
            static_assert(offsetof(C_CombatScene, m_groupLocks) == 0x48, "C_CombatScene::m_groupLocks offset mismatch");
            static_assert(offsetof(C_CombatScene, m_groupEntityIds) == 0x60, "C_CombatScene::m_groupEntityIds offset mismatch");
            static_assert(offsetof(C_CombatScene, m_pLodManager) == 0x38, "C_CombatScene::m_pLodManager offset mismatch");
        }
    };
    static_assert(sizeof(C_CombatScene) == 0x78, "C_CombatScene size mismatch");
} // namespace SDK::wh::combatmodule

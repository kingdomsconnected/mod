#pragma once

#include "sdk/patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::shared {
    /**
     * The engine's own RAII guard over the entity/script world.
     *
     * `CEntitySystem::SpawnEntity` takes this lock itself, but only across the
     * span it owns: asked for `autoInit = false` it releases before returning,
     * so the caller's own `InitEntity` -- and anything the caller does to the
     * entity's script table in between -- runs unguarded.
     *
     * That is why every native that spawns in two steps wraps the whole
     * sequence in an outer lock of its own: `CActorSystem::CreateActor`
     * (0x180B86120), `C_RiderPlayerControl::SpawnHorseActor` (0x1829F297C) and
     * `C_EntitySpawnerQueue::ExecuteSpawnRequest` (0x180A6C2B8) all do. Any
     * code here that reproduces that recipe has to hold it too.
     *
     * Constructed and destroyed through the game's own ctor and dtor rather
     * than reimplemented: the constructor also drains the lock's deferred
     * command queue when it is the one that took the lock, which is state this
     * side cannot see.
     *
     * Nesting one of these around a call that takes its own is safe, and is
     * what the game does: `CActorSystem::CreateActor` holds this lock across a
     * `SpawnEntity` that takes it again on the same slot and the same thread.
     * Every actor the game spawns would deadlock otherwise, so the underlying
     * mutex is recursive.
     */
    class C_ScopedEngineLock final {
      public:
        /**
         * `lockIndex` selects one of the engine's lock slots; every native
         * call site passes 0, which is the entity/script world.
         */
        explicit C_ScopedEngineLock(std::int32_t lockIndex = 0) {
            hook::this_call<void>(gPatterns.C_ScopedEngineLock__Construct, this, lockIndex);
        }

        ~C_ScopedEngineLock() {
            hook::this_call<void>(gPatterns.C_ScopedEngineLock__Destruct, this);
        }

        C_ScopedEngineLock(const C_ScopedEngineLock &)            = delete;
        C_ScopedEngineLock &operator=(const C_ScopedEngineLock &) = delete;
        C_ScopedEngineLock(C_ScopedEngineLock &&)                 = delete;
        C_ScopedEngineLock &operator=(C_ScopedEngineLock &&)      = delete;

      private:
        void *m_pEngineLock;         // 0x00, qword_18492D988 + 144 * lockIndex
        void *m_pMutex;              // 0x08, the same lock; its mutex is at its start
        bool m_owns;                 // 0x10
        std::byte m_padding0011[0x7]; // 0x11, padding to the native's 24 bytes

        static void AssertLayout() {
            static_assert(offsetof(C_ScopedEngineLock, m_pMutex) == 0x08, "C_ScopedEngineLock::m_pMutex offset mismatch");
            static_assert(offsetof(C_ScopedEngineLock, m_owns) == 0x10, "C_ScopedEngineLock::m_owns offset mismatch");
        }
    };

    static_assert(sizeof(C_ScopedEngineLock) == 0x18, "C_ScopedEngineLock size mismatch");
} // namespace SDK::wh::shared

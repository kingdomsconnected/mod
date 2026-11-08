#pragma once

#include <cstdint>

namespace SDK::wh {
    class C_ReadinessObserver;

    /**
     * Warhorse's "is the game ready to be shown" registry, reached through
     * ISystem slot 99 (`CSystem::GetReadinessService`, CSystem+0x2B20) and
     * ticked from `CSystem::Update` on the main thread.
     *
     * Tasks (`I_ReadinessTask`, e.g. `C_ReadinessProgress`) register here with
     * a category bit; an observer waits on a category mask and fires once every
     * task in that mask reports ready, or once its timeout
     * (`wh_sys_ReadinessObserverTimeoutDuration`, 50 s) passes. The service
     * does not own its tasks: RemoveTask only unlinks.
     *
     * Vtable 0x183DBE8A0; every slot below was read off its implementation.
     */
    class C_ReadinessService {
      public:
        virtual void AddTask(void *task)                                  = 0; // 0x00, 0x180B40BB8
        virtual void RemoveTask(void *task)                               = 0; // 0x08, 0x180D2C330
        virtual void AddObserver(C_ReadinessObserver *observer)           = 0; // 0x10, 0x180FC3FC0
        virtual void RemoveObserver(C_ReadinessObserver *observer)        = 0; // 0x18, 0x182436370
        virtual void ForEachTask(std::uint32_t mask, void *function)      = 0; // 0x20, 0x182441224
        virtual bool AreTasksReady(std::uint32_t mask)                    = 0; // 0x28, 0x1808BAEE0
        virtual float GetTimeout() const                                  = 0; // 0x30, 0x180FC3F40
        virtual void *DescribeTasks(void *result, std::uint32_t mask)     = 0; // 0x38, 0x18243EB44
        virtual void Update()                                             = 0; // 0x40, 0x180E88E10
    };
} // namespace SDK::wh

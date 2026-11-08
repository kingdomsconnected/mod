#pragma once

#include <cstddef>

namespace SDK::Cry::GamePlatform {
    /**
     * The platform's remotely stored statistics and achievements.
     *
     * Slots follow Steam::CStatistics's vtable at 0x183DB7B60 and differ
     * from the generic CryGamePlatform interface. Only established contracts
     * are named; the remaining slots stay opaque.
     */
    struct IStatistics {
        virtual ~IStatistics()                                                       = default; // +0x00
        virtual void Opaque01()                                                      = 0;       // +0x08 registers an achievement from a details struct
        virtual bool UnlockAchievement(const char *name)                             = 0;       // +0x10
        virtual void Opaque03()                                                      = 0;       // +0x18
        virtual void Opaque04()                                                      = 0;       // +0x20
        virtual bool Upload()                                                        = 0;       // +0x28
        virtual void Opaque06()                                                      = 0;       // +0x30
        virtual std::size_t GetAchievementCount() const                              = 0;       // +0x38
        virtual bool SetAchievementProgress(const char *name, unsigned int progress) = 0;       // +0x40
        virtual void Opaque09()                                                      = 0;       // +0x48
        virtual void Opaque10()                                                      = 0;       // +0x50
    };

    static_assert(sizeof(IStatistics) == 0x8, "IStatistics size mismatch");
} // namespace SDK::Cry::GamePlatform

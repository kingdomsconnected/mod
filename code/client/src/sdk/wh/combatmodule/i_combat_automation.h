#pragma once

#include <cstdint>

namespace SDK::wh::combatmodule {
    /**
     * Secondary interface of C_CombatAutomation (+0x18), table 0x183B09E88.
     * Enable returns an ownership token. Disable must return that same token;
     * zero owns nothing. The controls activate the combat subsystem without
     * resuming the actor's general AI brain.
     */
    class I_CombatAutomation {
      public:
        virtual ~I_CombatAutomation()                                     = default;
        virtual void SetOffenseEnabled(bool enabled, std::int32_t &token) = 0; // 1, 0x18099DA1C
        virtual void unk_fn0002()                                         = 0;
        virtual void unk_fn0003()                                         = 0;
        virtual void unk_fn0004()                                         = 0;
        virtual void unk_fn0005()                                         = 0;
        virtual void unk_fn0006()                                         = 0;
        virtual void unk_fn0007()                                         = 0;
        virtual void SetDefenseEnabled(bool enabled, std::int32_t &token) = 0; // 8, 0x18099DA80
        virtual void unk_fn0009()                                         = 0;
        virtual void unk_fn0010()                                         = 0;
        virtual void SetGuardEnabled(bool enabled, std::int32_t &token)   = 0; // 11, 0x18099DB5C
    };

    static_assert(sizeof(I_CombatAutomation) == 0x8, "I_CombatAutomation size mismatch");
} // namespace SDK::wh::combatmodule

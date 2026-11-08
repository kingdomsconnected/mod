#pragma once

#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * Primary minigame contract. C_Minigame's 47-slot table is 0x183A6A568;
     * C_Alchemy and C_Blacksmithing retain every index. It has no destructor
     * at slot zero: that slot returns the one-byte kind. Destruction is
     * reached through the secondary IActionListener interface instead.
     * Unknown slots must not be called through these placeholder signatures.
     */
    class I_Minigame {
      public:
        virtual std::uint8_t GetKind()          = 0; // 0; alchemy 0x181A78EB0 (3), smithing 0x181A99DC0 (12)
        virtual void unk_minigame0001()         = 0;
        virtual void unk_minigame0002()         = 0;
        virtual void unk_minigame0003()         = 0;
        virtual void BeginExit()                = 0; // 4; C_BattleArchery 0x182E5AC00 -- the player's exit key; queues the exit fragment, then Finish
        virtual void Finish()                   = 0; // 5; smithing C_Blacksmithing::Finish 0x180B684B8
        virtual void Cancel()                   = 0; // 6; smithing 0x182E59578
        virtual bool HasFinished()              = 0; // 7; smithing 0x180858F80, alchemy 0x180737914
        virtual void unk_minigame0008()         = 0;
        virtual void unk_minigame0009()         = 0;
        virtual void unk_minigame0010()         = 0;
        virtual void unk_minigame0011()         = 0;
        virtual void unk_minigame0012()         = 0;
        virtual void unk_minigame0013()         = 0;
        virtual void unk_minigame0014()         = 0;
        virtual void unk_minigame0015()         = 0;
        virtual void unk_minigame0016()         = 0;
        virtual void unk_minigame0017()         = 0;
        virtual bool PrepareInteraction()       = 0; // 18; C_Minigame::PrepareInteraction 0x18061885C -- starts the interaction action; not a predicate
        virtual void Update(float deltaSeconds) = 0; // 19
        virtual void unk_minigame0020()         = 0;
        virtual void Reset()                    = 0; // 21; alchemy 0x1806C4F1C
        virtual void unk_minigame0022()         = 0;
        virtual void unk_minigame0023()         = 0;
        virtual void unk_minigame0024()         = 0;
        virtual void unk_minigame0025()         = 0;
        virtual void unk_minigame0026()         = 0;
        virtual void unk_minigame0027()         = 0;
        virtual void unk_minigame0028()         = 0;
        virtual void unk_minigame0029()         = 0;
        virtual void unk_minigame0030()         = 0;
        virtual void unk_minigame0031()         = 0;
        virtual void unk_minigame0032()         = 0;
        virtual void unk_minigame0033()         = 0;
        virtual void unk_minigame0034()         = 0;
        virtual void unk_minigame0035()         = 0;
        virtual void unk_minigame0036()         = 0;
        virtual void unk_minigame0037()         = 0;
        virtual void unk_minigame0038()         = 0;
        virtual void unk_minigame0039()         = 0;
        virtual void unk_minigame0040()         = 0;
        virtual void unk_minigame0041()         = 0;
        virtual void unk_minigame0042()         = 0;
        virtual void unk_minigame0043()         = 0;
        virtual void unk_minigame0044()         = 0;
        virtual void unk_minigame0045()         = 0;
        virtual void RequestExit()              = 0; // 46; C_StoneThrowing 0x182EAE4B0, C_Dice -- queues the exit fragment, whose end calls Finish
    };

    static_assert(sizeof(I_Minigame) == 0x8);
} // namespace SDK::wh::playermodule

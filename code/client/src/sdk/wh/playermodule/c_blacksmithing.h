#pragma once

#include "../../patterns.h"
#include "../entitymodule/c_action_actor.h"
#include "../shared/c_signal.h"
#include "c_minigame.h"
#include "i_blacksmith_action_listener.h"
#include "i_blacksmithing_actions.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * Factory 0x1809F0234 allocates 0xF0 for kind 12. Constructor
     * 0x1809EF36C installs the two secondary interfaces at +0x68/+0x70.
     */
    class C_Blacksmithing
        : public SDK::wh::playermodule::C_Minigame
        , public SDK::wh::playermodule::I_BlacksmithingActions
        , public SDK::wh::playermodule::I_BlacksmithActionListener {
      public:
        /**
         * The minigame's observable state: 0x300 bytes allocated by
         * 0x1809EF6EC and initialised by 0x1809EEAA0, reached through
         * `I_BlacksmithingActions` slot 12. A run of model properties; the
         * engine's name for it is not known. Only the workpiece quality is
         * modelled: the float property at +0x58 keeps its value at +0x60,
         * which is what `FinishWorkpiece` reads.
         */
        class State final {
          public:
            State()                         = delete;
            ~State()                        = delete;
            State(const State &)            = delete;
            State &operator=(const State &) = delete;

            float GetQuality() const {
                return m_quality;
            }

          private:
            // Built by sub_1809EEAA0 (0x300 bytes, from sub_1809EF6EC). Every
            // field is a model property: vftable, value, C_Signal at +0x10,
            // eight trailing bytes. The string and Vec2 kinds are 0x30.
            std::byte m_stringProperty[0x30];      // 0x000; C_ModelProperty<CryStringT<char>>, signal unk_185669258
            std::byte m_floatProperty0030[0x28];   // 0x030; C_ModelProperty<float>
            const void *m_pQualityVTable;          // 0x058; C_ModelProperty<float> vftable
            float m_quality;                       // 0x060
            std::byte m_padding0064[0x4];
            SDK::wh::shared::C_Signal m_qualitySignal; // 0x068; dispatch unk_185666D10
            std::byte m_qualityTail[0x8];          // 0x078
            std::byte m_stateProperty[0x28];       // 0x080; C_ModelEnumProperty<E_BlacksmithState>
            std::byte m_hardeningProperty[0x28];   // 0x0A8; C_ModelEnumProperty<E_BlacksmithHardening>
            std::byte m_inspectingProperty[0x28];  // 0x0D0; C_ModelEnumProperty<E_BlacksmithInspecting>
            std::byte m_boolProperty00F8[0x28];    // 0x0F8; C_ModelProperty<bool>
            std::byte m_floatProperty0120[0x28];   // 0x120; C_ModelProperty<float>
            std::byte m_intProperty0148[0x28];     // 0x148; C_ModelProperty<int>
            std::byte m_boolProperty0170[0x28];    // 0x170; C_ModelProperty<bool>
            std::byte m_vec2Property0198[0x30];    // 0x198; C_ModelProperty<Vec2_tpl<float>>
            std::byte m_boolProperty01C8[0x28];    // 0x1C8; C_ModelProperty<bool>
            std::byte m_boolProperty01F0[0x28];    // 0x1F0; C_ModelProperty<bool>
            std::byte m_floatProperty0218[0x28];   // 0x218; C_ModelProperty<float>
            std::byte m_floatProperty0240[0x28];   // 0x240; C_ModelProperty<float>
            std::byte m_floatProperty0268[0x28];   // 0x268; C_ModelProperty<float>
            std::byte m_boolProperty0290[0x28];    // 0x290; C_ModelProperty<bool>
            std::byte m_boolProperty02B8[0x28];    // 0x2B8; C_ModelProperty<bool>
            void *m_ptrs02E0[4];                   // 0x2E0; ctor 0

            static void AssertLayout() {
                static_assert(offsetof(State, m_quality) == 0x60, "C_Blacksmithing::State::m_quality offset mismatch");
                static_assert(offsetof(State, m_qualitySignal) == 0x68, "C_Blacksmithing::State::m_qualitySignal offset mismatch");
                static_assert(offsetof(State, m_stateProperty) == 0x80, "C_Blacksmithing::State::m_stateProperty offset mismatch");
                static_assert(offsetof(State, m_vec2Property0198) == 0x198, "C_Blacksmithing::State::m_vec2Property0198 offset mismatch");
                static_assert(offsetof(State, m_ptrs02E0) == 0x2E0, "C_Blacksmithing::State::m_ptrs02E0 offset mismatch");
            }
        };

        bool Start(SDK::EntityId station) {
            return hook::this_call<bool>(SDK::gPatterns.C_Blacksmithing__Start, this, station);
        }

        /** The Craft button's callback: closes the recipe screen and starts the selected workpiece. */
        void AcceptSelectedRecipe() {
            hook::this_call<void>(SDK::gPatterns.C_Blacksmithing__AcceptSelectedRecipe, this);
        }

        const State *GetState() const {
            return m_state;
        }

      private:
        SDK::wh::shared::C_Signal m_signal0078;               // 0x78; ctor 0x1809EF36C dispatch unk_185669B40.
        SDK::wh::shared::C_Signal m_signal0088;               // 0x88; dispatch unk_185669460.
        State *m_state; // 0x98; owned, built by sub_1809EF6EC.
        void *m_ptr00A0;                                      // 0xA0; ctor 0.
        void *m_pObject00A8;                                  // 0xA8; owned, built by sub_1809EF858 (holds a std::function at +0x90).
        SDK::wh::entitymodule::C_ActionActor *m_pActionActor; // 0xB0; owned, C_ActionActor::Create(this).
        void *m_pSounds;                                      // 0xB8; owned wh::playermodule::C_BlacksmithingSounds (sub_1809EF9FC).
        void *m_pObject00C0;                                  // 0xC0; owned, built by sub_1809EF75C.
        void *m_ptrs00C8[3];                                  // 0xC8; ctor 0.
        std::int64_t m_value00E0;                             // 0xE0; ctor -100000.
        bool m_flag00E8;                                      // 0xE8; ctor 0 (word write 0x100).
        bool m_flag00E9;                                      // 0xE9; ctor 1.
        bool m_flag00EA;                                      // 0xEA; ctor 0.
        std::byte m_padding00EB[0x5];

        static void AssertLayout() {
            static_assert(offsetof(C_Blacksmithing, m_state) == 0x98, "C_Blacksmithing::m_state offset mismatch");
            static_assert(offsetof(C_Blacksmithing, m_signal0078) == 0x78, "C_Blacksmithing::m_signal0078 offset mismatch");
            static_assert(offsetof(C_Blacksmithing, m_pActionActor) == 0xB0, "C_Blacksmithing::m_pActionActor offset mismatch");
            static_assert(offsetof(C_Blacksmithing, m_value00E0) == 0xE0, "C_Blacksmithing::m_value00E0 offset mismatch");
            static_assert(offsetof(C_Blacksmithing, m_flag00EA) == 0xEA, "C_Blacksmithing::m_flag00EA offset mismatch");
        }
    };

    static_assert(sizeof(C_Blacksmithing::State) == 0x300, "C_Blacksmithing::State size mismatch");
    static_assert(sizeof(C_Blacksmithing) == 0xF0, "C_Blacksmithing size mismatch");
} // namespace SDK::wh::playermodule

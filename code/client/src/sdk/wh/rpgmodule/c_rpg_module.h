#pragma once

#include "../../cryengine/action/i_game_framework_listener.h"
#include "../c_wh_global_environment.h"
#include "../framework/c_base_module.h"
#include "../framework/i_module_message_listener.h"
#include "c_calendar.h"
#include "c_rpg_location_manager.h"
#include "c_soul_list.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::framework {
    class C_ParallelModuleUpdater: public IGameFrameworkListener {
      private:
        std::byte m_padding0008[0x8];  // ctor 0x180BE9170 writes nothing; +0x10 is 16-aligned.
        std::byte m_object0010[0x48];  // 0x10; built by sub_18078BDEC (ctor 0x180BE9170).
        void *m_ptr0058;               // 0x58; ctor 0.
        std::int32_t m_counter0060;    // 0x60; ctor zeroes it with an interlocked exchange.
        std::byte m_padding0064[0x4];
        std::uint32_t m_value0068;     // 0x68; ctor 0.
        std::byte m_padding006C[0x4];

        static void AssertLayout() {
            static_assert(offsetof(C_ParallelModuleUpdater, m_object0010) == 0x10);
            static_assert(offsetof(C_ParallelModuleUpdater, m_counter0060) == 0x60);
            static_assert(offsetof(C_ParallelModuleUpdater, m_value0068) == 0x68);
        }
    };

    static_assert(sizeof(C_ParallelModuleUpdater) == 0x70);
} // namespace SDK::wh::framework

namespace SDK::wh::rpgmodule {
    class C_EventFactory;
    // Declared, not included: I_EventListener is defined over C_Event, whose
    // header includes this one.
    class I_EventListener;

    // Release native references through the game.
    class I_Event {
      public:
        virtual ~I_Event()              = default;
        virtual void unk_fn0001()       = 0;
        virtual void unk_fn0002()       = 0;
        virtual std::uint32_t AddRef()  = 0;
        virtual std::uint32_t Release() = 0;
    };

    static_assert(sizeof(I_Event) == 0x8);

    /**
     * Layout behind the interface: +0x08 the embedded C_EventFactory,
     * +0x10..+0x20 the pending event queue ProcessEvent fills, +0x28..+0x38
     * the listener vector slots 2 and 3 maintain.
     *
     * Slots 1-3 are COMDAT-folded onto unrelated generic bodies, so the
     * addresses are shared with other symbols. The indices are
     * proven against the game's own call sites, not against those names.
     */
    class I_EventManager {
      public:
        // Slot 0 precedes the destructor.
        virtual bool ProcessEvent(I_Event *event) = 0;
        virtual C_EventFactory *GetEventFactory() = 0;
        virtual void RegisterListener(I_EventListener *listener) = 0;
        virtual void UnregisterListener(I_EventListener *listener) = 0;
    };

    static_assert(sizeof(I_EventManager) == 0x8);

    class C_RPGModule: public framework::C_BaseModule, public framework::C_ParallelModuleUpdater {
      public:
        I_EventManager *GetEventManager() const {
            return m_pEventManager;
        }

        C_RPGLocationManager *GetLocationManager() const {
            return m_pLocationManager;
        }

      private:
        C_SoulList *m_pSoulList;         // 0x80; 0x100220 bytes, also published at C_WHGameModules +0x1D0.
        C_Calendar *m_pCalendar;         // 0x88; 0xA0 bytes.
        void *m_pRPGDialog;              // 0x90; an 8-byte wh::rpgmodule::C_RPGDialog (vftable only).
        void *m_pObject0098;             // 0x98; 0x1438 bytes from sub_1817B45CC; ctor writes this module into its +0x08.
        void *m_pRPGQuest;               // 0xA0; an 8-byte wh::rpgmodule::C_RPGQuest (vftable only).
        void *m_pStorage00A8;            // 0xA8; 0x158 bytes allocated raw, unconstructed.
        I_EventManager *m_pEventManager; // 0xB0, ctor 0x180BE8EEC; a 0x40-byte C_EventManager.
        C_RPGLocationManager *m_pLocationManager; // 0xB8; 0x2B0 bytes, ctor 0x180E0F960.
        void *m_ptr00C0;                 // 0xC0; ctor 0.
        std::byte m_opaque00C8[0x8];
        void *m_ptrs00D0[3];             // 0xD0; ctor 0.
        std::byte m_opaque00E8[0x8];
        void *m_ptrs00F0[6];             // 0xF0; ctor 0.

        static void AssertLayout() {
            static_assert(offsetof(C_RPGModule, m_pEventManager) == 0xB0);
            static_assert(offsetof(C_RPGModule, m_pSoulList) == 0x80);
            static_assert(offsetof(C_RPGModule, m_pCalendar) == 0x88);
            static_assert(offsetof(C_RPGModule, m_pLocationManager) == 0xB8);
            static_assert(offsetof(C_RPGModule, m_ptrs00F0) == 0xF0);
        }
    };

    static_assert(sizeof(C_RPGModule) == 0x120);
} // namespace SDK::wh::rpgmodule

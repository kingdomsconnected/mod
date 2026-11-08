#pragma once

#include "../../patterns.h"
#include "../shared/c_signal.h"
#include "c_soul_state_storage.h"
#include "e_soul_derived_stat.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK::wh::rpgmodule {
    class C_Soul;

    /** Record of the per-property list; 24 bytes with the id in front. */
    struct S_SoulPropertyHandler {
        void *context                                                       = nullptr;
        void(__fastcall *callback)(void *context, std::uint32_t propertyId) = nullptr;
    };

    /** Record of the whole-soul list at notifier+0x40. */
    struct S_SoulListenerHandler {
        void *context                                           = nullptr;
        void(__fastcall *callback)(void *context, C_Soul *soul) = nullptr;
    };

    // A soul state raises the group 0x117 + E_SoulState, which expands through
    // the dependency table at 0x1840AC520 into every property it invalidates,
    // its own included. These are those.
    struct E_SoulProperty {
        enum Type : std::uint32_t {
            Health  = 279,
            Stamina = 280,
            Exhaust = 281,
            Hunger  = 282,
        };
    };

    /**
     * Property change notifier at `C_Soul+0x58`, soul slot 76.
     * RTTI 0x184126A18, vtable 0x183A33428.
     *
     * Two listener lists: per-property at +0x08, whole-soul at +0x40. Only the
     * second has a remove; the first is for listeners that live as long as the
     * soul. `C_Soul::~C_Soul` frees both, so a stale record is released rather
     * than dispatched. Registering during a dispatch is safe -- the notifier
     * counts re-entries and defers the insert.
     *
     * Never give two of these slots one name: MSVC emits same-name virtuals
     * reversed.
     */
    class C_SoulPropertyNotifier {
      public:
        virtual void AddPropertyListener(std::uint32_t propertyId, const S_SoulPropertyHandler &handler) = 0; // slot 0
        virtual void AddPropertyListenerRecord(const void *record)                                       = 0; // slot 1
        virtual void AddSoulListener(const S_SoulListenerHandler &handler)                               = 0; // slot 2, matched on (context, callback)
        virtual void RemoveSoulListener(const S_SoulListenerHandler &handler)                            = 0; // slot 3

        void NotifyStateChanged(E_SoulState state) {
            hook::this_call<void>(gPatterns.C_SoulPropertyEvents__NotifyStateChanged, this, state);
        }

        /**
         * Raises the group 35 + `stat`, what `C_StatXPEffect::Apply` raises
         * after writing a stat (0x180C65910). Skills are groups 0..34.
         */
        void NotifyStatProgressChanged(std::uint32_t stat) {
            hook::this_call<void>(gPatterns.C_SoulPropertyNotifier__NotifyProperty, this, 35 + stat);
        }

        /**
         * Raises the group 0x3D + `stat`, what a buff's modifier write raises
         * when it moves a derived stat (0x1808AE574): the drunkenness buff's
         * write for Drunkenness, 63, is group 0x7C. The group expands through
         * the same dependency table as a state's.
         */
        void NotifyDerivedStatChanged(E_SoulDerivedStat::Type stat) {
            hook::this_call<void>(gPatterns.C_SoulPropertyNotifier__NotifyProperty, this, static_cast<std::uint32_t>(0x3D + stat));
        }

      private:
        // Laid out by the constructor 0x1803F1EFC, and walked by the property
        // registration 0x1803F1F40 through the soul: records go straight into
        // the sorted list, or into the deferred one while a dispatch is running.
        void *m_propertyRecords[3];         // 0x08, sorted vector<S_SoulPropertyRecord>
        void *m_deferredPropertyRecords[3]; // 0x20, inserts made during a dispatch
        std::uint32_t m_dispatchDepth;      // 0x38
        bool m_hasDeferredRecords;          // 0x3C
        std::byte m_padding003D[0x3];
        wh::shared::C_Signal m_soulSignal;  // 0x40, the whole-soul listeners

        static void AssertLayout() {
            static_assert(offsetof(C_SoulPropertyNotifier, m_propertyRecords) == 0x08, "C_SoulPropertyNotifier::m_propertyRecords offset mismatch");
            static_assert(offsetof(C_SoulPropertyNotifier, m_dispatchDepth) == 0x38, "C_SoulPropertyNotifier::m_dispatchDepth offset mismatch");
            static_assert(offsetof(C_SoulPropertyNotifier, m_soulSignal) == 0x40, "C_SoulPropertyNotifier::m_soulSignal offset mismatch");
        }
    };

    static_assert(sizeof(C_SoulPropertyNotifier) == 0x50, "C_SoulPropertyNotifier size mismatch");
} // namespace SDK::wh::rpgmodule

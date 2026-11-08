#pragma once

#include "i_action_sets.h"

#include "../../cryengine/action/i_action_map_event_listener.h"
#include "../../cryengine/system/xml_node_ref.h"
#include "../../patterns.h"
#include "../../types.h"

#include <cstddef>
#include <cstdint>
#include <utils/hooking/hooking.h>

namespace SDK::wh::playermodule {
    /**
     * The action help: every hint record, grouped into one set per action map.
     *
     * Owned by `C_PlayerModule`, which allocates 0x90 bytes for it
     * (0x180BE9208) and keeps the `I_ActionSets` base at +0x60. The primary
     * base listens for action-map events: a map being enabled pushes its
     * set's records into `C_ActionHintManager`, and being disabled pulls them
     * out (0x18085B3D8 -> 0x1808E65A0).
     */
    class C_ActionSets final
        : public SDK::IActionMapEventListener
        , public I_ActionSets {
      public:
        C_ActionSets()                                = delete;
        C_ActionSets(const C_ActionSets &)            = delete;
        C_ActionSets &operator=(const C_ActionSets &) = delete;

        /**
         * Reads an `<action_help>` document into the sets, in the format
         * `defaultActionHelp.xml` documents in its own header. A set is keyed
         * by its action map, so a map nobody else names gets a set of its own.
         * Read-only on the document.
         */
        void ParseActionHelp(const SDK::XmlNodeRef &document) {
            hook::this_call<void>(gPatterns.C_ActionSets__ParseActionHelp, this, &document);
        }

      private:
        ~C_ActionSets() override;

        void unk_fn0001() override;

        void LoadDefaultActionHelp() override;
        void RefreshLocalizedActionHelp(std::uint32_t language) override;
        void Opaque03() override;
        void SetActionEnabled(const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, bool enabled, bool includeDerived) override;
        void SetActionVisible(const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, bool visible, bool includeDerived) override;
        void SetActionHint(const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, const SDK::CryStringT<char> &text, bool localize, bool includeDerived) override;
        void Opaque07() override;
        void Opaque08() override;
        void Opaque09() override;
        void Opaque10() override;
        void ClearActionCallbacks(const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, bool includeDerived) override;
        void SetActionHoldDuration(const SDK::CryStringT<char> &setName, const SDK::CryStringT<char> &actionName, float seconds, bool includeDerived) override;
        void Opaque13() override;
        void Opaque14() override;
        void Opaque15() override;
        void Opaque16() override;

        // Constructor 0x180BE9E68.
        std::byte m_actionSets[0x40]; // 0x10; std::unordered_map of the named action sets (load factor 1.0, bucket mask 7/8)
        std::byte m_hashMap0050[0x40]; // 0x50; std::unordered_map (sub_180BE9F38)

        static void AssertLayout() {
            static_assert(offsetof(C_ActionSets, m_actionSets) == 0x10, "C_ActionSets::m_actionSets offset mismatch");
            static_assert(offsetof(C_ActionSets, m_hashMap0050) == 0x50, "C_ActionSets::m_hashMap0050 offset mismatch");
        }
    };

    static_assert(sizeof(C_ActionSets) == 0x90, "C_ActionSets size mismatch");
} // namespace SDK::wh::playermodule

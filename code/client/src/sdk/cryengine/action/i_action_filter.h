#pragma once

#include "../system/cry_name.h"
#include "../system/xml_node_ref.h"

#include <cstdint>

namespace SDK {
    /**
     * A named list of actions the action-map manager refuses to dispatch.
     *
     * `CActionMapManager::ActionFiltered` (+0x1B8) is consulted for every bind
     * before it fires, so an enabled filter silences exactly the actions it
     * lists and leaves the rest of the player's input alone -- which is what
     * an exclusive action map cannot do.
     *
     * `CActionFilter`, vtable 0x183A53840. KCD2's `CreateActionFilter` ignores
     * the type it is handed and always builds an "action fail" filter, so a
     * filter always blocks what it lists.
     */
    struct IActionFilter {
        virtual ~IActionFilter() = default;

        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;

        /** Adds one action to the list. `CActionFilter::Filter` (0x180616ECC). */
        virtual void Filter(const CCryName &action) = 0; // +0x18

        virtual void unk_fn0004() = 0;

        /**
         * Reads the filter out of, or writes it into, an `<actionfilter>` node
         * (0x180616DAC). Loading walks the node's `<action name="..."/>`
         * children and interns each name itself, which is the only way to fill
         * a filter from here: `CCryName` cannot be built outside the engine.
         */
        virtual bool SerializeXML(const XmlNodeRef &node, bool loading) = 0; // +0x28

        virtual void unk_fn0006() = 0;

        /**
         * Adds one reason to hold this filter on (0x180616AAC).
         *
         * Reason-counted rather than a bool, which is where KCD2 parts company
         * with stock CryEngine: the filter keeps a count per reason at +0x08
         * and `Enabled` is true while any is positive. On the edge from
         * nothing-held to held it releases every action it filters, so a held
         * attack is let go rather than left stuck down.
         */
        virtual void Enable(std::uint8_t reason) = 0; // +0x38

        /**
         * Drops one reason, or every reason at once (0x180616B88).
         * `clearAllReasons` zeroes the whole map, which is the right teardown
         * for a filter only one owner ever turns on.
         */
        virtual void Disable(std::uint8_t reason, bool clearAllReasons) = 0; // +0x40

        virtual bool IsEnabledByReason(std::uint8_t reason) = 0; // +0x48

        /** Whether any reason at all is. Consulted first by `ActionFiltered`. */
        virtual bool Enabled() = 0; // +0x50
    };

    static_assert(sizeof(IActionFilter) == 0x8, "IActionFilter size mismatch");
} // namespace SDK

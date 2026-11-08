#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Which event an `SEntityEvent` carries.
     *
     * KCD2's event numbering is its own -- it is neither of the two CryEngine
     * orderings -- so this lists only what the game's own sinks prove, and
     * the spelling describes the observed contract rather than claiming the
     * engine's stripped enumerator name. `C_NPCManager::OnEntityEvent`
     * (0x1805D0D14) registers the NPC on event 43 with a nonzero `nParam[0]`
     * and unregisters it on zero.
     */
    enum EEntityEvent : std::int32_t {
        /**
         * An entity entered or left an area this entity owns; `Param(0)` is
         * the entity that crossed.
         *
         * From one site: `CEntity::SendEvent` (0x1804B6048) gates exactly 20
         * and 21 on the `ProximityTrigger` and `AreaTrigger` classes, which
         * exist for nothing else. Which of the two is the entry follows
         * CryEngine's order rather than a second observation, so a run
         * reporting entries as exits is this pair reversed and nothing else.
         */
        ENTITY_EVENT_ENTERAREA = 20,
        ENTITY_EVENT_LEAVEAREA = 21,

        ENTITY_EVENT_AI_REGISTRATION_CHANGED = 43,
    };

    // KCD2 constructor 0x18047F0A4 initializes 0x40 bytes. The remaining
    // payload differs from newer CryEngine versions and stays opaque.
    struct SEntityEvent {
      public:
        explicit SEntityEvent(EEntityEvent event): m_event(event) {}

        EEntityEvent Event() const {
            return m_event;
        }

        /** One of the event's four payload words. What each means is the event's own business. */
        std::intptr_t Param(std::size_t index) const {
            return index < 4 ? m_nParam[index] : 0;
        }

      private:
        EEntityEvent m_event;
        std::byte m_opaque0004[4] {};
        std::intptr_t m_nParam[4] {};
        std::byte m_opaque0028[24] {};

        static void AssertLayout() {
            static_assert(offsetof(SEntityEvent, m_event) == 0x0);
            static_assert(offsetof(SEntityEvent, m_nParam) == 0x8);
        }
    };

    static_assert(sizeof(SEntityEvent) == 0x40);
} // namespace SDK

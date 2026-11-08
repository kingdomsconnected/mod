#pragma once

#include "../../types.h"
#include "c_blacksmithing_attachment.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::playermodule {
    /**
     * A tool of the smithery the minigame hands the smith. Vtable
     * 0x183A7BAA8; constructor 0x180B67B40 stores the action type from its
     * fourth argument, zeroes the actor and the attached flag, copies the
     * tool's name and resolves the tool's entity by that name.
     */
    class C_BlacksmithingToolAttachment: public SDK::wh::playermodule::C_BlacksmithingAttachment {
      public:
        /** The smith the tool is on; written by `Attach` from `IGameObjectExtension::m_entityId`. */
        SDK::EntityId GetActorEntityId() const {
            return m_actorEntityId;
        }

        bool IsAttached() const {
            return m_attached;
        }

      private:
        std::int32_t m_actionType;        // 0x08
        SDK::EntityId m_actorEntityId;    // 0x0C
        std::uint32_t m_toolEntityId;     // 0x10
        bool m_attached;                  // 0x14
        std::byte m_padding0015[0x3];     // 0x15
        SDK::CryStringT<char> m_toolName; // 0x18
        // The destructor (sub_180B67BE8) detaches through the entity system
        // when +0x24 is non-zero, then zeroes both.
        std::uint32_t m_attachedEntityId; // 0x20; looked up through the entity system (GetInstance()+0x180, slot 3)
        std::uint32_t m_attachmentHandle; // 0x24; handed back to the detach call

        static void AssertLayout() {
            static_assert(offsetof(C_BlacksmithingToolAttachment, m_actionType) == 0x08, "C_BlacksmithingToolAttachment::m_actionType offset mismatch");
            static_assert(offsetof(C_BlacksmithingToolAttachment, m_actorEntityId) == 0x0C, "C_BlacksmithingToolAttachment::m_actorEntityId offset mismatch");
            static_assert(offsetof(C_BlacksmithingToolAttachment, m_toolEntityId) == 0x10, "C_BlacksmithingToolAttachment::m_toolEntityId offset mismatch");
            static_assert(offsetof(C_BlacksmithingToolAttachment, m_attached) == 0x14, "C_BlacksmithingToolAttachment::m_attached offset mismatch");
            static_assert(offsetof(C_BlacksmithingToolAttachment, m_toolName) == 0x18, "C_BlacksmithingToolAttachment::m_toolName offset mismatch");
            static_assert(offsetof(C_BlacksmithingToolAttachment, m_attachedEntityId) == 0x20, "C_BlacksmithingToolAttachment::m_attachedEntityId offset mismatch");
            static_assert(offsetof(C_BlacksmithingToolAttachment, m_attachmentHandle) == 0x24, "C_BlacksmithingToolAttachment::m_attachmentHandle offset mismatch");
        }
    };

    static_assert(sizeof(C_BlacksmithingToolAttachment) == 0x28, "C_BlacksmithingToolAttachment size mismatch");
} // namespace SDK::wh::playermodule

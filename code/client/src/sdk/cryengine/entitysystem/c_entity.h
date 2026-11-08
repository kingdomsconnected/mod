#pragma once

#include "i_entity.h"

#include <cstddef>
#include <cstdint>

namespace SDK {
    /**
     * Concrete CryEntitySystem entity used by spawned props and actors.
     *
     * CEntity's table at 0x184072C78 implements all 153 IEntity entries and
     * appends five concrete operations. Its scalar deleting destructor proves
     * the complete 0xF8-byte size.
     */
    class CEntity : public IEntity {
      public:
        ~CEntity() override = default;

        virtual void unk_fn0153() = 0;
        virtual void unk_fn0154() = 0;
        virtual void unk_fn0155() = 0;
        virtual void unk_fn0156() = 0;
        virtual void unk_fn0157() = 0; // +0x4E8

      private:
        std::uint32_t m_internalFlags; // 0x08
        EntityId m_id;                 // 0x0C
        EntityGUID m_guid;             // 0x10
        std::uint32_t m_flags;         // 0x18
        std::uint32_t m_extendedFlags; // 0x1C
        IEntityClass *m_pClass;        // 0x20
        IEntityArchetype *m_pArchetype; // 0x28
        Vec3 m_position;               // 0x30
        Quat m_rotation;               // 0x3C
        Vec3 m_scale;                  // 0x4C
        float m_worldTransform[12];    // 0x58, Matrix34-compatible storage
        Vec3 m_forwardDirection;       // 0x88
        // From the constructor 0x1803CE394.
        std::uint32_t m_state0094;     // 0x94
        std::uint64_t m_state0098;     // 0x98
        std::uint64_t m_state00A0;     // 0xA0
        std::byte m_state00A8[0x10];   // 0xA8, built by 0x1803CE0A8
        void *m_vector00B8[3];         // 0xB8, zeroed
        std::uint64_t m_state00D0;     // 0xD0
        std::uint64_t m_state00D8;     // 0xD8
        const char *m_name;            // 0xE0
        SDK::CryStringT<char> m_string00E8; // 0xE8, built empty beside the name
        std::uint16_t m_state00F0;     // 0xF0
        std::byte m_state00F2[0x4];    // 0xF2, zeroed
        bool m_ignoreLock;             // 0xF6, SEntitySpawnParams::bIgnoreLock (+0x48)
        bool m_staticEntityId;         // 0xF7, SEntitySpawnParams::bStaticEntityId (+0x49)
    };

    static_assert(sizeof(CEntity) == 0xF8, "CEntity size mismatch");
} // namespace SDK

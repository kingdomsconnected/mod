#pragma once

#include "../../cryengine/entitysystem/i_entity.h"
#include "../entitymodule/c_actor.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::guimodule {
    /**
     * The character page of the pause screen: the stage the inventory and
     * player views frame, and the dummies standing on it.
     *
     * Embedded in `C_UIApse` at +0x2158 by its constructor (0x18146CDF8 is
     * this class's own), table at 0x183ED8FE0. The stage is level content,
     * `Prefabs/ui/Apse.xml`, bound by link name (0x181F48B90); nothing here
     * spawns or moves it, apart from placing the dummies on their anchors.
     *
     * Every show of the page runs `SetupDummies` (slot 25): it registers the
     * local player as the look source of `DummyPlayer` through slot 27, which
     * runs `C_Actor::CopyForInventoryPreview` on the dummy, then stands the
     * dummy on `AnchorPlayer` turned towards the panel. Every hide runs slot
     * 26, which disconnects the mirrors and leaves the dummy as it was.
     *
     * The size is the room up to `C_UIApseCamera` at C_UIApse+0x25C0.
     */
    class C_UIApseCharacter {
      public:
        C_UIApseCharacter()                                     = delete;
        C_UIApseCharacter(const C_UIApseCharacter &)            = delete;
        C_UIApseCharacter &operator=(const C_UIApseCharacter &) = delete;

        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual void unk_fn0003() = 0;
        virtual void unk_fn0004() = 0;
        virtual void unk_fn0005() = 0;
        virtual void unk_fn0006() = 0;
        virtual void unk_fn0007() = 0;
        virtual void unk_fn0008() = 0;
        virtual void unk_fn0009() = 0;
        virtual void unk_fn0010() = 0;
        virtual void unk_fn0011() = 0;
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;

        /** Slot 25 (0x1808EFA98): registers and places every dummy, as a page show does. */
        virtual void SetupDummies() = 0;

        /** Slot 26 (0x1808F070C): disconnects every mirror, as a page hide does. */
        virtual void UnregisterAllLookMirrors() = 0;

        /**
         * Slot 27 (0x1808F052C): makes `target` wear `source` -- one copy of
         * soul and look, then live item mirroring. False when `target` is null.
         */
        virtual bool RegisterLookMirror(entitymodule::C_Actor *source, entitymodule::C_Actor *target) = 0;

        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;

        /**
         * Slot 31 (0x182B00E68): turns the figure on the stage. `delta` is
         * scaled by 200 into degrees and added to the page's yaw, unclamped,
         * and the dummy in `GetDummyPlayer` is set to the panel's rotation
         * turned by it. A horse on the stage is turned instead while the
         * page shows the horse.
         */
        virtual void RotateCharacter(float delta) = 0;

        /** The figure the player view shows: `DummyPlayer`, an `InventoryDummyPlayer` from the prefab. */
        entitymodule::C_Actor *GetDummyPlayer() const {
            return m_pDummyPlayer;
        }

        /**
         * Puts another actor in the figure's place. Read by the next show's
         * `SetupDummies` and by `RotateCharacter`; the prefab's own dummy is
         * what the page expects back before it is next shown.
         */
        void SetDummyPlayer(entitymodule::C_Actor *actor) {
            m_pDummyPlayer = actor;
        }

        /** The degrees `RotateCharacter` has added since the last show reset them to zero. */
        float GetCharacterYaw() const {
            return m_characterYaw;
        }

        /** `CharacterPanelEntity`: the dummy faces relative to its rotation. */
        IEntity *GetCharacterPanelEntity() const {
            return m_pCharacterPanelEntity;
        }

      protected:
        // Slot 0 is the deleting destructor; nothing here ever destroys one.
        ~C_UIApseCharacter() = default;

      private:
        // +0x00..+0x3F8 is the C_UICharacterBase layer (sub_180BC71D4), the
        // rest the constructor 0x18146CDF8.
        void *m_ptr0008;                       // 0x08; ctor 0
        const void *m_pVTables0010[2];         // 0x10; the other two tables
        entitymodule::C_Actor *m_pDummyPlayer; // 0x20, link "DummyPlayer"
        std::byte m_object0028[0xC8];          // 0x28; sub_180BC6F70(this + 0x28, this + 0x1C8)
        void *m_pOwned00F0;                    // 0xF0; owning, set from sub_18146D050(this, this + 0x1C8), released through slot 0
        float m_characterYaw; // 0xF8, zeroed by SetupPlayerDummy 0x1808EE70C
        float m_matrix00FC[3][4];              // 0xFC; a Matrix34 built by sub_180A70610 (zero)
        std::uint32_t m_values012C[2];         // 0x12C; ctor writes a zero qword
        std::uint32_t m_value0134;             // 0x134; ctor 0
        float m_value0138;                     // 0x138; ctor 1.0f
        std::uint32_t m_value013C;             // 0x13C; ctor 0
        void *m_ptr0140;                       // 0x140; ctor 0
        float m_value0148;                     // 0x148; ctor 1.0f
        bool m_flag014C;                       // 0x14C; ctor 0
        std::byte m_padding014D[0x3];
        float m_matrix0150[3][4];              // 0x150; a Matrix34 built by sub_180A70610 (scale 1.0)
        void *m_ptr0180;                       // 0x180; ctor 0
        std::uint32_t m_value0188;             // 0x188; ctor 0
        std::uint32_t m_values018C[2];         // 0x18C; ctor writes a qword
        std::uint32_t m_values0194[2];         // 0x194; ctor writes a zero qword
        std::uint32_t m_value019C;             // 0x19C
        void *m_ptr01A0;                       // 0x1A0; ctor 0
        IEntity *m_pCharacterPanelEntity; // 0x1A8, link "CharacterPanelEntity"
        void *m_ptrs01B0[2];                   // 0x1B0; ctor 0
        bool m_flag01C0;                       // 0x1C0; ctor 1
        std::byte m_padding01C1[0x7];
        std::byte m_object01C8[0x200];         // 0x1C8; sub_180BC7EA0
        std::uint16_t m_value03C8;             // 0x3C8; ctor 0
        std::uint8_t m_value03CA;              // 0x3CA; ctor 0
        std::byte m_padding03CB[0x1];
        std::uint32_t m_values03CC[2];         // 0x3CC; ctor writes a zero qword
        std::uint32_t m_value03D4;             // 0x3D4; ctor 0
        std::byte m_map03D8[0x10];             // 0x3D8; std::map {head, size}
        void *m_ptrs03E8[2];                   // 0x3E8; ctor 0
        std::uint32_t m_value03F8;             // 0x3F8; ctor 0
        std::byte m_padding03FC[0x4];
        void *m_ptrs0400[3];                   // 0x400; ctor 0
        std::byte m_map0418[0x10];             // 0x418; std::map {head, size}
        bool m_flag0428;                       // 0x428; ctor 1
        std::byte m_padding0429[0x7];
        void *m_pApse;                         // 0x430; the owning C_UIApse
        void *m_ptrs0438[4];                   // 0x438; ctor 0
        std::uint64_t m_value0458;             // 0x458; ctor copies qword_185326308
        bool m_flag0460;                       // 0x460; ctor 0
        std::byte m_padding0461[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_UIApseCharacter, m_pDummyPlayer) == 0x20, "C_UIApseCharacter::m_pDummyPlayer offset mismatch");
            static_assert(offsetof(C_UIApseCharacter, m_characterYaw) == 0xF8, "C_UIApseCharacter::m_characterYaw offset mismatch");
            static_assert(offsetof(C_UIApseCharacter, m_pCharacterPanelEntity) == 0x1A8, "C_UIApseCharacter::m_pCharacterPanelEntity offset mismatch");
            static_assert(offsetof(C_UIApseCharacter, m_object0028) == 0x28, "C_UIApseCharacter::m_object0028 offset mismatch");
            static_assert(offsetof(C_UIApseCharacter, m_matrix00FC) == 0xFC, "C_UIApseCharacter::m_matrix00FC offset mismatch");
            static_assert(offsetof(C_UIApseCharacter, m_matrix0150) == 0x150, "C_UIApseCharacter::m_matrix0150 offset mismatch");
            static_assert(offsetof(C_UIApseCharacter, m_values018C) == 0x18C, "C_UIApseCharacter::m_values018C offset mismatch");
            static_assert(offsetof(C_UIApseCharacter, m_object01C8) == 0x1C8, "C_UIApseCharacter::m_object01C8 offset mismatch");
            static_assert(offsetof(C_UIApseCharacter, m_values03CC) == 0x3CC, "C_UIApseCharacter::m_values03CC offset mismatch");
            static_assert(offsetof(C_UIApseCharacter, m_value03F8) == 0x3F8, "C_UIApseCharacter::m_value03F8 offset mismatch");
            static_assert(offsetof(C_UIApseCharacter, m_pApse) == 0x430, "C_UIApseCharacter::m_pApse offset mismatch");
            static_assert(offsetof(C_UIApseCharacter, m_value0458) == 0x458, "C_UIApseCharacter::m_value0458 offset mismatch");
        }
    };

    static_assert(sizeof(C_UIApseCharacter) == 0x468, "C_UIApseCharacter size mismatch");
} // namespace SDK::wh::guimodule

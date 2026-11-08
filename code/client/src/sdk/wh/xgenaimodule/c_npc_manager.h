#pragma once

#include "../../cryengine/entitysystem/i_entity.h"
#include "../../cryengine/entitysystem/i_entity_system_sink.h"
#include "../../types.h"
#include "../game/i_entity_side_effect_callback.h"
#include "../i_xgen_ai_npc_manager.h"
#include "c_ai_object.h"
#include "i_debug_draw.h"

#include <cstdint>

namespace SDK::wh::xgenaimodule {
    /**
     * An NPC: the AI-side half of an actor.
     *
     * Every entity that passes `C_NPCManager::OnSpawn`'s layer test gets one,
     * a KCDC-spawned body included. What it does *not* necessarily get is a
     * brain worth having: `C_NPCManager`'s record initialisation
     * (`0x180D48678`) reads the soul's brain GUID, looks a template up by it,
     * and falls back to a bare `Default` brain when the lookup misses.
     *
     * Its bases run down to `C_AIObject` at offset 0 (RTTI 0x184566B30:
     * `I_NPC`, `C_IntelligentObject`, `C_MessageCapableObject`,
     * `C_LinkableObject`, `C_AIObject`, every one at mdisp 0), so the WUID
     * it is known by is `C_AIObject`'s: the AI-object, linkable and
     * message-capable managers' ctors (0x18047BDFC, 0x18047BD70, 0x180A2BD74)
     * each register the one stored at +0x08. A spawned body has no
     * EntityGUID, so this is the only place its WUID can be read from.
     *
     * Slot 0 is the scalar deleting destructor, `C_AIObject`'s; of the rest
     * only the brain setter is typed, because it is the only slot the mod
     * calls. Its index is proven at `0x180D487D9`: `mov rcx, rbx /
     * call [rax+0A8h]` with the brain in RDX, and `0xA8 / 8` is 21.
     */
    class C_NPC: public C_AIObject {
      public:
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

        /** Replaces this NPC's brain with `brain`. Slot 21, `+0xA8`. */
        virtual void SetBrain(void *brain) = 0;
    };

    /**
     * The NPC registry.
     *
     * `GetNPCByEntityId` is slot 4 of its primary table at `0x183FF1968`,
     * where the surviving symbol proves both the index and the by-reference
     * entity id.
     */
    class I_NPCManager {
      public:
        virtual C_NPC *GetLocalPlayerNPC() const = 0;
        virtual void unk_fn0001()                = 0;
        virtual void unk_fn0002()                = 0;
        virtual void unk_fn0003()                = 0;

        /** The NPC behind an entity, or nullptr when it has none. */
        virtual C_NPC *GetNPCByEntityId(const EntityId &entityId) = 0;
    };
    static_assert(sizeof(I_NPCManager) == 0x8, "I_NPCManager size mismatch");

    class C_NPCManager
        : public I_NPCManager
        , public SDK::IEntitySystemSink
        , public I_DebugDraw
        , public SDK::wh::game::I_EntitySideEffectCallback
        , public SDK::wh::I_XGenAINPCManager {
      public:
        struct LocalPlayerBinding {
            SDK::IEntity *entity;
            C_NPC *npc;
            C_NPC *cachedNPC;
        };
        LocalPlayerBinding CaptureLocalPlayerBinding() const;
        void RestoreLocalPlayerBinding(const LocalPlayerBinding &binding);

        /** The process-wide registry, or nullptr before the AI module is up. */
        static C_NPCManager *GetInstance();

      private:
        // Typed from the ctor 0x180CA0614. Embedded objects are sized by the next written offset.
        std::uint8_t m_flag0028;                 // 0x28, ctor sets 1
        std::byte m_unset0029[0x7];              // 0x29, not written by the ctor
        SDK::StdVector<void *> m_vector0030;     // 0x30, ctor zeroes; its address is stored at +0x630
        std::uint64_t m_state0048[3];            // 0x48, ctor zeroes
        SDK::IEntity *m_pLocalPlayerEntity; // 0x60
        C_NPC *m_pLocalPlayerNPC;           // 0x68
        std::uint64_t m_state0070[11];           // 0x70, ctor zeroes
        std::uint16_t m_state00C8;               // 0xC8, ctor zeroes
        std::uint8_t m_flag00CA;                 // 0xCA, ctor zeroes
        std::byte m_unset00CB[0x5];              // 0xCB, not written by the ctor
        std::uint64_t m_state00D0[3];            // 0xD0, ctor zeroes
        std::uint8_t m_flag00E8;                 // 0xE8, ctor zeroes
        std::byte m_unset00E9[0x3];              // 0xE9, not written by the ctor
        std::byte m_state00EC[0x8];              // 0xEC, ctor zeroes as one unaligned qword
        std::uint32_t m_state00F4;               // 0xF4, ctor zeroes
        std::byte m_object00F8[0x40];            // 0xF8, built by 0x181851D04
        std::uint64_t m_state0138;               // 0x138, ctor zeroes
        std::uint8_t m_flag0140;                 // 0x140, ctor zeroes
        std::byte m_unset0141[0x7];              // 0x141, not written by the ctor
        std::byte m_object0148[0x160];           // 0x148, built by 0x18209B590
        std::byte m_object02A8[0x2B8];           // 0x2A8, built by 0x1819AA0D8; the ctor hands it a std::function bound to this manager
        std::byte m_object0560[0xD0];            // 0x560, built by 0x1820AFEE0 over +0x148
        void *m_pVector0630;                     // 0x630, ctor sets this+0x30
        std::uint64_t m_state0638;               // 0x638, ctor zeroes
        void *m_pSelf0640;                       // 0x640, ctor sets this+0x630
        std::byte m_unset0648[0x8];              // 0x648, not written by the ctor
        std::byte m_object0650[0x20];            // 0x650, built by 0x1819CA018 with this manager
        std::byte m_lodBudget0670[0x30];         // 0x670, seeded from that call's result; wh_ai_LOD_Hide is bound at +0x688
        std::byte m_tree06A0[0x10];              // 0x6A0, std::map
        std::byte m_object06B0[0x10];            // 0x6B0, built by 0x1819C6B3C
        std::byte m_object06C0[0x10];            // 0x6C0, built by 0x180CA0D30
        std::byte m_object06D0[0x10];            // 0x6D0, built by 0x180CA0D30
        std::byte m_object06E0[0x10];            // 0x6E0, built by 0x1819C6B20
        std::int32_t m_npcLookTarget;            // 0x6F0, wh_ai_NPCLookTarget, default 1
        std::int32_t m_npcDryUpdateMode;         // 0x6F4, wh_ai_NPCDryUpdateMode, default 2
        std::uint64_t m_state06F8;               // 0x6F8, ctor zeroes
        std::uint8_t m_flag0700;                 // 0x700, ctor sets 1
        std::byte m_unset0701[0x7];              // 0x701, not written by the ctor
        std::byte m_object0708[0x40];            // 0x708, built by 0x1806B5C70

        static void AssertLayout() {
            static_assert(offsetof(C_NPCManager, m_pLocalPlayerEntity) == 0x60, "C_NPCManager::m_pLocalPlayerEntity offset mismatch");
            static_assert(offsetof(C_NPCManager, m_pLocalPlayerNPC) == 0x68, "C_NPCManager::m_pLocalPlayerNPC offset mismatch");
            static_assert(offsetof(C_NPCManager, m_flag0028) == 0x28, "C_NPCManager::m_flag0028 offset mismatch");
            static_assert(offsetof(C_NPCManager, m_object0148) == 0x148, "C_NPCManager::m_object0148 offset mismatch");
            static_assert(offsetof(C_NPCManager, m_pVector0630) == 0x630, "C_NPCManager::m_pVector0630 offset mismatch");
            static_assert(offsetof(C_NPCManager, m_npcLookTarget) == 0x6F0, "C_NPCManager::m_npcLookTarget offset mismatch");
            static_assert(offsetof(C_NPCManager, m_object0708) == 0x708, "C_NPCManager::m_object0708 offset mismatch");
        }
    };
    static_assert(sizeof(C_NPCManager) == 0x748, "C_NPCManager size mismatch");

    /**
     * Builds the brain a template GUID names, for one NPC.
     *
     * The same call `C_NPCManager`'s record initialisation makes: it looks the
     * template up in the brain registry and walks its subbrain list through
     * `CreateSubbrainByName`. Returns nullptr when the GUID names no template.
     *
     * This is how a dog gets a mind. A soul whose brain is `animal_basic` --
     * which is every generic dog, and what a KCDC-spawned body falls back to
     * -- produces no `DogCompanion` subbrain and therefore no companion at
     * all, so the animal has nothing to decide with.
     */
    void *BuildBrainForGuid(const CryGUID &brain, C_NPC *npc);

    /**
     * Builds the brain the engine gives an NPC that is not meant to think.
     *
     * The same one record initialisation installs when a soul's brain GUID
     * names no template: the lookup falls back to the default template and the
     * factory answers a brain carrying a single `C_EmptySUBB`, whose table is
     * `return 0` stubs. Asked for with a zero GUID, which is the value an unset
     * brain GUID has and the miss that fallback exists for.
     */
    void *BuildEmptyBrain(C_NPC *npc);
} // namespace SDK::wh::xgenaimodule

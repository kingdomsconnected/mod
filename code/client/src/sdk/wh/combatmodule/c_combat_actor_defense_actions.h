#pragma once

#include "c_combat_action_defense_databases.h"
#include "c_combat_actor.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

// The actions a fight produces besides the swing itself, typed as far as it
// takes to recognise one in RunAction and to build the same one again from
// its rows. Each allocator hands back one reference, exactly like the attack
// factory; RunAction takes its own, and the builder drops the first.
namespace SDK::wh::combatmodule {
    /** Whether `action` was constructed with the primary vtable at `vtable`. */
    inline bool HasPrimaryVTable(const I_CombatActorAction *action, std::uint64_t vtable) {
        return action != nullptr && vtable != 0 && *reinterpret_cast<const std::uint64_t *>(action) == vtable;
    }

    /**
     * A plain perfect block, 0xF0 bytes (AllocatePerfectBlockAction,
     * 0x18164F0E4).
     *
     * `CreatePerfectBlockWithRiposte` (0x18164ED68) builds it with its riposte
     * already chosen: the perfect-block row at +0x70 and, at +0x78, the attack
     * row of the first candidate an attack query keyed on that row's input
     * class and zone returned. Only the row pointer is stored (a `movq` at
     * 0x18164EFAB); the candidate's first flag is folded into +0x58 and the
     * second dropped. The action's own animation plays the +0x78 row, so the
     * riposte travels with the block.
     */
    class C_CombatActorActionPerfectBlock: public C_CombatFrameworkAction {
      public:
        static bool IsInstance(const I_CombatActorAction *action) {
            return HasPrimaryVTable(action, gPatterns.C_CombatActorActionPerfectBlock__VTable);
        }

        /** A fresh action owned by `owner`, carrying one reference, or null. */
        static C_CombatActorActionPerfectBlock *Allocate(C_CombatActor *owner) {
            C_CombatActorActionPerfectBlock *action = nullptr;
            hook::call<void>(gPatterns.C_CombatActorActionPerfectBlock__Allocate, &action, owner);
            return action;
        }

        const S_CombatActionPerfectBlockDBData *GetPerfectBlockRow() const {
            return m_pPerfectBlockRow;
        }

        /** The attack row the block's own animation plays as its riposte. */
        const S_CombatActionAttackDataDBData *GetRiposteRow() const {
            return m_pRiposteRow;
        }

        /** Set when either the riposte query's candidate or the perfect-block query flagged itself. */
        std::uint8_t GetCandidateFlag() const {
            return m_candidateFlag0058;
        }

        /**
         * Fills the action the way CreatePerfectBlockWithRiposte does and ends
         * with the time alignment every perfect-block build ends with.
         * `perfectBlockFactory` is the owner's (action manager +0x60). The
         * alignment reads the owner's opponent without checking it, so the
         * owner must have one.
         */
        void Prepare(void *perfectBlockFactory, const S_CombatActionPerfectBlockDBData *row, const S_CombatActionAttackDataDBData *riposteRow, std::uint8_t candidateFlag) {
            m_pPerfectBlockRow  = row;
            m_pRiposteRow       = riposteRow;
            m_candidateFlag0058 = candidateFlag;
            hook::this_call<void>(gPatterns.C_CombatActionPerfectBlockFactory__ApplyAlignment, perfectBlockFactory, &m_params0050);
        }

      private:
        // +0x50..+0x80 is S_CombatActorActionPerfectBlockParams: 0x18164F43C
        // installs its table at +0x50, +0x60 and +0x68 and clears +0x58,
        // then 0x181A73BA0 fills the rest.
        std::byte m_params0050[0x8];                                // 0x50, the params the alignment is handed; it clears +0x09 when it refuses
        std::uint8_t m_candidateFlag0058;                           // 0x58
        std::byte m_padding0059[0x7];                               // 0x59
        const void *m_pParamsVTable0060;                            // 0x60, S_CombatActorActionPerfectBlockParams
        const void *m_pParamsVTable0068;                            // 0x68, S_CombatActorActionPerfectBlockParams
        const S_CombatActionPerfectBlockDBData *m_pPerfectBlockRow; // 0x70
        const S_CombatActionAttackDataDBData *m_pRiposteRow;        // 0x78
        // C_CombatActorAction's C_CombatActorObject base (abstract, so held
        // as bytes): its table, then the owning C_CombatActor.
        std::byte m_combatActorObject[sizeof(C_CombatActorObject)]; // 0x80
        std::uint16_t m_value0090;                                  // 0x90, 0 by default
        std::byte m_padding0092[0x2];                               // 0x92
        std::uint32_t m_value0094;                                  // 0x94, 0 by default
        void *m_values0098[2];                                      // 0x98, two 8-byte slots built by 0x180917E58
        const void *m_pBlockListenerVTable;                         // 0xA8, handed to C_CombatActionHelperBlock
        const void *m_pAttackListenerVTable;                        // 0xB0, handed to C_CombatActionHelperAttack
        std::uint32_t m_value00B8;                                  // 0xB8, 0 by default
        std::uint8_t m_flag00BC;                                    // 0xBC, false by default
        std::byte m_padding00BD[0x3];                               // 0xBD
        std::uint32_t m_value00C0;                                  // 0xC0, 0 by default
        std::byte m_padding00C4[0x4];                               // 0xC4
        void *m_pBlockHelper;                                       // 0xC8, C_CombatActionHelperBlock, 0x38 bytes
        void *m_pAttackHelper;                                      // 0xD0, C_CombatActionHelperAttack, 0x50 bytes
        void *m_pActiveHelper;                                      // 0xD8, the block helper at construction
        std::uint32_t m_value00E0;                                  // 0xE0, 0 by default
        std::byte m_padding00E4[0x4];                               // 0xE4
        std::uint16_t m_value00E8;                                  // 0xE8, 0 by default
        std::uint8_t m_flag00EA;                                    // 0xEA, false by default
        std::byte m_padding00EB[0x5];                               // 0xEB

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorActionPerfectBlock, m_combatActorObject) == 0x80, "C_CombatActorActionPerfectBlock::m_combatActorObject offset mismatch");
            static_assert(offsetof(C_CombatActorActionPerfectBlock, m_values0098) == 0x98, "C_CombatActorActionPerfectBlock::m_values0098 offset mismatch");
            static_assert(offsetof(C_CombatActorActionPerfectBlock, m_pBlockListenerVTable) == 0xA8, "C_CombatActorActionPerfectBlock::m_pBlockListenerVTable offset mismatch");
            static_assert(offsetof(C_CombatActorActionPerfectBlock, m_pBlockHelper) == 0xC8, "C_CombatActorActionPerfectBlock::m_pBlockHelper offset mismatch");
            static_assert(offsetof(C_CombatActorActionPerfectBlock, m_pActiveHelper) == 0xD8, "C_CombatActorActionPerfectBlock::m_pActiveHelper offset mismatch");
            static_assert(offsetof(C_CombatActorActionPerfectBlock, m_value00E8) == 0xE8, "C_CombatActorActionPerfectBlock::m_value00E8 offset mismatch");
            static_assert(offsetof(C_CombatActorActionPerfectBlock, m_params0050) == 0x50, "C_CombatActorActionPerfectBlock::m_params0050 offset mismatch");
            static_assert(offsetof(C_CombatActorActionPerfectBlock, m_candidateFlag0058) == 0x58, "C_CombatActorActionPerfectBlock::m_candidateFlag0058 offset mismatch");
            static_assert(offsetof(C_CombatActorActionPerfectBlock, m_pPerfectBlockRow) == 0x70, "C_CombatActorActionPerfectBlock::m_pPerfectBlockRow offset mismatch");
            static_assert(offsetof(C_CombatActorActionPerfectBlock, m_pRiposteRow) == 0x78, "C_CombatActorActionPerfectBlock::m_pRiposteRow offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorActionPerfectBlock) == 0xF0, "C_CombatActorActionPerfectBlock size mismatch");

    /**
     * A sync perfect block or a master strike -- the engine builds both as
     * this one class, 0xF8 bytes (AllocateSyncPerfectBlockAction,
     * 0x1814840F4), through `CreateQueriedPerfectBlockAction` (0x181483B04).
     *
     * The perfect-block row sits at +0x70 and the `CombatDb.SyncPerfectBlockHit`
     * row of the partner half at +0x78. On entry the action builds that
     * partner from +0x78 and runs it on its owner's opponent
     * (`StartOpponentSyncPerfectBlockAction`, 0x1810F3030), so only this half
     * is ever relayed: whoever replays it must already have the attacker as
     * the replaying body's opponent.
     */
    class C_CombatActorActionSyncPerfectBlock: public C_CombatFrameworkAction {
      public:
        /** What the lethal dry run upgrades a sync defense to (+0x94). */
        static constexpr std::uint32_t kLethalVariant = 4;

        static bool IsInstance(const I_CombatActorAction *action) {
            return HasPrimaryVTable(action, gPatterns.C_CombatActorActionSyncPerfectBlock__VTable);
        }

        static C_CombatActorActionSyncPerfectBlock *Allocate(C_CombatActor *owner) {
            C_CombatActorActionSyncPerfectBlock *action = nullptr;
            hook::call<void>(gPatterns.C_CombatActorActionSyncPerfectBlock__Allocate, &action, owner);
            return action;
        }

        const S_CombatActionPerfectBlockDBData *GetPerfectBlockRow() const {
            return m_pPerfectBlockRow;
        }

        const S_CombatActionSyncPerfectBlockHitDBData *GetPartnerRow() const {
            return m_pPartnerRow;
        }

        bool IsLethal() const {
            return m_variant0094 == kLethalVariant;
        }

        /** Fills the action exactly as CreateQueriedPerfectBlockAction's tail does. */
        void Prepare(void *perfectBlockFactory, const S_CombatActionPerfectBlockDBData *row, const S_CombatActionSyncPerfectBlockHitDBData *partnerRow, bool lethal) {
            constexpr std::uint32_t kQueriedState = 0xA;
            m_pPerfectBlockRow                    = row;
            m_pPartnerRow                         = partnerRow;
            SetState(kQueriedState);
            if (lethal) {
                m_variant0094 = kLethalVariant;
            }
            hook::this_call<void>(gPatterns.C_CombatActionPerfectBlockFactory__ApplyAlignment, perfectBlockFactory, &m_params0050);
        }

      private:
        // +0x50..+0x80 is S_CombatActorActionSyncPerfectBlockParams, built by
        // 0x1814845AC exactly as the perfect block's params are.
        std::byte m_params0050[0x20];                                 // 0x50, the params the alignment is handed
        const S_CombatActionPerfectBlockDBData *m_pPerfectBlockRow;   // 0x70
        const S_CombatActionSyncPerfectBlockHitDBData *m_pPartnerRow; // 0x78
        std::byte m_combatActorObject[sizeof(C_CombatActorObject)];   // 0x80, C_CombatActorAction's abstract base: table, owner
        std::uint16_t m_value0090;                                    // 0x90, 0 by default
        std::byte m_padding0092[0x2];                                 // 0x92
        std::uint32_t m_variant0094;                                  // 0x94, 0 by default
        void *m_value0098;                                            // 0x98, one 8-byte slot built by 0x18090D378
        const void *m_pBlockListenerVTable;                           // 0xA0, handed to C_CombatActionHelperBlock
        const void *m_pAttackListenerVTable;                          // 0xA8, handed to C_CombatActionHelperAttack
        std::uint32_t m_value00B0;                                    // 0xB0, 0 by default
        std::uint8_t m_flag00B4;                                      // 0xB4, false by default
        std::byte m_padding00B5[0x3];                                 // 0xB5
        std::uint32_t m_value00B8;                                    // 0xB8, 0 by default
        std::byte m_padding00BC[0x4];                                 // 0xBC
        void *m_pBlockHelper;                                         // 0xC0, C_CombatActionHelperBlock, 0x38 bytes
        void *m_pAttackHelper;                                        // 0xC8, C_CombatActionHelperAttack, 0x50 bytes
        void *m_pActiveHelper;                                        // 0xD0, the block helper at construction
        std::uint32_t m_value00D8;                                    // 0xD8, 0 by default
        std::byte m_padding00DC[0x4];                                 // 0xDC
        std::uint16_t m_value00E0;                                    // 0xE0, 0 by default
        std::uint8_t m_flag00E2;                                      // 0xE2, false by default
        std::byte m_padding00E3[0x5];                                 // 0xE3
        void *m_pHelper00E8;                                          // 0xE8, a 0x18-byte C_CombatActorObject built by 0x180914FA4
        std::uint64_t m_value00F0;                                    // 0xF0, 0 by default

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorActionSyncPerfectBlock, m_combatActorObject) == 0x80, "C_CombatActorActionSyncPerfectBlock::m_combatActorObject offset mismatch");
            static_assert(offsetof(C_CombatActorActionSyncPerfectBlock, m_pBlockListenerVTable) == 0xA0, "C_CombatActorActionSyncPerfectBlock::m_pBlockListenerVTable offset mismatch");
            static_assert(offsetof(C_CombatActorActionSyncPerfectBlock, m_pBlockHelper) == 0xC0, "C_CombatActorActionSyncPerfectBlock::m_pBlockHelper offset mismatch");
            static_assert(offsetof(C_CombatActorActionSyncPerfectBlock, m_pHelper00E8) == 0xE8, "C_CombatActorActionSyncPerfectBlock::m_pHelper00E8 offset mismatch");
            static_assert(offsetof(C_CombatActorActionSyncPerfectBlock, m_params0050) == 0x50, "C_CombatActorActionSyncPerfectBlock::m_params0050 offset mismatch");
            static_assert(offsetof(C_CombatActorActionSyncPerfectBlock, m_pPerfectBlockRow) == 0x70, "C_CombatActorActionSyncPerfectBlock::m_pPerfectBlockRow offset mismatch");
            static_assert(offsetof(C_CombatActorActionSyncPerfectBlock, m_pPartnerRow) == 0x78, "C_CombatActorActionSyncPerfectBlock::m_pPartnerRow offset mismatch");
            static_assert(offsetof(C_CombatActorActionSyncPerfectBlock, m_variant0094) == 0x94, "C_CombatActorActionSyncPerfectBlock::m_variant0094 offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorActionSyncPerfectBlock) == 0xF8, "C_CombatActorActionSyncPerfectBlock size mismatch");

    /**
     * The partner half of a sync perfect block, run on the attacker by the
     * defender's action. Only ever recognised, never built here: it is the
     * engine's own consequence of the half above.
     */
    class C_CombatActorActionSyncPerfectBlockHit {
      public:
        static bool IsInstance(const I_CombatActorAction *action) {
            return HasPrimaryVTable(action, gPatterns.C_CombatActorActionSyncPerfectBlockHit__VTable);
        }
    };

    /**
     * Whether an action is one half of a move paired with another body: a
     * sync attack (takedown, finisher) and the sync hit it runs on its
     * victim, the transition into one, or a sync perfect block and its
     * partner half. Cutting one short here leaves its partner playing alone.
     */
    inline bool IsSyncAction(const I_CombatActorAction *action) {
        return HasPrimaryVTable(action, gPatterns.C_CombatActorActionSyncAttack__VTable) || HasPrimaryVTable(action, gPatterns.C_CombatActorActionSyncHit__VTable) || HasPrimaryVTable(action, gPatterns.C_CombatActorActionSyncTransition__VTable)
            || C_CombatActorActionSyncPerfectBlock::IsInstance(action) || C_CombatActorActionSyncPerfectBlockHit::IsInstance(action);
    }

    /**
     * An attacker's recoil from a blow that was blocked or parried, 0x78
     * bytes (AllocateFailedAttackAction, 0x182749BA0).
     *
     * `CreateFailedAttackAction` (0x180D53B74) queries `CombatDb.FailedAttack`
     * on the attacker's opponent's state -- a perfect block in progress, or a
     * parry in place -- and stores the row at +0x50. The attacker's own
     * collision process runs it on the attacker.
     */
    class C_CombatActorActionFailedAttack: public C_CombatFrameworkAction {
      public:
        static bool IsInstance(const I_CombatActorAction *action) {
            return HasPrimaryVTable(action, gPatterns.C_CombatActorActionFailedAttack__VTable);
        }

        static C_CombatActorActionFailedAttack *Allocate(C_CombatActor *owner) {
            C_CombatActorActionFailedAttack *action = nullptr;
            hook::call<void>(gPatterns.C_CombatActorActionFailedAttack__Allocate, &action, owner);
            return action;
        }

        const S_CombatActionFailedAttackDBData *GetRow() const {
            return m_pRow;
        }

        void Prepare(const S_CombatActionFailedAttackDBData *row) {
            m_pRow = row;
        }

      private:
        const S_CombatActionFailedAttackDBData *m_pRow;              // 0x50, S_CombatActorActionFailedAttackParams
        std::byte m_combatActorObject[sizeof(C_CombatActorObject)];  // 0x58, C_CombatActorAction's abstract base: table, owner (0x182753B34)
        std::uint16_t m_value0068;                                   // 0x68, 0 by default
        std::byte m_padding006A[0x2];                                // 0x6A
        std::uint32_t m_value006C;                                   // 0x6C, 0 by default
        void *m_value0070;                                           // 0x70, one 8-byte slot built by 0x18090D378

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorActionFailedAttack, m_combatActorObject) == 0x58, "C_CombatActorActionFailedAttack::m_combatActorObject offset mismatch");
            static_assert(offsetof(C_CombatActorActionFailedAttack, m_value0070) == 0x70, "C_CombatActorActionFailedAttack::m_value0070 offset mismatch");
            static_assert(offsetof(C_CombatActorActionFailedAttack, m_pRow) == 0x50, "C_CombatActorActionFailedAttack::m_pRow offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorActionFailedAttack) == 0x78, "C_CombatActorActionFailedAttack size mismatch");

    /**
     * A victim's reaction to a blow, 0xD8 bytes (AllocateHitAction,
     * 0x180D49C28).
     *
     * `CreateHitAction` (0x180D49AB4) queries `CombatDb.Hit` on the victim's
     * manager and stores the row at +0x50, the attacking C_CombatActor at
     * +0x58 and two bits of the hit record in the byte at +0x60; when the row
     * asks for it, it then calls slot 19 with 2.
     */
    class C_CombatActorActionHit: public C_CombatFrameworkAction {
      public:
        // Slots 15 and 16 are I_Action's GetState and GetSlot.
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;

        /** Slot 19 (0x181A72920): writes its argument to +0x3C. */
        virtual void SetField003C(std::uint32_t value) = 0;

        static bool IsInstance(const I_CombatActorAction *action) {
            return HasPrimaryVTable(action, gPatterns.C_CombatActorActionHit__VTable);
        }

        static C_CombatActorActionHit *Allocate(C_CombatActor *victim) {
            C_CombatActorActionHit *action = nullptr;
            hook::call<void>(gPatterns.C_CombatActorActionHit__Allocate, &action, victim);
            return action;
        }

        const S_CombatActionHitDBData *GetRow() const {
            return m_pRow;
        }

        C_CombatActor *GetAttacker() const {
            return m_pAttacker;
        }

        /** Bit 0 and bit 1: the hit record's +0x40 and +0x41. */
        std::uint8_t GetRecordFlags() const {
            return m_recordFlags0060 & 0x3;
        }

        /** Fills the action exactly as CreateHitAction does. */
        void Prepare(const S_CombatActionHitDBData *row, C_CombatActor *attacker, std::uint8_t recordFlags) {
            constexpr std::uint32_t kRowRequestedField003C = 2;
            m_pRow                                         = row;
            m_pAttacker                                    = attacker;
            m_recordFlags0060                              = static_cast<std::uint8_t>((m_recordFlags0060 & ~0x3) | (recordFlags & 0x3));
            if (row != nullptr && row->SetsField003C()) {
                SetField003C(kRowRequestedField003C);
            }
        }

      private:
        const S_CombatActionHitDBData *m_pRow;                      // 0x50, S_CombatActorActionHitParams from here
        C_CombatActor *m_pAttacker;                                 // 0x58
        std::uint8_t m_recordFlags0060;                             // 0x60, low two bits cleared by 0x180D49CF4
        std::byte m_padding0061[0x7];                               // 0x61
        std::byte m_combatActorObject[sizeof(C_CombatActorObject)]; // 0x68, C_CombatActorAction's abstract base: table, owner
        std::uint16_t m_value0078;                                  // 0x78, 0 by default
        std::byte m_padding007A[0x2];                               // 0x7A
        std::uint32_t m_value007C;                                  // 0x7C, 0 by default
        void *m_values0080[2];                                      // 0x80, two 8-byte slots built by 0x180917E58
        // C_CombatActionHelperHit, embedded by 0x180D49D94 (constructor
        // 0x180D4A034): a C_CombatActorObject, then u16 +0x10, u32 +0x14, u8
        // +0x18, the "never" timestamp -100000 at +0x20 and u8 +0x28.
        std::byte m_helperHit[0x30];                                // 0x90
        std::uint64_t m_value00C0;                                  // 0xC0, 0 by default
        std::uint16_t m_value00C8;                                  // 0xC8, 0 by default
        std::byte m_padding00CA[0x6];                               // 0xCA
        std::int64_t m_timestamp00D0;                               // 0xD0, -100000 (never) by default

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorActionHit, m_combatActorObject) == 0x68, "C_CombatActorActionHit::m_combatActorObject offset mismatch");
            static_assert(offsetof(C_CombatActorActionHit, m_values0080) == 0x80, "C_CombatActorActionHit::m_values0080 offset mismatch");
            static_assert(offsetof(C_CombatActorActionHit, m_helperHit) == 0x90, "C_CombatActorActionHit::m_helperHit offset mismatch");
            static_assert(offsetof(C_CombatActorActionHit, m_timestamp00D0) == 0xD0, "C_CombatActorActionHit::m_timestamp00D0 offset mismatch");
            static_assert(offsetof(C_CombatActorActionHit, m_pRow) == 0x50, "C_CombatActorActionHit::m_pRow offset mismatch");
            static_assert(offsetof(C_CombatActorActionHit, m_pAttacker) == 0x58, "C_CombatActorActionHit::m_pAttacker offset mismatch");
            static_assert(offsetof(C_CombatActorActionHit, m_recordFlags0060) == 0x60, "C_CombatActorActionHit::m_recordFlags0060 offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorActionHit) == 0xD8, "C_CombatActorActionHit size mismatch");

    /**
     * A block raised against a swing, 0xB0 bytes (allocator 0x180C559F8),
     * run in the main slot.
     *
     * The block trigger fires it (`FireSlotAction` kind 6, 0x181483580): the
     * block factory (0x180C55BB4) allocates it and stores the first row
     * `QueryFreeBlockRows` returned at +0x58, which is all it is given.
     * `Enter` (0x180C54CA0) refuses it without that row, plays the row's
     * fragment, and through its block helper (0x180C53B30) puts the body in
     * PreparingToParry and starts the defence timeline.
     */
    class C_CombatActorActionBlock: public C_CombatFrameworkAction {
      public:
        static bool IsInstance(const I_CombatActorAction *action) {
            return HasPrimaryVTable(action, gPatterns.C_CombatActorActionBlock__VTable);
        }

        static C_CombatActorActionBlock *Allocate(C_CombatActor *owner) {
            C_CombatActorActionBlock *action = nullptr;
            hook::call<void>(gPatterns.C_CombatActorActionBlock__Allocate, &action, owner);
            return action;
        }

        const S_CombatActionBlockDBData *GetRow() const {
            return m_pRow;
        }

        /** Fills the action exactly as the block factory does. */
        void Prepare(const S_CombatActionBlockDBData *row) {
            m_pRow = row;
        }

      private:
        const void *m_pParamsVTable;                                // 0x50, S_CombatActorActionBlockParams
        const S_CombatActionBlockDBData *m_pRow;                    // 0x58
        std::byte m_combatActorObject[sizeof(C_CombatActorObject)]; // 0x60, C_CombatActorAction's abstract base: table, owner
        std::uint16_t m_value0070;                                  // 0x70, 0 by default
        std::byte m_padding0072[0x2];                               // 0x72
        std::uint32_t m_value0074;                                  // 0x74, 0 by default
        void *m_fragments0078[2];                                   // 0x78, two 8-byte slots built by 0x180917E58: the fragment Enter plays, and its second one
        const void *m_pHelperOwnerVTable;                           // 0x88, I_CombatActionHelperBlockOwner, handed to the block helper
        std::int64_t m_timestamp0090;                               // 0x90, -100000 (never) by default
        std::int64_t m_timestamp0098;                               // 0x98, -100000 (never) by default
        void *m_pBlockHelper;                                       // 0xA0, C_CombatActionHelperBlock, 0x38 bytes
        std::uint8_t m_flag00A8;                                    // 0xA8, false by default
        std::byte m_padding00A9[0x7];                               // 0xA9

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorActionBlock, m_pParamsVTable) == 0x50, "C_CombatActorActionBlock::m_pParamsVTable offset mismatch");
            static_assert(offsetof(C_CombatActorActionBlock, m_pRow) == 0x58, "C_CombatActorActionBlock::m_pRow offset mismatch");
            static_assert(offsetof(C_CombatActorActionBlock, m_combatActorObject) == 0x60, "C_CombatActorActionBlock::m_combatActorObject offset mismatch");
            static_assert(offsetof(C_CombatActorActionBlock, m_fragments0078) == 0x78, "C_CombatActorActionBlock::m_fragments0078 offset mismatch");
            static_assert(offsetof(C_CombatActorActionBlock, m_pHelperOwnerVTable) == 0x88, "C_CombatActorActionBlock::m_pHelperOwnerVTable offset mismatch");
            static_assert(offsetof(C_CombatActorActionBlock, m_pBlockHelper) == 0xA0, "C_CombatActorActionBlock::m_pBlockHelper offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorActionBlock) == 0xB0, "C_CombatActorActionBlock size mismatch");

    /**
     * What the dodge query is asked, as the jump input builds it in a fight
     * (`HandleCombatJump`, 0x1818094F0) and 0x18090A89C clears it.
     */
    struct S_CombatActionDodgeQuery {
        /** Clearance of the dodge's end point is tested (0x1815AB57C); the jump input sets it. */
        static constexpr std::uint32_t kTestsClearance = 1;

        float m_direction[2] {};                    // 0x00, the input direction in the body's frame, unit length
        const C_CombatActor *m_pOpponent = nullptr; // 0x08, the fighter the dodge circles
        std::uint32_t m_flags            = 0;       // 0x10
        std::byte m_padding0014[0x4] {};            // 0x14

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_CombatActionDodgeQuery, m_pOpponent) == 0x08, "S_CombatActionDodgeQuery::m_pOpponent offset mismatch");
            static_assert(offsetof(S_CombatActionDodgeQuery, m_flags) == 0x10, "S_CombatActionDodgeQuery::m_flags offset mismatch");
        }
    };

    static_assert(sizeof(S_CombatActionDodgeQuery) == 0x18, "S_CombatActionDodgeQuery size mismatch");

    /**
     * A dodge's parameters, 0x30 bytes, as the query's row filter
     * (0x181E3A730) fills them for the first row whose end point it accepts
     * and the builder copies them to the action's +0x50. The arc is worked
     * out by 0x18090A90C from where the body and its opponent stand.
     */
    struct S_CombatActorActionDodgeParams {
        const S_CombatActionDodgeDBData *m_pRow = nullptr; // 0x00
        std::uint16_t m_value0008               = 0;       // 0x08, 0 as the filter writes it
        std::byte m_padding000A[0x2] {};                   // 0x0A
        std::uint32_t m_value000C = 0;                     // 0x0C, 0 as the filter writes it
        float m_arcAngle          = 0.0F;                  // 0x10, around the opponent
        float m_distance          = 0.0F;                  // 0x14
        float m_end[3] {};                                 // 0x18, world position the arc ends at
        float m_direction[2] {};                           // 0x24, the query's direction
        std::uint8_t m_steersMovement = 0;                 // 0x2C, the body is player controlled: the action steers its movement request (0x182755C54)
        std::byte m_padding002D[0x3] {};                   // 0x2D

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_CombatActorActionDodgeParams, m_arcAngle) == 0x10, "S_CombatActorActionDodgeParams::m_arcAngle offset mismatch");
            static_assert(offsetof(S_CombatActorActionDodgeParams, m_end) == 0x18, "S_CombatActorActionDodgeParams::m_end offset mismatch");
            static_assert(offsetof(S_CombatActorActionDodgeParams, m_direction) == 0x24, "S_CombatActorActionDodgeParams::m_direction offset mismatch");
            static_assert(offsetof(S_CombatActorActionDodgeParams, m_steersMovement) == 0x2C, "S_CombatActorActionDodgeParams::m_steersMovement offset mismatch");
        }
    };

    static_assert(sizeof(S_CombatActorActionDodgeParams) == 0x30, "S_CombatActorActionDodgeParams size mismatch");

    /**
     * A dodge, 0xF8 bytes, run in the main slot.
     *
     * The jump input in a fight asks its fighter's dodge factory (action
     * manager +0x68) for parameters (`Query`) and has it build the action
     * from them (`Create`), then runs it (0x181482F08). `Enter` (0x181648A28)
     * copies the arc part of the parameters to +0xA0, plays the row's
     * fragment, puts the body in Dodge and raises the RPG dodge event
     * (0x181648E2C, built by 0x181648F28): the dodger's stamina and
     * experience and, on a success, an effect on the attacker. While it runs,
     * a player controlled body's combat input hands its movement request to
     * the action (`FilterMovementRequest` 0x180830BCC through 0x182755C54),
     * which adds the asked direction's movement about the opponent to the
     * request (0x18118B9FC); nothing places the body.
     */
    class C_CombatActorActionDodge: public C_CombatFrameworkAction {
      public:
        static bool IsInstance(const I_CombatActorAction *action) {
            return HasPrimaryVTable(action, gPatterns.C_CombatActorActionDodge__VTable);
        }

        /** The factory's own row query; false when no row fits, or with `kTestsClearance` none whose end point is free. */
        static bool Query(void *dodgeFactory, const S_CombatActionDodgeQuery &query, S_CombatActorActionDodgeParams &params) {
            return hook::call<bool>(gPatterns.C_CombatActionDodgeFactory__Query, dodgeFactory, &query, &params);
        }

        /** A fresh dodge owned by the factory's fighter, carrying one reference. */
        static C_CombatActorActionDodge *Create(void *dodgeFactory, const S_CombatActorActionDodgeParams &params) {
            C_CombatActorActionDodge *action = nullptr;
            hook::call<void>(gPatterns.C_CombatActionDodgeFactory__Create, dodgeFactory, &action, &params);
            return action;
        }

        const S_CombatActorActionDodgeParams &GetParams() const {
            return m_params;
        }

      private:
        S_CombatActorActionDodgeParams m_params;                    // 0x50
        std::byte m_combatActorObject[sizeof(C_CombatActorObject)]; // 0x80, C_CombatActorAction's abstract base: table, owner
        std::byte m_value0090[0x8];                                 // 0x90
        void *m_fragment0098;                                       // 0x98, the fragment Enter plays, one slot built by 0x18090D378
        std::byte m_arc00A0[0x20];                                  // 0xA0, Enter's copy of the parameters' +0x10..+0x30
        void *m_pMover00C0;                                         // 0xC0, the movement helper 0x18118B9FC works on: the owning fighter,
        std::byte m_value00C8[0x28];                                // 0xC8, then its state, cleared by the constructor
        std::uint16_t m_value00F0;                                  // 0xF0, cleared by the constructor; Enter sets its first byte
        std::byte m_padding00F2[0x6];                               // 0xF2

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorActionDodge, m_params) == 0x50, "C_CombatActorActionDodge::m_params offset mismatch");
            static_assert(offsetof(C_CombatActorActionDodge, m_combatActorObject) == 0x80, "C_CombatActorActionDodge::m_combatActorObject offset mismatch");
            static_assert(offsetof(C_CombatActorActionDodge, m_fragment0098) == 0x98, "C_CombatActorActionDodge::m_fragment0098 offset mismatch");
            static_assert(offsetof(C_CombatActorActionDodge, m_arc00A0) == 0xA0, "C_CombatActorActionDodge::m_arc00A0 offset mismatch");
            static_assert(offsetof(C_CombatActorActionDodge, m_pMover00C0) == 0xC0, "C_CombatActorActionDodge::m_pMover00C0 offset mismatch");
            static_assert(offsetof(C_CombatActorActionDodge, m_value00F0) == 0xF0, "C_CombatActorActionDodge::m_value00F0 offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorActionDodge) == 0xF8, "C_CombatActorActionDodge size mismatch");
} // namespace SDK::wh::combatmodule

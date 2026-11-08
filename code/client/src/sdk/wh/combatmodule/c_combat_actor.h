#pragma once

#include "../../cryengine/entitysystem/entity_event_listener_with_cleanup.h"
#include "../databasemodule/c_object_database.h"
#include "../framework/c_action.h"
#include "c_combat_actor_action_manager.h"
#include "c_combat_actor_anim_collisions.h"
#include "c_combat_actor_director.h"
#include "c_combat_actor_model.h"
#include "c_combat_actor_object.h"
#include "c_combat_actor_opponent_manager.h"
#include "c_combat_actor_slot_planner.h"
#include "c_combat_actor_time_cop.h"
#include "c_combat_actor_timewarp.h"
#include "c_combat_actor_updated_object.h"
#include "c_combat_rpg.h"
#include "i_combat_actor_horse_pull_down.h"
#include "i_combat_automation.h"

#include "../shared/c_listeners.h"
#include "../shared/c_recursive_srw_lock.h"
#include "../shared/c_signal.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace SDK::wh::entitymodule {
    class C_Actor;
} // namespace SDK::wh::entitymodule

namespace SDK::wh::combatmodule {
    // The scene's header reaches this one through its LOD manager's
    // C_CombatPlayer, so it cannot be included here.
    class C_CombatScene;
} // namespace SDK::wh::combatmodule

namespace SDK::wh::combatmodule {
    class C_CombatActorWeapons;

    class I_CombatActorAction: public framework::I_Action {};

    static_assert(sizeof(I_CombatActorAction) == 0x10, "I_CombatActorAction size mismatch");

    /** Intrusive private interface carried by every queued combat action. */
    class I_CombatActorActionPrivate: public I_CombatActorAction {
      public:
        ~I_CombatActorActionPrivate() override = default;
    };

    static_assert(sizeof(I_CombatActorActionPrivate) == 0x10, "I_CombatActorActionPrivate size mismatch");

    /**
     * One immutable row in the process-global accepted-attack database.
     *
     * The named columns are the ones the database's binder (0x1800B0440)
     * describes for `combat_action_attack.xml`, each by its offset in the row.
     * The four times are seconds of the authored animation: the swing's
     * timeline (`C_CombatActorTimeCop::BeginAttackTimeline`) divides start and
     * hit by the attack's speed and keeps the withdraw a fixed span past the hit.
     */
    struct S_CombatActionAttackDataDBData {
        /** `attack_type_id`: the `combat_attack_type` row the blow raises. */
        std::int32_t GetAttackTypeId() const {
            return m_attackTypeId;
        }

        /** `attack_time_to_hit`, seconds before the speed is applied. */
        float GetTimeToHit() const {
            return m_timeToHit;
        }

        /** `attack_time_to_withdraw`, seconds: from here on the blade can no longer land. */
        float GetTimeToWithdraw() const {
            return m_timeToWithdraw;
        }

      private:
        std::byte m_opaque0000[0xD8];
        std::int32_t m_attackTypeId; // 0xD8
        std::byte m_opaque00DC[0x4];
        float m_timeToStart;    // 0xE0
        float m_timeToHit;      // 0xE4
        float m_timeToWithdraw; // 0xE8
        float m_timeToEnd;      // 0xEC
        std::byte m_opaque00F0[0x140];

        static void AssertLayout() {
            static_assert(offsetof(S_CombatActionAttackDataDBData, m_attackTypeId) == 0xD8, "S_CombatActionAttackDataDBData::m_attackTypeId offset mismatch");
            static_assert(offsetof(S_CombatActionAttackDataDBData, m_timeToStart) == 0xE0, "S_CombatActionAttackDataDBData::m_timeToStart offset mismatch");
            static_assert(offsetof(S_CombatActionAttackDataDBData, m_timeToHit) == 0xE4, "S_CombatActionAttackDataDBData::m_timeToHit offset mismatch");
            static_assert(offsetof(S_CombatActionAttackDataDBData, m_timeToWithdraw) == 0xE8, "S_CombatActionAttackDataDBData::m_timeToWithdraw offset mismatch");
            static_assert(offsetof(S_CombatActionAttackDataDBData, m_timeToEnd) == 0xEC, "S_CombatActionAttackDataDBData::m_timeToEnd offset mismatch");
        }
    };

    static_assert(sizeof(S_CombatActionAttackDataDBData) == 0x230, "S_CombatActionAttackDataDBData size mismatch");

    /** The exact compact candidate consumed by the lower attack factory. */
    struct S_CombatActionAttackCandidate {
        const S_CombatActionAttackDataDBData *m_pAttackData = nullptr; // 0x00
        std::uint8_t m_candidateFlag0                       = 0;       // 0x08
        std::uint8_t m_candidateFlag1                       = 0;       // 0x09
        std::byte m_opaque000A[0x6] {};

      private:
        static void AssertLayout() {
            static_assert(offsetof(S_CombatActionAttackCandidate, m_pAttackData) == 0x00, "S_CombatActionAttackCandidate::m_pAttackData offset mismatch");
            static_assert(offsetof(S_CombatActionAttackCandidate, m_candidateFlag0) == 0x08, "S_CombatActionAttackCandidate::m_candidateFlag0 offset mismatch");
            static_assert(offsetof(S_CombatActionAttackCandidate, m_candidateFlag1) == 0x09, "S_CombatActionAttackCandidate::m_candidateFlag1 offset mismatch");
        }
    };

    static_assert(sizeof(S_CombatActionAttackCandidate) == 0x10, "S_CombatActionAttackCandidate size mismatch");

    /** Composed parameters stored at framework::C_ParamAction +0x50. */
    class S_CombatActorActionAttackParams {
      public:
        virtual ~S_CombatActorActionAttackParams() = default;

        const S_CombatActionAttackDataDBData *GetAttackData() const {
            return m_pAttackData;
        }
        std::uint8_t GetContextFlag0() const {
            return m_contextFlag0;
        }
        std::uint8_t GetContextFlag1() const {
            return m_contextFlag1;
        }
        std::uint8_t GetCandidateFlag0() const {
            return m_candidateFlag0;
        }
        std::uint8_t GetCandidateFlag1() const {
            return m_candidateFlag1;
        }

        void SetContextFlags(std::uint8_t flag0, std::uint8_t flag1) {
            m_contextFlag0 = flag0;
            m_contextFlag1 = flag1;
        }

      private:
        const S_CombatActionAttackDataDBData *m_pAttackData; // 0x08
        std::uint8_t m_contextFlag0;                         // 0x10
        std::uint8_t m_contextFlag1;                         // 0x11
        // CreateSelectedAttack copies candidate +0x09 before +0x08.
        std::uint8_t m_candidateFlag1;                       // 0x12
        std::uint8_t m_candidateFlag0;                       // 0x13
        std::byte m_opaque0014[0x4];

        static void AssertLayout() {
            static_assert(offsetof(S_CombatActorActionAttackParams, m_pAttackData) == 0x08, "S_CombatActorActionAttackParams::m_pAttackData offset mismatch");
            static_assert(offsetof(S_CombatActorActionAttackParams, m_contextFlag0) == 0x10, "S_CombatActorActionAttackParams::m_contextFlag0 offset mismatch");
            static_assert(offsetof(S_CombatActorActionAttackParams, m_contextFlag1) == 0x11, "S_CombatActorActionAttackParams::m_contextFlag1 offset mismatch");
            static_assert(offsetof(S_CombatActorActionAttackParams, m_candidateFlag0) == 0x13, "S_CombatActorActionAttackParams::m_candidateFlag0 offset mismatch");
            static_assert(offsetof(S_CombatActorActionAttackParams, m_candidateFlag1) == 0x12, "S_CombatActorActionAttackParams::m_candidateFlag1 offset mismatch");
        }
    };

    static_assert(sizeof(S_CombatActorActionAttackParams) == 0x18, "S_CombatActorActionAttackParams size mismatch");

    using C_CombatFrameworkAction      = framework::C_Action<I_CombatActorActionPrivate>;
    using C_CombatFrameworkParamAction = framework::C_ParamAction<S_CombatActorActionAttackParams, C_CombatFrameworkAction>;

    static_assert(sizeof(C_CombatFrameworkAction) == 0x50, "combat framework C_Action size mismatch");
    static_assert(sizeof(C_CombatFrameworkParamAction) == 0x68, "combat framework C_ParamAction size mismatch");

    template <typename TInterface, typename TParamAction>
    class C_CombatActorAction
        : public TParamAction
        , public C_CombatActorObject {
      protected:
        void ClearCommitContext() {
            m_commitContext = 0;
        }

      private:
        std::uint16_t m_opaque0078;
        std::byte m_opaque007A[0x2];
        std::uint32_t m_commitContext; // 0x7C

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorAction, m_commitContext) == 0x7C, "C_CombatActorAction::m_commitContext offset mismatch");
        }
    };

    using C_CombatActorActionAttackBase = C_CombatActorAction<I_CombatActorActionPrivate, C_CombatFrameworkParamAction>;

    static_assert(sizeof(C_CombatActorActionAttackBase) == 0x80, "C_CombatActorAction attack base size mismatch");

    template <typename TParams, typename TInterface>
    class C_CombatActorParamAction: public C_CombatActorAction<TInterface, framework::C_ParamAction<TParams, framework::C_Action<TInterface>>> {};

    template <typename TParams, bool, typename TInterface>
    class C_CombatActorAnimatedAction: public C_CombatActorParamAction<TParams, TInterface> {
      private:
        std::uint64_t m_opaque0080;
    };

    using C_CombatActorAnimatedAttack = C_CombatActorAnimatedAction<S_CombatActorActionAttackParams, true, I_CombatActorActionPrivate>;

    static_assert(sizeof(C_CombatActorAnimatedAttack) == 0x88, "C_CombatActorAnimatedAction attack size mismatch");

    class I_CombatActionHelperAttackOwner {
      public:
        virtual ~I_CombatActionHelperAttackOwner() = default;
    };

    static_assert(sizeof(I_CombatActionHelperAttackOwner) == 0x08, "I_CombatActionHelperAttackOwner size mismatch");

    /**
     * Accepted attack action after the native high-level selector has stamped
     * its presentation context. Only proven fields are exposed.
     */
    class C_CombatActorActionAttack
        : public C_CombatActorAnimatedAttack
        , public I_CombatActionHelperAttackOwner {
      public:
        /**
         * Whether a queued action is one of these. A fighter with an opponent
         * can have the factory hand back a sync attack instead, which shares
         * none of this layout, so a caller reading the fields below has to
         * ask first.
         */
        static bool IsInstance(const I_CombatActorAction *action) {
            return action != nullptr && *reinterpret_cast<const std::uint64_t *>(action) == gPatterns.C_CombatActorActionAttack__VTable;
        }

        const S_CombatActionAttackDataDBData *GetAttackData() const {
            return GetParams().GetAttackData();
        }
        std::uint8_t GetContextFlag0() const {
            return GetParams().GetContextFlag0();
        }
        std::uint8_t GetContextFlag1() const {
            return GetParams().GetContextFlag1();
        }
        std::uint8_t GetCandidateFlag0() const {
            return GetParams().GetCandidateFlag0();
        }
        std::uint8_t GetCandidateFlag1() const {
            return GetParams().GetCandidateFlag1();
        }
        std::uint32_t GetStateMode() const {
            return GetState();
        }

        void ApplyAcceptedContext(std::uint8_t contextFlag0, std::uint8_t contextFlag1, std::uint32_t stateMode) {
            GetParams().SetContextFlags(contextFlag0, contextFlag1);
            ClearCommitContext();
            SetState(stateMode);
        }

      private:
        std::uint64_t m_opaque0090;
        std::uint16_t m_opaque0098;
        std::byte m_opaque009A[0x6];
        void *m_pAttackHelper; // 0xA0
        std::byte m_opaque00A8[0x8];

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActorActionAttack, m_pAttackHelper) == 0xA0, "C_CombatActorActionAttack::m_pAttackHelper offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActorActionAttack) == 0xB0, "C_CombatActorActionAttack size mismatch");

    /**
     * Process-global database used by the game's own attack-row resolver.
     * RTTI proves the shared typed database base; scalar deletion proves the
     * complete 0x110-byte extent.
     */
    class C_CombatActionAttackDatabase: public databasemodule::C_ObjectDatabase<S_CombatActionAttackDataDBData, std::vector> {
      public:
        static C_CombatActionAttackDatabase *GetInstance() {
            return reinterpret_cast<C_CombatActionAttackDatabase *>(gPatterns.C_CombatActionAttackDatabase__Instance);
        }

        std::uint32_t GetRowIndex(const S_CombatActionAttackDataDBData *row) const {
            const auto *begin       = ObjectsBegin();
            const auto *end         = ObjectsEnd();
            const auto rowAddress   = reinterpret_cast<std::uintptr_t>(row);
            const auto beginAddress = reinterpret_cast<std::uintptr_t>(begin);
            const auto endAddress   = reinterpret_cast<std::uintptr_t>(end);
            if (row == nullptr || begin == nullptr || end == nullptr || rowAddress < beginAddress || rowAddress >= endAddress) {
                return std::numeric_limits<std::uint32_t>::max();
            }
            const std::uintptr_t delta = rowAddress - beginAddress;
            if (delta % sizeof(S_CombatActionAttackDataDBData) != 0) {
                return std::numeric_limits<std::uint32_t>::max();
            }
            const std::size_t index = static_cast<std::size_t>(delta / sizeof(S_CombatActionAttackDataDBData));
            return index <= std::numeric_limits<std::uint32_t>::max() ? static_cast<std::uint32_t>(index) : std::numeric_limits<std::uint32_t>::max();
        }

        const S_CombatActionAttackDataDBData *GetRow(std::uint32_t index) const {
            return index < ObjectCount() ? ObjectsBegin() + index : nullptr;
        }

      private:
        std::byte m_opaque0048[0xC8];
    };

    static_assert(sizeof(C_CombatActionAttackDatabase) == 0x110, "C_CombatActionAttackDatabase size mismatch");

    class C_CombatActionAttackFactory: public C_CombatActorObject {
      public:
        /**
         * Builds the same selected native attack action used after player
         * combat input has resolved an immutable database row.
         *
         * The member returns its intrusive handle through hidden return
         * storage in RDX, with zero commit flags. The high-level selector
         * may stamp query-only flags on its result; those must be cleared
         * before committing an ordinary attack.
         */
        C_CombatActorActionAttack *CreateSelectedAttack(const S_CombatActionAttackCandidate &candidate) {
            C_CombatActorActionAttack *result = nullptr;
            hook::this_call<void>(gPatterns.C_CombatActionAttackFactory__CreateSelectedAttack, this, &result, &candidate);
            return result;
        }
    };

    static_assert(sizeof(C_CombatActionAttackFactory) == 0x10, "C_CombatActionAttackFactory size mismatch");

    /**
     * Primary 80-slot combat interface shared by concrete combat actors.
     *
     * Only the one slot this mod has proven is named. Slot 79 is what the
     * game's own `CanHorsePullDown` and `RequestHorsePullDown` script bindings
     * reach the pull-down contract through, both of them loading the component
     * from `+0x278` and calling `+0x278` on its table.
     */
    class I_CombatActor {
      public:
        virtual ~I_CombatActor()  = default; // slot 0
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
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;
        virtual void unk_fn0033() = 0;
        virtual void unk_fn0034() = 0;
        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;
        virtual void unk_fn0038() = 0;
        virtual void unk_fn0039() = 0;
        virtual void unk_fn0040() = 0;
        virtual void unk_fn0041() = 0;
        virtual void unk_fn0042() = 0;
        virtual void unk_fn0043() = 0;
        virtual void unk_fn0044() = 0;
        /**
         * Whether the action running in the main slot is a dodge: its type
         * is the `dodge` combat action type (0x18072E630, slot 45 of both
         * C_CombatActor's and C_CombatPlayer's tables).
         */
        virtual bool IsDodging() const = 0; // slot 45
        virtual void unk_fn0046() = 0;
        virtual void unk_fn0047() = 0;
        virtual void unk_fn0048() = 0;
        virtual void unk_fn0049() = 0;
        virtual void unk_fn0050() = 0;
        virtual void unk_fn0051() = 0;
        virtual void unk_fn0052() = 0;
        /**
         * Reach of the weapon this fighter leads with, in metres.
         *
         * The engine compares it against GetOpponentDistance in
         * CreateAttackAction when deciding whether a queried row can
         * connect, so it is the game's own answer to "can I touch them
         * from here" rather than a guess.
         */
        virtual float GetPrimaryWeaponRange() = 0; // slot 53
        virtual void unk_fn0054()             = 0;
        virtual void unk_fn0055()             = 0;
        virtual void unk_fn0056()             = 0;
        virtual void unk_fn0057()             = 0;
        virtual void unk_fn0058()             = 0;
        virtual void unk_fn0059()             = 0;
        virtual void unk_fn0060()             = 0;
        virtual void unk_fn0061()             = 0;
        virtual void unk_fn0062()             = 0;
        virtual void unk_fn0063()             = 0;
        virtual void unk_fn0064()             = 0;
        virtual void unk_fn0065()             = 0;
        /**
         * Locks this fighter on to `entity`, or releases the lock for null.
         *
         * Slot 66 (0x180B21364), the call the local player's own opponent
         * picker makes. It hands the entity to the opponent manager's
         * `SetTarget` (0x180B21384), which records a C_CombatTarget at model
         * +0xCF0 and makes it the opponent -- at once if the fighter is at
         * rest, otherwise on the manager's first update that finds it so.
         * Nothing happens before Init has run.
         */
        virtual void SetTarget(IEntity *entity) = 0; // slot 66
        virtual void unk_fn0067()               = 0;
        virtual void unk_fn0068()               = 0;
        virtual void unk_fn0069()               = 0;
        virtual void unk_fn0070()               = 0;
        virtual void unk_fn0071()               = 0;
        virtual void unk_fn0072()               = 0;
        virtual void unk_fn0073()               = 0;
        virtual void unk_fn0074()               = 0;
        virtual void unk_fn0075()               = 0;
        virtual void unk_fn0076()               = 0;
        virtual void unk_fn0077()               = 0;
        virtual void unk_fn0078()               = 0;

        /** The pull-down contract, or null when this actor has none. */
        virtual I_CombatActorHorsePullDown *GetHorsePullDown() = 0; // slot 79, +0x278
        virtual void unk_fn0080()                              = 0;
        virtual void unk_fn0081()                              = 0;
        virtual void unk_fn0082()                              = 0;
        virtual void unk_fn0083()                              = 0;
        virtual void unk_fn0084()                              = 0;
        virtual void unk_fn0085()                              = 0;
        virtual void unk_fn0086()                              = 0;
        /** Lazily creates automation; native BT callers use +0x2B8. */
        virtual I_CombatAutomation *GetAutomation() = 0; // 87, 0x1803EFE44
    };

    /**
     * An actor's complete combat component, held at `C_Actor +0x278`.
     *
     * The RTTI class hierarchy names three bases: `I_CombatActor` at +0x00,
     * `EntityEventListenerWithCleanup` -> `IEntityEventListener` at +0x08,
     * and an empty `UnsafeOp::CryDeferrable<0>` tag at +0x31, which carries
     * no storage and is deliberately not given a speculative SDK type.
     *
     * `C_CombatActor::Init` (0x180916ECC) builds roughly thirty sub-systems
     * and stores each at a fixed offset; every one named below is proven by
     * the table its own constructor installs. `C_CombatPlayer` derives from
     * this and places its `I_EquipmentManagerListener` sub-object at +0x448,
     * which is what fixes the size: Init's last act is an SRWLOCK at +0x440.
     */
    class C_CombatActor
        : public I_CombatActor
        , public SDK::EntityEventListenerWithCleanup {
      public:
        ~C_CombatActor() override = default;

        /**
         * What a source is asking the fighter's guard to be.
         *
         * The five sources of the model's guard property each write one of
         * these and the highest wins, so the values are ordered rather than
         * flags: an automation guard outranks a held block, and both outrank
         * nothing. Read off `C_CombatActor::RequestGuard` at 0x18093A4D4,
         * whose two branches build a different guard action per value.
         */
        enum GuardRequest : std::uint8_t {
            /** Arms down. Needs no action -- the running one ends itself. */
            GUARD_REQUEST_NONE = 0,

            /**
             * The arm guard: what the local player's right click asks for,
             * and what a replicated fighter's guard has to be driven with.
             */
            GUARD_REQUEST_GUARD = 1,

            /** What C_CombatAutomationGuard asks for on an AI fighter. */
            GUARD_REQUEST_AUTOMATION = 2,
        };

        /** Which of the guard property's five sources a request is made as. */
        enum GuardRequestSource : std::uint8_t {
            /**
             * The local player's own combat controller.
             *
             * A body driven from the network has no controller and no
             * automation, so this slot is free on a puppet -- and using the
             * same one the engine uses for a real player is what makes a
             * replicated guard behave like a played one.
             */
            GUARD_SOURCE_PLAYER = 0,

            /** C_CombatAutomationBlock and C_CombatAutomationDefense. */
            GUARD_SOURCE_AUTOMATION_BLOCK = 1,

            /** C_CombatAutomationGuard. */
            GUARD_SOURCE_AUTOMATION_GUARD = 2,
        };

        /** Optional mode row consumed by TryEnterCombatMode at 0x180C5A1BC. */
        struct CombatModeRequest {
            bool hasActionType        = false;
            std::int32_t actionTypeId = 0;
        };

        static_assert(sizeof(CombatModeRequest) == 0x8);
        static_assert(offsetof(CombatModeRequest, hasActionType) == 0x0);
        static_assert(offsetof(CombatModeRequest, actionTypeId) == 0x4);

        /**
         * Enters the native default combat mode, subject to actor restrictions.
         * NPC automation needs this mode; the player's freeGuard stance does
         * not admit its normal opening attack rows.
         */
        bool TryEnterCombatMode();

        /** What `RequestFightingStance` takes; 12 bytes, laid out by the game. */
        struct FightingStanceRequest {
            /** Fists (or weapon) up, or back down. */
            bool up = false;

            /**
             * The `combat_action_type` row to transition through, -1 for none.
             *
             * The player's own input path names the `freeGuard` row here;
             * C_CombatAutomation passes -1. The mod mirrors the player path:
             * EnterCombatMode
             * records this row and the attack factory reads it
             * back, so -1 makes a later replicated swing unselectable.
             */
            std::int32_t actionTypeId = -1;

            /** Zero at every call site in the game. */
            bool unknown08 = false;
        };

        /**
         * Puts this body's fists -- or its weapon -- up, or takes them down.
         * Needs no fight and no opponent; the player's own BeginAttack and
         * BeginGuard ask for it first.
         *
         * Runs as an actor action on `C_Actor + 0x280`, so it is
         * asynchronous: `IsFightingStanceUp` says when the stance arrived.
         */
        bool RequestFightingStance(bool up);

        /**
         * Ends combat mode the way the engine does once a fight is over --
         * the player controller's delayed end, or `PostUpdate` with the
         * combat queue run dry -- and only while in it. Leaving tears the
         * fighter's `C_ActorActionCombat` down, which is what outranks a
         * stance change in the actor queue: a fighter in combat mode cannot
         * be asked to lower its fists.
         */
        void LeaveCombatModeIfActive();

        /** Whether the fists are up yet (`C_CombatActorModel + 0x008`). */
        bool IsFightingStanceUp() const;

        /**
         * Whether the stance is the free guard rather than a fight stance:
         * the model's action type is the freeGuard row, as the engine's own
         * `IsInFreeGuardStance` (0x1812EDF54) reads it.
         */
        bool IsInFreeGuardStance() const;

        /**
         * Raises or lowers this fighter's guard as one of the five sources.
         *
         * The engine's own call, so everything downstream of it happens:
         * the model's guard property moves, its signal fires, and the action
         * manager builds and runs the matching guard action. Idempotent --
         * a request that does not change the source's own byte does nothing.
         */
        void RequestGuard(GuardRequest request, GuardRequestSource source = GUARD_SOURCE_PLAYER);

        /**
         * Points this fighter at one direction of the combat star.
         *
         * `zoneId` is a row of the game's C_CombatZoneDatabase and -1 is
         * ignored. Both the attack and the guard factory read the zone off the
         * model when they build an action, so this is what has to be set
         * before either is asked for -- it is the difference between a punch
         * thrown from the left and one thrown from above.
         */
        void SetCombatZone(std::int32_t zoneId);

        /**
         * Turns this fighter towards one direction of the combat star the way
         * the player's own input does (`RequestCombatZone`, 0x180938E94).
         *
         * In a guard it goes through `TryApplyCombatZone` (0x180936A28), which
         * also picks the guard side for the zone and this fighter's weapons
         * (model `+0xC0`), records the previous zone and fires the zone signal
         * the raised guard re-aims on. Outside a guard it only validates the
         * zone and stores it as pending. `SetCombatZone` does none of that, so
         * it is right for a swing whose row is already chosen and wrong for a
         * guard. Answers whether the zone was taken.
         */
        bool RequestCombatZone(std::int32_t zoneId);

        /**
         * Turns this fighter towards `zoneId` with the guard side it needs:
         * `TryApplyCombatZone` (0x180936A28), what `RequestCombatZone` runs
         * in the guard and while the guard reads to strike, with the current
         * side. It keeps `guardSide` when the weapon's heavy swings reach the
         * zone from it and takes the other side otherwise, validates the
         * pair, writes the side (model +0xC0) and the zone with its previous,
         * and in ReadingToStrike signals the change to this fighter and its
         * opponent. The running action may refuse the zone. Answers whether
         * anything changed.
         */
        bool TryApplyCombatZone(std::int32_t zoneId, std::int32_t guardSide);

        /** Why a swing did not happen, for a caller that has to say so. */
        enum class AttackOutcome : std::uint8_t {
            Swung,         //< The action was built and queued.
            NoManager,     //< The fighter has no action manager yet.
            NoFactory,     //< The manager's attack factory slot is empty.
            ActionRefused, //< The factory built nothing -- the engine said no.
            QueueRefused,  //< The action was built but the queue turned it down.
            InvalidRow,    //< The sender's immutable attack row is not in this database.
        };

        /**
         * The press half of a swing, as BeginAttack performs it: the attack
         * request bit and the input class, which move the fighter into
         * ReadingToStrike. Nothing is queued -- the 0x2000 action the engine
         * builds here is a probe it releases again.
         */
        void BeginAttack(std::int32_t inputClassId);

        /** Publishes the input class a swing is being made with, or -1. */
        void SetAttackInputClass(std::int32_t inputClassId, bool active);

        /**
         * Rebuilds and queues the exact accepted action another player queued,
         * letting it in over a late hit reaction, parry or swing still running
         * here. Reads the running action, so the caller holds
         * `GetCombatGroupLock()`.
         */
        AttackOutcome ReplayAcceptedAttack(std::uint32_t attackRowIndex, std::uint8_t candidateFlag0, std::uint8_t candidateFlag1, std::uint8_t contextFlag0, std::uint8_t contextFlag1, std::uint32_t stateMode);

        /** Selects an ordinary attack through the native unpaired query, then queues its committed action. */
        AttackOutcome TryUnpairedAttack(std::int32_t inputClassId);

        /** Queues an already-built combat action; AddRefs the handle itself. */
        bool RunAction(I_CombatActorActionPrivate **action);

        /**
         * Whether Init has run. Every accessor below reads a pointer Init
         * writes, so nothing here means anything until this is set.
         */
        bool IsInitialised() const {
            return m_initialised;
        }

        entitymodule::C_Actor *GetOwner() const {
            return m_pOwner;
        }

        /** The scene this fighter belongs to; what vtable slot 201 (0x181A7D7E0) returns. */
        C_CombatScene *GetScene() const {
            return m_pScene;
        }

        /** This fighter's state, and the only part of it that is fully named. */
        C_CombatActorModel *GetModel() const {
            return m_pModel;
        }

        C_CombatActorActionManager *GetActionManager() const {
            return m_pActionManager;
        }

        C_CombatActorWeapons *GetWeapons() const {
            return m_pWeapons;
        }

        // The deciders. A body driven from the network has to stop these from
        // choosing for it, which is why they are named rather than padded.
        C_CombatActorDirector *GetDirector() const {
            return m_pDirector;
        }

        /**
         * The combat action running in `slot` of this fighter's queue, or
         * null; 0 is the main action. Borrowed, and only stable under
         * `GetCombatGroupLock()`.
         */
        I_CombatActorAction *GetRunningAction(std::int32_t slot) const {
            framework::I_Action *action = m_pDirector != nullptr ? m_pDirector->GetSlotAction(slot) : nullptr;
            return static_cast<I_CombatActorAction *>(action);
        }

        /**
         * The combat group this fighter ticks in, or `kNoCombatGroup` while
         * `C_CombatScene::RebuildCombatGroups` (0x1809D45A4) has not reached
         * it. Fighters outside every group share one fallback lock.
         */
        std::uint16_t GetCombatGroupIndex() const {
            return m_combatGroupIndex;
        }

        static constexpr std::uint16_t kNoCombatGroup = 0xFFFFU;

        void *GetNextGuardSelector() const {
            return m_pNextGuardSelector;
        }

        C_CombatActorSlotPlanner *GetSlotPlanner() const {
            return m_pSlotPlanner;
        }

        C_CombatActorOpponentManager *GetOpponentManager() const {
            return m_pOpponentManager;
        }

        C_CombatActorTimewarp *GetTimewarp() const {
            return m_pTimewarp;
        }

        C_CombatActorTimeCop *GetTimeCop() const {
            return m_pTimeCop;
        }

        C_CombatRPG *GetRPG() const {
            return m_pRPG;
        }

        /** Whom this fighter has locked on to, or null. */
        IEntity *GetTargetEntity() const {
            auto *model = GetModel();
            return model != nullptr ? model->GetTargetEntity() : nullptr;
        }

        /**
         * The recursive lock of this fighter's combat group (0x1823D0D20):
         * the one the engine holds around ApplySlotDelay.
         */
        shared::C_RecursiveSRWLock *GetCombatGroupLock() {
            return hook::this_call<shared::C_RecursiveSRWLock *>(gPatterns.C_CombatActor__GetCombatGroupLock, this);
        }

        /** Where this body's blows become contacts. Null before Init has run. */
        C_CombatActorAnimCollisions *GetAnimCollisions() const {
            return m_pAnimCollisions;
        }

        /**
         * The fighter this one is squared up against, or null.
         *
         * Read off the model rather than off the manager, because the model is
         * where every melee path reads it from.
         */
        C_CombatActor *GetOpponent() const {
            auto *model = GetModel();
            return model != nullptr ? model->GetOpponent() : nullptr;
        }

        /**
         * Squares this fighter up against `entity`, or stands it down for null.
         *
         * Nothing happens without a manager, which means nothing happens
         * before Init has run.
         */
        void SetOpponent(IEntity *entity) {
            if (auto *manager = GetOpponentManager()) {
                manager->SetOpponent(entity);
            }
        }

        void *GetEvents() const {
            return m_pEvents;
        }

      private:
        // Past both base vptrs -- I_CombatActor at +0x00 and
        // IEntityEventListener at +0x08 -- up to the owner.
        // Laid out by the constructor 0x1810F01A8 and its reset 0x1810EFF60.
        std::uint64_t m_state0010[3];     // 0x010
        std::uint8_t m_state0028;         // 0x028, 4 by default
        std::byte m_opaque0029[0xF];
        // Twenty-eight model signals, in kinds of two and three alike.
        wh::shared::C_Signal m_signals[28]; // 0x038
        entitymodule::C_Actor *m_pOwner; // 0x1F8
        // C_CombatActorDirector, which derives from C_ActionActor: it is the
        // combat action queue `RunAction` inserts into, and is not the
        // C_Actor+0x280 queue that carries the I_ActorAction family.
        C_CombatActorDirector *m_pDirector;               // 0x200
        C_CombatScene *m_pScene;                          // 0x208
        C_CombatActorModel *m_pModel;                     // 0x210
        void *m_pCollisions;                              // 0x218, C_CombatActorCollisions
        C_CombatActorAnimCollisions *m_pAnimCollisions;   // 0x220
        std::byte m_opaque0228[0x8];                      // 0x228
        void *m_pEffects;                                 // 0x230, C_CombatEffects
        void *m_pRestriction;                             // 0x238, C_CombatActorRestriction
        void *m_pEnvironmentManager;                      // 0x240, C_CombatEnvironmentManager
        std::byte m_opaque0248[0x8];                      // 0x248
        void *m_pAlignmentManager;                        // 0x250, C_CombatAlignmentManager
        void *m_pVirtualWeapon;                           // 0x258, C_CombatActorVirtualWeapon
        void *m_pModifierAim;                             // 0x260, C_CombatModifierAim
        void *m_pModifierLookEnemy;                       // 0x268, C_CombatModifierLookEnemy
        std::uint64_t m_state0270;                        // 0x270
        std::uint16_t m_state0278;                        // 0x278, zeroed by the reset
        bool m_initialised;                               // 0x27A
        std::byte m_padding027B[0x1];
        std::int32_t m_state027C;                         // 0x27C, -1 after the reset
        bool m_state0280;                                 // 0x280, true by default
        bool m_state0281;                                 // 0x281
        std::byte m_padding0282[0x2];
        std::byte m_state0284[0x8];                       // 0x284, zeroed by the reset
        std::uint8_t m_flags028C;                         // 0x28C, low two bits cleared by the reset
        std::byte m_padding028D[0x3];
        float m_state0290;                                // 0x290, -1 after the reset
        float m_state0294;                                // 0x294, -1 after the reset
        std::byte m_opaque0298[0x1C];
        std::uint8_t m_state02B4;                         // 0x2B4, 4 after the reset
        std::byte m_padding02B5[0x3];
        wh::shared::C_Listeners<C_CombatActorObject, 1> m_objectListeners;                // 0x2B8
        wh::shared::C_Listeners<C_CombatActorUpdatedObject, 1> m_updatedObjectListeners[4]; // 0x2E0
        std::uint32_t m_state0380;                        // 0x380, zeroed by the reset
        std::uint16_t m_combatGroupIndex;                 // 0x384, written by SetCombatGroupIndex (0x180D5D0E4)
        std::byte m_opaque0386[0x2];                      // 0x386
        C_CombatRPG *m_pRPG;                              // 0x388
        std::byte m_opaque0390[0x8];                      // 0x390
        void *m_pHumanPhysics;                            // 0x398, C_CombatHumanPhysics
        std::byte m_opaque03A0[0x8];                      // 0x3A0
        C_CombatActorActionManager *m_pActionManager;     // 0x3A8
        void *m_pComboManager;                            // 0x3B0, C_CombatComboManager
        C_CombatActorOpponentManager *m_pOpponentManager; // 0x3B8
        C_CombatActorTimeCop *m_pTimeCop;                 // 0x3C0
        C_CombatActorTimewarp *m_pTimewarp;               // 0x3C8
        void *m_pNextGuardSelector;                       // 0x3D0, C_CombatActorNextGuardSelector
        void *m_pGuardVarSystem;                          // 0x3D8, C_CombatActorGuardVarSystem
        C_CombatActorSlotPlanner *m_pSlotPlanner;         // 0x3E0
        C_CombatActorWeapons *m_pWeapons;                 // 0x3E8
        void *m_pClinch;                                  // 0x3F0, C_CombatActorClinch
        void *m_pStealth;                                 // 0x3F8, C_CombatActorStealth
        void *m_pHorsePullDownObject;                     // 0x400, C_CombatActorHorsePullDown
        void *m_pMercyKill;                               // 0x408, C_CombatActorMercyKill
        void *m_pHuntAttack;                              // 0x410, C_CombatActorHuntAttack
        void *m_pEvents;                                  // 0x418, C_CombatActorEvents
        void *m_pOpponentAimingListener;                  // 0x420, C_CombatOpponentAimingListener
        void *m_pClenchedHand;                            // 0x428, C_CombatActorClenchedHand
        void *m_pActivity;                                // 0x430, C_CombatActorActivity
        std::byte m_opaque0438[0x8];                      // 0x438
        void *m_lock;                                     // 0x440, SRWLOCK

        static void AssertLayout() {
            static_assert(offsetof(C_CombatActor, m_pOwner) == 0x1F8, "C_CombatActor::m_pOwner offset mismatch");
            static_assert(offsetof(C_CombatActor, m_signals) == 0x38, "C_CombatActor::m_signals offset mismatch");
            static_assert(offsetof(C_CombatActor, m_state027C) == 0x27C, "C_CombatActor::m_state027C offset mismatch");
            static_assert(offsetof(C_CombatActor, m_objectListeners) == 0x2B8, "C_CombatActor::m_objectListeners offset mismatch");
            static_assert(offsetof(C_CombatActor, m_updatedObjectListeners) == 0x2E0, "C_CombatActor::m_updatedObjectListeners offset mismatch");
            static_assert(offsetof(C_CombatActor, m_state0380) == 0x380, "C_CombatActor::m_state0380 offset mismatch");
            static_assert(offsetof(C_CombatActor, m_pScene) == 0x208, "C_CombatActor::m_pScene offset mismatch");
            static_assert(offsetof(C_CombatActor, m_pDirector) == 0x200, "C_CombatActor::m_pDirector offset mismatch");
            static_assert(offsetof(C_CombatActor, m_pModel) == 0x210, "C_CombatActor::m_pModel offset mismatch");
            static_assert(offsetof(C_CombatActor, m_pVirtualWeapon) == 0x258, "C_CombatActor::m_pVirtualWeapon offset mismatch");
            static_assert(offsetof(C_CombatActor, m_initialised) == 0x27A, "C_CombatActor::m_initialised offset mismatch");
            static_assert(offsetof(C_CombatActor, m_combatGroupIndex) == 0x384, "C_CombatActor::m_combatGroupIndex offset mismatch");
            static_assert(offsetof(C_CombatActor, m_pActionManager) == 0x3A8, "C_CombatActor::m_pActionManager offset mismatch");
            static_assert(offsetof(C_CombatActor, m_pOpponentManager) == 0x3B8, "C_CombatActor::m_pOpponentManager offset mismatch");
            static_assert(offsetof(C_CombatActor, m_pNextGuardSelector) == 0x3D0, "C_CombatActor::m_pNextGuardSelector offset mismatch");
            static_assert(offsetof(C_CombatActor, m_pSlotPlanner) == 0x3E0, "C_CombatActor::m_pSlotPlanner offset mismatch");
            static_assert(offsetof(C_CombatActor, m_pWeapons) == 0x3E8, "C_CombatActor::m_pWeapons offset mismatch");
            static_assert(offsetof(C_CombatActor, m_pEvents) == 0x418, "C_CombatActor::m_pEvents offset mismatch");
            static_assert(offsetof(C_CombatActor, m_pActivity) == 0x430, "C_CombatActor::m_pActivity offset mismatch");
            static_assert(offsetof(C_CombatActor, m_lock) == 0x440, "C_CombatActor::m_lock offset mismatch");
        }
    };

    static_assert(sizeof(C_CombatActor) == 0x448, "C_CombatActor size mismatch");
} // namespace SDK::wh::combatmodule

#pragma once

#include "c_tag_definition.h"

#include "../../patterns.h"
#include "../../types.h"

#include <intrin.h>
#include <utils/hooking/hooking.h>

#include <cstddef>
#include <cstdint>

namespace SDK {
    // KCD2 uses a 20-byte tag state.
    struct SFragTagState {
        std::uint8_t bits[20] {};
    };

    static_assert(sizeof(SFragTagState) == 0x14, "SFragTagState size mismatch");
    static_assert(offsetof(SFragTagState, bits) == 0x0, "SFragTagState::bits offset mismatch");

    // CryEngine IAction, distinct from wh::framework::I_Action.
    // Layout verified against the native TAction constructor at 0x18160E52C:
    // priority +0x24, fragment id +0x38, fragment tags +0x3C, option +0x50.
    class IAction {
      public:
        /**
         * CryEngine's `IAction::EStatus`, at +0x28. `CActionController::Queue`
         * (0x180684D8C) moves an action to Pending before inserting it,
         * installation makes it Installed -- `IAction::SetFragment`
         * (0x1804AD95C) tests exactly `== 2` -- and both Exit and Fail put it
         * back to None. A pending action whose queue time runs out is
         * Finished without ever being installed.
         */
        enum class EStatus : std::uint32_t {
            None      = 0,
            Pending   = 1,
            Installed = 2,
            Exiting   = 3,
            Finished  = 4,
        };

        /**
         * The virtual slots of KCD2's `IAction`:
         * `Interrupt` is virtual here (slot 8), and slot 7 is what `Interrupt`
         * calls instead of `Exit` for an action that never started. Only the
         * slots below are proven.
         *
         * - `Enter` (+0x10) and `Exit` (+0x20): `Interrupt` (0x181E3E120) calls
         *   `Exit` for a started action; `C_CallbackAction` overrides both.
         * - `Fail` (+0x18): the same body as `Exit` on `TAction`
         *   (0x181A7DDD0), so only the slot tells them apart.
         * - `UpdatePending` (+0x28): called by `ResolveActionInstallations`
         *   for every queued action before its status is read; it is where a
         *   queue time running out turns the status to Finished.
         * - `InterruptedBeforeStart` (+0x38): `Interrupt`'s call for an
         *   action still installing or never entered, after setting None.
         * - `OnActionFinished` (+0x88): `CActionController::Update`
         *   (0x1804B3DF8) calls it once per pass when a one-shot fragment
         *   reaches its completion point -- its length less the blend-out.
         * - `DoDelete` (+0xB8): `Release`'s last reference.
         *
         * `TAction<SAnimationContext>`'s table has 29 slots (0x183BD4530).
         */
        enum class VirtualSlot : std::size_t {
            Enter                  = 2,
            Fail                   = 3,
            Exit                   = 4,
            UpdatePending          = 5,
            InterruptedBeforeStart = 7,
            OnActionFinished       = 17,
            DoDelete               = 23,
        };
        static constexpr std::size_t kTActionVirtualSlots = 29;

        /** What `Fail` is told; the engine's `EActionFailure`. */
        enum class EFailure : std::int32_t {
            QueueFull      = 0,
            InvalidEntry   = 1,
            InvalidContext = 2,
        };

        virtual ~IAction() = default;

        void AddRef() {
            _InterlockedIncrement(&m_referenceCount);
        }
        void Release() {
            hook::this_call<void>(gPatterns.IAction__Release, this);
        }

        /**
         * Ends the action now: status Finished and not interruptable. On the
         * controller's next update `BlendOffActions` takes an installed action
         * off at higher priority with no time left, and a queued one is
         * dropped from the queue.
         */
        void ForceFinish() {
            hook::this_call<void>(gPatterns.IAction__ForceFinish, this);
        }

        /**
         * Only asks the action to blend out: it keeps playing until its
         * scope's next blend point, which for a one-shot is its end.
         */
        void Stop() {
            hook::this_call<void>(gPatterns.IAction__Stop, this);
        }

        /**
         * Replaces the fragment an installed action plays. On an installed
         * action that is not already requeued it marks itself requeued and
         * hands itself back to its controller, which installs it again -- the
         * same action object, no Exit or Enter in between. The engine's own
         * movement actions change fragment this way.
         */
        void SetFragment(std::uint32_t fragment, const SFragTagState &tags, std::uint32_t option, std::uint32_t userToken, bool trumpSelf) {
            hook::this_call<void>(gPatterns.IAction__SetFragment, this, fragment, &tags, option, userToken, trumpSelf);
        }

        /** Plays the current fragment again from the start, as another pass. */
        void Replay() {
            SetFragment(m_fragmentId, m_fragmentTags, m_optionIndex, m_userToken, false);
        }

        EStatus GetStatus() const {
            return static_cast<EStatus>(m_status);
        }

        /** Still waiting to play, or playing: what `ForceFinish` has to end. */
        bool IsRunning() const {
            const EStatus status = GetStatus();
            return status == EStatus::Pending || status == EStatus::Installed;
        }

        /**
         * Gone from its controller: Exit and Fail put an action back to None, a
         * finished or expired one is Finished. Exiting is still blending out.
         */
        bool HasEnded() const {
            const EStatus status = GetStatus();
            return status == EStatus::None || status == EStatus::Finished;
        }

        /**
         * The virtual table this action dispatches through. An action the mod
         * builds may be given a table of its own -- a copy of its class's
         * with some slots overridden -- which is how it hears about its own
         * edges and nothing else's. The copy has to include the RTTI locator
         * in front of slot 0.
         */
        const std::uint64_t *GetVirtualTable() const {
            return *reinterpret_cast<const std::uint64_t *const *>(this);
        }
        void SetVirtualTable(const std::uint64_t *table) {
            *reinterpret_cast<const std::uint64_t **>(this) = table;
        }

        void SetOptionIndex(std::uint32_t option) {
            m_optionIndex = option;
        }

        /** The option asked for; `kOptionRandom` leaves the variant to the database. */
        std::uint32_t GetOptionIndex() const {
            return m_optionIndex;
        }

        /**
         * The priority `C_Player::PlayAnim` queues its generic action at, and
         * the one the game's item-handling and stance transitions use.
         */
        static constexpr std::int32_t kPlayAnimPriority = 5;

        /** What the constructor sets when no option was asked for (0x18160E52C). */
        static constexpr std::uint32_t kOptionRandom = 0xFFFFFFFEU;

        /** The controller-local fragment id, meaningful only against the controller it was queued on. */
        std::uint32_t GetFragmentId() const {
            return m_fragmentId;
        }

        /** The fragment's own tags, in the dictionary `GetFragmentTagDefinition(GetFragmentId())` answers. */
        const SFragTagState &GetFragmentTags() const {
            return m_fragmentTags;
        }

        std::int32_t GetPriority() const {
            return m_priority;
        }

        /**
         * Sets a named transform the fragment's procedural clips read, as
         * CryEngine's `IAction::SetParam(const char *, const QuatT &)` does:
         * keyed by the name's lower-case CRC32 (`GetParam`, 0x18078A048, reads
         * "TargetPos" back this way for the PositionAdjust clips).
         */
        void SetParam(const char *name, const QuatT &value) {
            hook::this_call<void>(gPatterns.CMannequinParams__SetParam, &m_mannequinParams, LowercaseCrc32(name), &value);
        }

        /**
         * A float param, which the engine keeps in the first four bytes of
         * the same 28-byte value a transform fills: the lockpicking turn's
         * `TurnFraction` is written that way (0x18089A53C) and read that way
         * by its LayerManualUpdate clip (0x180AB0B10).
         */
        void SetParam(const char *name, float value) {
            SetParam(name, QuatT {Quat {value, 0.0F, 0.0F, 0.0F}, Vec3 {0.0F, 0.0F, 0.0F}});
        }

        /** The float param `name` from this action's own params; false when it has none. */
        bool GetParam(const char *name, float &value) const {
            QuatT stored {};
            if (!hook::this_call<bool>(gPatterns.CMannequinParams__GetParam, &m_mannequinParams, LowercaseCrc32(name), &stored)) {
                return false;
            }
            value = stored.q.x;
            return true;
        }

        /**
         * `IAction::GetParam(const char *, QuatT &)` as the PositionAdjust
         * clips call it (0x18078A048): "TargetPos" from this action's own
         * params, else from its root scope's context. False when neither has one.
         */
        bool GetTargetPos(QuatT &value) const {
            return hook::this_call<bool>(gPatterns.IAction__GetTargetPos, this, "TargetPos", &value);
        }

        /** CCrc32::ComputeLowercase: reflected CRC32 (0xEDB88320) over the ASCII-lower-cased name. */
        static std::uint32_t LowercaseCrc32(const char *name) {
            std::uint32_t crc = 0xFFFFFFFFU;
            for (const char *c = name; *c != '\0'; ++c) {
                const char lower = *c >= 'A' && *c <= 'Z' ? static_cast<char>(*c + 32) : *c;
                crc ^= static_cast<std::uint8_t>(lower);
                for (int bit = 0; bit < 8; ++bit) {
                    crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
                }
            }
            return ~crc;
        }

        /**
         * The flags word (+0x2C): what the constructor was given -- 0x4 makes
         * IAction::Interrupt (0x181E3E120) ask the controller to queue the
         * action again -- and, above them, the controller's own state bits
         * (0x10 installing, 0x20 started, and more).
         */
        std::uint32_t GetFlags() const {
            return m_flags;
        }

        /** True until CActionController has erased this exact action. */
        bool IsRegisteredWithActionController() const {
            return (m_flags & 0x40U) != 0;
        }

      private:
        void *m_context;                     // 0x08, SAnimationContext *, ctor null (TAction ctor 0x18160E52C)
        float m_activeTime;                  // 0x10, ctor 0.0
        float m_queueTime;                   // 0x14, ctor -1.0
        std::uint32_t m_forcedScopeMask;     // 0x18, ctor argument; 32-bit ActionScopes in this build
        std::uint32_t m_installedScopeMask;  // 0x1C, ctor 0
        std::int32_t m_subContext;           // 0x20, TagID, ctor -1 (TAG_ID_INVALID)
        std::int32_t m_priority; // 0x24
        std::uint32_t m_status;  // 0x28
        std::uint32_t m_flags;   // 0x2C
        void *m_rootScope;            // 0x30, IScope *, ctor null
        std::uint32_t m_fragmentId;   // 0x38
        SFragTagState m_fragmentTags; // 0x3C
        std::uint32_t m_optionIndex;  // 0x50
        std::uint32_t m_userToken;    // 0x54
        long m_referenceCount; // 0x58
        float m_speedBias;                   // 0x5C, ctor 1.0
        float m_animWeight;                  // 0x60, ctor 1.0
        std::byte m_opaque0064[0x4];         // 0x64
        void *m_mannequinParams;             // 0x68, CMannequinParams: DynArray<SMannParameter> data, ctor the empty-array sentinel
        std::byte m_opaque0070[0x20];        // 0x70, ctor copies a 16-byte constant and zeroes +0x80; +0x88 holds a second DynArray sentinel

        static void AssertLayout() {
            static_assert(offsetof(IAction, m_priority) == 0x24, "IAction::m_priority offset mismatch");
            static_assert(offsetof(IAction, m_fragmentId) == 0x38, "IAction::m_fragmentId offset mismatch");
            static_assert(offsetof(IAction, m_fragmentTags) == 0x3C, "IAction::m_fragmentTags offset mismatch");
            static_assert(offsetof(IAction, m_status) == 0x28, "IAction::m_status offset mismatch");
            static_assert(offsetof(IAction, m_flags) == 0x2C, "IAction::m_flags offset mismatch");
            static_assert(offsetof(IAction, m_optionIndex) == 0x50, "IAction::m_optionIndex offset mismatch");
            static_assert(offsetof(IAction, m_userToken) == 0x54, "IAction::m_userToken offset mismatch");
            static_assert(offsetof(IAction, m_referenceCount) == 0x58, "IAction::m_referenceCount offset mismatch");
            static_assert(offsetof(IAction, m_context) == 0x08, "IAction::m_context offset mismatch");
            static_assert(offsetof(IAction, m_queueTime) == 0x14, "IAction::m_queueTime offset mismatch");
            static_assert(offsetof(IAction, m_forcedScopeMask) == 0x18, "IAction::m_forcedScopeMask offset mismatch");
            static_assert(offsetof(IAction, m_subContext) == 0x20, "IAction::m_subContext offset mismatch");
            static_assert(offsetof(IAction, m_rootScope) == 0x30, "IAction::m_rootScope offset mismatch");
            static_assert(offsetof(IAction, m_speedBias) == 0x5C, "IAction::m_speedBias offset mismatch");
            static_assert(offsetof(IAction, m_animWeight) == 0x60, "IAction::m_animWeight offset mismatch");
            static_assert(offsetof(IAction, m_mannequinParams) == 0x68, "IAction::m_mannequinParams offset mismatch");
        }
    };

    static_assert(sizeof(IAction) == 0x90, "IAction size mismatch");

    template <typename TContext>
    class TAction: public IAction {
      public:
        /**
         * A new action at refcount zero. `priority` is Mannequin's: an action
         * displaces one of lower priority on the scopes they share. The game's
         * own `C_Player::PlayAnim` and its item-handling actions use 5.
         * `flags` are the constructor's (see GetFlags); the game's
         * C_CallbackAction passes them in the same place.
         */
        static TAction *Create(std::uint32_t fragment, const SFragTagState &tags, std::int32_t priority, std::uint32_t flags = 0) {
            auto *storage = hook::call<void *>(gPatterns.CryActionAllocate, sizeof(TAction));
            if (storage == nullptr) {
                return nullptr;
            }
            return hook::this_call<TAction *>(gPatterns.TAction__Construct, storage, priority, fragment, &tags, flags, 0U, 0U);
        }
    };

    static_assert(sizeof(TAction<SAnimationContext>) == 0x90, "TAction size mismatch");
} // namespace SDK

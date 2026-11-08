#pragma once

#include "../framework/wuid.h"
#include "c_rpg_module.h"
#include "i_cause.h"
#include "s_alchemy_started_data.h"
#include "s_missile_hit_data.h"

#include <algorithm>

namespace SDK::wh::rpgmodule {
    class C_Event;

    class C_Effect {
      public:
        virtual ~C_Effect()       = default;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual bool Apply()      = 0;

        // The member return buffer occupies RDX, including for WUID.
        virtual framework::WUID *GetSubjectWuid(framework::WUID *result) const = 0;
        virtual std::uint8_t Prepare()                                         = 0;
        virtual void unk_fn0006()                                              = 0;
        virtual void unk_fn0007()                                              = 0;
        virtual void unk_fn0008()                                              = 0;

        C_Event *GetEvent() const {
            return m_event;
        }

      private:
        friend class C_Event;

        C_Event *m_event; // 0x08

        static void AssertLayout() {
            static_assert(offsetof(C_Effect, m_event) == 0x8);
        }
    };

    static_assert(sizeof(C_Effect) == 0x10);

    struct MissileEffectSubjects {
        std::uint64_t soul;
        std::uint64_t weapon;
        std::uint64_t ammunition;

        bool Contains(std::uint64_t subject) const {
            return subject != 0 && subject != UINT64_MAX && (subject == soul || subject == weapon || subject == ammunition);
        }
    };

    class C_Event: public I_Event {
      public:
        // Creates an empty event and cause; does not calculate another hit.
        static C_Event *CreateForMissileHit(I_EventManager &manager, const S_MissileHitData &data) {
            S_EventHandle182D291D0 result;

            hook::this_call<void>(gPatterns.C_EventFactory__CreateMissileHitEvent, manager.GetEventFactory(), &result, &data);

            return static_cast<C_Event *>(result.event);
        }

        /**
         * An empty event about one soul, to hang effects on and hand to
         * `ProcessEvent` -- the way `C_ScriptBindSoul::AddSkillXP` does it.
         * The caller owns the returned reference and releases it.
         */
        static C_Event *CreateForSoul(I_EventManager &manager, framework::WUID soul) {
            S_EventHandle182D291D0 result;
            const S_AlchemyStartedData data {soul};

            hook::this_call<void>(gPatterns.C_EventFactory__CreateAlchemyStartedEvent, manager.GetEventFactory(), &result, &data);

            return static_cast<C_Event *>(result.event);
        }

        /** Never null on an event the game built; every Create<X>Event passes a cause. */
        I_Cause *GetCause() const {
            return m_cause;
        }

        std::size_t GetEffectCount() const {
            return m_effects.begin != nullptr ? static_cast<std::size_t>(m_effects.end - m_effects.begin) : 0;
        }

        C_Effect *GetEffectAt(std::size_t index) const {
            return m_effects.begin[index];
        }

        std::size_t MoveEffectsTo(C_Event &destination, const MissileEffectSubjects &subjects) {
            if (&destination == this) {
                return 0;
            }

            std::size_t moved = 0;

            for (std::size_t index = 0; index < GetEffectCount();) {
                auto *effect = GetEffectAt(index);
                framework::WUID subject {};

                effect->GetSubjectWuid(&subject);

                if (!subjects.Contains(subject.value)) {
                    ++index;
                    continue;
                }

                // Grow through the game allocator before transferring effect ownership.
                hook::this_call<C_Effect **>(gPatterns.C_EffectVector__EmplaceBack, &destination.m_effects, &effect);
                effect->m_event = &destination;
                std::move(m_effects.begin + index + 1, m_effects.end, m_effects.begin + index);
                --m_effects.end;
                ++moved;
            }

            return moved;
        }

      private:
        // Game-owned vector storage; C_Event releases its effects.
        struct EffectStorage {
            C_Effect **begin;
            C_Effect **end;
            C_Effect **capacity;
        };

        static_assert(sizeof(EffectStorage) == 0x18);
        static_assert(offsetof(EffectStorage, begin) == 0);
        static_assert(offsetof(EffectStorage, end) == 0x8);
        static_assert(offsetof(EffectStorage, capacity) == 0x10);

        I_Cause *m_cause;           // 0x08
        EffectStorage m_effects;    // 0x10
        std::uint32_t m_state;      // 0x28
        std::uint32_t m_references; // 0x2C
        std::uint8_t m_flags;       // 0x30
        std::byte m_padding0031[0x7];

        static void AssertLayout() {
            static_assert(offsetof(C_Event, m_cause) == 0x8);
            static_assert(offsetof(C_Event, m_effects) == 0x10);
            static_assert(offsetof(C_Event, m_state) == 0x28);
            static_assert(offsetof(C_Event, m_references) == 0x2C);
            static_assert(offsetof(C_Event, m_flags) == 0x30);
        }
    };

    static_assert(sizeof(C_Event) == 0x38);
} // namespace SDK::wh::rpgmodule

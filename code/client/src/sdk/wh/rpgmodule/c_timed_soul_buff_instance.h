#pragma once

#include "c_soul_buff_instance.h"

#include <cstddef>
#include <optional>
#include <string_view>

namespace SDK::wh::rpgmodule {
    /**
     * A buff that expires after a fixed amount of work.
     *
     * Constructor 0x18046C4A4; its factory 0x18046C3D0 allocates 0xE8 bytes.
     * Unrelated implementations store other types at these same offsets, so
     * an instance may be read as this class only when its definition names
     * one of the implementations `FindTimerTraits` knows -- each one's
     * factory constructs this class (or a single-inheritance subclass of it)
     * and its update advances these two doubles through `UpdateDurationRaw`
     * (0x1811908DC).
     */
    class C_TimedSoulBuffInstance: public C_SoulBuffInstance {
      public:
        /** What is proven about one implementation's timer. */
        struct TimerTraits {
            /** The update scales its real delta by the world-time ratio, so both doubles count game seconds. */
            bool worldTime;
        };

        /** The timer of an implementation name, or nothing when its layout is not proven to be this class. */
        static std::optional<TimerTraits> FindTimerTraits(const char *implementation) {
            struct Entry {
                std::string_view name;
                TimerTraits traits;
            };
            static constexpr Entry kEntries[] = {
                {"Cpp:BasicTimed", {false}},
                {"Cpp:WorldTimeTimed", {true}},
                {"Cpp:NonHealingPotionBasicTimed", {false}},
                {"Cpp:NonHealingPotionAnimal", {false}},
                {"Cpp:NonHealingPotionFading", {false}},
                {"Cpp:NonHealingPotionNonReward", {false}},
                {"Cpp:PotionHealth", {false}},
                {"Cpp:PoisoningBasicTimed", {false}},
                {"Cpp:PoisoningWorldTimeTimed", {true}},
                {"Cpp:Fading", {false}},
                {"Cpp:Perfume", {false}},
                {"Cpp:Hangover", {true}},
            };
            if (implementation == nullptr) {
                return std::nullopt;
            }
            const std::string_view name(implementation);
            for (const Entry &entry : kEntries) {
                if (entry.name == name) {
                    return entry.traits;
                }
            }
            return std::nullopt;
        }

        /** In the implementation's own unit: game seconds where `TimerTraits::worldTime`, real seconds otherwise. */
        double GetDuration() const {
            return m_duration;
        }
        double GetElapsed() const {
            return m_elapsed;
        }
        /** Writes both doubles; the update carries on from `elapsed` toward `duration`. */
        void SetTimer(double duration, double elapsed) {
            m_duration = duration;
            m_elapsed  = elapsed;
        }

      private:
        double m_duration; // 0xD8
        double m_elapsed;  // 0xE0

        static void AssertLayout() {
            static_assert(offsetof(C_TimedSoulBuffInstance, m_duration) == 0xD8, "C_TimedSoulBuffInstance::m_duration offset mismatch");
            static_assert(offsetof(C_TimedSoulBuffInstance, m_elapsed) == 0xE0, "C_TimedSoulBuffInstance::m_elapsed offset mismatch");
        }
    };
    static_assert(sizeof(C_TimedSoulBuffInstance) == 0xE8, "C_TimedSoulBuffInstance size mismatch");
} // namespace SDK::wh::rpgmodule

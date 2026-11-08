#pragma once

#include "c_actor.h"

#include <cstddef>
#include <cstdint>

namespace SDK::wh::entitymodule {
    /** Exact constructor-proven layout of C_Animal's derived state. */
    struct AnimalRuntimeStateLayout {
        std::int32_t m_state0000;       // 0x00, initialized to -1
        std::uint32_t m_state0004[3];   // 0x04
        std::uint64_t m_state0010;      // 0x10, initialized to 0xA00000
        std::uint64_t m_state0018;      // 0x18
        std::uint32_t m_state0020;      // 0x20
        std::uint8_t m_state0024;       // 0x24
        std::byte m_padding0025[0x3];
    };

    static_assert(sizeof(AnimalRuntimeStateLayout) == 0x28,
                  "AnimalRuntimeStateLayout size mismatch");

    /**
     * Shared animal actor base.
     *
     * C_Animal's primary table at 0x183A3A7C0 contains exactly 280 slots:
     * all 275 C_Actor entries followed by five opaque C_Animal contracts.
     * Its factory allocates exactly 0x9E8 bytes.
     */
    class C_Animal : public C_Actor {
      public:
        static constexpr const char *ActorClassName = "C_Animal";
        static constexpr ActorClass ActorClassId = ActorClass_Animal;

        ~C_Animal() override = default;

        virtual void unk_fn0275() = 0; // first C_Animal slot, +0x898
        virtual void unk_fn0276() = 0;
        virtual void unk_fn0277() = 0;
        virtual void unk_fn0278() = 0;
        virtual void unk_fn0279() = 0; // final C_Animal slot, +0x8B8

        AnimalRuntimeStateLayout *GetAnimalRuntimeState() { return &m_runtimeState; }
        const AnimalRuntimeStateLayout *GetAnimalRuntimeState() const { return &m_runtimeState; }

      private:
        AnimalRuntimeStateLayout m_runtimeState; // 0x9C0
    };

    static_assert(sizeof(C_Animal) == 0x9E8, "C_Animal size mismatch");
} // namespace SDK::wh::entitymodule

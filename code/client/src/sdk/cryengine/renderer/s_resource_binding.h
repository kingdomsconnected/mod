#pragma once

#include <cstdint>

namespace SDK {
    /**
     * Packed key identifying one shader resource bind point.
     *
     * yasli registers the exact 0x10-byte extent. VectorMap ordering compares
     * the leading qword directly; the second qword's private packed semantics
     * are deliberately left unnamed.
     */
    struct SResourceBindPoint {
        std::uint64_t GetPackedKey() const { return m_packedKey0000; }

        std::uint64_t m_packedKey0000; // 0x00
        std::uint64_t m_state0008;     // 0x08
    };

    /**
     * Resource and view retained for a shader bind point.
     *
     * yasli registers the exact 0x10-byte extent. Resource-set clear/copy
     * paths prove that +0x00 is the resource reference used for invalidation
     * registration; +0x08 carries its associated view/flags payload.
     */
    struct SResourceBinding {
        std::uint64_t GetResourceReferenceRaw() const {
            return m_resourceReference0000;
        }
        std::uint64_t GetViewAndFlagsRaw() const {
            return m_viewAndFlags0008;
        }
        bool HasResource() const { return m_resourceReference0000 != 0; }

        std::uint64_t m_resourceReference0000; // 0x00, UResourceReference
        std::uint64_t m_viewAndFlags0008;       // 0x08
    };

    /** Exact VectorMap element pairing a bind-point key with its binding. */
    struct ResourceBindingPairLayout {
        SResourceBindPoint bindPoint; // 0x00
        SResourceBinding binding;     // 0x10
    };

    static_assert(sizeof(SResourceBindPoint) == 0x10,
                  "SResourceBindPoint size mismatch");
    static_assert(sizeof(SResourceBinding) == 0x10,
                  "SResourceBinding size mismatch");
    static_assert(sizeof(ResourceBindingPairLayout) == 0x20,
                  "ResourceBindingPairLayout size mismatch");
} // namespace SDK

#pragma once

#include <cstddef>
#include <cstdint>

namespace SDK {
    /** Base blend-space sampler retained by a live CAnimation. */
    struct SParametricSampler {
        virtual void unk_fn0000() = 0;
        virtual void unk_fn0001() = 0;
        virtual void unk_fn0002() = 0;
        virtual ~SParametricSampler() = default;

        std::uint8_t m_parametricType; // 0x08
        std::uint8_t m_dimensionCount; // 0x09
        std::byte m_padding000A[0x2];
        float m_motionParameters[4]; // 0x0C
        std::uint8_t m_motionParameterIds[4];   // 0x1C, 0xFF when unused
        std::uint8_t m_motionParameterFlags[4]; // 0x20, CA_Dimension_Flags
        std::byte m_padding0024[0x4];
    };

    static_assert(sizeof(SParametricSampler) == 0x28,
                  "SParametricSampler size mismatch");

    /**
     * Exact KCD2 blend-space sampler stored behind CAnimation::m_pParametricSampler.
     *
     * Native construction and scalar deletion independently prove the 0x4C0
     * allocation. KCD2 supports up to 46 contributing examples; the selected
     * animation IDs, live segment indices, and blend weights are consumed by
     * normalized-time calculation and segment selection.
     */
    struct SParametricSamplerInternal : SParametricSampler {
        static constexpr std::uint32_t MaxExamples = 46;

        ~SParametricSamplerInternal() override = default;

        std::uint32_t GetExampleCount() const {
            return m_exampleCount <= MaxExamples ? m_exampleCount : MaxExamples;
        }

        std::int32_t GetExampleAnimationId(std::uint32_t index) const {
            return index < GetExampleCount() ? m_exampleAnimationIds[index] : -1;
        }

        std::uint8_t GetExampleSegmentIndex(std::uint32_t index) const {
            return index < GetExampleCount() ? m_exampleSegmentIndices[index] : 0;
        }

        float GetExampleBlendWeight(std::uint32_t index) const {
            return index < GetExampleCount() ? m_exampleBlendWeights[index] : 0.0f;
        }

        std::int32_t GetSelectedExampleIndex() const {
            return m_selectedExampleIndex;
        }

      private:
        std::uint32_t m_exampleCount;                    // 0x028
        std::int32_t m_exampleAnimationIds[MaxExamples]; // 0x02C
        std::uint8_t m_nLMGAnimIdx[0x40];                 // 0x0E4, MAX_LMG_EXAMPLES
        std::uint8_t m_nSegmentCounterPrev[2][MaxExamples]; // 0x124
        std::uint8_t m_exampleSegmentIndices[MaxExamples]; // 0x180
        std::uint8_t m_nSegmentCounterRow1[MaxExamples];   // 0x1AE, m_nSegmentCounter[1]
        float m_exampleBlendWeights[MaxExamples]; // 0x1DC
        float m_fPlaybackScale[MaxExamples];               // 0x294
        float m_values034C[MaxExamples];                   // 0x34C, KCD2's own per-example values
        float m_values0404[MaxExamples];                   // 0x404, KCD2's own per-example values
        std::int32_t m_selectedExampleIndex; // 0x4BC
    };

    static_assert(sizeof(SParametricSamplerInternal) == 0x4C0,
                  "SParametricSamplerInternal size mismatch");
} // namespace SDK

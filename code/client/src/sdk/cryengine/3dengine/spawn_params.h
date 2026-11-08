#pragma once

#include "../../types.h"
#include "../audio/i_audio_interfaces_common_data.h"

#include <cstdint>

namespace SDK {
    /** What a particle emitter is attached to, when `SpawnParams::attachType` is not `None`. */
    enum class EGeomType : std::int32_t {
        None        = 0,
        BoundingBox = 1,
        Physics     = 2,
        Render      = 3,
    };

    /** Which aspect of the attached shape particles come off. */
    enum class EGeomForm : std::int32_t {
        Vertices = 0,
        Edges    = 1,
        Surface  = 2,
        Volume   = 3,
    };

    /**
     * Per-emitter overrides that `IParticleEffect::Spawn` and
     * `IEntity::LoadParticleEmitter` both take by pointer and both accept null
     * for.
     *
     * CryEngine 5.x field for field, plus one Warhorse `bool` at `0x40`. The
     * offsets are proven twice: the default constructor at 0x180422DA4 writes
     * each one where it is given here, and `CScriptBind_Entity::LoadParticleEffect`
     * (0x180F2D600) reads each named Lua property into the matching stack slot.
     * The extent comes from the compiler-generated `operator=` at 0x1807390CC,
     * which copies to the byte at `0x40` and no further.
     *
     * **Build one only through `MakeSpawnParams`.** `audioRtpc`'s empty state
     * is a pointer twelve bytes into a refcount header the engine's allocator
     * lays out, so a zeroed or hand-written struct hands the emitter a null it
     * dereferences.
     *
     * Copying one is a bitwise copy of that pointer with no refcount bump,
     * which is safe only because the empty header is immortal and nothing here
     * ever assigns a real string over it. Giving `audioRtpc` a value would make
     * every copy a double free, so that has to go through the engine's own
     * `operator=` (0x1807390CC) if it is ever wanted.
     */
    struct SpawnParams {
        /** Advance the emitter to its equilibrium state, as if it had been running. What a fire or a torch wants at spawn. */
        bool prime;                   // 0x00
        bool ignoreVisAreas;          // 0x01
        bool registerByBBox;          // 0x02
        bool nowhere;                 // 0x03
        bool placed;                  // 0x04
        bool ignoreTerrainLayerBlend; // 0x05, defaults true
        bool ignoreDecalBlend;        // 0x06, defaults true
        std::uint8_t reserved_07;     // 0x07
        float countScale;             // 0x08
        float sizeScale;              // 0x0C
        float speedScale;             // 0x10
        float timeScale;              // 0x14
        /** Seconds between restarts of the whole emitter; 0 never restarts it. */
        float pulsePeriod; // 0x18
        /** Drives the effect's own strength curves; -1 leaves them at what the effect authored. */
        float strength; // 0x1C
        /** -1 picks a random seed, which is what a one-off burst wants. */
        std::int32_t seed;                 // 0x20
        std::int32_t spec;                 // 0x24
        EGeomType attachType;              // 0x28
        EGeomForm attachForm;              // 0x2C, defaults Surface
        bool countPerUnit;                 // 0x30
        bool enableAudio;                  // 0x31, defaults true
        std::uint8_t reserved_32[2];       // 0x32
        EAudioOcclusionType occlusionType; // 0x34, defaults Ignore
        /** Owned by the engine's allocator; see the class comment. */
        CryStringT<char> audioRtpc; // 0x38
        /** Warhorse's own addition, defaulting true; the stock struct ends at 0x38. */
        bool lodUpdateEnabled;       // 0x40
        std::uint8_t reserved_41[7]; // 0x41

      private:
        static void AssertLayout() {
            static_assert(offsetof(SpawnParams, prime) == 0x00, "SpawnParams::prime offset mismatch");
            static_assert(offsetof(SpawnParams, ignoreVisAreas) == 0x01, "SpawnParams::ignoreVisAreas offset mismatch");
            static_assert(offsetof(SpawnParams, registerByBBox) == 0x02, "SpawnParams::registerByBBox offset mismatch");
            static_assert(offsetof(SpawnParams, nowhere) == 0x03, "SpawnParams::nowhere offset mismatch");
            static_assert(offsetof(SpawnParams, placed) == 0x04, "SpawnParams::placed offset mismatch");
            static_assert(offsetof(SpawnParams, ignoreTerrainLayerBlend) == 0x05, "SpawnParams::ignoreTerrainLayerBlend offset mismatch");
            static_assert(offsetof(SpawnParams, ignoreDecalBlend) == 0x06, "SpawnParams::ignoreDecalBlend offset mismatch");
            static_assert(offsetof(SpawnParams, countScale) == 0x08, "SpawnParams::countScale offset mismatch");
            static_assert(offsetof(SpawnParams, sizeScale) == 0x0C, "SpawnParams::sizeScale offset mismatch");
            static_assert(offsetof(SpawnParams, speedScale) == 0x10, "SpawnParams::speedScale offset mismatch");
            static_assert(offsetof(SpawnParams, timeScale) == 0x14, "SpawnParams::timeScale offset mismatch");
            static_assert(offsetof(SpawnParams, pulsePeriod) == 0x18, "SpawnParams::pulsePeriod offset mismatch");
            static_assert(offsetof(SpawnParams, strength) == 0x1C, "SpawnParams::strength offset mismatch");
            static_assert(offsetof(SpawnParams, seed) == 0x20, "SpawnParams::seed offset mismatch");
            static_assert(offsetof(SpawnParams, spec) == 0x24, "SpawnParams::spec offset mismatch");
            static_assert(offsetof(SpawnParams, attachType) == 0x28, "SpawnParams::attachType offset mismatch");
            static_assert(offsetof(SpawnParams, attachForm) == 0x2C, "SpawnParams::attachForm offset mismatch");
            static_assert(offsetof(SpawnParams, countPerUnit) == 0x30, "SpawnParams::countPerUnit offset mismatch");
            static_assert(offsetof(SpawnParams, enableAudio) == 0x31, "SpawnParams::enableAudio offset mismatch");
            static_assert(offsetof(SpawnParams, occlusionType) == 0x34, "SpawnParams::occlusionType offset mismatch");
            static_assert(offsetof(SpawnParams, audioRtpc) == 0x38, "SpawnParams::audioRtpc offset mismatch");
            static_assert(offsetof(SpawnParams, lodUpdateEnabled) == 0x40, "SpawnParams::lodUpdateEnabled offset mismatch");
        }
    };

    static_assert(sizeof(SpawnParams) == 0x48, "SpawnParams size mismatch");

    /**
     * A `SpawnParams` as the engine's own default constructor leaves it: every
     * scale at 1, strength and seed at -1, occlusion ignored, `audioRtpc` on
     * the shared empty-string header.
     *
     * Destroying the result needs nothing: that header is immortal and the mod
     * never assigns a real string over it, so no buffer is ever owned.
     */
    SpawnParams MakeSpawnParams();
} // namespace SDK

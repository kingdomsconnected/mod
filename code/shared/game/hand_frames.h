#pragma once

#include <cstdint>
#include <vector>

namespace KCDC::Shared::Game {
    enum class HandFrameKind : std::uint8_t {
        // The joint takes `frame`; its parent's frame change reaches it too.
        Frame,
        // Never touched: the joint's translation carries data, not a pose.
        Verbatim,
    };

    /** A rotation as x, y, z, w, the order the engine's quaternions use. */
    struct HandFrameRotation {
        float x;
        float y;
        float z;
        float w;
    };

    /**
     * One joint of a woman's hand, with the frame change that puts it on the
     * man's: C = inverse(her world bind rotation) * his. A rotation authored
     * on her skeleton becomes inverse(C(parent)) * q * C(joint).
     */
    struct HandFrameEntry {
        const char *joint;
        // The plain CRC32 of the name, as the .chr and every clip key it.
        std::uint32_t jointCrc;
        std::uint32_t parentCrc;
        HandFrameKind kind;
        HandFrameRotation frame;
    };

    extern const std::vector<HandFrameEntry> g_HandFrames;

    inline const HandFrameEntry *FindHandFrame(std::uint32_t jointCrc) {
        for (const auto &entry : g_HandFrames) {
            if (entry.jointCrc == jointCrc) {
                return &entry;
            }
        }
        return nullptr;
    }
} // namespace KCDC::Shared::Game

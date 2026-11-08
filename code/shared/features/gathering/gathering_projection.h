#pragma once

#include <glm/vec3.hpp>

#include <compare>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace KCDC::Shared::Gathering {
    /** Authored coordinates identify one source; nearby plants remain distinct. */
    struct SourceKey {
        std::uint8_t kind = 0;
        float x           = 0;
        float y           = 0;
        float z           = 0;

        auto operator<=>(const SourceKey &) const = default;
    };

    struct ProjectedSource {
        std::uint64_t until = 0;
        std::uint8_t kind   = 0;
        glm::vec3 position {0.0F};
    };

    /** Client projection bookkeeping, independent of the native respawn manager. */
    struct Projection {
        std::optional<std::uint32_t> virtualWorld;
        std::map<SourceKey, ProjectedSource> depletions;
        std::vector<SourceKey> pending;
        std::vector<ProjectedSource> pendingRespawns;
        std::set<SourceKey> claimed;
        std::uint64_t syncGeneration = 1;
        bool fullSyncRequired        = true;
        bool replayPending           = false;

        void InvalidateSync() {
            // Zero is reserved for requests without generation correlation.
            if (++syncGeneration == 0) {
                ++syncGeneration;
            }
            fullSyncRequired = true;
        }

        bool CompleteSync(std::uint64_t generation) {
            if (generation != syncGeneration) {
                return false;
            }
            fullSyncRequired = false;
            replayPending    = false;
            return true;
        }

        static SourceKey KeyOf(std::uint8_t kind, const glm::vec3 &position) {
            return {kind, position.x, position.y, position.z};
        }

        bool AcceptDelivery(std::optional<std::uint64_t> generation) const {
            return !replayPending || generation == syncGeneration;
        }

        void RestoreClaims() {
            // Retain older queued restorations and ownership until the native
            // tick actually restores them, including during session cleanup.
            for (const auto &[key, state] : depletions) {
                if (claimed.contains(key)) {
                    pendingRespawns.push_back(state);
                }
            }
            depletions.clear();
            pending.clear();
        }

        void RecoverOverflow() {
            RestoreClaims();
            InvalidateSync();
            // Old ordered deliveries may still be in flight. Only a replay
            // requested after the loss can establish the current projection.
            replayPending = true;
        }

        bool ChangeWorld(std::uint32_t world) {
            if (virtualWorld == world) {
                return false;
            }
            if (!virtualWorld) {
                virtualWorld = world;
                return true;
            }
            RestoreClaims();
            virtualWorld = world;
            InvalidateSync();
            return true;
        }

        void Forget() {
            InvalidateSync();
            virtualWorld.reset();
            depletions.clear();
            pending.clear();
            pendingRespawns.clear();
            claimed.clear();
        }
    };
} // namespace KCDC::Shared::Gathering

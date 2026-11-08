#pragma once

#include "core/feature_registry.h"

#include "sdk/cryengine/3dengine/i_material.h"
#include "sdk/cryengine/3dengine/i_render_node.h"
#include "sdk/types.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace KCDC::Features::Prop {
    /** One mesh inside a ghost, posed in the ghost's own frame. */
    struct GhostPart {
        std::string model; // `objects/....cgf`, a path from the prop catalog or a mounted streamed one
        SDK::Vec3 offset {0.0F, 0.0F, 0.0F};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
        float scale = 1.0F;

        /**
         * A further scale along each of the part's own axes, on top of
         * `scale`: a unit cylinder stretched along its height is a segment
         * of a line.
         */
        SDK::Vec3 stretch {1.0F, 1.0F, 1.0F};
    };

    /**
     * Red, green and blue in the top three bytes, alpha in the low one.
     *
     * Zero means "leave every mesh its own materials" rather than black.
     * Alpha below 255 is what makes a ghost translucent.
     */
    using GhostTint = std::uint32_t;

    inline constexpr GhostTint kGhostUntinted = 0;

    constexpr GhostTint PackGhostTint(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) {
        return (static_cast<GhostTint>(red) << 24) | (static_cast<GhostTint>(green) << 16) | (static_cast<GhostTint>(blue) << 8) | static_cast<GhostTint>(alpha);
    }

    /**
     * Previews: geometry that is in the world and is not part of it.
     *
     * Each part is a `CBrush` render node carrying a stat object and a private
     * copy of its material -- not an entity, which would bring a system
     * record, a script table and a physics proxy a preview has no use for.
     *
     * Nothing here collides. `CBrush` physicalises itself the first time it is
     * given geometry or moved unless bit 1 of its render state is set, so
     * `DisablePhysicalization(true)` is the first call made on every node and
     * `Physicalize` is never called at all -- it would clear that bit again.
     *
     * Game thread only, except `NativeContextChanged`.
     */
    class PropGhostService final: public KCDC::Core::IFeatureService {
      public:
        /** Never handed out, so it is the "no ghost" answer everywhere. */
        static constexpr std::uint32_t kNoHandle = 0;

        /** Bounds on a leaking caller, rather than on anything the engine minds. */
        static constexpr std::size_t kMaxLiveGhosts    = 64;
        static constexpr std::size_t kMaxPartsPerGhost = 32;

        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;

        /**
         * Builds a ghost from `parts`, tinted, at a pose. `kNoHandle` when the
         * world is not up, the budget is spent, or no part's mesh would load;
         * a ghost that lost some of its parts is still built.
         */
        std::uint32_t Create(const std::vector<GhostPart> &parts, GhostTint tint, const SDK::Vec3 &position, const SDK::Quat &rotation, float scale);

        /** Restates the pose. Called every frame for one that follows a crosshair. */
        bool SetPose(std::uint32_t handle, const SDK::Vec3 &position, const SDK::Quat &rotation, float scale);

        /**
         * Moves each part within the ghost: `layout[i]`'s offset, rotation,
         * scale and stretch for part `i`; its model is not read, and parts
         * past the end of `layout` stay where they are. Cheap enough to call
         * every frame -- one `SetMatrix` a part -- where building the ghost
         * again would load and tint every mesh anew. False for a ghost that
         * has gone.
         */
        bool SetParts(std::uint32_t handle, const std::vector<GhostPart> &layout);

        /** Recolours it. A tint it already carries is dropped rather than re-applied. */
        bool SetTint(std::uint32_t handle, GhostTint tint);

        /** Takes it out of the draw without dropping it. */
        bool SetHidden(std::uint32_t handle, bool hidden);

        /** The union of its parts' world bounds. False when it has no geometry. */
        bool Bounds(std::uint32_t handle, SDK::AABB &bounds) const;

        bool Destroy(std::uint32_t handle);
        void DestroyAll();

        bool IsAlive(std::uint32_t handle) const;

        /** Whether `node` is a preview, so the editor's picker can skip it. */
        bool OwnsNode(const SDK::IRenderNode *node) const {
            return node != nullptr && _nodes.contains(node);
        }
        const std::unordered_set<const SDK::IRenderNode *> &Nodes() const {
            return _nodes;
        }
        std::size_t LiveCount() const {
            return _ghosts.size();
        }

      private:
        struct Part {
            SDK::IRenderNode *node = nullptr;

            /** Where this part sits in the ghost's own frame. */
            SDK::Vec3 offset {0.0F, 0.0F, 0.0F};
            SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
            float scale = 1.0F;
            SDK::Vec3 stretch {1.0F, 1.0F, 1.0F};

            /** The mesh's own material, borrowed, for putting an untinted ghost back. */
            SDK::IMaterial *ownMaterial = nullptr;

            /**
             * This part's private copy once it has been tinted, or null.
             *
             * `SetMaterial` assigns through a smart pointer that releases what
             * the node held before, and nothing else references a clone -- so
             * the moment this is swapped off the node it is freed. It is
             * swapped off exactly once, and forgotten in the same breath.
             */
            SDK::IMaterial *tinted = nullptr;
        };

        struct Ghost {
            std::uint32_t handle = kNoHandle;
            std::vector<Part> parts;

            /** The ghost's own pose, as last set, which its parts are placed in. */
            SDK::Vec3 position {0.0F, 0.0F, 0.0F};
            SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
            float scale = 1.0F;

            GhostTint tint = kGhostUntinted;
            bool hidden    = false;
        };

        Ghost *Find(std::uint32_t handle);
        const Ghost *Find(std::uint32_t handle) const;

        bool HasBudget();

        void Release(const Ghost &ghost) const;
        void ApplyPose(Ghost &ghost, const SDK::Vec3 &position, const SDK::Quat &rotation, float scale) const;

        /**
         * Puts `tint` on every part, cloning each part's material the first
         * time it needs one and recolouring the copy it already has after.
         *
         * Cloned because a shipped material is shared by every brush in the
         * level that names it. The copy is deep: passing no sub-material name
         * clones every sub-material too, which is what lets a tint reach a
         * mesh with several material slots instead of only slot 0.
         */
        void ApplyTint(Ghost &ghost, GhostTint tint);

        /** Writes `diffuse` and `opacity` on the material and every sub-material it has. */
        static std::size_t WriteTint(SDK::IMaterial *material, GhostTint tint);

        /** Drops every ghost after a level change. Called from every public entry point: outside a session nothing ticks this service. */
        void ObserveNativeContext();

        std::unordered_map<std::uint32_t, Ghost> _ghosts;

        /** Every live part's node, for `OwnsNode`. */
        std::unordered_set<const SDK::IRenderNode *> _nodes;

        std::uint32_t _nextHandle = 1;
        bool _warnedAtBudget      = false;

        std::atomic<std::uint64_t> _nativeContext {0};
        std::uint64_t _observedContext = 0;
    };
} // namespace KCDC::Features::Prop

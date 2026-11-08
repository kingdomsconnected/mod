#include "prop_ghost.h"

// Before the replication headers: they bring WinSock2, which has to precede
// whatever pulls in Windows.h.
#include <utils/safe_win32.h>

#include "common/module_support.h"
#include "common/vector_math.h"

#include "sdk/cryengine/3dengine/i_3d_engine.h"
#include "sdk/cryengine/3dengine/i_brush.h"
#include "sdk/cryengine/3dengine/i_material_manager.h"
#include "sdk/cryengine/3dengine/i_stat_obj.h"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <logging/logger.h>

namespace KCDC::Features::Prop {
    namespace {
        constexpr const char *kLogger = "PropGhost";

        /** A guard against a corrupt material; `GetSubMtl` answers null past the end anyway. */
        constexpr int kMaxSubMaterials = 64;

        float ClampScale(float scale) {
            return std::isfinite(scale) ? std::clamp(scale, 0.01F, 100.0F) : 1.0F;
        }

        /** The ghost's own frame composed onto a part's, in glm because that is what composes. */
        SDK::Matrix34 PartMatrix(const SDK::Vec3 &position, const SDK::Quat &rotation, float scale, const SDK::Vec3 &offset, const SDK::Quat &partRotation, float partScale, const SDK::Vec3 &stretch) {
            const glm::quat base  = glm::normalize(KCDC::Common::ToGlm(rotation));
            const glm::vec3 local = KCDC::Common::ToGlm(offset) * scale;
            const glm::vec3 world = KCDC::Common::ToGlm(position) + base * local;
            const glm::quat spin  = glm::normalize(base * glm::normalize(KCDC::Common::ToGlm(partRotation)));

            const float total = scale * partScale;
            return SDK::Matrix34FromTransform(KCDC::Common::ToNative(world), KCDC::Common::ToNative(spin), SDK::Vec3 {total * stretch.x, total * stretch.y, total * stretch.z});
        }

        /**
         * Colours one material and tells the engine it now needs the pass that
         * can blend it.
         *
         * The flag is not optional. `CMatInfo::UpdateMaterialFlags` classifies
         * a material forward-or-deferred from the opacity it was *parsed*
         * with and never re-runs, so an opacity written at runtime is dropped
         * unless the flag is set by hand.
         */
        bool WriteOneTint(SDK::IMaterial *material, const SDK::Vec3 &diffuse, float opacity) {
            const bool tookDiffuse = material->SetMaterialParamVec3("diffuse", diffuse);
            const bool tookOpacity = material->SetMaterialParamFloat("opacity", opacity);

            if (opacity < 1.0F) {
                material->SetFlags(material->GetFlags() | SDK::MTL_FLAG_REQUIRE_FORWARD_RENDERING | SDK::MTL_FLAG_NOSHADOW);
            }

            return tookDiffuse || tookOpacity;
        }

        /** Two boxes joined; `into` may be empty, which is what `first` says. */
        void Grow(SDK::AABB &into, const SDK::AABB &box, bool &first) {
            if (first) {
                into  = box;
                first = false;
                return;
            }
            into.min = SDK::Vec3 {std::min(into.min.x, box.min.x), std::min(into.min.y, box.min.y), std::min(into.min.z, box.min.z)};
            into.max = SDK::Vec3 {std::max(into.max.x, box.max.x), std::max(into.max.y, box.max.y), std::max(into.max.z, box.max.z)};
        }
    } // namespace

    std::size_t PropGhostService::WriteTint(SDK::IMaterial *material, GhostTint tint) {
        if (material == nullptr) {
            return 0;
        }

        SDK::Vec3 diffuse {static_cast<float>((tint >> 24) & 0xFFU) / 255.0F, static_cast<float>((tint >> 16) & 0xFFU) / 255.0F, static_cast<float>((tint >> 8) & 0xFFU) / 255.0F};
        float opacity = static_cast<float>(tint & 0xFFU) / 255.0F;

        std::size_t written = 0;

        // The root first; on a multi-material it carries no shader resources
        // and simply answers false.
        if (WriteOneTint(material, diffuse, opacity)) {
            ++written;
        }

        // Then every slot: a mesh with several material slots draws each
        // submesh through its own, so slot 0 alone leaves the rest opaque.
        for (int index = 0; index < kMaxSubMaterials; ++index) {
            SDK::IMaterial *slot = material->GetSubMtl(index);
            if (slot == nullptr) {
                break;
            }
            if (WriteOneTint(slot, diffuse, opacity)) {
                ++written;
            }
        }

        return written;
    }

    void PropGhostService::ApplyTint(Ghost &ghost, GhostTint tint) {
        for (Part &part : ghost.parts) {
            if (part.node == nullptr) {
                continue;
            }

            if (tint == kGhostUntinted) {
                if (part.tinted != nullptr) {
                    // This releases the clone, so the pointer goes with it.
                    part.node->SetMaterial(part.ownMaterial);
                    part.tinted = nullptr;
                }
                continue;
            }

            if (part.tinted == nullptr) {
                SDK::IMaterialManager *materials = part.ownMaterial != nullptr ? part.ownMaterial->GetMaterialManager() : nullptr;
                SDK::IMaterial *clone            = materials != nullptr ? materials->CloneMaterial(part.ownMaterial, nullptr) : nullptr;
                if (clone == nullptr) {
                    Framework::Logging::GetLogger(kLogger)->warn("The material manager would not clone \"{}\" for tinting", part.ownMaterial != nullptr && part.ownMaterial->GetName() != nullptr ? part.ownMaterial->GetName() : "?");
                    continue;
                }

                // Coloured and flagged before it is handed over, so the node
                // is registered against a material already asking for the
                // forward pass.
                if (WriteTint(clone, tint) == 0) {
                    Framework::Logging::GetLogger(kLogger)->warn("That mesh's material declares neither diffuse nor opacity on any slot, so the ghost cannot be tinted");
                }

                part.node->SetMaterial(clone);
                part.tinted = clone;
            }
            else {
                // Recolour the copy it has; swapping in a second would release
                // the first, which the next flip back wants.
                WriteTint(part.tinted, tint);
            }

            // The octree reads the forward-pass flag when a node registers,
            // so a tint that just turned the material translucent has to put
            // it back through registration.
            if (SDK::I3DEngine *engine = SDK::I3DEngine::GetInstance()) {
                engine->RegisterEntity(part.node);
            }
        }

        ghost.tint = tint;
    }

    bool PropGhostService::HasBudget() {
        if (_ghosts.size() < kMaxLiveGhosts) {
            return true;
        }
        if (!_warnedAtBudget) {
            _warnedAtBudget = true;
            Framework::Logging::GetLogger(kLogger)->warn("Refusing ghosts: {} are already built, which is the budget", kMaxLiveGhosts);
        }
        return false;
    }

    std::uint32_t PropGhostService::Create(const std::vector<GhostPart> &parts, GhostTint tint, const SDK::Vec3 &position, const SDK::Quat &rotation, float scale) {
        ObserveNativeContext();

        SDK::I3DEngine *engine = SDK::I3DEngine::GetInstance();
        if (engine == nullptr || parts.empty() || !HasBudget()) {
            return kNoHandle;
        }

        Ghost ghost;
        ghost.tint = tint;

        const std::size_t wanted = std::min(parts.size(), kMaxPartsPerGhost);
        for (std::size_t index = 0; index < wanted; ++index) {
            const GhostPart &source = parts[index];
            if (source.model.empty()) {
                continue;
            }

            SDK::IStatObj *mesh = engine->LoadStatObj(source.model.c_str(), nullptr, nullptr, true, 0);
            if (mesh == nullptr) {
                Framework::Logging::GetLogger(kLogger)->warn("The game would not load ghost geometry \"{}\"", source.model);
                continue;
            }

            SDK::IRenderNode *node = engine->CreateRenderNode(SDK::eERType_Brush);
            if (node == nullptr) {
                Framework::Logging::GetLogger(kLogger)->warn("The 3D engine would not make a brush node for \"{}\"", source.model);
                continue;
            }
            auto *brush = static_cast<SDK::IBrush *>(node);

            // Before the geometry: `SetEntityStatObj` reads bit 1 of the
            // node's render state and physicalises when it is clear, and so
            // does every later `SetMatrix`. `Physicalize` clears the bit
            // again, so nothing here calls it.
            brush->DisablePhysicalization(true);

            node->SetEntityStatObj(mesh, nullptr);

            // Sorts last in its render list, so a translucent preview draws
            // over what it overlaps instead of fighting it.
            brush->SetDrawLast(true);

            Part part;
            part.node        = node;
            part.offset      = KCDC::Common::IsFinite(source.offset) ? source.offset : SDK::Vec3 {0.0F, 0.0F, 0.0F};
            part.rotation    = KCDC::Common::IsFinite(source.rotation) ? source.rotation : SDK::Quat {0.0F, 0.0F, 0.0F, 1.0F};
            part.scale       = ClampScale(source.scale);
            part.stretch     = SDK::Vec3 {ClampScale(source.stretch.x), ClampScale(source.stretch.y), ClampScale(source.stretch.z)};
            part.ownMaterial = node->GetMaterial(nullptr);
            ghost.parts.push_back(part);
        }

        if (ghost.parts.empty()) {
            return kNoHandle;
        }

        ApplyPose(ghost, position, rotation, ClampScale(scale));

        if (tint != kGhostUntinted) {
            ApplyTint(ghost, tint);
        }

        // The order the level loader uses. `SetMatrix` has usually registered
        // it already; re-registering is idempotent, and covers the case where
        // `SetMatrix` returned early for a matrix the node already had.
        for (const Part &part : ghost.parts) {
            engine->RegisterEntity(part.node);
        }

        ghost.handle = _nextHandle++;
        if (ghost.handle == kNoHandle) {
            ghost.handle = _nextHandle++;
        }

        for (const Part &part : ghost.parts) {
            _nodes.insert(part.node);
        }

        const std::uint32_t handle = ghost.handle;
        _ghosts.emplace(handle, std::move(ghost));
        return handle;
    }

    void PropGhostService::ApplyPose(Ghost &ghost, const SDK::Vec3 &position, const SDK::Quat &rotation, float scale) const {
        ghost.position = position;
        ghost.rotation = rotation;
        ghost.scale    = scale;
        for (const Part &part : ghost.parts) {
            if (part.node != nullptr) {
                // `CBrush::SetMatrix` re-sorts the node into its octree
                // bucket itself, so following a crosshair is this one call.
                part.node->SetMatrix(PartMatrix(position, rotation, scale, part.offset, part.rotation, part.scale, part.stretch));
            }
        }
    }

    bool PropGhostService::SetPose(std::uint32_t handle, const SDK::Vec3 &position, const SDK::Quat &rotation, float scale) {
        Ghost *ghost = Find(handle);
        if (ghost == nullptr || !KCDC::Common::IsFinite(position, rotation)) {
            return false;
        }
        ApplyPose(*ghost, position, rotation, ClampScale(scale));
        return true;
    }

    bool PropGhostService::SetParts(std::uint32_t handle, const std::vector<GhostPart> &layout) {
        Ghost *ghost = Find(handle);
        if (ghost == nullptr) {
            return false;
        }
        const std::size_t count = std::min(layout.size(), ghost->parts.size());
        for (std::size_t index = 0; index < count; ++index) {
            const GhostPart &source = layout[index];
            Part &part              = ghost->parts[index];
            if (!KCDC::Common::IsFinite(source.offset) || !KCDC::Common::IsFinite(source.rotation)) {
                continue;
            }
            part.offset   = source.offset;
            part.rotation = source.rotation;
            part.scale    = ClampScale(source.scale);
            part.stretch  = SDK::Vec3 {ClampScale(source.stretch.x), ClampScale(source.stretch.y), ClampScale(source.stretch.z)};
            if (part.node != nullptr) {
                part.node->SetMatrix(PartMatrix(ghost->position, ghost->rotation, ghost->scale, part.offset, part.rotation, part.scale, part.stretch));
            }
        }
        return true;
    }

    bool PropGhostService::SetTint(std::uint32_t handle, GhostTint tint) {
        Ghost *ghost = Find(handle);
        if (ghost == nullptr) {
            return false;
        }

        // A placement loop calls this every frame and means it on two of
        // them; re-writing the same colour touches every sub-material.
        if (ghost->tint == tint) {
            return true;
        }

        ApplyTint(*ghost, tint);
        return true;
    }

    bool PropGhostService::SetHidden(std::uint32_t handle, bool hidden) {
        Ghost *ghost = Find(handle);
        if (ghost == nullptr) {
            return false;
        }
        if (ghost->hidden == hidden) {
            return true;
        }
        for (const Part &part : ghost->parts) {
            if (part.node != nullptr) {
                part.node->SetHidden(hidden);
            }
        }
        ghost->hidden = hidden;
        return true;
    }

    bool PropGhostService::Bounds(std::uint32_t handle, SDK::AABB &bounds) const {
        const Ghost *ghost = Find(handle);
        if (ghost == nullptr) {
            return false;
        }

        bool first = true;
        for (const Part &part : ghost->parts) {
            if (part.node == nullptr) {
                continue;
            }
            SDK::AABB box {};
            part.node->FillBBox(box);

            // A node with no geometry answers with the inverted box the
            // engine uses as empty, which would swallow the union.
            if (box.min.x > box.max.x || box.min.y > box.max.y || box.min.z > box.max.z) {
                continue;
            }
            Grow(bounds, box, first);
        }
        return !first;
    }

    void PropGhostService::Release(const Ghost &ghost) const {
        SDK::I3DEngine *engine = SDK::I3DEngine::GetInstance();
        if (engine == nullptr) {
            return;
        }
        for (const Part &part : ghost.parts) {
            if (part.node != nullptr) {
                engine->DeleteRenderNode(part.node);
            }
        }
    }

    bool PropGhostService::Destroy(std::uint32_t handle) {
        ObserveNativeContext();

        const auto it = _ghosts.find(handle);
        if (handle == kNoHandle || it == _ghosts.end()) {
            return false;
        }
        for (const Part &part : it->second.parts) {
            _nodes.erase(part.node);
        }
        Release(it->second);
        _ghosts.erase(it);
        return true;
    }

    void PropGhostService::DestroyAll() {
        ObserveNativeContext();

        std::unordered_map<std::uint32_t, Ghost> owned;
        owned.swap(_ghosts);
        _nodes.clear();
        for (const auto &[handle, ghost] : owned) {
            Release(ghost);
        }
    }

    PropGhostService::Ghost *PropGhostService::Find(std::uint32_t handle) {
        ObserveNativeContext();
        return const_cast<Ghost *>(static_cast<const PropGhostService *>(this)->Find(handle));
    }

    const PropGhostService::Ghost *PropGhostService::Find(std::uint32_t handle) const {
        if (handle == kNoHandle) {
            return nullptr;
        }
        const auto it = _ghosts.find(handle);
        return it != _ghosts.end() ? &it->second : nullptr;
    }

    bool PropGhostService::IsAlive(std::uint32_t handle) const {
        return Find(handle) != nullptr;
    }

    void PropGhostService::NativeContextChanged() {
        ++_nativeContext;
    }

    void PropGhostService::ObserveNativeContext() {
        const std::uint64_t current = _nativeContext.load();
        if (current == _observedContext) {
            return;
        }
        _observedContext = current;

        // Dropped rather than released: the octree these point into has gone.
        _ghosts.clear();
        _nodes.clear();
        _warnedAtBudget = false;
    }

    void PropGhostService::Update() {
        ObserveNativeContext();
    }

    void PropGhostService::Reset() {
        // Each ghost's clones go with the nodes that hold them.
        DestroyAll();
        _warnedAtBudget = false;
    }
} // namespace KCDC::Features::Prop

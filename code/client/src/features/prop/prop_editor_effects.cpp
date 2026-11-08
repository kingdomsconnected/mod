#include "prop_editor_effects.h"

#include "core/application.h"
#include "common/module_support.h"
#include "common/vector_math.h"

#include "shared/game/vfx_catalog.h"

#include <algorithm>
#include <cmath>

namespace KCDC::Features::Prop {
    namespace {
        KCDC::Features::Vfx::VfxService *Service() {
            auto *application = Core::gApplication.get();
            return application != nullptr ? &application->GetVfxService() : nullptr;
        }

        /** The last segment of `library.group.effect`. */
        std::string LeafOf(std::string_view name) {
            const std::size_t dot = name.find_last_of('.');
            return std::string {dot == std::string_view::npos ? name : name.substr(dot + 1)};
        }
    } // namespace

    bool EditorEffects::Start(PlacedEffect &effect) {
        if (!effect.editorVisible) {
            return true;
        }
        KCDC::Features::Vfx::VfxService *service = Service();
        if (service == nullptr) {
            return false;
        }
        effect.handle = service->Spawn(Shared::Game::VfxEffectRef::Catalog(effect.effectIndex), KCDC::Common::ToGlm(effect.position), KCDC::Common::ToGlm(effect.rotation), effect.tuning, 0);
        return effect.handle != KCDC::Features::Vfx::VfxService::kNoHandle;
    }

    void EditorEffects::Stop(PlacedEffect &effect) {
        if (KCDC::Features::Vfx::VfxService *service = Service(); service != nullptr && effect.handle != 0) {
            service->Stop(effect.handle, true);
        }
        effect.handle = 0;
    }

    const PlacedEffect *EditorEffects::Find(std::uint64_t id) const {
        const auto found = std::find_if(_effects.begin(), _effects.end(), [id](const PlacedEffect &effect) {
            return effect.id == id;
        });
        return found != _effects.end() ? &*found : nullptr;
    }

    PlacedEffect *EditorEffects::FindMutable(std::uint64_t id) {
        return const_cast<PlacedEffect *>(static_cast<const EditorEffects *>(this)->Find(id));
    }

    std::uint64_t EditorEffects::Place(std::size_t effectIndex, const SDK::Vec3 &position, const SDK::Quat &rotation, const Shared::Game::VfxTuning &tuning) {
        const Shared::Game::VfxEffectEntry *entry = Shared::Game::VfxEffectAt(effectIndex);
        if (entry == nullptr || !tuning.IsSane() || !KCDC::Common::IsFinite(position, rotation)) {
            return 0;
        }

        PlacedEffect effect;
        effect.id          = _nextId++;
        effect.effectIndex = effectIndex;
        effect.name        = std::string {entry->name};
        effect.label       = LeafOf(entry->name);
        effect.position    = position;
        effect.rotation    = rotation;
        effect.tuning      = tuning;
        if (!Start(effect)) {
            return 0;
        }

        _effects.push_back(std::move(effect));
        return _effects.back().id;
    }

    bool EditorEffects::SetTransform(std::uint64_t id, const SDK::Vec3 &position, const SDK::Quat &rotation) {
        PlacedEffect *effect = FindMutable(id);
        if (effect == nullptr || !KCDC::Common::IsFinite(position, rotation)) {
            return false;
        }
        effect->position = position;
        effect->rotation = rotation;

        KCDC::Features::Vfx::VfxService *service = Service();
        if (service == nullptr || !service->Move(effect->handle, KCDC::Common::ToGlm(position), KCDC::Common::ToGlm(rotation))) {
            // The emitter went; start it again where it now belongs.
            Start(*effect);
        }
        return true;
    }

    bool EditorEffects::SetTuning(std::uint64_t id, const Shared::Game::VfxTuning &tuning) {
        PlacedEffect *effect = FindMutable(id);
        if (effect == nullptr || !tuning.IsSane()) {
            return false;
        }
        if (effect->tuning == tuning) {
            return true;
        }

        // Tuning is read when an emitter starts, so it has to start again.
        Stop(*effect);
        effect->tuning = tuning;
        return Start(*effect);
    }

    std::uint64_t EditorEffects::Duplicate(std::uint64_t id) {
        const PlacedEffect *source = Find(id);
        if (source == nullptr) {
            return 0;
        }

        const SDK::Matrix34 basis = SDK::Matrix34FromTransform(SDK::Vec3 {}, source->rotation, SDK::Vec3 {1.0F, 1.0F, 1.0F});
        const SDK::Vec3 position {source->position.x + basis.m[0][0], source->position.y + basis.m[1][0], source->position.z + basis.m[2][0]};
        return Place(source->effectIndex, position, source->rotation, source->tuning);
    }

    void EditorEffects::ApplyLayerVisibility(const EditorLayers *layers) {
        for (auto &effect : _effects) {
            const bool visible = !layers || layers->Visible("effect:" + std::to_string(effect.id));
            if (effect.editorVisible == visible) {
                continue;
            }
            effect.editorVisible = visible;
            if (visible) {
                Start(effect);
            }
            else {
                Stop(effect);
            }
        }
    }

    void EditorEffects::SetGroup(std::uint64_t id, const std::string &group) {
        if (PlacedEffect *record = FindMutable(id)) {
            record->group = group;
        }
    }

    bool EditorEffects::Remove(std::uint64_t id) {
        const auto found = std::find_if(_effects.begin(), _effects.end(), [id](const PlacedEffect &effect) {
            return effect.id == id;
        });
        if (found == _effects.end()) {
            return false;
        }
        Stop(*found);
        _effects.erase(found);
        return true;
    }

    bool EditorEffects::Restore(const PlacedEffect &record) {
        if (record.id == 0 || Find(record.id) != nullptr) {
            return false;
        }

        PlacedEffect effect = record;
        effect.handle       = 0;
        if (!Start(effect)) {
            return false;
        }

        const auto at = std::find_if(_effects.begin(), _effects.end(), [&effect](const PlacedEffect &other) {
            return other.id > effect.id;
        });
        _effects.insert(at, std::move(effect));
        _nextId = std::max(_nextId, record.id + 1);
        return true;
    }

    void EditorEffects::Clear() {
        for (PlacedEffect &effect : _effects) {
            Stop(effect);
        }
        _effects.clear();
    }

    void EditorEffects::Forget() {
        _effects.clear();
    }

    void EditorEffects::Refresh() {
        KCDC::Features::Vfx::VfxService *service = Service();
        if (service == nullptr) {
            return;
        }
        // Only effects that were playing: a start that fails leaves the
        // handle at 0, so it is not retried every tick.
        for (PlacedEffect &effect : _effects) {
            if (effect.handle != 0 && !service->IsAlive(effect.handle)) {
                Start(effect);
            }
        }
    }

    std::uint64_t EditorEffects::PickNearest(const SDK::Vec3 &origin, const SDK::Vec3 &direction, float maxDistance, float &distance, const EditorLayers *layers) const {
        std::uint64_t picked = 0;
        float nearest        = maxDistance;

        for (const PlacedEffect &effect : _effects) {
            if (layers && !layers->Editable("effect:" + std::to_string(effect.id))) {
                continue;
            }
            // Ray against the marker sphere.
            const SDK::Vec3 offset {effect.position.x - origin.x, effect.position.y - origin.y, effect.position.z - origin.z};
            const float along = offset.x * direction.x + offset.y * direction.y + offset.z * direction.z;
            if (along < 0.0F) {
                continue;
            }
            const float radius      = kMarkerRadius * std::max(effect.tuning.scale, 0.5F);
            const float missSquared = offset.x * offset.x + offset.y * offset.y + offset.z * offset.z - along * along;
            if (missSquared > radius * radius) {
                continue;
            }
            const float hit = along - std::sqrt(std::max(radius * radius - missSquared, 0.0F));
            if (hit < nearest) {
                nearest = hit;
                picked  = effect.id;
            }
        }

        distance = picked != 0 ? nearest : maxDistance;
        return picked;
    }

    SDK::AABB EditorEffects::BoundsOf(const PlacedEffect &effect) {
        const float radius = kMarkerRadius * std::max(effect.tuning.scale, 0.5F);
        return SDK::AABB {SDK::Vec3 {effect.position.x - radius, effect.position.y - radius, effect.position.z - radius}, SDK::Vec3 {effect.position.x + radius, effect.position.y + radius, effect.position.z + radius}};
    }
} // namespace KCDC::Features::Prop

#include "marker.h"
#include "marker_service.h"

#include "core/application.h"

#include "shared/game/marker_catalog.h"

#include <cmath>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <logging/logger.h>

namespace KCDC::Features::Marker {
    namespace {
        constexpr const char *kLogger = "Marker";

        /** The same interval a prop holds its build on. */
        constexpr MafiaNet::Time kBuildRetryIntervalMs = 1000;

        /** Seconds for one full rise and fall of a bobbing marker. */
        constexpr float kBobPeriodSeconds = 2.0F;
    } // namespace

    void Marker::DeallocReplica(MafiaNet::Connection_RM3 *) {
        KCDC::Core::gApplication->GetMarkerService().QueueRemove(_handle);
        delete this;
    }

    void Marker::OnConstructed() {
        _dirty = true;
    }

    void Marker::OnDeserialized(bool) {
        _dirty = true;
    }

    void Marker::Unbind() {
        // Not Remove: the engine that owned what this was showing is gone too.
        _handle             = 0;
        _builtMaterialIndex = Shared::Entities::MarkerEntity::kNoMaterial;
        _builtShape         = 0xFFU;
        _builtTrigger       = false;
        _builtColor         = 0;
        _nextBuildAttempt   = 0;
        _buildDeferred      = false;
        _buildRefused       = false;
        _dirty              = true;
    }

    void Marker::AnimationOffsets(float &outBobHeight, float &outSpinDegrees) {
        outBobHeight   = 0.0F;
        outSpinDegrees = 0.0F;

        // Double, and wrapped before it becomes a float: the raw product loses
        // a quarter of a degree of resolution after an hour at full spin.
        const double seconds = static_cast<double>(MafiaNet::GetTime()) / 1000.0;

        // Off the network id, so markers placed together do not move in unison.
        const float phase = static_cast<float>(GetNetworkID() % 997U) / 997.0F * glm::two_pi<float>();

        if (spin != 0.0F) {
            outSpinDegrees = static_cast<float>(std::fmod(static_cast<double>(spin) * seconds, 360.0)) + glm::degrees(phase);
        }
        if (bob > 0.0F) {
            const float cycle = static_cast<float>(std::fmod(seconds, static_cast<double>(kBobPeriodSeconds)) / kBobPeriodSeconds);
            outBobHeight      = bob * std::sin(cycle * glm::two_pi<float>() + phase);
        }
    }

    bool Marker::TryBuild() {
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (now < _nextBuildAttempt) {
            return false;
        }
        _nextBuildAttempt = now + kBuildRetryIntervalMs;

        const Shared::Game::MarkerMaterialEntry *entry = Shared::Game::MarkerMaterialAt(materialIndex);
        const bool sane                                = Shared::Entities::MarkerEntity::IsSaneSize(size) && Shared::Entities::MarkerEntity::IsSaneHeight(height) && Shared::Entities::MarkerEntity::IsSaneMotion(spin, bob) && Shared::Entities::IsMarkerShape(shape);
        if (entry == nullptr || !sane) {
            Framework::Logging::GetLogger(kLogger)->error("Marker {} names catalog row {} as shape {}, which this build cannot place", GetNetworkID(), materialIndex, shape);
            _buildRefused = true;
            return false;
        }

        _handle = KCDC::Core::gApplication->GetMarkerService().Place(GetNetworkID(), materialIndex, GetShape(), position, glm::normalize(rotation), size, height, color, trigger);
        if (_handle == MarkerService::kNoHandle) {
            // Once, not once a second: a level load would otherwise cost a line
            // per marker the session is holding.
            if (!_buildDeferred) {
                _buildDeferred = true;
                Framework::Logging::GetLogger(kLogger)->info("Holding the build of marker {} (\"{}\")", GetNetworkID(), entry->name);
            }
            return false;
        }

        _builtMaterialIndex = materialIndex;
        _builtShape         = shape;
        _builtTrigger       = trigger;
        _builtColor         = color;
        _buildDeferred      = false;

        // What the first Frame compares against, so a still marker is never
        // written to again.
        _appliedPosition = position;
        _appliedRotation = rotation;
        _appliedSize     = size;
        _appliedHeight   = height;
        return true;
    }

    void Marker::Frame() {
        if (_buildRefused) {
            return;
        }

        MarkerService &service = KCDC::Core::gApplication->GetMarkerService();

        // A level change may have taken it already.
        if (_handle != MarkerService::kNoHandle && !service.IsAlive(_handle)) {
            _handle = MarkerService::kNoHandle;
        }

        // A rebuild, because none of the three can be restated: a decal node
        // reads its material once, a slot holds one piece of geometry, and the
        // area is hung on the carrier when the marker is built.
        if (_handle != MarkerService::kNoHandle && (_builtMaterialIndex != materialIndex || _builtShape != shape || _builtTrigger != trigger || _builtColor != color)) {
            service.Remove(_handle);
            _handle = MarkerService::kNoHandle;

            // The interval is for a world that was not ready, and this one is;
            // leaving it would blank a re-shaped marker for a second.
            _nextBuildAttempt = 0;
        }

        if (_handle == MarkerService::kNoHandle) {
            // Still dirty on the way out, so the pose is stated once it stands.
            TryBuild();
            return;
        }

        const bool animated = IsAnimated();
        if (!_dirty && !animated) {
            return;
        }

        // Only when it actually changed: a moved entity raises XFORM, which a
        // trigger marker's area answers with a full recompute.
        if (position != _appliedPosition || rotation != _appliedRotation || size != _appliedSize || height != _appliedHeight) {
            if (service.Move(_handle, position, glm::normalize(rotation), size, height)) {
                _appliedPosition = position;
                _appliedRotation = rotation;
                _appliedSize     = size;
                _appliedHeight   = height;
            }
        }

        // Motion plays inside the slot, so the entity -- and the trigger
        // volume with it -- stays put.
        if (animated) {
            float bobHeight   = 0.0F;
            float spinDegrees = 0.0F;
            AnimationOffsets(bobHeight, spinDegrees);
            service.Animate(_handle, bobHeight, spinDegrees, size, height);
        }
        _dirty = false;
    }
} // namespace KCDC::Features::Marker

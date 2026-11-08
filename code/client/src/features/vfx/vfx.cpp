#include "vfx.h"
#include "vfx_service.h"

#include "core/application.h"

#include "shared/game/vfx_catalog.h"

#include <logging/logger.h>

namespace KCDC::Features::Vfx {
    namespace {
        constexpr const char *kLogger = "Vfx";

        /** The same interval a prop holds its build on. */
        constexpr MafiaNet::Time kBuildRetryIntervalMs = 1000;
    } // namespace

    void Vfx::DeallocReplica(MafiaNet::Connection_RM3 *) {
        KCDC::Core::gApplication->GetVfxService().QueueStop(_handle);
        delete this;
    }

    void Vfx::OnConstructed() {
        _dirty = true;
    }

    void Vfx::OnDeserialized(bool) {
        _dirty = true;
    }

    void Vfx::Unbind() {
        // Not Stop: the entity system the handle's carrier lived in is gone too.
        _handle           = 0;
        _builtEffect      = {};
        _nextBuildAttempt = 0;
        _buildDeferred    = false;
        _buildRefused     = false;
        _dirty            = true;
    }

    bool Vfx::TryBuild() {
        const MafiaNet::Time now = MafiaNet::GetTime();
        if (now < _nextBuildAttempt) {
            return false;
        }
        _nextBuildAttempt = now + kBuildRetryIntervalMs;

        const std::optional<Shared::Game::VfxEffectEntry> entry = effect.Entry();
        if (!entry || !tuning.IsSane()) {
            Framework::Logging::GetLogger(kLogger)->error("Effect {} names catalog row {} or '{}', which this build cannot play", GetNetworkID(), effect.index, effect.streamed);
            _buildRefused = true;
            return false;
        }

        // Zero duration: the replica going is what ends it.
        _handle = KCDC::Core::gApplication->GetVfxService().Spawn(effect, position, glm::normalize(rotation), tuning, 0);
        if (_handle == VfxService::kNoHandle) {
            // Once rather than once a second: a level load would otherwise
            // cost a log line for every effect the session is holding.
            if (!_buildDeferred) {
                _buildDeferred = true;
                Framework::Logging::GetLogger(kLogger)->info("Holding the build of effect {} (\"{}\")", GetNetworkID(), entry->name);
            }
            return false;
        }

        _builtEffect      = effect;
        _builtTuning      = tuning;
        _buildDeferred    = false;

        // These are placed one at a time by a script rather than streamed in
        // bulk, so one line each is affordable and makes a build that never
        // happened visible.
        Framework::Logging::GetLogger(kLogger)->info("Effect {} (\"{}\") built as handle {}", GetNetworkID(), entry->name, _handle);

        // The spawn already placed it; this is what the first Frame compares
        // against, so an effect that never moves is never written to.
        _appliedPosition = position;
        _appliedRotation = rotation;
        return true;
    }

    void Vfx::Frame() {
        if (_buildRefused || !_dirty) {
            return;
        }

        VfxService &service = KCDC::Core::gApplication->GetVfxService();

        // Confirmed rather than polled for: a level change or the reaper may
        // have taken the emitter already.
        if (_handle != VfxService::kNoHandle && !service.IsAlive(_handle)) {
            _handle = VfxService::kNoHandle;
        }

        if (_handle != VfxService::kNoHandle && NeedsRebuild()) {
            service.Stop(_handle);
            _handle = VfxService::kNoHandle;

            // The interval is for a world that was not ready, and this one
            // plainly is; leaving it would blank a retuned effect for a second.
            _nextBuildAttempt = 0;
        }

        if (_handle == VfxService::kNoHandle) {
            // Still dirty on the way out, so the pose is stated once it stands.
            TryBuild();
            return;
        }

        if (position != _appliedPosition || rotation != _appliedRotation) {
            if (service.Move(_handle, position, glm::normalize(rotation))) {
                _appliedPosition = position;
                _appliedRotation = rotation;
            }
        }
        _dirty = false;
    }
} // namespace KCDC::Features::Vfx

#include "light.h"
#include "core/application.h"
#include "light_service.h"
#include "shared/pose_math.h"

namespace KCDC::Features::Light {
    void Light::DeallocReplica(MafiaNet::Connection_RM3 *) {
        KCDC::Core::gApplication->GetLightService().QueueStop(_handle);
        delete this;
    }

    void Light::Unbind() {
        _handle           = 0;
        _nextBuildAttempt = 0;
        _dirty            = true;
    }

    void Light::Frame() {
        auto &service = KCDC::Core::gApplication->GetLightService();
        if ((!_dirty && service.IsAlive(_handle)) || !tuning.IsSane() || !Shared::Game::IsWorldPosition(position)) {
            return;
        }
        if (_handle != 0 && !service.IsAlive(_handle)) {
            _handle = 0;
        }
        if (_handle == 0) {
            const auto now = MafiaNet::GetTime();
            if (now < _nextBuildAttempt) {
                return;
            }
            _nextBuildAttempt = now + 1000;
            _handle           = service.Spawn(position, tuning);
            if (_handle == 0) {
                return;
            }
            _appliedPosition = position;
            _appliedTuning   = tuning;
        }
        if (position != _appliedPosition) {
            if (!service.Move(_handle, position)) {
                return;
            }
            _appliedPosition = position;
        }
        if (tuning != _appliedTuning) {
            if (!service.SetTuning(_handle, tuning)) {
                return;
            }
            _appliedTuning = tuning;
        }
        _dirty = false;
    }
} // namespace KCDC::Features::Light

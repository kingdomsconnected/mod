#include "c_ui_map.h"

#include "c_ui_apse.h"
#include "../../cryengine/3dengine/i_3d_engine.h"
#include <cmath>

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::guimodule {
    bool C_UIMap::GetCursorWorldPosition(Vec3 &position) {
        auto *movie = m_content.GetFlashPlayer();
        auto *engine = SDK::I3DEngine::GetInstance();
        if (!IsOpen() || !IsCursorOnMap() || movie == nullptr || engine == nullptr) {
            return false;
        }
        // ActionScript names from Libs/UI/UIElements/ApseMap.xml, also used
        // through FlashUI by PlaceCheckpointAtCursor at 0x182AFDC74.
        const auto read = [movie](const char *name, float &value) {
            SDK::SFlashVarValue result {};
            if (!movie->Invoke(name, nullptr, 0, &result)) {
                return false;
            }
            switch (result.type) {
            case SDK::EFlashVariableType::Double: value = static_cast<float>(result.d); break;
            case SDK::EFlashVariableType::Float: value = result.f; break;
            case SDK::EFlashVariableType::Int: value = static_cast<float>(result.i); break;
            case SDK::EFlashVariableType::UInt: value = static_cast<float>(result.u); break;
            default: return false;
            }
            return std::isfinite(value);
        };
        if (!read("_root.fc_getCurrentCursorPosX", position.x) || !read("_root.fc_getCurrentCursorPosY", position.y)) {
            return false;
        }
        const float size = static_cast<float>(engine->GetTerrainSize());
        if (position.x < 0 || position.y < 0 || position.x >= size || position.y >= size) {
            return false;
        }
        position.z = engine->GetTerrainElevation(position.x, position.y);
        return std::isfinite(position.z);
    }

    C_UIMap *C_UIMap::GetInstance() {
        C_UIApse *apse = C_UIApse::GetInstance();
        return apse != nullptr ? apse->GetMap() : nullptr;
    }

    bool C_UIMap::HasCheckpoint() {
        return hook::this_call<bool>(gPatterns.C_UIMap__HasCheckpoint, this);
    }

    CheckpointMarkRef C_UIMap::FindCheckpointForCurrentMap() {
        // A member returning a shared_ptr by value: `this` keeps RCX and the
        // hidden return pointer is the second argument. Both words are written
        // unconditionally, including the empty answer.
        CheckpointMarkRef mark;
        hook::this_call<void>(gPatterns.C_UIMap__FindCheckpointForCurrentMap, this, &mark);
        return mark;
    }

    void C_UIMap::RemoveCheckpoint() {
        hook::this_call<void>(gPatterns.C_UIMap__RemoveCheckpoint, this);
    }

    void C_UIMap::SyncCheckpointToFlash() {
        hook::this_call<void>(gPatterns.C_UIMap__SyncCheckpointToFlash, this);
    }

    void C_UIMap::RefreshQuestMarkers() {
        // Surviving symbol is a free function taking the map, not a member.
        hook::call<void>(gPatterns.RefreshQuestMarkers, this);
    }

    void C_UIMap::SetMarkTypeVisible(E_CompassMarkType type, bool visible) {
        // The byte governs marks added from here on; the native re-evaluates
        // the ones already placed.
        const int index          = NormalizeCompassMarkType(type);
        m_markTypeVisible[index] = visible ? 1 : 0;
        SetPoiMarkersOfTypeVisible(index, visible);
    }

    void C_UIMap::SetPoiMarkersOfTypeVisible(int normalizedType, bool show) {
        hook::this_call<void>(gPatterns.C_UIMap__SetPoiMarkersOfTypeVisible, this, normalizedType, show);
    }
} // namespace SDK::wh::guimodule

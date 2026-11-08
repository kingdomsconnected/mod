#include "blip.h"

#include "core/application.h"

#include "sdk/wh/guimodule/e_compass_mark_type.h"

#include "shared/game/blip_icon_catalog.h"

#include <logging/logger.h>

namespace KCDC::Features::Blip {
    namespace {
        constexpr const char *kLogger = "Blip";

        /** Whether two descriptions draw the same thing, owner aside. */
        bool SameRow(const KCDC::Features::Map::BlipDescription &lhs, const KCDC::Features::Map::BlipDescription &rhs) {
            return lhs.type == rhs.type && lhs.state == rhs.state && lhs.label == rhs.label && lhs.compass == rhs.compass && lhs.map == rhs.map && lhs.position.x == rhs.position.x && lhs.position.y == rhs.position.y && lhs.position.z == rhs.position.z;
        }
    } // namespace

    KCDC::Features::Map::BlipDescription Blip::Describe() const {
        KCDC::Features::Map::BlipDescription description;

        // The server only sends a type from the same catalog; one this build
        // does not know draws as the default.
        const std::uint8_t type = KCDC::Shared::Game::BlipIconOf(icon) != nullptr ? icon : KCDC::Shared::Game::kDefaultBlipIcon;
        description.type        = static_cast<SDK::wh::guimodule::E_CompassMarkType>(type);

        description.position   = SDK::Vec3 {position.x, position.y, position.z};
        description.state      = state;
        description.label      = label;
        description.compass    = compass;
        description.map        = map;
        description.fromServer = true;
        return description;
    }

    void Blip::Publish() {
        if (KCDC::Core::gApplication == nullptr) {
            return;
        }
        KCDC::Features::Map::MapBlipService &blips = KCDC::Core::gApplication->GetMapBlipService();

        const KCDC::Features::Map::BlipDescription wanted = Describe();
        if (_rowId == 0) {
            // A full registry is retried on the next update rather than lost.
            _rowId = blips.Create(wanted);
            if (_rowId == 0) {
                if (!_warnedFull) {
                    _warnedFull = true;
                    Framework::Logging::GetLogger(kLogger)->warn("Blip {} has no room at the {} blip limit", GetNetworkID(), KCDC::Features::Map::MapBlipService::kMaxBlips);
                }
                return;
            }
            _published = wanted;
            return;
        }

        if (!SameRow(_published, wanted) && blips.SetDescription(_rowId, wanted)) {
            _published = wanted;
        }
    }

    void Blip::OnConstructed() {
        Publish();
    }

    void Blip::OnDeserialized(bool) {
        Publish();
    }

    void Blip::DeallocReplica(MafiaNet::Connection_RM3 *) {
        if (_rowId != 0 && KCDC::Core::gApplication != nullptr) {
            KCDC::Core::gApplication->GetMapBlipService().Remove(_rowId);
        }
        delete this;
    }
} // namespace KCDC::Features::Blip

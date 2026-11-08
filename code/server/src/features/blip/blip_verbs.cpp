#include "blip_verbs.h"

#include "shared/entity_filter.h"
#include "shared/game/blip_icon_catalog.h"
#include "shared/pose_math.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/replication_manager.h>

namespace KCDC::Features::Blip {
    namespace {
        constexpr const char *kLogger = "Blip";
    } // namespace

    std::vector<Shared::Entities::BlipEntity *> Blips::Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        std::vector<Shared::Entities::BlipEntity *> blips;
        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->ForEach<Shared::Entities::BlipEntity>([&](Shared::Entities::BlipEntity *blip) {
                if (Shared::Entities::InWorld(blip, virtualWorld)) {
                    blips.push_back(blip);
                }
            });
        }
        return blips;
    }

    std::string Blips::BoundLabel(const std::string &label) {
        if (label.size() <= Shared::Entities::BlipEntity::kMaxLabelLength) {
            return label;
        }

        // Back off any UTF-8 continuation bytes so the cut lands between characters.
        std::size_t length = Shared::Entities::BlipEntity::kMaxLabelLength;
        while (length > 0 && (static_cast<unsigned char>(label[length]) & 0xC0U) == 0x80U) {
            --length;
        }
        return label.substr(0, length);
    }

    Shared::Entities::BlipEntity *Blips::Place(const glm::vec3 &position, const BlipLook &look, std::uint32_t virtualWorld, MafiaNet::PeerGuid viewer) {
        if (!Shared::Game::IsFinite(position) || Shared::Game::BlipIconOf(look.icon) == nullptr) {
            return nullptr;
        }

        auto *replication = Framework::CoreModules::GetReplication();
        if (replication == nullptr) {
            return nullptr;
        }

        auto *blip = replication->CreateEntity<Shared::Entities::BlipEntity>();
        if (blip == nullptr) {
            return nullptr;
        }

        // All before the entity replicates, so it rides the construction
        // snapshot -- and a private blip is never sent to anyone else, not
        // even for the tick before a later restriction would land.
        blip->icon     = look.icon;
        blip->state    = look.state;
        blip->label    = BoundLabel(look.label);
        blip->compass  = look.compass;
        blip->map      = look.map;
        blip->position = position;
        blip->SetVirtualWorld(virtualWorld);

        // A blip exists to point at something far away, so distance never
        // takes it off a screen; the virtual world, or the one viewer, still
        // decides who has it.
        blip->streaming.alwaysVisible = true;
        blip->streaming.targetGUID    = viewer;

        Framework::Logging::GetLogger(kLogger)->info("Placed blip {} ({}) in world {} at {:.2f}, {:.2f}, {:.2f}", blip->GetNetworkID(), Shared::Game::BlipIconOf(look.icon)->name, virtualWorld, position.x, position.y, position.z);
        return blip;
    }

    void Blips::SetLook(Shared::Entities::BlipEntity *blip, const BlipLook &look) {
        if (blip == nullptr || Shared::Game::BlipIconOf(look.icon) == nullptr) {
            return;
        }

        blip->icon    = look.icon;
        blip->state   = look.state;
        blip->label   = BoundLabel(look.label);
        blip->compass = look.compass;
        blip->map     = look.map;
    }

    void Blips::Destroy(Shared::Entities::BlipEntity *blip) {
        if (blip == nullptr) {
            return;
        }

        if (auto *replication = Framework::CoreModules::GetReplication()) {
            replication->DestroyEntity(blip);
        }
    }

    std::size_t Blips::DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld) {
        const std::vector<Shared::Entities::BlipEntity *> doomed = Collect(virtualWorld);
        for (Shared::Entities::BlipEntity *blip : doomed) {
            Destroy(blip);
        }
        return doomed.size();
    }
} // namespace KCDC::Features::Blip

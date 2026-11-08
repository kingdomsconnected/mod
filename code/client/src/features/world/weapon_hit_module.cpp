#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "weapon_hit_module.h"

#include "core/application.h"

#include "common/module_support.h"

#include "features/world/world_query_scripting.h"
#include "features/world/world_query_service.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/physics/c_physical_world.h"
#include "sdk/cryengine/physics/i_physical_entity.h"
#include "sdk/patterns.h"
#include "sdk/wh/combatmodule/c_combat_actor.h"
#include "sdk/wh/entitymodule/c_actor.h"

#include "shared/features/world/world_level_hash.h"
#include "shared/features/world/world_query_rpc.h"

#include <core_modules.h>
#include <networking/network_peer.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace KCDC::Features::World {
    namespace {
        using SweepWeaponShape_t = SDK::wh::combatmodule::S_CombatContact *(__fastcall *)(SDK::wh::combatmodule::C_CombatActorCollisions *, int, int, const void *, const void *, float);

        SweepWeaponShape_t g_sweepWeaponShapeOriginal = nullptr;

        /**
         * One step of one weapon shape's sweep, for any fighter. The original
         * runs first and decides everything -- the recoil, the hit, the dedupe
         * -- and leaves its first new contact behind for the module to read.
         */
        SDK::wh::combatmodule::S_CombatContact *__fastcall SweepWeaponShape_Hook(SDK::wh::combatmodule::C_CombatActorCollisions *collisions, int mode, int shapeId, const void *previous, const void *current, float deltaTime) {
            SDK::wh::combatmodule::S_CombatContact *closest = g_sweepWeaponShapeOriginal(collisions, mode, shapeId, previous, current, deltaTime);
            if (collisions != nullptr) {
                WeaponHitModule::Get().NoteSweep(*collisions);
            }
            return closest;
        }

        float Dot(const SDK::Vec3 &a, const SDK::Vec3 &b) {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }
    } // namespace

    WeaponHitModule &WeaponHitModule::Get() {
        static WeaponHitModule instance;
        return instance;
    }

    void WeaponHitModule::Install() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_CombatActorCollisions__SweepWeaponShape), reinterpret_cast<LPVOID>(&SweepWeaponShape_Hook), reinterpret_cast<LPVOID *>(&g_sweepWeaponShapeOriginal));
    }

    void WeaponHitModule::NoteSweep(const SDK::wh::combatmodule::C_CombatActorCollisions &collisions) {
        const SDK::wh::combatmodule::S_CombatContact &contact = collisions.GetFirstContact();
        if (!contact.IsValid() || contact.GetVictim() != nullptr || contact.GetPhysicalEntityId() == INT32_MIN) {
            return;
        }

        // Every fighter sweeps; only the local player's swings are this machine's to report.
        const SDK::EntityId local                  = _localEntityId.load();
        SDK::wh::combatmodule::C_CombatActor *self = collisions.GetCombatActor();
        SDK::wh::entitymodule::C_Actor *owner      = self != nullptr ? self->GetOwner() : nullptr;
        if (local == 0 || owner == nullptr || owner->GetEntityId() != local) {
            return;
        }

        std::scoped_lock lock(_contactsMutex);
        if (_contacts.size() < kMaxQueuedContacts) {
            _contacts.push_back({contact.GetPoint(), contact.GetNormal(), contact.GetDirection(), contact.GetSurfaceIndex(), contact.GetPhysicalEntityId()});
        }
    }

    void WeaponHitModule::Reset() {
        _localEntityId.store(0);
        std::scoped_lock lock(_contactsMutex);
        _contacts.clear();
    }

    void WeaponHitModule::Update() {
        _localEntityId.store(KCDC::Common::LocalActorEntityId());

        std::vector<Contact> contacts;
        {
            std::scoped_lock lock(_contactsMutex);
            contacts.swap(_contacts);
        }
        auto *physics = SDK::CPhysicalWorld::GetInstance();
        if (contacts.empty() || physics == nullptr || !KCDC::Core::gApplication) {
            return;
        }

        SDK::IEntity *player             = KCDC::Common::LocalPlayer();
        const std::string level          = KCDC::Core::gApplication->GetCurrentLevelName();
        const std::uint32_t levelHash    = level.empty() ? 0 : KCDC::Shared::RPC::LevelHash(level);
        const WorldQueryService &queries = KCDC::Core::gApplication->GetWorldQueryService();
        for (const Contact &contact : contacts) {
            // By id rather than by pointer: the body could have gone between the sweep and this frame, and an id that names nothing now resolves to nothing.
            SDK::IPhysicalEntity *collider = physics->GetPhysicalEntityById(contact.physicalEntityId);
            if (collider == nullptr) {
                continue;
            }

            // The engine's normal points into the surface for an armed weapon; the one a trace reports points out of it, against the weapon.
            SDK::Vec3 normal = contact.normal;
            if (Dot(normal, contact.direction) > 0.0F) {
                normal = SDK::Vec3 {-normal.x, -normal.y, -normal.z};
            }

            WorldQueryService::RayHit hit = queries.DescribeContact(collider, contact.point, normal, static_cast<std::int16_t>(contact.surfaceIndex), -1, false);
            if (player != nullptr) {
                const SDK::Vec3 feet = player->GetWorldPos();
                const SDK::Vec3 span {contact.point.x - feet.x, contact.point.y - feet.y, contact.point.z - feet.z};
                hit.distance = std::sqrt(Dot(span, span));
            }

            KCDC::Scripting::Events::WeaponHit(hit);

            KCDC::Shared::RPC::WeaponHitReport report;
            report.levelHash = levelHash;
            report.hit       = WorldQueryService::ToWire(hit);
            auto *net        = Framework::CoreModules::GetNetworkPeer();
            if (net != nullptr && levelHash != 0 && report.IsSane()) {
                net->BroadcastRPC(report, MafiaNet::Priority::High, MafiaNet::Reliability::ReliableOrdered);
            }
        }
    }
} // namespace KCDC::Features::World

static InitFunction init(
    []() {
        KCDC::Features::World::WeaponHitModule::Install();
    },
    "WeaponHits");

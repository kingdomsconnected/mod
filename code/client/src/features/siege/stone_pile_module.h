#pragma once

#include "core/feature_registry.h"

#include "shared/features/siege/stone_pile_rpc.h"

#include "sdk/types.h"
#include "sdk/wh/playermodule/c_stone_throwing.h"
#include "sdk/wh/playermodule/c_stone_throwing_pile.h"

#include <glm/glm.hpp>

#include <atomic>
#include <cstdint>

namespace KCDC::Features::Siege {
    class StonePile;

    /**
     * Stone piles on this machine: the tick that builds every `StonePile`,
     * the local player's throws as the game's own stone throwing makes them,
     * and the replay of everyone else's on their bodies.
     *
     * Three natives are hooked. `C_StoneThrowing::StartWithPile` is the grab
     * -- the only place its fragment is built -- and a grab by the local
     * player at one of these piles is reported (`StonePileUse`). `Finish` is
     * the player stepping away. `C_StoneThrowingPile::SetStoneUsageState`'s
     * state 2 is the stone leaving the hands as a rigid body; the local
     * player's stone is followed from there, frame by frame, until it first
     * meets a body or the world, and that point is reported
     * (`StoneImpactReport`).
     *
     * A grab relayed by the server (`StonePileUsed`) runs the same
     * `StartWithPile` on the thrower's body here, which the game plays out --
     * the grab, the heave, the stone -- exactly as it did for them. The
     * server's verdict on an impact (`StoneImpact`) hurts the local player
     * through the game's own damage, as a siege engine's blast does.
     *
     * Game thread only: the hooks run in the game's minigame code and the
     * client RPCs are dispatched from the game tick.
     */
    class StonePileModule final: public KCDC::Core::IFeatureService {
      public:
        static StonePileModule &Get();

        /** Creates the hooks. Once, at load. */
        static void Install();

        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;

      private:
        /** The local player's stone in the air, followed until it meets something. */
        struct Flight {
            std::uint64_t pileNetworkId = 0;
            SDK::EntityId stoneEntityId = 0;
            glm::vec3 lastPoint {0.0F};
            std::uint64_t launchedAt = 0;
        };

        static std::uint8_t __fastcall StartWithPile(SDK::wh::playermodule::C_StoneThrowing *minigame, SDK::EntityId pileEntityId);
        static void __fastcall Finish(SDK::wh::playermodule::C_StoneThrowing *minigame);
        static void __fastcall RequestThrow(SDK::wh::playermodule::C_StoneThrowing *minigame);
        static void __fastcall SetStoneUsageState(SDK::wh::playermodule::C_StoneThrowingPile *pile, SDK::EntityId stoneEntityId, int state);

        void InstallRPCs();

        /** The replica whose native pile is `pileEntityId`, or null for a pile of the level's own. */
        StonePile *FindByPileEntity(SDK::EntityId pileEntityId) const;

        void OnUsed(const Shared::RPC::StonePileUsed &used);
        void OnImpact(const Shared::RPC::StoneImpact &impact);

        void Send(std::uint64_t pileNetworkId, Shared::RPC::StonePileAction action);

        /** Follows the local player's stone and reports where it first met something. */
        void Follow(std::uint64_t now);
        void Report(const glm::vec3 &point);

        /** Writes the game's launch cvars once they exist; false until then. */
        bool ApplyLaunch();

        bool _rpcsInstalled = false;
        bool _launchApplied = false;

        /** Set while this machine replays a grab, so the hooks do not take it for the local player's. */
        bool _replaying = false;

        Flight _flight;

        std::atomic<bool> _nativeContextLost = false;

        std::uint8_t(__fastcall *_originalStartWithPile)(SDK::wh::playermodule::C_StoneThrowing *, SDK::EntityId)                 = nullptr;
        void(__fastcall *_originalFinish)(SDK::wh::playermodule::C_StoneThrowing *)                                               = nullptr;
        void(__fastcall *_originalRequestThrow)(SDK::wh::playermodule::C_StoneThrowing *)                                         = nullptr;
        void(__fastcall *_originalSetStoneUsageState)(SDK::wh::playermodule::C_StoneThrowingPile *, SDK::EntityId, int)           = nullptr;
    };
} // namespace KCDC::Features::Siege

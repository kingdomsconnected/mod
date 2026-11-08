#pragma once

#include "shared/features/siege/stone_pile_entity.h"

#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/types.h"
#include "sdk/wh/playermodule/c_stone_throwing_pile.h"

#include <mafianet/replica_manager3.h>

#include <cstdint>
#include <vector>

namespace KCDC::Features::Siege {
    /**
     * Client binding between a replicated stone pile and the game's own
     * battlement pile that stands for it.
     *
     * Built as `hurlingStonePile_onesided_walls_left_a` places it: nine
     * `GeomEntity` stones, a `StoneThrowingNode` for the path, an entity for
     * the pick point and the `StoneThrowingPile` itself, linked the way the
     * prefab links them (`path`, `pickPoint`, `stone`) and reset through the
     * pile's own native once the links are in. From then on the prompt, the
     * grab and the throw are the game's `C_StoneThrowing`.
     *
     * The heap shows what the server's count allows. The game takes a stone
     * off it itself at every grab, so the count is written back only when it
     * disagrees with what the grabs this machine saw left, and never while a
     * grab is under way.
     */
    class StonePile final: public Shared::Entities::StonePileEntity {
      public:
        /** Queues the built entities for removal: a replica can be destroyed from the loading-time peer pump. */
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        /** Builds the pile and keeps its heap at the server's count. Game thread only. */
        void Frame(std::uint64_t nowLocal);

        /** Forgets the native side without touching it, for a world that is gone. */
        void Unbind();

        /** The `StoneThrowingPile` entity, 0 while unbuilt. */
        SDK::EntityId GetPileEntityId() const {
            return _pileEntityId;
        }

        /** The pile's extension, or null while unbuilt or gone. */
        SDK::wh::playermodule::C_StoneThrowingPile *ResolvePile() const;

        /** A grab began here, this machine's or a replayed one: the game took a stone off the heap. */
        void NoteGrab(std::uint64_t nowLocal);

      private:
        bool TryBuild();
        void Despawn();

        /** Makes the pile's sphere the invisible, ray-only trigger `UsableItem:SetupTrigger` makes it. */
        void HideTrigger();

        /** Spawns one part, its properties written before it is initialised; 0 on refusal. */
        SDK::EntityId SpawnPart(const char *className, const char *name, const glm::vec3 &at, const glm::quat &rotation, float scale);

        /** The sphere's own surface mapping, kept alive for a queued physics request: nodraw's would make it pierceable to the interactor. */
        std::vector<std::int32_t> _triggerSurfaces;

        std::vector<SDK::EntityId> _parts;
        SDK::EntityId _pileEntityId      = 0;
        std::uint64_t _nextBuildAttempt  = 0;
        bool _buildRefused               = false;

        /** What the heap shows now, by this machine's count. */
        int _shown = -1;

        /** No count is written back before this: a grab or a throw is under way. */
        std::uint64_t _busyUntil = 0;

    };
} // namespace KCDC::Features::Siege

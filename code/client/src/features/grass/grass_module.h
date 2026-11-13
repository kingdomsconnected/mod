#pragma once

#include "sdk/cryengine/3dengine/c_merged_mesh_render_node.h"
#include "sdk/cryengine/3dengine/i_merge_mesh_stream_listener.h"
#include "sdk/cryengine/3dengine/i_merged_meshes_manager.h"
#include "sdk/cryengine/3dengine/i_render_node.h"
#include "shared/features/grass/grass_clearing.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Grass {
    /** What one change did, for whoever reports it. */
    struct GrassChange {
        std::size_t sectors = 0; // streamed-in sectors the box reaches
        std::size_t plants  = 0; // plants hidden, or put back
    };

    /**
     * The clearings standing in the level's merged terrain vegetation, and
     * the one place that writes them into the engine.
     *
     * The grass is not objects. Each sixteen-metre cube of terrain holds its
     * plants as rows of one render node, and the game takes a gathered herb
     * out by zeroing that row's scale. A clearing does the same to every
     * plant rooted in its box, in every group the player cannot gather; a
     * sector in view is redrawn without them within the second.
     *
     * A cube's rows are read back from the level file each time it streams
     * in, so nothing done to them lasts: the module listens for sectors
     * coming in, beside the game's own respawn manager, and hides again what
     * its clearings cover before the sector is first drawn. It keeps no node
     * pointer between calls -- a sector is looked up when a clearing is
     * added or removed, and handed over by the engine when it streams in.
     *
     * Whoever adds a clearing owns its id: the World Builder for the map
     * being edited, the level-edit service for what the server sent.
     * Overlapping clearings are independent; a plant comes back only when
     * the last box holding it goes. Game thread only.
     */
    class GrassModule final: public SDK::IMergeMeshStreamListener {
      public:
        static GrassModule &Get();

        /** Hides the plants in `clearing` now and whenever their sector streams in. 0 for a box that is not sane. */
        std::uint64_t Add(const KCDC::Shared::Game::GrassClearing &clearing, GrassChange *change = nullptr);

        /** Takes a clearing back and shows its plants again, except those another clearing still holds. */
        bool Remove(std::uint64_t id, GrassChange *change = nullptr);

      private:
        void OnMergedMeshStreamedIn(SDK::IRenderNode *node) override;
        void OnMergedMeshStreamedOut(SDK::IRenderNode *node) override;

        /** Joins or leaves the engine's listeners so that it listens exactly while a clearing stands. */
        void SyncListening();

        /** The streamed-in sectors whose cube meets the clearing's box, into `_sectors`. */
        void CollectSectors(const KCDC::Shared::Game::GrassClearing &clearing);

        /** Hides every plant of `sector` that `clearing` holds. Returns how many it hid. */
        static std::size_t Hide(SDK::CMergedMeshRenderNode &sector, const KCDC::Shared::Game::GrassClearing &clearing);

        /** Shows the hidden plants of `sector` that `removed` held and no standing clearing does. */
        std::size_t Restore(SDK::CMergedMeshRenderNode &sector, const KCDC::Shared::Game::GrassClearing &removed) const;

        std::unordered_map<std::uint64_t, KCDC::Shared::Game::GrassClearing> _clearings;
        std::uint64_t _nextId = 1;

        /** The manager this is a listener of, or null. Compared, never called through once the level has changed. */
        SDK::IMergedMeshesManager *_listeningTo = nullptr;

        /** Reused between calls so a change does not allocate. */
        std::vector<SDK::IRenderNode *> _nodes;
        std::vector<SDK::CMergedMeshRenderNode *> _sectors;
    };
} // namespace KCDC::Features::Grass

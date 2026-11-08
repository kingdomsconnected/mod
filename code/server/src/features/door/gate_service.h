#pragma once

#include "shared/features/door/gate_entity.h"
#include "shared/game/gate_catalog.h"

#include <mafianet/virtual_world.h>

#include <glm/vec3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::Door {
    /**
     * Server authority over castle gates: the animated drawbridge and
     * portcullis, and the gates the level swaps between layers of static
     * brushes. A swapped gate's cycle has no length, so it is open or shut
     * from the next tick.
     *
     * Gates are replicated entities, so this owns none of their pose -- only
     * which gates exist and when a cycle starts. All of it comes from the compiled gate catalog: which gates the
     * level places, where, and how long each clip runs. No client reports a
     * gate, measures one, or says what state it is in.
     *
     * A cycle is closed here on the server clock, so a disconnect cannot
     * strand a gate mid-swing.
     */
    class GateService final {
      public:
        /** Builds every gate the level places in the global world. */
        void Install(const std::string &level);

        /** Settles any cycle that has run its course. */
        void Update();

        /** What a toggle did, and the fact to build a reply out of when it did nothing. */
        struct ToggleResult {
            bool accepted = false;

            /** Why nothing moved, as a phrase -- `already open`. Empty when it did. */
            std::string reason;

            /** The direction taken, or the one that was refused. */
            bool opening = false;

            /** How long the motion still has to run, in seconds. 0 when nothing moved. */
            float seconds = 0.0F;
        };

        /**
         * Starts a cycle on one gate.
         *
         * `open` chooses the direction; empty means "the other way". A gate
         * already moving the other way is reversed from where it has got to
         * rather than refused.
         */
        ToggleResult Toggle(KCDC::Shared::Entities::GateEntity &gate, std::optional<bool> open);

        /**
         * The gate the level places nearest `position` within `radius`, or
         * null. Measured from the catalog, so it finds gates in any virtual
         * world whether or not anything there has reached them yet.
         */
        KCDC::Shared::Entities::GateEntity *Nearest(const glm::vec3 &position, MafiaNet::VirtualWorldId virtualWorld, float radius);

        /** Every gate built so far, in one virtual world or in all of them. */
        std::vector<KCDC::Shared::Entities::GateEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) const;

        /**
         * The gate for one level GUID in one virtual world, built from its
         * catalog row the first time anything there asks. Null only for a
         * GUID the level does not place.
         */
        KCDC::Shared::Entities::GateEntity *Resolve(std::uint64_t gateGuid, MafiaNet::VirtualWorldId virtualWorld);

        /** What kind of gate this is -- `drawbridge`, `portcullis`. */
        static std::string Name(const KCDC::Shared::Entities::GateEntity &gate);

        /** How far open the gate is right now, 0 shut to 1 fully open. */
        static float Openness(const KCDC::Shared::Entities::GateEntity &gate);

        std::size_t Count() const {
            return _index.size();
        }

      private:
        struct GateKey {
            MafiaNet::VirtualWorldId virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
            std::uint64_t gateGuid                = 0;

            bool operator==(const GateKey &other) const {
                return virtualWorld == other.virtualWorld && gateGuid == other.gateGuid;
            }
        };

        struct GateKeyHash {
            std::size_t operator()(const GateKey &key) const;
        };

        struct GateRecord {
            KCDC::Shared::Entities::GateEntity *entity = nullptr;
            std::uint64_t networkId                    = 0;
        };

        /**
         * Whether a record's copy still exists: a copy a script asks for in a
         * world belongs to that script's resource and goes when it stops, while
         * the record stays (DoorService::IsLive). Read as absent until the gate
         * is asked for again and built anew.
         */
        static bool IsLive(const GateRecord &record);

        /** Creates one gate in one virtual world, open, as the level shows it. */
        KCDC::Shared::Entities::GateEntity *Materialize(const KCDC::Shared::Game::GatePlacement &placement, MafiaNet::VirtualWorldId virtualWorld);

        KCDC::Shared::Entities::GateEntity *Find(std::uint64_t gateGuid, MafiaNet::VirtualWorldId virtualWorld) const;

        /** How far open the gate is at `now`, 0 shut to 1 fully open. */
        static float OpennessAt(const KCDC::Shared::Entities::GateEntity &gate, std::uint64_t now);

        /** Which end a bare `/gate` should head away from. */
        static bool MostlyOpen(const KCDC::Shared::Entities::GateEntity &gate, float openness);

        /** Finishes a gate's cycle at the end it was heading for. */
        static void Settle(KCDC::Shared::Entities::GateEntity &gate);

        std::unordered_map<GateKey, GateRecord, GateKeyHash> _index;

        /** This level's rows of the gate catalog. */
        std::vector<const KCDC::Shared::Game::GatePlacement *> _placements;
        std::string _level;
        std::uint32_t _levelHash = 0;
    };
} // namespace KCDC::Features::Door

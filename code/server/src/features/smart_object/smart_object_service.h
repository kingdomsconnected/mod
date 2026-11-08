#pragma once

#include "shared/features/smart_object/smart_object_entity.h"
#include "shared/game/crafting_catalog.h"
#include "shared/game/dice_catalog.h"
#include "shared/game/smart_object_blueprints.h"

#include <mafianet/virtual_world.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace KCDC::Features::SmartObject {
    /**
     * Server authority over smart objects: the ones scripts and world
     * resources place, and the level's own stations.
     *
     * A placed object is a replicated entity every client builds from its
     * blueprint. A placed station also gets a row of its own here, in the
     * form the static catalogs give the level's -- a `CraftStation`, a
     * `DiceTable`, a `PreparationStation` -- so crafting, dice and food
     * preparation judge it by the same rules. Every check that used to walk
     * a catalog asks here instead, and here a level station a level edit has
     * taken out of a world is gone from that world.
     */
    class SmartObjectService final {
      public:
        static SmartObjectService &Get();

        void Install(const std::string &level);

        /** Drops the rows of placed objects destroyed with the resource that placed them. */
        void Update();

        /** One station, as listings show it: the level's own or a placed one. */
        struct Station {
            std::uint64_t guid                 = 0;
            Shared::Game::SmartObjectKind kind = Shared::Game::SmartObjectKind::Alchemy;
            glm::vec3 position {0.0F};
            /** The placed object it belongs to, or 0 for one of the level's own. */
            std::uint64_t smartObjectId = 0;
        };

        /**
         * Places a smart object in one virtual world, under GUIDs every
         * client builds its parts by. The caller (a script, a world resource)
         * owns it. Null, with `error` saying why, for an unknown blueprint or
         * a transform that is not finite.
         */
        Shared::Entities::SmartObjectEntity *Spawn(const Shared::Game::SmartObjectDefinition &definition, MafiaNet::VirtualWorldId virtualWorld, std::string &error);

        /** Takes a placed smart object down everywhere. */
        void Despawn(Shared::Entities::SmartObjectEntity &object);

        /** What a placed object was placed as, in the form `Spawn` takes it; null for one that has gone. */
        const Shared::Game::SmartObjectDefinition *DefinitionOf(const Shared::Entities::SmartObjectEntity &object) const;

        /** Every placed smart object, in one virtual world or in all of them. */
        std::vector<Shared::Entities::SmartObjectEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld) const;

        /** Every station players in `virtualWorld` can use: the level's own not taken out of it, and the placed ones there. */
        std::vector<Station> Stations(MafiaNet::VirtualWorldId virtualWorld) const;

        /** A crafting station by its `level:guid` id, the level's own or a placed one. Removal is not judged: a session already running keeps its table. */
        const Shared::Game::CraftStation *FindCraftStation(std::string_view id) const;

        /** The crafting station of `kind` with this GUID that a player in `virtualWorld` can use, or null. */
        const Shared::Game::CraftStation *FindCraftStation(std::uint64_t guid, Shared::Game::CraftKind kind, MafiaNet::VirtualWorldId virtualWorld) const;

        /** The dice table with this GUID that a player in `virtualWorld` can use, or null. */
        const Shared::Game::DiceTable *FindDiceTable(std::uint64_t guid, MafiaNet::VirtualWorldId virtualWorld) const;

        /** Every dice table a player in `virtualWorld` can use. */
        std::vector<const Shared::Game::DiceTable *> DiceTables(MafiaNet::VirtualWorldId virtualWorld) const;

        /** Every crafting station a player in `virtualWorld` can use. */
        std::vector<const Shared::Game::CraftStation *> CraftStations(MafiaNet::VirtualWorldId virtualWorld) const;

        /**
         * The id of the drying rack or smokehouse of `kind` nearest
         * `position` within `reachSquared`, that a player in `virtualWorld`
         * can use; empty for none.
         */
        std::string PreparationStationAt(Shared::Game::PreparationKind kind, const glm::vec3 &position, float reachSquared, MafiaNet::VirtualWorldId virtualWorld) const;

        /** Whether a grindstone a player in `virtualWorld` can use stands within reach of `position`. */
        bool AtGrindstone(const glm::vec3 &position, float reachSquared, MafiaNet::VirtualWorldId virtualWorld) const;

        /**
         * Takes one of the level's stations out of a virtual world through a
         * level edit on its entity, which every player there applies; out of
         * the global world, it is out of every world. False, with `error`
         * saying why, for a GUID that is not one of the level's stations.
         */
        bool Remove(std::uint64_t guid, MafiaNet::VirtualWorldId virtualWorld, std::string &error);

        /** Puts back a level station that world's level edit took out. False when there is none. */
        bool Unremove(std::uint64_t guid, MafiaNet::VirtualWorldId virtualWorld);

        /** Whether a level edit has the level station out of that world, or out of the global one. */
        bool IsRemoved(std::uint64_t guid, MafiaNet::VirtualWorldId virtualWorld) const;

        /** The level's stations a level edit has out of some world, with the world they are out of. */
        std::vector<std::pair<Station, MafiaNet::VirtualWorldId>> Removed(std::optional<MafiaNet::VirtualWorldId> virtualWorld) const;

        /** The level's own station with this GUID, or null. */
        const Station *LevelStation(std::uint64_t guid) const;

      private:
        /** A placed object, and the rows it adds to the stations every check reads. */
        struct Placed {
            Shared::Game::SmartObjectDefinition definition;
            std::uint64_t networkId               = 0;
            MafiaNet::VirtualWorldId virtualWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;
            std::uint32_t serial                  = 0;

            // The station's rows, pointing into the strings below; a seat or a bed has none.
            std::string id;
            std::string level;
            std::optional<Shared::Game::CraftStation> craft;
            std::optional<Shared::Game::DiceTable> dice;
            std::optional<Shared::Game::PreparationStation> preparation;
        };

        /** Whether a placed object is still there, rather than destroyed with its resource. */
        static bool IsLive(const Placed &placed);

        /** Whether a player in `virtualWorld` sees a placed object in `placedWorld`. */
        static bool Sees(MafiaNet::VirtualWorldId placedWorld, MafiaNet::VirtualWorldId virtualWorld);

        std::string _level;
        std::uint32_t _nextSerial = 1;

        /** By NetworkID. Heap-held: the rows' string views point into them. */
        std::unordered_map<std::uint64_t, std::unique_ptr<Placed>> _placed;

        /** This level's own stations, by GUID, from every catalog. */
        std::unordered_map<std::uint64_t, Station> _levelStations;
    };
} // namespace KCDC::Features::SmartObject

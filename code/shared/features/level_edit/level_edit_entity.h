#pragma once

#include "shared/features/level_edit/level_edit_definition.h"

#include <cstddef>
#include <cstdint>
#include <mafianet/string.h>
#include <networking/replication/network_entity.h>
#include <string>

namespace KCDC::Shared::Entities {
    /**
     * One change the server makes to an object the level placed: a brush or
     * a level entity taken out of the world, moved, swapped for another mesh,
     * or a mix of those -- what a World Builder map's `world` array carries,
     * pushed to every player instead of loaded by hand on each.
     *
     * A replicated entity for the reason a blip is one: whoever joins later
     * has to get it. It is never culled by distance, since the object it
     * names can be seen from further than anything streams, and it reaches
     * every player in its virtual world. Taking the edit back is destroying
     * the entity; each client then puts the object back as the level had it.
     *
     * The key -- what the object is and where the level put it -- is
     * construction-only, so a live edit always names the same object. Only
     * what is done to it changes. Nothing is ever reported back.
     */
    class LevelEditEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::LevelEdit";

        /** A mesh path is a few directories deep; past this it names nothing the level ships. */
        static constexpr std::size_t kMaxNameLength = 260;

        /** The client-side clamp the World Builder puts its own scale under. */
        static constexpr float kMinScale = 0.01F;
        static constexpr float kMaxScale = 100.0F;

        Shared::Game::LevelEditKind kind = Shared::Game::LevelEditKind::Brush;
        std::uint64_t guid               = 0;
        std::string name;

        /** Where the level put the object; `position` is only the entity's own bookkeeping. */
        glm::vec3 at {0.0F};

        bool hidden = false;

        /** The mesh standing in the object's place, or empty; see `LevelEditDefinition::model`. */
        std::string model;

        bool moved = false;
        glm::vec3 movedPosition {0.0F};
        glm::quat movedRotation {1.0F, 0.0F, 0.0F, 0.0F};
        glm::vec3 movedScale {1.0F};

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            // Seeded from the current value, for the reason PropEntity gives.
            std::uint8_t wireKind = static_cast<std::uint8_t>(kind);
            fields.Field(wireKind);
            kind = wireKind == static_cast<std::uint8_t>(Shared::Game::LevelEditKind::Entity) ? Shared::Game::LevelEditKind::Entity : Shared::Game::LevelEditKind::Brush;

            fields.Field(guid);

            MafiaNet::RakString wireName(name.c_str());
            fields.Field(wireName);
            if (!fields.Writing()) {
                name = wireName.C_String();
            }

            // Full floats: the key is matched to 5 cm, finer than the
            // transform channel's quantization guarantees.
            fields.Field(at.x);
            fields.Field(at.y);
            fields.Field(at.z);

            SerializeEdit(fields);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeEdit(fields);
        }

        /** Nothing: the edit never moves, and what it does to the object rides the state channel. */
        void SerializeTransform(Framework::Networking::Replication::FieldSerializer &) override {}

        /** The edit as a map entry says it. */
        Shared::Game::LevelEditDefinition Describe() const {
            Shared::Game::LevelEditDefinition edit;
            edit.kind     = kind;
            edit.guid     = guid;
            edit.name     = name;
            edit.at       = at;
            edit.hidden   = hidden;
            edit.model    = model;
            edit.moved    = moved;
            edit.position = movedPosition;
            edit.rotation = movedRotation;
            edit.scale    = movedScale;
            return edit;
        }

      private:
        /** What is done to the object, everything a receiver re-applies. */
        void SerializeEdit(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(hidden);

            MafiaNet::RakString wireModel(model.c_str());
            fields.Field(wireModel);
            if (!fields.Writing()) {
                model = wireModel.C_String();
            }

            fields.Field(moved);
            fields.Field(movedPosition.x);
            fields.Field(movedPosition.y);
            fields.Field(movedPosition.z);
            fields.Field(movedRotation.x);
            fields.Field(movedRotation.y);
            fields.Field(movedRotation.z);
            fields.Field(movedRotation.w);
            fields.Field(movedScale.x);
            fields.Field(movedScale.y);
            fields.Field(movedScale.z);
        }
    };
} // namespace KCDC::Shared::Entities

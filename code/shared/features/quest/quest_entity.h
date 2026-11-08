#pragma once

#include "shared/features/quest/quest_progress.h"

#include <mafianet/string.h>
#include <networking/replication/network_entity.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace KCDC::Shared::Entities {
    /** One line of a quest, as a script or a command hands it over. */
    struct QuestObjective {
        std::string text;
        QuestProgress progress = QuestProgress::Active;
        bool optional          = false;

        /** Where the objective is, for the game's own quest markers; ignored unless `hasPosition`. */
        bool hasPosition   = false;
        glm::vec3 position = glm::vec3(0.0F);
    };

    /**
     * A quest the server wrote and every client shows in the game's own
     * journal.
     *
     * Replicated rather than messaged, because the journal is a view of state:
     * a player who joins late or walks out of range still opens the log and
     * finds the quest there. Removing one is destroying the entity. It carries
     * no transform, so it streams to everyone in the virtual world.
     *
     * The objective list is a fixed array because the delta serializer tracks
     * a field by its position in the stream.
     *
     * Nothing here says "announce this": a replica is constructed again on
     * every connection, so the toast is a `Shared::RPC::QuestAnnounced` event
     * sent once to whoever was there to hear it.
     */
    class QuestEntity: public Framework::Networking::Replication::NetworkEntity {
      public:
        static constexpr const char *kTypeName = "KCDC::Quest";

        /** The journal shows them all; past eight a quest is a checklist. */
        static constexpr std::size_t kMaxObjectives = 8;

        /** Long enough for a sentence, short enough that a script cannot flood the stream. */
        static constexpr std::size_t kMaxTextLength = 256;

        /** The key that identifies the quest, and the node name the game files it under. */
        std::string questKey;

        std::string title;
        std::string description;

        /** A `QuestType`, kept as its underlying type so the serializer takes it directly. */
        std::uint8_t questType = static_cast<std::uint8_t>(QuestType::Side);

        /** A `QuestProgress`. */
        std::uint8_t progress = static_cast<std::uint8_t>(QuestProgress::Active);

        /** How many of the objective slots below carry anything. */
        std::uint8_t objectiveCount = 0;

        std::string objectiveText[kMaxObjectives];

        /** A `QuestProgress` per objective. */
        std::uint8_t objectiveProgress[kMaxObjectives] = {};

        /** An objective the player may skip; the journal greys it rather than hiding it. */
        bool objectiveOptional[kMaxObjectives] = {};

        /**
         * Where an objective points, drawn as the game's own quest marker on
         * the map and compass while the quest is followed. A slot without
         * `objectiveHasPosition` has no marker, like most shipped objectives.
         */
        bool objectiveHasPosition[kMaxObjectives]   = {};
        glm::vec3 objectivePosition[kMaxObjectives] = {};

        /** The wire bytes as their enums, falling back rather than handing the game a value it has no enumerator for. */
        QuestType GetQuestType() const {
            return IsQuestType(questType) ? static_cast<QuestType>(questType) : QuestType::Side;
        }

        QuestProgress GetProgress() const {
            return IsQuestProgress(progress) ? static_cast<QuestProgress>(progress) : QuestProgress::Active;
        }

        QuestProgress GetObjectiveProgress(std::size_t index) const {
            const std::uint8_t raw = index < kMaxObjectives ? objectiveProgress[index] : 0;
            return IsQuestProgress(raw) ? static_cast<QuestProgress>(raw) : QuestProgress::Active;
        }

        static bool IsSaneKey(const std::string &key) {
            if (key.empty() || key.size() > 64) {
                return false;
            }

            for (const char character : key) {
                const bool allowed = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9') || character == '_' || character == '-';
                if (!allowed) {
                    return false;
                }
            }

            return true;
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeState(fields);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            SerializeState(fields);
        }

      private:
        void SerializeState(Framework::Networking::Replication::FieldSerializer &fields) {
            fields.Field(questKey);
            fields.Field(title);
            fields.Field(description);
            fields.Field(questType);
            fields.Field(progress);
            fields.Field(objectiveCount);

            // Every slot, every time. See the note on kMaxObjectives.
            for (std::size_t index = 0; index < kMaxObjectives; ++index) {
                fields.Field(objectiveText[index]);
                fields.Field(objectiveProgress[index]);
                fields.Field(objectiveOptional[index]);
                fields.Field(objectiveHasPosition[index]);
                fields.Field(objectivePosition[index].x);
                fields.Field(objectivePosition[index].y);
                fields.Field(objectivePosition[index].z);
            }
        }
    };
} // namespace KCDC::Shared::Entities

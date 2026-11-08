#pragma once

#include "shared/features/quest/quest_entity.h"
#include "shared/features/quest/quest_progress.h"

#include <algorithm>
#include <cstdint>
#include <mafianet/bit_stream.h>
#include <networking/replication/network_entity.h>
#include <networking/rpc/rpc_identifier.h>
#include <string>

namespace KCDC::Shared::RPC {
    /**
     * What a `QuestAnnounced` is about.
     *
     * `ShowQuestEvent` only knows started, finished and failed, so sending it
     * for an objective change announces a ticked step as a quest starting.
     * An objective change is a different kind, shown a different way, and
     * says which state the objective reached.
     */
    enum class QuestAnnounceKind : std::uint8_t {
        Started            = 0,
        Completed          = 1,
        Failed             = 2,
        ObjectiveChanged   = 3,
        ObjectiveCompleted = 4,
        ObjectiveFailed    = 5,
    };

    inline bool IsQuestAnnounceKind(std::uint8_t value) {
        return value <= static_cast<std::uint8_t>(QuestAnnounceKind::ObjectiveFailed);
    }

    namespace QuestRpcDetail {
        template <std::size_t Limit>
        bool SerializeBoundedString(MafiaNet::BitStream *bs, bool write, std::string &value) {
            if (write && value.size() > Limit) {
                return false;
            }

            std::uint16_t length = write ? static_cast<std::uint16_t>(value.size()) : 0;
            if (!bs->Serialize(write, length) || length > Limit) {
                return false;
            }
            if (!write) {
                value.assign(length, '\0');
            }
            return length == 0 || bs->Serialize(write, value.data(), length);
        }
    } // namespace QuestRpcDetail

    /**
     * Server -> client: say out loud that a quest changed.
     *
     * The journal is state and arrives as a `QuestEntity`; the toast is an
     * event and arrives here. A replica is constructed again on every
     * connection, so a client that inferred "announce this" from state would
     * tell a returning player about every quest they already had.
     *
     * It carries its own text rather than a network id, so the toast reads the
     * same whatever order the two land in.
     */
    struct QuestAnnounced {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::QuestAnnounced");

        /** The quest's key, which is also the node name the HUD keys its tracking id on. */
        std::string questKey;

        /** The line the toast shows. */
        std::string title;

        /** A `Shared::Entities::QuestType`. Chooses the toast's icon and wording. */
        std::uint8_t questType = static_cast<std::uint8_t>(Entities::QuestType::Side);

        /** The objective line an objective kind is about; empty otherwise. */
        std::string detail;

        /** A `QuestAnnounceKind`. */
        std::uint8_t kind = static_cast<std::uint8_t>(QuestAnnounceKind::Started);

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            _complete = QuestRpcDetail::SerializeBoundedString<64>(bs, write, questKey);
            _complete = _complete && QuestRpcDetail::SerializeBoundedString<Entities::QuestEntity::kMaxTextLength>(bs, write, title);
            _complete = _complete && QuestRpcDetail::SerializeBoundedString<Entities::QuestEntity::kMaxTextLength>(bs, write, detail);
            _complete = _complete && bs->Serialize(write, questType);
            _complete = _complete && bs->Serialize(write, kind);
        }

        bool IsSane() const {
            return _complete && Entities::QuestEntity::IsSaneKey(questKey) && Entities::IsQuestType(questType) && IsQuestAnnounceKind(kind);
        }

      private:
        bool _complete = true;
    };

    /**
     * Server -> client: start or stop following a quest, as if the player had
     * pressed the journal's track button.
     *
     * An instruction rather than state, because following is the player's to
     * change afterwards: the journal can untrack what the server tracked, and
     * the result comes back as an ordinary `QuestTrackChanged`. It may land
     * before the quest's replica does, so the client holds it until the quest
     * is built.
     */
    struct QuestTrackRequest {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::QuestTrackRequest");

        /** The quest's key, which is the node name the tracked table holds. */
        std::string questKey;

        /** True to start following, false to stop. */
        bool tracked = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            _complete = QuestRpcDetail::SerializeBoundedString<64>(bs, write, questKey);
            _complete = _complete && bs->Serialize(write, tracked);
        }

        bool IsSane() const {
            return _complete && Entities::QuestEntity::IsSaneKey(questKey);
        }

      private:
        bool _complete = true;
    };

    /**
     * Client -> server: the player started or stopped following a quest.
     *
     * The only message this feature sends upstream, and it has to be one:
     * following a quest is decided on the client. It reports what already
     * happened so a script can react; the server neither refuses it nor writes
     * back. Carries the key, because that is the node name the game knows a
     * tracked quest by.
     */
    struct QuestTrackChanged {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::QuestTrackChanged");

        std::string questKey;

        /** True for started following, false for stopped. */
        bool tracked = false;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            _complete = QuestRpcDetail::SerializeBoundedString<64>(bs, write, questKey);
            _complete = _complete && bs->Serialize(write, tracked);
        }

        bool IsSane() const {
            return _complete && Entities::QuestEntity::IsSaneKey(questKey);
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC

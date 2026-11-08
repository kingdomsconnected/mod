#pragma once

#include "shared/features/world/world_level_hash.h"

#include <mafianet/bit_stream.h>
#include <mafianet/time.h>
#include <mafianet/virtual_world.h>
#include <networking/rpc/rpc_identifier.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

namespace KCDC::Shared::RPC {
    constexpr std::size_t kMaxDoorObjectAnimationLength = 128;

    /**
     * What a player did to a door. A player never locks one, and only ever
     * unlocks one in the same use that opens it, so a lock edge of its own is
     * always the world's and never reported.
     */
    enum class DoorEvent : std::uint8_t {
        Manipulation = 0,
        // 1 was a standalone lock edge. It stays unused, so a peer still
        // sending one is refused rather than read as a lockpick.
        //
        // Successful door-side unlock and callbacks. The lockpicking minigame
        // is a separate player presentation, not durable door state.
        Lockpicked = 2
    };

    /**
     * The interactive action AnimDoor.lua starts on the human half of a
     * transition. There are exactly eight -- leaf side, approach side and
     * direction -- so the wire carries the choice, not the name.
     */
    enum class DoorActorAction : std::uint8_t {
        None,
        LeftBackClose,
        LeftBackOpen,
        LeftFrontClose,
        LeftFrontOpen,
        RightBackClose,
        RightBackOpen,
        RightFrontClose,
        RightFrontOpen
    };

    /** The native action name, or null for None. */
    inline const char *DoorActorActionName(DoorActorAction action) {
        switch (action) {
        case DoorActorAction::None: return nullptr;
        case DoorActorAction::LeftBackClose: return "door_l_b_c";
        case DoorActorAction::LeftBackOpen: return "door_l_b_o";
        case DoorActorAction::LeftFrontClose: return "door_l_f_c";
        case DoorActorAction::LeftFrontOpen: return "door_l_f_o";
        case DoorActorAction::RightBackClose: return "door_r_b_c";
        case DoorActorAction::RightBackOpen: return "door_r_b_o";
        case DoorActorAction::RightFrontClose: return "door_r_f_c";
        case DoorActorAction::RightFrontOpen: return "door_r_f_o";
        }
        return nullptr;
    }

    inline std::uint32_t DoorLevelHash(const std::string &level) {
        return LevelHash(level);
    }

    /** The action the game just named, or None when it is not a door action. */
    inline DoorActorAction DoorActorActionFromName(const char *name) {
        if (name == nullptr) {
            return DoorActorAction::None;
        }
        for (std::uint8_t candidate = 1; candidate <= static_cast<std::uint8_t>(DoorActorAction::RightFrontOpen); ++candidate) {
            const char *candidateName = DoorActorActionName(static_cast<DoorActorAction>(candidate));
            if (candidateName != nullptr && std::string(candidateName) == name) {
                return static_cast<DoorActorAction>(candidate);
            }
        }
        return DoorActorAction::None;
    }

    namespace DoorDetail {
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

        inline bool IsBoundedText(const std::string &value, std::size_t limit) {
            return value.size() <= limit && std::all_of(value.begin(), value.end(), [](unsigned char character) {
                return character >= 0x20 && character != 0x7F;
            });
        }

        inline bool IsEvent(DoorEvent event) {
            return event == DoorEvent::Manipulation || event == DoorEvent::Lockpicked;
        }

        inline bool IsActorAction(DoorActorAction action) {
            return action >= DoorActorAction::None && action <= DoorActorAction::RightFrontOpen;
        }

        template <typename Value>
        bool SerializeByteEnum(MafiaNet::BitStream *bs, bool write, Value &value) {
            std::uint8_t wire = static_cast<std::uint8_t>(value);
            if (!bs->Serialize(write, wire)) {
                return false;
            }
            if (!write) {
                value = static_cast<Value>(wire);
            }
            return true;
        }
    } // namespace DoorDetail

    constexpr std::size_t kMaxDoorNameLength = 64;

    /** Server -> client: which door is this player standing nearest? */
    struct DoorNearestQuery {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DoorNearestQuery");

        std::uint64_t requestId = 0;

        bool IsSane() const {
            return _complete && requestId != 0;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            _complete = bs->Serialize(write, requestId);
        }

      private:
        bool _complete = true;
    };

    /** Client -> server: the nearest door and what this client sees of it. */
    struct DoorNearestAnswer {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DoorNearestAnswer");

        std::string name;
        std::uint64_t requestId = 0;
        std::uint64_t doorGuid  = 0;
        std::uint32_t levelHash = 0;
        float distance          = 0.0F;
        bool open               = false;
        bool locked             = false;
        bool found              = false;

        bool IsSane() const {
            if (!_complete || requestId == 0 || !DoorDetail::IsBoundedText(name, kMaxDoorNameLength)) {
                return false;
            }
            if (!found) {
                return doorGuid == 0 && name.empty();
            }
            return doorGuid != 0 && levelHash != 0 && std::isfinite(distance) && distance >= 0.0F;
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            _complete = DoorDetail::SerializeBoundedString<kMaxDoorNameLength>(bs, write, name) && bs->Serialize(write, requestId) && bs->Serialize(write, doorGuid) && bs->Serialize(write, levelHash) && bs->Serialize(write, distance) && bs->Serialize(write, open)
                        && bs->Serialize(write, locked) && bs->Serialize(write, found);
        }

      private:
        bool _complete = true;
    };

    /**
     * One accepted door interaction.
     *
     * Client to server it is a report the server folds into the door entity.
     * Server to client it goes only to players near enough to watch, stamped
     * with the body that did it.
     */
    struct DoorInteraction {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::DoorInteraction");

        std::string objectAnimation;
        std::uint64_t doorGuid             = 0;
        std::uint64_t presentationSequence = 0;
        /** Filled in by the server, never by a client. */
        std::uint64_t actorNetworkId      = 0;
        std::uint32_t levelHash           = 0;
        std::uint32_t actorIncarnation    = 0;
        std::uint32_t actorCombatRevision = 0;
        std::uint8_t actorStateEpoch      = 0;
        DoorEvent event                   = DoorEvent::Manipulation;
        DoorActorAction actorAction       = DoorActorAction::None;
        bool open                         = false;
        bool locked                       = false;
        bool fromFront                    = false;

        /**
         * The world of the door copy the interaction changed, filled in by
         * the server. A player in the global world streams every world's
         * bodies, so it hears of another world's door being worked; only a
         * viewer showing that copy (Shared::Game::WorldCopyRank) plays the
         * door half.
         */
        std::uint32_t copyWorld = MafiaNet::VIRTUAL_WORLD_GLOBAL;

        /**
         * When the server relayed this, on the server's clock; zero on the
         * way up. A receiver shifts it onto its own clock -- the one the
         * actor's relayed poses are stamped on -- and plays it once the body
         * is drawn at that moment, rather than ahead of the pose.
         */
        MafiaNet::Time relayedAt = 0;

        bool IsSane() const {
            if (!_complete || doorGuid == 0 || levelHash == 0 || presentationSequence == 0) {
                return false;
            }
            if (actorIncarnation == 0 || actorCombatRevision == 0) {
                return false;
            }
            if (!DoorDetail::IsEvent(event) || !DoorDetail::IsActorAction(actorAction) || !DoorDetail::IsBoundedText(objectAnimation, kMaxDoorObjectAnimationLength)) {
                return false;
            }
            if (event == DoorEvent::Lockpicked && locked) {
                return false;
            }
            // Only a manipulation moves the leaf, so only it carries the
            // two animations that move it.
            return event == DoorEvent::Manipulation ? (actorAction != DoorActorAction::None && !objectAnimation.empty()) : (actorAction == DoorActorAction::None && objectAnimation.empty());
        }

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            _complete = DoorDetail::SerializeBoundedString<kMaxDoorObjectAnimationLength>(bs, write, objectAnimation) && bs->Serialize(write, doorGuid) && bs->Serialize(write, presentationSequence) && bs->Serialize(write, actorNetworkId) && bs->Serialize(write, levelHash)
                        && bs->Serialize(write, actorIncarnation) && bs->Serialize(write, actorCombatRevision) && bs->Serialize(write, actorStateEpoch) && DoorDetail::SerializeByteEnum(bs, write, event) && DoorDetail::SerializeByteEnum(bs, write, actorAction)
                        && bs->Serialize(write, open) && bs->Serialize(write, locked) && bs->Serialize(write, fromFront) && bs->Serialize(write, copyWorld) && bs->Serialize(write, relayedAt);
        }

      private:
        bool _complete = true;
    };
} // namespace KCDC::Shared::RPC

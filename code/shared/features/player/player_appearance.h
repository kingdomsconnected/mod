#pragma once

#include "shared/game/appearance_catalog.h"

#include <networking/replication/network_entity.h>

#include <mafianet/bit_stream.h>
#include <networking/rpc/rpc_identifier.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace KCDC::Shared::Game {
    /**
     * How one player's body looks, as the game's own component names.
     *
     * Four of the five strings a KCD2 soul carries; `Underwear` is left out
     * because it names a Clothing component and clothing already replicates
     * as `HumanEntity::equipment`.
     *
     * Gender comes from the soul's archetype rather than the description,
     * but it partitions the catalog, so it travels here and a receiver
     * applies it first. Every part may be empty, meaning "leave that slot to
     * the soul" -- an appearance nobody has chosen is all-empty and applies
     * nothing.
     */
    struct CharacterAppearance {
        /** Bounds what a peer can make a receiver store. */
        static constexpr std::size_t kMaxPartLength = 64;

        static constexpr std::size_t kPartCount = static_cast<std::size_t>(AppearancePart::Count);

        /** Indexed by `AppearancePart`, so the wire order is the native's. */
        std::array<std::string, kPartCount> parts;

        AppearanceGender gender = AppearanceGender::Male;

        const std::string &Part(AppearancePart part) const {
            return parts[static_cast<std::size_t>(part)];
        }

        std::string &Part(AppearancePart part) {
            return parts[static_cast<std::size_t>(part)];
        }

        bool IsEmpty() const {
            return std::all_of(parts.begin(), parts.end(), [](const std::string &part) { return part.empty(); });
        }

        bool operator==(const CharacterAppearance &other) const = default;

        // One body for both channels: FieldSerializer wraps a plain
        // bitstream as readily as the delta context, so the replicated field
        // and the RPC cannot drift into two wire formats.
        void Serialize(Framework::Networking::Replication::FieldSerializer &fields) {
            auto wireGender = static_cast<std::uint8_t>(gender);
            fields.Field(wireGender);
            if (!fields.Writing()) {
                gender = wireGender == static_cast<std::uint8_t>(AppearanceGender::Female) ? AppearanceGender::Female : AppearanceGender::Male;
            }
            for (std::string &part : parts) {
                fields.Field(part);
            }
        }

        /**
         * Whether every part is a name the game would resolve for this
         * gender. Checked by the receiver rather than trusted from the
         * sender: a name the tree does not have goes into the soul and
         * leaves the body missing a head, silently.
         */
        bool IsSane() const {
            for (std::size_t i = 0; i < kPartCount; ++i) {
                if (parts[i].size() > kMaxPartLength || !IsAppearanceName(static_cast<AppearancePart>(i), gender, parts[i])) {
                    return false;
                }
            }
            return true;
        }

        /**
         * Whether the beard goes with the face, as far as a sender can tell.
         *
         * Not part of `IsSane`: an appearance naming no head keeps the one
         * the body already has, which only the owning client can read. This
         * is the half anyone can rule out; the owner checks the rest against
         * the face it is actually wearing.
         */
        bool BeardFitsNamedHead() const {
            const std::string &head = Part(AppearancePart::Head);
            return head.empty() || IsBeardOnHead(gender, head, Part(AppearancePart::Beard));
        }
    };
} // namespace KCDC::Shared::Game

namespace KCDC::Shared::RPC {
    /**
     * What the server wants one player to look like, sent only to that
     * player. The owning client is authoritative for its own body, so this
     * is a request it republishes rather than a write anyone else can make.
     */
    struct SetPlayerAppearance {
        static constexpr auto kIdentifier = FW_RPC_IDENTIFIER("KCDC::SetPlayerAppearance");

        Shared::Game::CharacterAppearance appearance;

        bool valid = true;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Framework::Networking::Replication::FieldSerializer fields(bs, write);
            appearance.Serialize(fields);
            valid = fields.Good();
        }

        bool IsSane() const {
            return valid && appearance.IsSane();
        }
    };
} // namespace KCDC::Shared::RPC

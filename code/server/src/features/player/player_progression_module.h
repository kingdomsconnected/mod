#pragma once

#include "shared/features/player/player_progression.h"
#include "shared/game/progression_catalog.h"

#include <mafianet/types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace KCDC::Shared::Entities {
    class HumanEntity;
} // namespace KCDC::Shared::Entities

namespace KCDC::Features::Player {
    /**
     * Server authority over skills, stats and perks.
     *
     * The server holds no soul, so it never computes a level: the owner's
     * engine does, through the game's own effect path. What the server owns
     * is the input. Every XP gain the owner's game produces arrives here as a
     * request, is weighed against this server's rules and the resources'
     * veto, and goes back as an order; every perk the player confirms on the
     * perk screen is ruled on here the way the engine's own availability test
     * would. The owner then reports a digest, which this module bounds by the
     * orders that digest says it contains before accepting it -- so a level
     * no order could have produced is flagged rather than believed quietly.
     *
     * Nothing is decided by default beyond what the game itself decides: XP
     * rates are 1, budgets unlimited, caps the game's own 30, no perk blocked.
     * The rules are levers for a resource to pull.
     */
    class Progression final {
      public:
        /**
         * The most any one XP order can come out as after the owner's own
         * multipliers. In the shipped tables `XPMultiplier` (derived stat 47)
         * peaks at 1.5 (`potion_bard_4`) and the per-track `*XPOverride`
         * params at 1.2, about 2 together. A bound on what the server
         * accepts, with room for a stacking nobody found, not a game rule.
         */
        static constexpr std::uint64_t kMaxMultiplier = 4;

        /** The closest two XP requests from one player may arrive. The client batches every 250 ms. */
        static constexpr std::uint64_t kMinimumRequestMilliseconds = 50;

        void Install();

        /** Hands the new connection the current policy, so its gates are armed on arrival. */
        void OnPlayerConnect(MafiaNet::PeerGuid guid);
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid, std::uint64_t playerNetworkId);

        // --- Rules ------------------------------------------------------------

        /** Whether a player's own game may earn XP at all. Off, only resources grant it. */
        void SetNativeXp(bool enabled);
        bool NativeXp() const {
            return _nativeXp;
        }

        /** Scales every gain the game asks for on `track` (every track for `kNoProgressionTrack`) before it is ordered. 0 refuses them. */
        void SetXpRate(std::uint8_t track, double rate);

        /** Limits the XP a player's own game may earn on `track`, per minute; 0 is unlimited. Resource grants are exempt. */
        void SetXpBudget(std::uint8_t track, double xpPerMinute);

        /** The level `track` stops at: gains past it are not ordered. At most the game's own cap. */
        void SetLevelCap(std::uint8_t track, std::uint32_t level);

        void SetPerkBlocked(std::uint16_t perk, bool blocked);
        const Shared::RPC::PerkSet &BlockedPerks() const {
            return _blocked;
        }

        /** Sends every connected client the current policy. Call after a batch of rule changes. */
        void BroadcastPolicy();

        // --- Verbs ------------------------------------------------------------
        // Each is an order to the owning client; false when there is nobody
        // to ask or the arguments are out of range.

        /** `scaled` runs the owner's own XP multipliers over it, as a gain the game made would; a restore passes false to land exactly. */
        bool AddXp(Shared::Entities::HumanEntity *player, std::uint8_t track, double xp, bool scaled);
        bool SetLevel(Shared::Entities::HumanEntity *player, std::uint8_t track, std::uint32_t level);
        /** `announce` lands a raise as a gain, with its level-up popups and perk points; false restores it silently. */
        bool SetXp(Shared::Entities::HumanEntity *player, std::uint8_t track, double xp, bool announce = true);
        bool RemoveXp(Shared::Entities::HumanEntity *player, std::uint8_t track, double xp);
        bool GrantPerk(Shared::Entities::HumanEntity *player, std::uint16_t perk);
        bool RemovePerk(Shared::Entities::HumanEntity *player, std::uint16_t perk);
        bool AddPerkPoints(Shared::Entities::HumanEntity *player, std::uint8_t tree, std::uint32_t count);
        bool SetPerkPoints(Shared::Entities::HumanEntity *player, std::uint8_t tree, std::uint32_t count);
        bool Respec(Shared::Entities::HumanEntity *player);

        // --- Reads ------------------------------------------------------------

        /** The last digest this server accepted, or null before the player's first. */
        const Shared::RPC::ProgressionDigest *Accepted(std::uint64_t playerNetworkId) const;

      private:
        struct Outstanding {
            std::uint32_t sequence = 0;
            Shared::RPC::ProgressionOrderKind kind;
            std::uint8_t track = Shared::RPC::kNoProgressionTrack;

            // How far this order can move its track's total XP, once applied:
            // the ordered amount times the multiplier bound when the owner
            // scales it, the exact amount when not.
            std::uint64_t boundQ7 = 0;

            // What an XP order asks for before any multiplier: where the track
            // is heading, as opposed to how far it could be pushed.
            std::uint64_t faceQ7 = 0;

            // A level order's floor: the total XP of the level it raises to.
            std::uint64_t levelTotalQ7 = 0;

            std::uint16_t perk = Shared::RPC::kNoPerk;
        };

        struct Budget {
            double tokensQ7        = 0.0;
            std::uint64_t filledAt = 0;
        };

        struct PlayerProgression {
            MafiaNet::PeerGuid owner = MafiaNet::PeerGuid();
            bool hasDigest           = false;
            Shared::RPC::ProgressionDigest accepted;
            std::uint32_t nextSequence = 1;
            std::vector<Outstanding> outstanding;
            std::array<Budget, Shared::RPC::kProgressionTracks> budgets {};
            std::uint64_t lastRequestAt = 0;
        };

        bool AdmitXpRequest(PlayerProgression &state);

        void AcceptXpRequest(Shared::Entities::HumanEntity *player, const Shared::RPC::XpRequest &request);
        void AcceptLearnRequest(Shared::Entities::HumanEntity *player, std::uint16_t perk);
        void AcceptDigest(Shared::Entities::HumanEntity *player, const Shared::RPC::ProgressionDigest &digest);
        bool SendOrder(Shared::Entities::HumanEntity *player, Shared::RPC::ProgressionOrder order, std::uint64_t boundQ7, std::uint64_t levelTotalQ7);
        void Refuse(Shared::Entities::HumanEntity *player, std::uint16_t perk, Shared::RPC::PerkRefusal reason);
        std::uint64_t ProjectedTotalQ7(const PlayerProgression &state, std::uint8_t track) const;
        std::uint32_t TakeBudget(PlayerProgression &state, std::uint8_t track, std::uint32_t xpQ7);
        PlayerProgression &StateOf(Shared::Entities::HumanEntity *player);
        const PlayerProgression *FindPlayer(std::uint64_t playerNetworkId) const;

        std::map<std::uint64_t, PlayerProgression> _players;

        bool _nativeXp = true;
        std::array<double, Shared::RPC::kProgressionTracks> _rates {};
        std::array<double, Shared::RPC::kProgressionTracks> _budgetsPerMinute {};
        std::array<std::uint32_t, Shared::RPC::kProgressionTracks> _caps {};
        Shared::RPC::PerkSet _blocked;
    };
} // namespace KCDC::Features::Player

#include "features/player/player_progression_module.h"

#include "features/player/player_progression_scripting.h"
#include "features/player/player_roster.h"

#include "shared/features/player/player_entity.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>

#include <mafianet/get_time.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace KCDC::Features::Player {
    namespace {
        constexpr const char *kLogger = "Progression";

        // One XP in the game's own units, which is also the rounding slack a
        // bound allows: the owner multiplies in fixed point and truncates.
        constexpr std::uint64_t kOneXpQ7 = 128;

        std::string TrackName(std::uint8_t track) {
            return track < Shared::Game::g_ProgressionTracks.size() ? std::string(Shared::Game::g_ProgressionTracks[track].name) : std::string("<unknown track>");
        }
    } // namespace

    void Progression::Install() {
        for (std::size_t track = 0; track < Shared::RPC::kProgressionTracks; ++track) {
            _rates[track]            = 1.0;
            _budgetsPerMinute[track] = 0.0;
            _caps[track]             = track < Shared::Game::g_ProgressionTracks.size() ? Shared::Game::g_ProgressionTracks[track].cap : 0;
        }
        if (Shared::Game::g_ProgressionTracks.size() != Shared::RPC::kProgressionTracks || Shared::Game::g_Perks.size() > Shared::RPC::kPerkWords * 64) {
            Framework::Logging::GetLogger(kLogger)->error("The progression catalog ({} tracks, {} perks) does not fit the wire ({} tracks, {} perks); regenerate it", Shared::Game::g_ProgressionTracks.size(), Shared::Game::g_Perks.size(), Shared::RPC::kProgressionTracks,
                Shared::RPC::kPerkWords * 64);
        }

        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }

        net->RegisterRPC<Shared::RPC::XpRequest>([this](const Shared::RPC::XpRequest &request, MafiaNet::Packet *packet) {
            auto *player = request.IsSane() ? Players::FromPacket(packet) : nullptr;
            if (player != nullptr) {
                AcceptXpRequest(player, request);
            }
        });

        net->RegisterRPC<Shared::RPC::LearnPerkRequest>([this](const Shared::RPC::LearnPerkRequest &request, MafiaNet::Packet *packet) {
            auto *player = request.IsSane() ? Players::FromPacket(packet) : nullptr;
            if (player != nullptr) {
                AcceptLearnRequest(player, request.perk);
            }
        });

        net->RegisterRPC<Shared::RPC::ProgressionDigest>([this](const Shared::RPC::ProgressionDigest &digest, MafiaNet::Packet *packet) {
            auto *player = digest.IsSane() ? Players::FromPacket(packet) : nullptr;
            if (player != nullptr) {
                AcceptDigest(player, digest);
            }
        });
    }

    void Progression::OnPlayerConnect(MafiaNet::PeerGuid guid) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        Shared::RPC::ProgressionPolicy policy;
        policy.nativeXp = _nativeXp;
        policy.blocked  = _blocked;
        net->SendRPC(policy, MafiaNet::ToGuid(guid));
    }

    void Progression::OnPlayerDisconnect(MafiaNet::PeerGuid, std::uint64_t playerNetworkId) {
        _players.erase(playerNetworkId);
    }

    // --- Rules ----------------------------------------------------------------

    void Progression::SetNativeXp(bool enabled) {
        _nativeXp = enabled;
        BroadcastPolicy();
    }

    void Progression::SetXpRate(std::uint8_t track, double rate) {
        const double clamped = std::isfinite(rate) ? std::clamp(rate, 0.0, 100.0) : 1.0;
        for (std::size_t i = 0; i < Shared::RPC::kProgressionTracks; ++i) {
            if (track == Shared::RPC::kNoProgressionTrack || track == i) {
                _rates[i] = clamped;
            }
        }
    }

    void Progression::SetXpBudget(std::uint8_t track, double xpPerMinute) {
        const double clamped = std::isfinite(xpPerMinute) ? std::max(xpPerMinute, 0.0) : 0.0;
        for (std::size_t i = 0; i < Shared::RPC::kProgressionTracks; ++i) {
            if (track == Shared::RPC::kNoProgressionTrack || track == i) {
                _budgetsPerMinute[i] = clamped;
            }
        }
    }

    void Progression::SetLevelCap(std::uint8_t track, std::uint32_t level) {
        for (std::size_t i = 0; i < Shared::RPC::kProgressionTracks && i < Shared::Game::g_ProgressionTracks.size(); ++i) {
            if (track == Shared::RPC::kNoProgressionTrack || track == i) {
                _caps[i] = std::min<std::uint32_t>(level, Shared::Game::g_ProgressionTracks[i].cap);
            }
        }
    }

    void Progression::SetPerkBlocked(std::uint16_t perk, bool blocked) {
        _blocked.Set(perk, blocked);
        if (!blocked) {
            return;
        }
        // Blocked means nobody has it: whoever owns it now is ordered to give
        // it up. A later `player.addPerk` is the script's own decision.
        auto *replication = Framework::CoreModules::GetReplication();
        for (const auto &[networkId, state] : _players) {
            auto *player = replication != nullptr && state.hasDigest && state.accepted.perks.Has(perk) ? replication->GetEntity<Shared::Entities::HumanEntity>(networkId) : nullptr;
            if (player != nullptr) {
                RemovePerk(player, perk);
            }
        }
    }

    void Progression::BroadcastPolicy() {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        Shared::RPC::ProgressionPolicy policy;
        policy.nativeXp = _nativeXp;
        policy.blocked  = _blocked;
        net->BroadcastRPC(policy);
    }

    // --- Verbs ----------------------------------------------------------------

    bool Progression::AddXp(Shared::Entities::HumanEntity *player, std::uint8_t track, double xp, bool scaled) {
        if (track >= Shared::RPC::kProgressionTracks || Shared::Game::IsInducedTrack(track) || !std::isfinite(xp) || xp <= 0.0) {
            return false;
        }
        const double q7 = std::min(xp * 128.0, static_cast<double>(Shared::RPC::kMaxXpQ7));
        if (q7 < 1.0) {
            return false;
        }

        Shared::RPC::ProgressionOrder order;
        order.kind   = Shared::RPC::ProgressionOrderKind::Xp;
        order.track  = track;
        order.value  = static_cast<std::uint32_t>(q7);
        order.scaled = scaled;
        return SendOrder(player, order, static_cast<std::uint64_t>(order.value) * (scaled ? kMaxMultiplier : 1), 0);
    }

    bool Progression::SetLevel(Shared::Entities::HumanEntity *player, std::uint8_t track, std::uint32_t level) {
        if (track >= Shared::RPC::kProgressionTracks || Shared::Game::IsInducedTrack(track) || level > Shared::Game::g_ProgressionTracks[track].cap) {
            return false;
        }
        return SetXp(player, track, static_cast<double>(Shared::Game::TotalXpQ7ForLevel(Shared::Game::g_ProgressionTracks[track], level)) / 128);
    }

    bool Progression::SetXp(Shared::Entities::HumanEntity *player, std::uint8_t track, double xp, bool announce) {
        if (track >= Shared::RPC::kProgressionTracks || Shared::Game::IsInducedTrack(track) || !std::isfinite(xp) || xp < 0) {
            return false;
        }
        const auto &entry = Shared::Game::g_ProgressionTracks[track];
        if (xp * 128 > Shared::Game::TotalXpQ7ForLevel(entry, entry.cap)) {
            return false;
        }
        Shared::RPC::ProgressionOrder order;
        order.kind     = Shared::RPC::ProgressionOrderKind::SetTotalXp;
        order.track    = track;
        order.value    = static_cast<std::uint32_t>(xp * 128);
        order.announce = announce;
        return SendOrder(player, order, 0, order.value);
    }
    bool Progression::RemoveXp(Shared::Entities::HumanEntity *player, std::uint8_t track, double xp) {
        if (player == nullptr || track >= Shared::RPC::kProgressionTracks || Shared::Game::IsInducedTrack(track) || !std::isfinite(xp) || xp <= 0) {
            return false;
        }
        const auto &state = StateOf(player);
        if (!state.hasDigest) {
            return false;
        }
        const auto total   = ProjectedTotalQ7(state, track);
        const auto removal = static_cast<std::uint64_t>(std::min(xp * 128, static_cast<double>(total)));
        return SetXp(player, track, static_cast<double>(total - removal) / 128);
    }

    bool Progression::GrantPerk(Shared::Entities::HumanEntity *player, std::uint16_t perk) {
        if (Shared::Game::PerkAt(perk) == nullptr) {
            return false;
        }
        Shared::RPC::ProgressionOrder order;
        order.kind = Shared::RPC::ProgressionOrderKind::GrantPerk;
        order.perk = perk;
        return SendOrder(player, order, 0, 0);
    }

    bool Progression::RemovePerk(Shared::Entities::HumanEntity *player, std::uint16_t perk) {
        if (Shared::Game::PerkAt(perk) == nullptr) {
            return false;
        }
        Shared::RPC::ProgressionOrder order;
        order.kind = Shared::RPC::ProgressionOrderKind::RemovePerk;
        order.perk = perk;
        return SendOrder(player, order, 0, 0);
    }

    bool Progression::AddPerkPoints(Shared::Entities::HumanEntity *player, std::uint8_t tree, std::uint32_t count) {
        if (tree >= Shared::RPC::kPerkTrees || count == 0 || count > 0xFFFF) {
            return false;
        }
        Shared::RPC::ProgressionOrder order;
        order.kind  = Shared::RPC::ProgressionOrderKind::AddPerkPoints;
        order.track = tree;
        order.value = count;
        return SendOrder(player, order, 0, 0);
    }

    bool Progression::SetPerkPoints(Shared::Entities::HumanEntity *player, std::uint8_t tree, std::uint32_t count) {
        if (tree >= Shared::RPC::kPerkTrees || count > 0xFFFF) {
            return false;
        }
        Shared::RPC::ProgressionOrder order;
        order.kind  = Shared::RPC::ProgressionOrderKind::SetPerkPoints;
        order.track = tree;
        order.value = count;
        return SendOrder(player, order, 0, 0);
    }

    bool Progression::Respec(Shared::Entities::HumanEntity *player) {
        Shared::RPC::ProgressionOrder order;
        order.kind = Shared::RPC::ProgressionOrderKind::Respec;
        return SendOrder(player, order, 0, 0);
    }

    const Shared::RPC::ProgressionDigest *Progression::Accepted(std::uint64_t playerNetworkId) const {
        const PlayerProgression *state = FindPlayer(playerNetworkId);
        return state != nullptr && state->hasDigest ? &state->accepted : nullptr;
    }

    // --- Requests ---------------------------------------------------------------

    bool Progression::AdmitXpRequest(PlayerProgression &state) {
        // An honest client batches its gains four times a second; anything
        // much faster is a flood, and every gain in a request raises a
        // script event.
        const std::uint64_t now = MafiaNet::GetTime();
        if (state.lastRequestAt != 0 && now - state.lastRequestAt < kMinimumRequestMilliseconds) {
            return false;
        }
        state.lastRequestAt = now;
        return true;
    }

    void Progression::AcceptXpRequest(Shared::Entities::HumanEntity *player, const Shared::RPC::XpRequest &request) {
        PlayerProgression &state = StateOf(player);
        if (!_nativeXp || !AdmitXpRequest(state)) {
            return;
        }

        for (const Shared::RPC::XpGain &gain : request.gains) {
            if (Shared::Game::IsInducedTrack(gain.track) || _rates[gain.track] <= 0.0) {
                continue;
            }
            if (!Scripting::Events::PlayerXpGaining(player->GetNetworkID(), TrackName(gain.track), static_cast<double>(gain.xpQ7) / 128.0, gain.source)) {
                continue;
            }

            const double rated = std::min(static_cast<double>(gain.xpQ7) * _rates[gain.track], static_cast<double>(Shared::RPC::kMaxXpQ7));
            std::uint32_t xpQ7 = TakeBudget(state, gain.track, static_cast<std::uint32_t>(rated));
            bool scaled        = gain.scaled;
            if (xpQ7 == 0) {
                continue;
            }

            // A cap below the game's own is reached exactly: near it, the gain
            // goes out unscaled for precisely what is left, so no multiplier
            // can carry the track past it.
            const Shared::Game::ProgressionTrack &track = Shared::Game::g_ProgressionTracks[gain.track];
            if (_caps[gain.track] < track.cap) {
                const std::uint64_t capTotal  = Shared::Game::TotalXpQ7ForLevel(track, _caps[gain.track]);
                const std::uint64_t projected = ProjectedTotalQ7(state, gain.track);
                if (projected >= capTotal) {
                    continue;
                }
                const std::uint64_t reach = static_cast<std::uint64_t>(xpQ7) * (scaled ? kMaxMultiplier : 1);
                if (projected + reach > capTotal) {
                    xpQ7   = static_cast<std::uint32_t>(std::min<std::uint64_t>(capTotal - projected, xpQ7));
                    scaled = false;
                }
            }

            Shared::RPC::ProgressionOrder order;
            order.kind   = Shared::RPC::ProgressionOrderKind::Xp;
            order.track  = gain.track;
            order.value  = xpQ7;
            order.scaled = scaled;
            SendOrder(player, order, static_cast<std::uint64_t>(xpQ7) * (scaled ? kMaxMultiplier : 1), 0);
        }
    }

    void Progression::AcceptLearnRequest(Shared::Entities::HumanEntity *player, std::uint16_t perk) {
        const Shared::Game::PerkEntry *entry = Shared::Game::PerkAt(perk);
        PlayerProgression &state             = StateOf(player);
        if (entry == nullptr || !state.hasDigest) {
            return;
        }

        const Shared::RPC::ProgressionDigest &accepted = state.accepted;
        const int tree                                 = Shared::Game::PerkTreeOf(*entry);

        // Everything already in flight counts: a double click on the perk
        // screen must not spend the same point twice.
        bool pendingSame        = false;
        std::uint32_t pendingIn = 0;
        for (const Outstanding &outstanding : state.outstanding) {
            if (outstanding.kind == Shared::RPC::ProgressionOrderKind::LearnPerk || outstanding.kind == Shared::RPC::ProgressionOrderKind::GrantPerk) {
                pendingSame                          = pendingSame || outstanding.perk == perk;
                const Shared::Game::PerkEntry *other = Shared::Game::PerkAt(outstanding.perk);
                if (outstanding.kind == Shared::RPC::ProgressionOrderKind::LearnPerk && other != nullptr && Shared::Game::PerkTreeOf(*other) == tree) {
                    ++pendingIn;
                }
            }
        }

        if (entry->visibility != Shared::Game::PerkVisibility::kVisible || tree < 0) {
            Refuse(player, perk, Shared::RPC::PerkRefusal::Hidden);
            return;
        }
        if (accepted.perks.Has(perk) || pendingSame) {
            Refuse(player, perk, Shared::RPC::PerkRefusal::Owned);
            return;
        }
        if (_blocked.Has(perk)) {
            Refuse(player, perk, Shared::RPC::PerkRefusal::Blocked);
            return;
        }

        // The engine's own availability test, `TryMakePerkAvailable`: the
        // tree's level, then the parent.
        const std::uint32_t treeLevel = tree == Shared::RPC::kMainPerkTree ? accepted.mainLevel : accepted.tracks[static_cast<std::size_t>(tree)].level;
        if (treeLevel < entry->level) {
            Refuse(player, perk, Shared::RPC::PerkRefusal::Level);
            return;
        }
        if (entry->parent >= 0 && !accepted.perks.Has(static_cast<std::size_t>(entry->parent))) {
            Refuse(player, perk, Shared::RPC::PerkRefusal::Parent);
            return;
        }
        const std::uint16_t partner = Shared::Game::ExclusivePartnerOf(perk);
        if (partner != Shared::RPC::kNoPerk && accepted.perks.Has(partner)) {
            Refuse(player, perk, Shared::RPC::PerkRefusal::Exclusive);
            return;
        }
        if (accepted.perkPoints[static_cast<std::size_t>(tree)] <= pendingIn) {
            Refuse(player, perk, Shared::RPC::PerkRefusal::Points);
            return;
        }
        if (!Scripting::Events::PlayerPerkLearning(player->GetNetworkID(), std::string(entry->name))) {
            Refuse(player, perk, Shared::RPC::PerkRefusal::Script);
            return;
        }

        Shared::RPC::ProgressionOrder order;
        order.kind = Shared::RPC::ProgressionOrderKind::LearnPerk;
        order.perk = perk;
        SendOrder(player, order, 0, 0);
    }

    void Progression::AcceptDigest(Shared::Entities::HumanEntity *player, const Shared::RPC::ProgressionDigest &digest) {
        PlayerProgression &state = StateOf(player);

        // Wrap-safe "came after", so a digest overtaken in flight is dropped
        // rather than winding the body's progression backwards.
        if (state.hasDigest && static_cast<std::int32_t>(digest.revision - state.accepted.revision) <= 0) {
            return;
        }

        const bool first                              = !state.hasDigest;
        const Shared::RPC::ProgressionDigest previous = state.accepted;

        // Everything the orders this digest contains could have moved.
        std::array<std::uint64_t, Shared::RPC::kProgressionTracks> allowance {};
        std::array<std::uint64_t, Shared::RPC::kProgressionTracks> floor {};
        std::array<bool, Shared::RPC::kProgressionTracks> replaced {};
        Shared::RPC::PerkSet granted;
        Shared::RPC::PerkSet learnt;
        Shared::RPC::PerkSet removed;
        bool respecced = false;
        for (const Outstanding &outstanding : state.outstanding) {
            if (static_cast<std::int32_t>(outstanding.sequence - digest.appliedOrder) > 0) {
                continue;
            }
            if (outstanding.track < Shared::RPC::kProgressionTracks) {
                if (outstanding.kind == Shared::RPC::ProgressionOrderKind::SetTotalXp) {
                    allowance[outstanding.track] = 0;
                    floor[outstanding.track]     = outstanding.levelTotalQ7;
                    replaced[outstanding.track]  = true;
                }
                else {
                    allowance[outstanding.track] += outstanding.boundQ7;
                    floor[outstanding.track] = std::max(floor[outstanding.track], outstanding.levelTotalQ7);
                }
            }
            switch (outstanding.kind) {
            case Shared::RPC::ProgressionOrderKind::GrantPerk: granted.Set(outstanding.perk, true); break;
            case Shared::RPC::ProgressionOrderKind::LearnPerk: learnt.Set(outstanding.perk, true); break;
            case Shared::RPC::ProgressionOrderKind::RemovePerk: removed.Set(outstanding.perk, true); break;
            case Shared::RPC::ProgressionOrderKind::Respec: respecced = true; break;
            default: break;
            }
        }
        std::erase_if(state.outstanding, [&digest](const Outstanding &outstanding) {
            return static_cast<std::int32_t>(outstanding.sequence - digest.appliedOrder) <= 0;
        });

        // The owner is authoritative for its own soul, so the digest is
        // adopted whatever it says: reads stay
        // truthful, and what to do about a rejected one is the resources'
        // call. Adopted, and published to everyone else, before any event is
        // raised, so a handler reads the state the event is about.
        state.accepted  = digest;
        state.hasDigest = true;

        // What everyone else sees: levels and perks only, so XP inside a level
        // changes nothing on the wire.
        Shared::Game::ProgressionView view;
        view.mainLevel = digest.mainLevel;
        for (std::size_t i = 0; i < Shared::RPC::kProgressionTracks; ++i) {
            view.levels[i] = static_cast<std::uint8_t>(std::min<std::uint32_t>(digest.tracks[i].level, 255));
        }
        view.perks = digest.perks;
        if (!(view == player->progression)) {
            player->progression = view;
        }

        // The first digest is the character the session started with: a fresh
        // context never loads a save, so it is the player soul's own table
        // values, and there is nothing to bound it by or to announce.
        if (first) {
            return;
        }

        const std::uint64_t playerNetworkId = player->GetNetworkID();
        std::string rejected;
        for (std::size_t i = 0; i < Shared::RPC::kProgressionTracks && i < Shared::Game::g_ProgressionTracks.size(); ++i) {
            const Shared::Game::ProgressionTrack &track = Shared::Game::g_ProgressionTracks[i];
            const std::uint64_t before                  = Shared::Game::TotalXpQ7(track, previous.tracks[i]);
            const std::uint64_t after                   = Shared::Game::TotalXpQ7(track, digest.tracks[i]);

            // Fencing is derived from the weapon skills the bound already
            // covers; the engine moves it on its own.
            // A replaced track may also stay where it was: the owner acknowledges
            // an order it could not apply, and keeping the accepted value gains
            // nothing.
            const auto minimum = replaced[i] ? std::min(before, floor[i]) : before;
            const auto maximum = std::max(before, floor[i]) + allowance[i];
            if (!Shared::Game::IsInducedTrack(i) && (after > maximum + kOneXpQ7 || (after < minimum && minimum - after > kOneXpQ7)) && rejected.empty()) {
                rejected = "track " + std::string(track.name) + " moved outside what this server ordered";
            }
            if (after > before) {
                Scripting::Events::PlayerXpGained(playerNetworkId, std::string(track.name), static_cast<double>(after - before) / 128.0);
            }
            if (digest.tracks[i].level > previous.tracks[i].level) {
                Scripting::Events::PlayerLevelUp(playerNetworkId, std::string(track.name), digest.tracks[i].level, digest.perkPoints[i]);
            }
        }

        Shared::RPC::PerkSet::ForEachChange(previous.perks, digest.perks, [&](std::size_t index, bool owned) {
            const Shared::Game::PerkEntry *entry = Shared::Game::PerkAt(index);
            if (entry == nullptr) {
                return;
            }
            if (!owned) {
                Scripting::Events::PlayerPerkRemoved(playerNetworkId, std::string(entry->name), removed.Has(index) || respecced ? "server" : "native");
                return;
            }
            // A perk-screen perk only ever arrives through a learn this server
            // ordered; everything else the game may grant on its own, unless
            // it is blocked.
            const bool ordered = granted.Has(index) || learnt.Has(index);
            if (!ordered && ((entry->visibility == Shared::Game::PerkVisibility::kVisible && !entry->autolearnable) || _blocked.Has(index)) && rejected.empty()) {
                rejected = "perk " + std::string(entry->name) + " arrived without an order";
            }
            // A blocked perk the game handed out anyway -- a new character's
            // starting perks, a quest -- is taken back as soon as it shows.
            if (!ordered && _blocked.Has(index)) {
                RemovePerk(player, static_cast<std::uint16_t>(index));
            }
            Scripting::Events::PlayerPerkAdded(playerNetworkId, std::string(entry->name), learnt.Has(index) ? "learned" : granted.Has(index) ? "server" : "native");
        });

        if (!rejected.empty()) {
            Framework::Logging::GetLogger(kLogger)->warn("Player '{}' progression digest rejected: {}", player->nickname, rejected);
            Scripting::Events::PlayerProgressionRejected(playerNetworkId, rejected);
        }
    }

    // --- Plumbing -------------------------------------------------------------

    bool Progression::SendOrder(Shared::Entities::HumanEntity *player, Shared::RPC::ProgressionOrder order, std::uint64_t boundQ7, std::uint64_t levelTotalQ7) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (player == nullptr || net == nullptr) {
            return false;
        }
        PlayerProgression &state = StateOf(player);
        order.sequence           = state.nextSequence;
        if (!order.IsSane()) {
            return false;
        }
        state.nextSequence = state.nextSequence + 1 == 0 ? 1 : state.nextSequence + 1;

        Outstanding outstanding;
        outstanding.sequence     = order.sequence;
        outstanding.kind         = order.kind;
        outstanding.track        = order.kind == Shared::RPC::ProgressionOrderKind::Xp || order.kind == Shared::RPC::ProgressionOrderKind::SetTotalXp ? order.track : Shared::RPC::kNoProgressionTrack;
        outstanding.boundQ7      = boundQ7;
        outstanding.faceQ7       = order.kind == Shared::RPC::ProgressionOrderKind::Xp ? order.value : 0;
        outstanding.levelTotalQ7 = levelTotalQ7;
        outstanding.perk         = order.perk;
        state.outstanding.push_back(outstanding);

        net->SendRPC(order, MafiaNet::ToGuid(player->ownerGUID));
        return true;
    }

    void Progression::Refuse(Shared::Entities::HumanEntity *player, std::uint16_t perk, Shared::RPC::PerkRefusal reason) {
        auto *net = Framework::CoreModules::GetNetworkPeer();
        if (net == nullptr) {
            return;
        }
        Shared::RPC::PerkRefused refused;
        refused.perk   = perk;
        refused.reason = reason;
        net->SendRPC(refused, MafiaNet::ToGuid(player->ownerGUID));
    }

    std::uint64_t Progression::ProjectedTotalQ7(const PlayerProgression &state, std::uint8_t track) const {
        const Shared::Game::ProgressionTrack &entry = Shared::Game::g_ProgressionTracks[track];
        std::uint64_t total                         = state.hasDigest ? Shared::Game::TotalXpQ7(entry, state.accepted.tracks[track]) : 0;
        std::uint64_t floor                         = 0;
        for (const Outstanding &outstanding : state.outstanding) {
            if (outstanding.track != track) {
                continue;
            }
            if (outstanding.kind == Shared::RPC::ProgressionOrderKind::SetTotalXp) {
                total = outstanding.levelTotalQ7;
                floor = 0;
                continue;
            }
            // Counted at face value rather than at the bound: projection is
            // where the track is heading, and the bound is for catching lies.
            total += outstanding.faceQ7;
            floor = std::max(floor, outstanding.levelTotalQ7);
        }
        return std::max(total, floor);
    }

    std::uint32_t Progression::TakeBudget(PlayerProgression &state, std::uint8_t track, std::uint32_t xpQ7) {
        const double perMinute = _budgetsPerMinute[track];
        if (perMinute <= 0.0) {
            return xpQ7;
        }

        // A bucket that holds a minute's worth and refills continuously, so a
        // burst after a quiet spell is allowed and a steady grind is not.
        Budget &budget          = state.budgets[track];
        const std::uint64_t now = MafiaNet::GetTime();
        const double capacity   = perMinute * 128.0;
        if (budget.filledAt == 0) {
            budget.tokensQ7 = capacity;
        }
        else {
            budget.tokensQ7 = std::min(capacity, budget.tokensQ7 + capacity * static_cast<double>(now - budget.filledAt) / 60000.0);
        }
        budget.filledAt = now;

        const double granted = std::min(static_cast<double>(xpQ7), std::floor(budget.tokensQ7));
        budget.tokensQ7 -= granted;
        return static_cast<std::uint32_t>(granted);
    }

    Progression::PlayerProgression &Progression::StateOf(Shared::Entities::HumanEntity *player) {
        PlayerProgression &state = _players[player->GetNetworkID()];
        state.owner              = player->ownerGUID;
        return state;
    }

    const Progression::PlayerProgression *Progression::FindPlayer(std::uint64_t playerNetworkId) const {
        const auto found = _players.find(playerNetworkId);
        return found != _players.end() ? &found->second : nullptr;
    }
} // namespace KCDC::Features::Player

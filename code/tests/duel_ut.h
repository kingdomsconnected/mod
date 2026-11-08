#pragma once

#include "features/player/player_duel_roster.h"
#include "shared/features/player/player_entity.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <thread>
#include <vector>

namespace DuelTests {
    /** Stand-ins for a fighter and a body; the roster only ever compares their addresses. */
    struct Fighter {
        int tag = 0;
    };

    struct Body {
        int tag = 0;
    };

    using Roster = KCDC::Features::Player::DuelRoster<Fighter, Body>;

    /**
     * One machine's view: its own fighter (id 1), two remote players' puppets
     * (ids 2 and 3, the first pickable) and an NPC that is nobody's.
     */
    struct Scene {
        Fighter local, remoteA, remoteB, npc;
        Body localBody, remoteABody, remoteBBody, npcBody;
        Roster roster;

        Scene() {
            roster.SetLocal(&local, 1);
            roster.AddRemote(&remoteA, 2, &remoteABody, true);
            roster.AddRemote(&remoteB, 3, &remoteBBody, false);
        }
    };
} // namespace DuelTests

MODULE(duel_roster, {
    IT("clinch exclusion covers player pairs but preserves NPC fights", {
        DuelTests::Scene scene;
        EQUALS(scene.roster.IsPlayerPair(&scene.local, &scene.remoteA), true);
        EQUALS(scene.roster.IsPlayerPair(&scene.remoteA, &scene.local), true);
        EQUALS(scene.roster.IsPlayerPair(&scene.remoteA, &scene.remoteB), true);
        EQUALS(scene.roster.IsPlayerPair(&scene.local, &scene.npc), false);
        EQUALS(scene.roster.IsPlayerPair(&scene.remoteA, &scene.npc), false);
        EQUALS(scene.roster.IsPlayerPair(&scene.npc, &scene.remoteA), false);
        EQUALS(scene.roster.IsPlayerPair(&scene.npc, &scene.npc), false);
        EQUALS(scene.roster.IsPlayerPair(&scene.remoteA, &scene.remoteA), false);
        EQUALS(scene.roster.IsPlayerPair(&scene.local, nullptr), false);
        scene.roster.SetLocalTarget(2);
        EQUALS(scene.roster.IsPlayerPair(&scene.local, &scene.npc), false);
    });

    IT("only owner-requested remote stances survive an automatic idle exit", {
        DuelTests::Scene scene;
        scene.roster.HoldRemoteStance(&scene.remoteA);
        scene.roster.HoldRemoteStance(&scene.local);
        scene.roster.HoldRemoteStance(&scene.npc);
        EQUALS(scene.roster.HoldsRemoteStance(&scene.remoteA), true);
        EQUALS(scene.roster.HoldsRemoteStance(&scene.remoteB), false);
        EQUALS(scene.roster.HoldsRemoteStance(&scene.local), false);
        EQUALS(scene.roster.HoldsRemoteStance(&scene.npc), false);
        scene.roster.Clear();
        scene.roster.AddRemote(&scene.remoteA, 2, &scene.remoteABody, true);
        EQUALS(scene.roster.HoldsRemoteStance(&scene.remoteA), false);
    });

    IT("hands only a remote fighter its owner's lock, until the next roster", {
        DuelTests::Scene scene;
        scene.roster.HoldRemoteLock(&scene.remoteA);
        scene.roster.HoldRemoteLock(&scene.local);
        scene.roster.HoldRemoteLock(&scene.npc);
        EQUALS(scene.roster.HoldsRemoteLock(&scene.remoteA), true);
        EQUALS(scene.roster.HoldsRemoteLock(&scene.remoteB), false);
        EQUALS(scene.roster.HoldsRemoteLock(&scene.local), false);
        EQUALS(scene.roster.HoldsRemoteLock(&scene.npc), false);
        scene.roster.Clear();
        scene.roster.AddRemote(&scene.remoteA, 2, &scene.remoteABody, true);
        EQUALS(scene.roster.HoldsRemoteLock(&scene.remoteA), false);
    });

    IT("tells the local fighter, the remote ones and everyone else apart", {
        DuelTests::Scene scene;
        EQUALS(scene.roster.IsLocal(&scene.local), true);
        EQUALS(scene.roster.IsRemote(&scene.local), false);
        EQUALS(scene.roster.IsRemote(&scene.remoteA), true);
        EQUALS(scene.roster.IsRemote(&scene.remoteB), true);
        EQUALS(scene.roster.IsRemote(&scene.npc), false);
        EQUALS(scene.roster.IsLocal(&scene.npc), false);
        EQUALS(scene.roster.IsRemote(nullptr), false);
        EQUALS(scene.roster.IsLocal(nullptr), false);
        EQUALS(scene.roster.NetworkIdOf(&scene.local), static_cast<std::uint64_t>(1));
        EQUALS(scene.roster.NetworkIdOf(&scene.remoteA), static_cast<std::uint64_t>(2));
        EQUALS(scene.roster.NetworkIdOf(&scene.remoteB), static_cast<std::uint64_t>(3));
        EQUALS(scene.roster.NetworkIdOf(&scene.npc), static_cast<std::uint64_t>(0));
        EQUALS(scene.roster.NetworkIdOf(nullptr), static_cast<std::uint64_t>(0));
        EQUALS(scene.roster.LocalNetworkId(), static_cast<std::uint64_t>(1));
    });

    IT("offers the picker only the bodies it may weigh", {
        DuelTests::Scene scene;
        EQUALS(scene.roster.IsPickable(&scene.remoteABody), true);
        EQUALS(scene.roster.IsPickable(&scene.remoteBBody), false);
        EQUALS(scene.roster.IsPickable(&scene.npcBody), false);
        EQUALS(scene.roster.IsPickable(nullptr), false);
        EQUALS(scene.roster.PickableBodies().size(), static_cast<std::size_t>(1));
    });

    IT("ignores a body with no fighter", {
        DuelTests::Roster roster;
        DuelTests::Body body;
        roster.AddRemote(nullptr, 9, &body, true);
        EQUALS(roster.IsPickable(&body), false);
        EQUALS(roster.PickableBodies().empty(), true);
    });

    IT("drops one remote player's blow on another's body and nothing else", {
        DuelTests::Scene scene;
        EQUALS(scene.roster.ShouldDropContact(&scene.local, &scene.remoteA), false);
        EQUALS(scene.roster.ShouldDropContact(&scene.remoteB, &scene.remoteA), true);
        EQUALS(scene.roster.ShouldDropContact(&scene.npc, &scene.remoteA), false);
        EQUALS(scene.roster.ShouldDropContact(&scene.remoteA, &scene.local), false);
        EQUALS(scene.roster.ShouldDropContact(&scene.local, &scene.npc), false);
        EQUALS(scene.roster.ShouldDropContact(&scene.remoteA, &scene.npc), false);
        EQUALS(scene.roster.ShouldDropContact(nullptr, &scene.remoteA), false);
        EQUALS(scene.roster.ShouldDropContact(&scene.local, nullptr), false);
    });

    IT("resolves here only a remote player's blow on the local body", {
        DuelTests::Scene scene;
        EQUALS(scene.roster.ResolvingAttacker(&scene.remoteA, &scene.local) == &scene.remoteA, true);
        EQUALS(scene.roster.ResolvingAttacker(&scene.npc, &scene.local) == nullptr, true);
        EQUALS(scene.roster.ResolvingAttacker(&scene.remoteA, &scene.remoteB) == nullptr, true);
        EQUALS(scene.roster.ResolvingAttacker(&scene.local, &scene.remoteA) == nullptr, true);
        EQUALS(scene.roster.ResolvingAttacker(nullptr, &scene.local) == nullptr, true);
    });

    IT("posts a planner's windows onto the local body and never onto a puppet", {
        DuelTests::Scene scene;
        EQUALS(scene.roster.MayDispatchSlots(&scene.npc, &scene.npc), true);
        EQUALS(scene.roster.MayDispatchSlots(&scene.local, &scene.npc), true);
        EQUALS(scene.roster.MayDispatchSlots(&scene.npc, &scene.local), true);
        EQUALS(scene.roster.MayDispatchSlots(&scene.remoteA, &scene.local), true);
        EQUALS(scene.roster.MayDispatchSlots(&scene.local, &scene.remoteA), false);
        EQUALS(scene.roster.MayDispatchSlots(&scene.remoteA, &scene.remoteB), false);
        EQUALS(scene.roster.MayDispatchSlots(&scene.remoteA, &scene.npc), false);
        EQUALS(scene.roster.MayDispatchSlots(&scene.npc, &scene.remoteA), false);
        EQUALS(scene.roster.MayDispatchSlots(&scene.remoteA, nullptr), false);
        EQUALS(scene.roster.MayDispatchSlots(nullptr, nullptr), true);
    });

    IT("never weighs a lethal variant in a fight with a player in it", {
        DuelTests::Scene scene;
        EQUALS(scene.roster.MayWeighLethal(&scene.npc, &scene.npc), true);
        EQUALS(scene.roster.MayWeighLethal(&scene.local, &scene.npc), true);
        EQUALS(scene.roster.MayWeighLethal(&scene.local, &scene.remoteA), false);
        EQUALS(scene.roster.MayWeighLethal(&scene.remoteA, &scene.local), false);
        EQUALS(scene.roster.MayWeighLethal(&scene.npc, &scene.remoteB), false);
        EQUALS(scene.roster.MayWeighLethal(&scene.npc, &scene.local), false);
        EQUALS(scene.roster.MayWeighLethal(nullptr, &scene.remoteA), true);
    });

    IT("counts the local body's lock as a fight with a remote player", {
        DuelTests::Scene scene;
        EQUALS(scene.roster.InvolvesRemote(&scene.remoteA, nullptr), true);
        EQUALS(scene.roster.InvolvesRemote(&scene.npc, &scene.remoteB), true);
        EQUALS(scene.roster.InvolvesRemote(&scene.npc, &scene.npc), false);
        EQUALS(scene.roster.InvolvesRemote(&scene.local, nullptr), false);
        scene.roster.SetLocalTarget(2);
        EQUALS(scene.roster.InvolvesRemote(&scene.local, nullptr), true);
        EQUALS(scene.roster.InvolvesRemote(&scene.npc, nullptr), false);
        EQUALS(scene.roster.InvolvesRemote(nullptr, &scene.remoteA), false);
    });

    IT("lets only the local picker find a pickable remote body hostile", {
        DuelTests::Scene scene;
        EQUALS(scene.roster.IsHostileCandidate(&scene.local, &scene.remoteA, &scene.remoteABody), true);
        EQUALS(scene.roster.IsHostileCandidate(&scene.local, &scene.remoteB, &scene.remoteBBody), false);
        EQUALS(scene.roster.IsHostileCandidate(&scene.local, &scene.npc, &scene.npcBody), false);
        EQUALS(scene.roster.IsHostileCandidate(&scene.remoteB, &scene.remoteA, &scene.remoteABody), false);
        EQUALS(scene.roster.IsHostileCandidate(&scene.npc, &scene.remoteA, &scene.remoteABody), false);
        EQUALS(scene.roster.IsHostileCandidate(nullptr, &scene.remoteA, &scene.remoteABody), false);
    });

    IT("forgets everyone when cleared", {
        DuelTests::Scene scene;
        scene.roster.SetLocalTarget(3);
        scene.roster.Clear();
        EQUALS(scene.roster.IsLocal(&scene.local), false);
        EQUALS(scene.roster.IsRemote(&scene.remoteA), false);
        EQUALS(scene.roster.IsPickable(&scene.remoteABody), false);
        EQUALS(scene.roster.LocalFighter() == nullptr, true);
        EQUALS(scene.roster.LocalNetworkId(), static_cast<std::uint64_t>(0));
        EQUALS(scene.roster.LocalTargetNetworkId(), static_cast<std::uint64_t>(0));
        EQUALS(scene.roster.NetworkIdOf(&scene.remoteA), static_cast<std::uint64_t>(0));
    });

    IT("takes no network id for a local body the session has not named", {
        DuelTests::Roster roster;
        DuelTests::Fighter local;
        roster.SetLocal(&local, 0);
        EQUALS(roster.IsLocal(&local), true);
        EQUALS(roster.NetworkIdOf(&local), static_cast<std::uint64_t>(0));
    });
});

MODULE(duel_roster_ring, {
    IT("never hands out the current roster to be filled", {
        KCDC::Features::Player::RosterRing<DuelTests::Roster, 4> ring;
        for (int tick = 0; tick < 12; ++tick) {
            NEQUALS(&ring.Next(), &ring.Current());
            ring.Publish();
        }
    });

    IT("publishes the roster it was filled into", {
        KCDC::Features::Player::RosterRing<DuelTests::Roster, 4> ring;
        DuelTests::Fighter local;
        DuelTests::Roster &next = ring.Next();
        next.SetLocal(&local, 7);
        EQUALS(ring.Current().IsLocal(&local), false);
        ring.Publish();
        EQUALS(&ring.Current(), &next);
        EQUALS(ring.Current().LocalNetworkId(), static_cast<std::uint64_t>(7));
    });

    IT("keeps a published roster untouched for the whole rotation after it", {
        constexpr std::size_t kSlots = 4;
        KCDC::Features::Player::RosterRing<DuelTests::Roster, kSlots> ring;
        ring.Publish();
        const DuelTests::Roster *held = &ring.Current();
        for (std::size_t tick = 1; tick < kSlots; ++tick) {
            NEQUALS(&ring.Next(), held);
            ring.Publish();
        }
        EQUALS(&ring.Next(), held);
    });

    IT("clears every slot and starts over on a reset", {
        KCDC::Features::Player::RosterRing<DuelTests::Roster, 3> ring;
        DuelTests::Fighter local;
        for (int tick = 0; tick < 3; ++tick) {
            ring.Next().SetLocal(&local, 5);
            ring.Publish();
        }
        ring.Reset([](DuelTests::Roster &roster) {
            roster.Clear();
        });
        EQUALS(ring.Current().IsLocal(&local), false);
        NEQUALS(&ring.Next(), &ring.Current());
        for (int tick = 0; tick < 3; ++tick) {
            EQUALS(ring.Next().IsLocal(&local), false);
            ring.Publish();
        }
    });
});

MODULE(forced_attack_speed, {
    IT("hands the sender's time scale to the next timeline only", {
        KCDC::Features::Player::ForcedAttackSpeeds<DuelTests::Fighter> speeds;
        DuelTests::Fighter fighter;
        speeds.Force(&fighter, 1.25F);
        float speed = 0.0F;
        EQUALS(speeds.Take(&fighter, speed), true);
        EQUALS(speed, 1.25F);
        EQUALS(speeds.Take(&fighter, speed), false);
        EQUALS(speeds.Size(), static_cast<std::size_t>(0));
    });

    IT("owes nothing for an unknown or absurd time scale", {
        KCDC::Features::Player::ForcedAttackSpeeds<DuelTests::Fighter> speeds;
        DuelTests::Fighter fighter;
        speeds.Force(&fighter, 0.0F);
        speeds.Force(&fighter, -1.0F);
        speeds.Force(&fighter, std::numeric_limits<float>::quiet_NaN());
        speeds.Force(nullptr, 1.0F);
        float speed = 0.0F;
        EQUALS(speeds.Take(&fighter, speed), false);
        EQUALS(speeds.Size(), static_cast<std::size_t>(0));
    });

    IT("keeps the latest value per fighter and keeps fighters apart", {
        KCDC::Features::Player::ForcedAttackSpeeds<DuelTests::Fighter> speeds;
        DuelTests::Fighter first, second;
        speeds.Force(&first, 1.0F);
        speeds.Force(&first, 1.5F);
        speeds.Force(&second, 0.8F);
        float speed = 0.0F;
        EQUALS(speeds.Take(&second, speed), true);
        EQUALS(speed, 0.8F);
        EQUALS(speeds.Take(&first, speed), true);
        EQUALS(speed, 1.5F);
    });

    IT("forgets what is owed to a fighter that is gone", {
        KCDC::Features::Player::ForcedAttackSpeeds<DuelTests::Fighter> speeds;
        DuelTests::Fighter kept, gone;
        speeds.Force(&kept, 1.1F);
        speeds.Force(&gone, 1.2F);
        speeds.Retain([&kept](const DuelTests::Fighter *fighter) {
            return fighter == &kept;
        });
        EQUALS(speeds.Size(), static_cast<std::size_t>(1));
        float speed = 0.0F;
        EQUALS(speeds.Take(&gone, speed), false);
        EQUALS(speeds.Take(&kept, speed), true);
    });

    IT("forgets what a refused replay never used", {
        KCDC::Features::Player::ForcedAttackSpeeds<DuelTests::Fighter> speeds;
        DuelTests::Fighter first, second;
        speeds.Force(&first, 1.1F);
        speeds.Force(&second, 1.2F);
        speeds.Forget(&first);
        float speed = 0.0F;
        EQUALS(speeds.Take(&first, speed), false);
        speeds.Clear();
        EQUALS(speeds.Take(&second, speed), false);
    });

    IT("is consumed exactly once under concurrent timelines", {
        KCDC::Features::Player::ForcedAttackSpeeds<DuelTests::Fighter> speeds;
        std::vector<DuelTests::Fighter> fighters(16);
        std::atomic<int> taken {0};
        constexpr int kRounds = 5000;

        std::thread writer([&]() {
            for (int round = 0; round < kRounds; ++round) {
                for (DuelTests::Fighter &fighter : fighters) {
                    speeds.Force(&fighter, 1.0F + static_cast<float>(round % 7) * 0.1F);
                }
            }
        });
        std::vector<std::thread> consumers;
        for (int consumer = 0; consumer < 4; ++consumer) {
            consumers.emplace_back([&]() {
                float speed = 0.0F;
                for (int round = 0; round < kRounds; ++round) {
                    for (DuelTests::Fighter &fighter : fighters) {
                        if (speeds.Take(&fighter, speed)) {
                            taken.fetch_add(1);
                            if (!(speed >= 1.0F && speed <= 1.61F)) {
                                taken.fetch_add(1000000);
                            }
                        }
                    }
                }
            });
        }
        writer.join();
        for (std::thread &consumer : consumers) {
            consumer.join();
        }
        float speed   = 0.0F;
        int remaining = 0;
        for (DuelTests::Fighter &fighter : fighters) {
            remaining += speeds.Take(&fighter, speed) ? 1 : 0;
        }
        LESSEREQ(taken.load() + remaining, static_cast<int>(fighters.size()) * kRounds);
        LESSER(taken.load(), 1000000);
        EQUALS(speeds.Size(), static_cast<std::size_t>(0));
    });
});

MODULE(swing_generations, {
    IT("names a fighter's blows by the swing it last played", {
        KCDC::Features::Player::SwingGenerations<DuelTests::Fighter> generations;
        DuelTests::Fighter fighter;
        EQUALS(generations.Of(&fighter), static_cast<std::uint32_t>(0));
        generations.Note(&fighter, 12);
        EQUALS(generations.Of(&fighter), static_cast<std::uint32_t>(12));
        generations.Note(&fighter, 13);
        EQUALS(generations.Of(&fighter), static_cast<std::uint32_t>(13));
    });

    IT("ignores what is not a swing", {
        KCDC::Features::Player::SwingGenerations<DuelTests::Fighter> generations;
        DuelTests::Fighter fighter;
        generations.Note(&fighter, 0);
        generations.Note(nullptr, 4);
        EQUALS(generations.Size(), static_cast<std::size_t>(0));
    });

    IT("forgets the fighters that are gone", {
        KCDC::Features::Player::SwingGenerations<DuelTests::Fighter> generations;
        DuelTests::Fighter kept;
        DuelTests::Fighter gone;
        generations.Note(&kept, 1);
        generations.Note(&gone, 2);
        generations.Retain([&kept](const DuelTests::Fighter *fighter) {
            return fighter == &kept;
        });
        EQUALS(generations.Of(&kept), static_cast<std::uint32_t>(1));
        EQUALS(generations.Of(&gone), static_cast<std::uint32_t>(0));
        generations.Clear();
        EQUALS(generations.Size(), static_cast<std::size_t>(0));
    });
});

MODULE(swing_hold, {
    IT("holds a swing back by both legs of the round trip plus the relay", {
        const float margin = KCDC::Features::Player::SwingHoldSeconds(0, 0.05F);
        EQUALS(margin, 0.05F);
        EQUALS(std::fabs(KCDC::Features::Player::SwingHoldSeconds(100, 0.05F) - 0.25F) < 1e-6F, true);
        EQUALS(std::fabs(KCDC::Features::Player::SwingHoldSeconds(40, 0.0F) - 0.08F) < 1e-6F, true);
    });

    IT("grows with the round trip", { LESSER(KCDC::Features::Player::SwingHoldSeconds(50, 0.05F), KCDC::Features::Player::SwingHoldSeconds(120, 0.05F)); });

    IT("holds nothing without a round trip to base it on", { EQUALS(KCDC::Features::Player::SwingHoldSeconds(-1, 0.05F), 0.0F); });
});

MODULE(combat_target_replication, {
    IT("carries the lock a player's picker made to every copy", {
        HumanSoulTests::Channel channel;
        KCDC::Shared::Entities::HumanEntity owner;
        KCDC::Shared::Entities::HumanEntity copy;
        owner.soul = HumanSoulTests::Sample();
        EQUALS(copy.combatTargetNetworkId, static_cast<std::uint64_t>(0));

        channel.Send(owner, copy, true);
        EQUALS(copy.combatTargetNetworkId, static_cast<std::uint64_t>(0));

        owner.combatTargetNetworkId = 42;
        EQUALS(channel.Send(owner, copy), true);
        EQUALS(copy.combatTargetNetworkId, static_cast<std::uint64_t>(42));

        owner.combatTargetNetworkId = 0;
        EQUALS(channel.Send(owner, copy), true);
        EQUALS(copy.combatTargetNetworkId, static_cast<std::uint64_t>(0));
    });

    IT("sends nothing while the lock holds", {
        HumanSoulTests::Channel channel;
        KCDC::Shared::Entities::HumanEntity owner;
        KCDC::Shared::Entities::HumanEntity copy;
        owner.soul                  = HumanSoulTests::Sample();
        owner.combatTargetNetworkId = 5;
        channel.Send(owner, copy, true);
        EQUALS(copy.combatTargetNetworkId, static_cast<std::uint64_t>(5));
        EQUALS(channel.Send(owner, copy), false);
    });
});

MODULE(duel_engagement, {
    IT("holds a fight for a while past its last exchange", {
        KCDC::Features::Player::DuelEngagement engagement;
        EQUALS(engagement.Current(1000), 0ULL);
        engagement.Note(7, 1000);
        EQUALS(engagement.Current(1000), 7ULL);
        EQUALS(engagement.Current(1000 + KCDC::Features::Player::DuelEngagement::kHoldMs - 1), 7ULL);
        EQUALS(engagement.Current(1000 + KCDC::Features::Player::DuelEngagement::kHoldMs), 0ULL);
    });

    IT("is refreshed by every exchange and taken over by a new opponent", {
        KCDC::Features::Player::DuelEngagement engagement;
        engagement.Note(7, 1000);
        engagement.Note(7, 4000);
        EQUALS(engagement.Current(1000 + KCDC::Features::Player::DuelEngagement::kHoldMs + 500), 7ULL);
        engagement.Note(9, 5000);
        EQUALS(engagement.Current(5001), 9ULL);
    });

    IT("ignores nobody and ends on demand", {
        KCDC::Features::Player::DuelEngagement engagement;
        engagement.Note(0, 1000);
        EQUALS(engagement.Current(1000), 0ULL);
        engagement.Note(7, 1000);
        engagement.End();
        EQUALS(engagement.Current(1001), 0ULL);
    });

    IT("puts the engaged body on the roster and clears it with the rest", {
        DuelTests::Scene scene;
        EQUALS(scene.roster.EngagedBody() == nullptr, true);
        scene.roster.SetEngaged(&scene.remoteABody, 2);
        EQUALS(scene.roster.EngagedBody() == &scene.remoteABody, true);
        EQUALS(scene.roster.EngagedNetworkId(), 2ULL);
        scene.roster.SetEngaged(nullptr, 2);
        EQUALS(scene.roster.EngagedNetworkId(), 0ULL);
        scene.roster.SetEngaged(&scene.remoteABody, 2);
        scene.roster.Clear();
        EQUALS(scene.roster.EngagedBody() == nullptr, true);
    });
});

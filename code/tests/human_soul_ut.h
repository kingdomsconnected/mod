#pragma once

#include <utils/safe_win32.h>

#include "sdk/wh/rpgmodule/c_soul.h"
#include "shared/features/player/player_entity.h"

#include <cmath>
#include <limits>

namespace HumanSoulTests {
    struct Channel {
        MafiaNet::VariableDeltaSerializer serializer;

        bool Send(KCDC::Shared::Entities::HumanEntity &source, KCDC::Shared::Entities::HumanEntity &destination, bool first = false) {
            MafiaNet::BitStream stream;
            MafiaNet::VariableDeltaSerializer::SerializationContext writeContext;
            serializer.OnPreSerializeTick();
            serializer.BeginIdenticalSerialize(&writeContext, first, &stream);
            Framework::Networking::Replication::FieldSerializer writer(&serializer, &writeContext, true);
            source.SerializeFields(writer);
            const bool changed = writeContext.anyVariablesWritten;
            serializer.EndSerialize(&writeContext);
            if (changed) {
                MafiaNet::VariableDeltaSerializer::DeserializationContext readContext;
                serializer.BeginDeserialize(&readContext, &stream);
                Framework::Networking::Replication::FieldSerializer reader(&serializer, &readContext, true);
                destination.SerializeFields(reader);
                serializer.EndDeserialize(&readContext);
            }
            return changed;
        }
    };
} // namespace HumanSoulTests

namespace HumanSoulTests {
    inline KCDC::Shared::Game::HumanSoulState Sample() {
        KCDC::Shared::Game::HumanSoulState state;
        state.valid          = true;
        state.incarnation    = 1;
        state.revision       = 1;
        state.physicsProfile = 1;
        state.resources      = {{{73.0F, 100.0F}, {30.0F, 120.0F}, {50.0F, 100.0F}, {60.0F, 100.0F}}};
        state.healthyStamina = 150.0F;
        state.conditions[2]  = 1.0F;
        state.stats          = {12.0F, 16.0F, 18.0F};
        state.relativeStats  = {0.4F, 0.5F, 0.6F};
        state.skills[1]      = 22.0F;
        return state;
    }
} // namespace HumanSoulTests

MODULE(human_soul_replication, {

    IT("preserves native units, tiny positive stamina, and legitimate death capacity", {
        auto state = HumanSoulTests::Sample();
        EQUALS(state.IsSane(), true);
        EQUALS(state.HealthPercent(), 73.0F);
        EQUALS(state.CanAct(), true);
        state.resources[1].current = 0.000001F;
        EQUALS(state.IsSane(), true);
        GREATER(state.resources[1].current, 0.0F);
        state.resources[0].current = 0.0F;
        state.resources[1].maximum = 0.0F;
        EQUALS(state.IsSane(), true);
        EQUALS(state.CanAct(), false);
        EQUALS(state.HealthPercent(), 0.0F);
    });

    IT("rejects nonfinite state and keeps conditions distinct from readiness", {
        auto state          = HumanSoulTests::Sample();
        state.conditions[2] = 0.0F;
        EQUALS(state.CanAct(), false);
        state.conditions[2] = 1.0F;
        state.conditions[1] = 0.001F;
        EQUALS(state.CanAct(), false);
        state.conditions[1] = 0.0F;
        state.valid         = false;
        EQUALS(state.CanAct(), false);
        for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.0F}) {
            state                      = HumanSoulTests::Sample();
            state.resources[1].maximum = bad;
            EQUALS(state.IsSane(), false);
            state               = HumanSoulTests::Sample();
            state.conditions[4] = bad;
            EQUALS(state.IsSane(), false);
        }
        state                = HumanSoulTests::Sample();
        state.physicsProfile = 7;
        EQUALS(state.IsSane(), false);
    });

    IT("sends only the stat readings that moved, without advancing the soul", {
        KCDC::Shared::Entities::HumanEntity owner, server, observer, lateJoiner;
        HumanSoulTests::Channel upstream, downstream;
        owner.soul = HumanSoulTests::Sample();
        for (std::size_t i = 0; i < owner.statReadings.values.size(); ++i) {
            owner.statReadings.values[i] = static_cast<float>(i + 1);
        }
        owner.statReadings.values.back() = -2.5F;
        EQUALS(owner.statReadings.IsSane(), true);
        EQUALS(upstream.Send(owner, server, true), true);
        EQUALS(downstream.Send(server, observer, true), true);
        EQUALS(observer.statReadings == owner.statReadings, true);
        const auto revision              = owner.soul.revision;
        owner.statReadings.values.back() = -5.5F;
        EQUALS(upstream.Send(owner, server), true);
        EQUALS(downstream.Send(server, observer), true);
        EQUALS(observer.statReadings == owner.statReadings, true);
        EQUALS(observer.soul.revision, revision);
        HumanSoulTests::Channel construction;
        EQUALS(construction.Send(server, lateJoiner, true), true);
        EQUALS(lateJoiner.statReadings == owner.statReadings, true);
        EQUALS(upstream.Send(owner, server), false);
        owner.statReadings.values.front() = KCDC::Shared::Game::PlayerStatReadings::Quantize(1.001F);
        EQUALS(upstream.Send(owner, server), false);
        for (float bad : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 1000001.0F, -1000001.0F}) {
            const auto accepted              = server.statReadings;
            owner.statReadings.values.back() = bad;
            EQUALS(owner.statReadings.IsSane(), false);
            upstream.Send(owner, server);
            EQUALS(server.statReadings == accepted, true);
        }
    });

    IT("carries a body with no reportable physics profile instead of dropping it", {
        // CryEngine answers 255 for an aspect whose profile manager is not
        // there yet. That is a body with no profile to replicate, not a
        // snapshot worth throwing the health and stamina out of.
        auto state = HumanSoulTests::Sample();
        EQUALS(state.HasPhysicsProfile(), true);
        state.physicsProfile = KCDC::Shared::Game::HumanSoulState::kUnknownPhysicsProfile;
        EQUALS(state.HasPhysicsProfile(), false);
        EQUALS(state.IsSane(), true);
        EQUALS(state.CanAct(), true);
        EQUALS(state.HealthPercent(), 73.0F);
        for (std::uint8_t profile = 0; profile <= KCDC::Shared::Game::HumanSoulState::kMaxPhysicsProfile; ++profile) {
            state.physicsProfile = profile;
            EQUALS(state.IsSane(), true);
            EQUALS(state.HasPhysicsProfile(), true);
        }
        state.physicsProfile = KCDC::Shared::Game::HumanSoulState::kMaxPhysicsProfile + 1;
        EQUALS(state.IsSane(), false);
        EQUALS(KCDC::Shared::Game::HumanSoulState().HasPhysicsProfile(), false);
    });

    IT("advances counters past the zero reserved for never-set", {
        EQUALS(KCDC::Shared::Game::HumanSoulState::Advance(0), 1U);
        EQUALS(KCDC::Shared::Game::HumanSoulState::Advance(1), 2U);
        EQUALS(KCDC::Shared::Game::HumanSoulState::Advance(UINT32_MAX), 1U);
        EQUALS(KCDC::Shared::Game::HumanSoulState::IsNewer(KCDC::Shared::Game::HumanSoulState::Advance(UINT32_MAX), UINT32_MAX), true);
        EQUALS(KCDC::Shared::Entities::HumanEntity::AdvanceCombatRevision(0), 1U);
        EQUALS(KCDC::Shared::Entities::HumanEntity::AdvanceCombatRevision(1), 2U);
        EQUALS(KCDC::Shared::Entities::HumanEntity::AdvanceCombatRevision(UINT32_MAX), 1U);
        EQUALS(KCDC::Shared::Entities::HumanEntity::IsCombatRevisionNewer(KCDC::Shared::Entities::HumanEntity::AdvanceCombatRevision(UINT32_MAX), UINT32_MAX), true);
    });

    IT("relays stationary soul deltas and gives late joiners the whole profile", {
        KCDC::Shared::Entities::HumanEntity owner, server, observer, lateJoiner;
        HumanSoulTests::Channel upstream, downstream;
        owner.soul           = HumanSoulTests::Sample();
        owner.guardRequest   = 1;
        owner.combatRevision = 17;
        EQUALS(upstream.Send(owner, server, true), true);
        EQUALS(downstream.Send(server, observer, true), true);
        EQUALS(observer.soul == owner.soul, true);
        EQUALS(observer.guardRequest, 1);
        EQUALS(observer.combatRevision, 17U);
        EQUALS(upstream.Send(owner, server), false);
        EQUALS(downstream.Send(server, observer), false);
        owner.combatRevision = 18;
        EQUALS(upstream.Send(owner, server), true);
        EQUALS(downstream.Send(server, observer), true);
        EQUALS(observer.combatRevision, 18U);
        for (float current : {0.0F, 0.000001F, 120.0F}) {
            owner.soul.resources[1].current = current;
            ++owner.soul.revision;
            EQUALS(upstream.Send(owner, server), true);
            EQUALS(downstream.Send(server, observer), true);
            EQUALS(observer.soul == owner.soul, true);
        }
        EQUALS(downstream.Send(server, lateJoiner, true), true);
        EQUALS(lateJoiner.soul == owner.soul, true);
        EQUALS(lateJoiner.combatRevision, 18U);
        owner.soul.valid = false;
        ++owner.soul.revision;
        upstream.Send(owner, server);
        downstream.Send(server, observer);
        EQUALS(observer.soul.valid, false);
    });

    IT("quarantines a malformed delta until a fresh construction", {
        KCDC::Shared::Entities::HumanEntity owner, receiver;
        HumanSoulTests::Channel channel;
        owner.soul = HumanSoulTests::Sample();
        channel.Send(owner, receiver, true);
        owner.soul.resources[0].current = 50.0F;
        owner.soul.resources[1].maximum = std::numeric_limits<float>::quiet_NaN();
        channel.Send(owner, receiver);
        EQUALS(receiver.soul.valid, false);
        EQUALS(receiver.soul.resources[0].current, 73.0F);
        owner.soul = HumanSoulTests::Sample();
        channel.Send(owner, receiver);
        EQUALS(receiver.soul.valid, false);
        MafiaNet::BitStream construction;
        Framework::Networking::Replication::FieldSerializer writer(&construction, true);
        owner.OnSerializeConstruction(writer);
        Framework::Networking::Replication::FieldSerializer reader(&construction, false);
        receiver.OnSerializeConstruction(reader);
        channel.Send(owner, receiver, true);
        EQUALS(receiver.soul == owner.soul, true);
    });

    IT("withdraws a quarantined stream as a step its own observers can follow", {
        // A relay that rejects its owner republishes what it holds. If the
        // withdrawal did not carry a revision of its own it would reach every
        // observer as a state that follows nothing, quarantining all of them
        // for a stream that was only ever bad upstream of the relay.
        KCDC::Shared::Entities::HumanEntity owner, server, observer;
        HumanSoulTests::Channel upstream, downstream;
        owner.soul = HumanSoulTests::Sample();
        EQUALS(upstream.Send(owner, server, true), true);
        EQUALS(downstream.Send(server, observer, true), true);
        EQUALS(observer.soul == owner.soul, true);

        owner.soul.resources[1].maximum = std::numeric_limits<float>::quiet_NaN();
        owner.soul.revision             = 2;
        upstream.Send(owner, server);
        EQUALS(server.soul.valid, false);
        GREATER(server.soul.revision, 1U);

        downstream.Send(server, observer);
        EQUALS(observer.soul.valid, false);

        // The relay is quarantined; the observer is not, so the state a
        // reconstructed relay publishes still lands.
        server.soul          = HumanSoulTests::Sample();
        server.soul.revision = 9;
        downstream.Send(server, observer);
        EQUALS(observer.soul.valid, true);
        EQUALS(observer.soul.revision, 9U);
    });

    IT("rejects every truncated plain soul payload atomically", {
        KCDC::Shared::Entities::HumanEntity source;
        source.soul = HumanSoulTests::Sample();
        MafiaNet::BitStream payload;
        Framework::Networking::Replication::FieldSerializer writer(&payload, true);
        source.soul.SerializeFields(writer);
        for (unsigned bytes = 0; bytes < payload.GetNumberOfBytesUsed(); ++bytes) {
            MafiaNet::BitStream truncated(payload.GetData(), bytes, true);
            Framework::Networking::Replication::FieldSerializer reader(&truncated, false);
            KCDC::Shared::Entities::HumanEntity receiver;
            receiver.soul = HumanSoulTests::Sample();
            receiver.SerializeFields(reader);
            EQUALS(reader.Good(), false);
            EQUALS(receiver.soul.valid, false);
            EQUALS(receiver.soul.resources[0].current, 73.0F);
        }
    });

    IT("distinguishes an unchanged delta bit from a truncated delta", {
        MafiaNet::VariableDeltaSerializer serializer;
        MafiaNet::VariableDeltaSerializer::DeserializationContext context;
        MafiaNet::BitStream payload;
        payload.Write(false);
        serializer.BeginDeserialize(&context, &payload);
        Framework::Networking::Replication::FieldSerializer reader(&serializer, &context, true);
        float value = 12.0F;
        reader.Field(value);
        EQUALS(reader.Good(), true);
        EQUALS(value, 12.0F);
        // Padding bits can be read; exhaust the final byte, then require data.
        payload.SetReadOffset(payload.GetNumberOfBitsUsed());
        reader.Field(value);
        EQUALS(reader.Good(), false);
        serializer.EndDeserialize(&context);
    });

    IT("captures bounded action queries without replacing victim inputs", {
        KCDC::Shared::Game::HumanSoulAction action;
        action.incarnation = action.revision = action.targetIncarnation = 1;
        action.complete                                                 = true;
        KCDC::Shared::Game::HumanSoulQuery query;
        query.kind       = KCDC::Shared::Game::HumanSoulQueryKind::Modifier;
        query.selector   = 2;
        query.base       = 50.0F;
        query.result     = 75.0F;
        query.hasContext = true;
        query.reason     = 5;
        query.references = {2, 0, 3, 0};
        EQUALS(action.Add(query), true);
        EQUALS(action.Add(query), true);
        EQUALS(action.count, 1);
        EQUALS(action.IsSane(), true);
        float result = 0.0F;
        EQUALS(action.Find(query, result) == KCDC::Shared::Game::HumanSoulAction::Lookup::Recorded, true);
        EQUALS(result, 75.0F);
        // A transformed answer cannot be moved to an input we did not record.
        query.base = 60.0F;
        EQUALS(action.Find(query, result) == KCDC::Shared::Game::HumanSoulAction::Lookup::Rebased, true);
        // Nor can a question the attacker never answered be invented.
        query.selector = 3;
        EQUALS(action.Find(query, result) == KCDC::Shared::Game::HumanSoulAction::Lookup::Missing, true);
        query.selector = 2;
        query.base     = 50.0F;
        query.result   = 76.0F;
        EQUALS(action.Add(query), false);
        MafiaNet::BitStream stream;
        EQUALS(action.Serialize(&stream, true), true);
        KCDC::Shared::Game::HumanSoulAction received;
        EQUALS(received.Serialize(&stream, false), true);
        EQUALS(received.IsSane(), true);
        EQUALS(received.Find(query, result) == KCDC::Shared::Game::HumanSoulAction::Lookup::Recorded, true);
        EQUALS(result, 75.0F);
        received.queries[0].references[0] = 6;
        EQUALS(received.IsSane(), false);
        for (std::uint32_t i = 1; i < KCDC::Shared::Game::HumanSoulAction::kMaxQueries; ++i) {
            query.selector = i + 2;
            EQUALS(action.Add(query), true);
        }
        query.selector = 100;
        EQUALS(action.Add(query), false);
    });

    IT("accepts a calculation that needed nothing from the attacker soul", {
        KCDC::Shared::Game::HumanSoulAction action;
        action.incarnation = action.revision = action.targetIncarnation = 1;
        action.complete                                                 = true;
        EQUALS(action.count, 0);
        EQUALS(action.IsSane(), true);
        float result = 0.0F;
        EQUALS(action.Find(KCDC::Shared::Game::HumanSoulQuery {}, result) == KCDC::Shared::Game::HumanSoulAction::Lookup::Missing, true);
        // Incomplete capture and an unfenced body stay rejections.
        action.complete = false;
        EQUALS(action.IsSane(), false);
        action.complete          = true;
        action.targetIncarnation = 0;
        EQUALS(action.IsSane(), false);
    });
    IT("fences revisions and delayed poses across body replacement", {
        auto previous             = HumanSoulTests::Sample();
        auto next                 = previous;
        next.resources[0].current = 20.0F;
        EQUALS(next.Follows(previous), false);
        ++next.revision;
        EQUALS(next.Follows(previous), true);
        ++next.incarnation;
        next.revision = 1;
        EQUALS(next.Follows(previous), true);
        EQUALS(previous.Follows(next), false);
        EQUALS(KCDC::Shared::Game::HumanSoulState::IsNewer(1, UINT32_MAX), true);
        KCDC::Shared::Entities::HumanEntity oldBody, newBody;
        oldBody.soul            = previous;
        oldBody.poseIncarnation = previous.incarnation;
        oldBody.transformReady  = true;
        oldBody.position        = glm::vec3(100.0F);
        newBody.soul            = next;
        newBody.poseIncarnation = next.incarnation;
        newBody.transformReady  = true;
        newBody.position        = glm::vec3(20.0F);
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        oldBody.SerializeTransform(writer);
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        newBody.SerializeTransform(reader);
        EQUALS(newBody.poseIncarnation, next.incarnation);
        EQUALS(newBody.position.x, 20.0F);
    });
    IT("round trips a pose as yaw, look angles and climb input", {
        auto state = HumanSoulTests::Sample();
        KCDC::Shared::Entities::HumanEntity sender, receiver;
        sender.soul              = state;
        receiver.soul            = state;
        sender.poseIncarnation   = state.incarnation;
        sender.transformReady    = true;
        sender.position          = glm::vec3(1234.567F, -876.5F, 151.25F);
        sender.rotation          = KCDC::Shared::Entities::HumanEntity::RotationForYaw(2.5F);
        sender.headLookReady     = true;
        sender.headLookDirection = glm::normalize(glm::vec3(0.3F, -0.8F, 0.4F));
        sender.ladderClimbInput  = -2.0F;
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        sender.SerializeTransform(writer);
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        receiver.SerializeTransform(reader);
        EQUALS(reader.Good(), true);
        EQUALS(receiver.poseIncarnation, state.incarnation);
        EQUALS(glm::distance(receiver.position, sender.position) < 0.01F, true);
        EQUALS(std::fabs(KCDC::Shared::Entities::HumanEntity::YawOf(receiver.rotation) - 2.5F) < 0.001F, true);
        EQUALS(glm::dot(receiver.headLookDirection, sender.headLookDirection) > 0.9999F, true);
        EQUALS(std::fabs(receiver.ladderClimbInput + 2.0F) < 0.05F, true);
        EQUALS(stream.GetNumberOfBitsUsed() <= 150, true);
    });
    IT("sends a hit or a run-dry at once and holds drift for the resample", {
        const auto previous = HumanSoulTests::Sample();
        auto drift          = previous;
        drift.resources[KCDC::Shared::Game::HumanSoulState::kStamina].current -= 1.0F;
        EQUALS(drift.IsEdgeFrom(previous), false);
        auto hit = previous;
        hit.resources[KCDC::Shared::Game::HumanSoulState::kHealth].current -= 1.0F;
        EQUALS(hit.IsEdgeFrom(previous), true);
        auto empty = previous;
        empty.resources[KCDC::Shared::Game::HumanSoulState::kStamina].current = 0.0F;
        EQUALS(empty.IsEdgeFrom(previous), previous.resources[KCDC::Shared::Game::HumanSoulState::kStamina].current > 0.0F);
        auto replaced = previous;
        ++replaced.incarnation;
        EQUALS(replaced.IsEdgeFrom(previous), true);
    });

    IT("carries the movement input beside the pose, and drops it with a stale one", {
        const auto soul = HumanSoulTests::Sample();
        KCDC::Shared::Entities::HumanEntity owner, receiver;
        owner.soul            = soul;
        owner.poseIncarnation = soul.incarnation;
        owner.transformReady  = true;
        owner.position        = glm::vec3(1.0F, 2.0F, 3.0F);
        owner.velocity        = glm::vec3(4.0F, 0.0F, 0.0F);
        owner.input.moveX     = -64;
        owner.input.moveY     = 127;
        owner.input.run       = false;
        owner.input.sprint    = true;
        receiver.soul         = soul;
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        owner.SerializeTransform(writer);
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        receiver.SerializeTransform(reader);
        EQUALS(receiver.input.moveX, -64);
        EQUALS(receiver.input.moveY, 127);
        EQUALS(receiver.input.run, false);
        EQUALS(receiver.input.sprint, true);
        EQUALS(std::fabs(receiver.velocity.x - 4.0F) < 0.01F, true);

        // A pose of the body before this one keeps what the receiver had.
        KCDC::Shared::Entities::HumanEntity stale;
        stale.soul            = soul;
        stale.poseIncarnation = soul.incarnation - 1;
        stale.transformReady  = true;
        stale.input.moveY     = 1;
        MafiaNet::BitStream staleStream;
        Framework::Networking::Replication::FieldSerializer staleWriter(&staleStream, true);
        stale.SerializeTransform(staleWriter);
        Framework::Networking::Replication::FieldSerializer staleReader(&staleStream, false);
        receiver.SerializeTransform(staleReader);
        EQUALS(receiver.input.moveY, 127);
    });

    IT("sends the facing as a yaw, rest as rest, and the incarnation as its low bits", {
        auto soul        = HumanSoulTests::Sample();
        soul.incarnation = 37;
        KCDC::Shared::Entities::HumanEntity owner, receiver;
        owner.soul              = soul;
        owner.poseIncarnation   = soul.incarnation;
        owner.transformReady    = true;
        owner.position          = glm::vec3(1.0F, 2.0F, 3.0F);
        owner.velocity          = glm::vec3(0.0F);
        owner.rotation          = glm::angleAxis(1.0F, glm::vec3(0.0F, 0.0F, 1.0F));
        owner.headLookReady     = true;
        owner.headLookDirection = glm::normalize(glm::vec3(-0.5F, 0.8F, 0.3F));
        owner.input.jumps       = 13;
        receiver.soul           = soul;
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        owner.SerializeTransform(writer);
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        receiver.SerializeTransform(reader);
        EQUALS(receiver.poseIncarnation, 37U);
        EQUALS(receiver.velocity == glm::vec3(0.0F), true);
        EQUALS(std::fabs(glm::dot(receiver.rotation, owner.rotation)) > 0.99999F, true);
        EQUALS(glm::dot(receiver.headLookDirection, owner.headLookDirection) > 0.9999F, true);
        EQUALS(receiver.input.jumps, (13 & KCDC::Shared::Entities::PlayerInput::kCountMask));
        EQUALS(receiver.ladderClimbInput, 0.0F);
        LESSER(glm::length(receiver.position - owner.position), 0.002F);
        LESSER(stream.GetNumberOfBitsUsed(), 150U);

        // A fresh replica has no soul to widen against: construction carries
        // the whole incarnation after the pose.
        KCDC::Shared::Entities::HumanEntity fresh;
        MafiaNet::BitStream seed;
        Framework::Networking::Replication::FieldSerializer seedWriter(&seed, true);
        owner.SerializeTransform(seedWriter);
        owner.OnSerializeConstruction(seedWriter);
        Framework::Networking::Replication::FieldSerializer seedReader(&seed, false);
        fresh.SerializeTransform(seedReader);
        EQUALS(fresh.transformReady, true);
        fresh.OnSerializeConstruction(seedReader);
        EQUALS(fresh.poseIncarnation, 37U);
    });

    IT("round trips an impact with its action snapshot and rejects truncations", {
        KCDC::Shared::RPC::MissileImpact impact;
        impact.launch.sequence              = 1;
        impact.launch.incarnation           = 1;
        impact.launch.weapon                = {1, 2};
        impact.launch.ammunition            = {3, 4};
        impact.launch.velocity              = {0.0F, 10.0F, 0.0F};
        impact.targetNetworkId              = 2;
        impact.soulAction.incarnation       = 1;
        impact.soulAction.targetIncarnation = 2;
        impact.soulAction.revision          = 42;
        impact.soulAction.resources[1]      = 0.000001F;
        impact.soulAction.complete          = true;
        KCDC::Shared::Game::HumanSoulQuery query;
        query.kind     = KCDC::Shared::Game::HumanSoulQueryKind::State;
        query.selector = 1;
        query.result   = 0.000001F;
        impact.soulAction.Add(query);
        EQUALS(impact.IsSane(), true);
        MafiaNet::BitStream stream;
        impact.Serialize(&stream, true);
        KCDC::Shared::RPC::MissileImpact received;
        received.Serialize(&stream, false);
        EQUALS(received.IsSane(), true);
        EQUALS(received.soulAction.revision, 42U);
        EQUALS(received.soulAction.resources[1], 0.000001F);
        for (unsigned bytes = 0; bytes < stream.GetNumberOfBytesUsed(); ++bytes) {
            MafiaNet::BitStream truncated(stream.GetData(), bytes, true);
            KCDC::Shared::RPC::MissileImpact bad;
            bad.Serialize(&truncated, false);
            EQUALS(bad.IsSane(), false);
        }
        received.soulAction.incarnation = 3;
        EQUALS(received.IsSane(), false);
    });
});

#pragma once

#include "shared/features/npc/npc_entity.h"

#include <mafianet/bit_stream.h>
#include <mafianet/variable_delta_serializer.h>

namespace NpcReplicationTests {
    struct Channel {
        MafiaNet::VariableDeltaSerializer serializer;

        bool Send(KCDC::Shared::Entities::NpcEntity &source, KCDC::Shared::Entities::NpcEntity &destination, bool first = false) {
            MafiaNet::BitStream stream;
            MafiaNet::VariableDeltaSerializer::SerializationContext writeContext;
            serializer.OnPreSerializeTick();
            serializer.BeginIdenticalSerialize(&writeContext, first, &stream);
            Framework::Networking::Replication::FieldSerializer writer(&serializer, &writeContext, false);
            source.SerializeFields(writer);
            const bool changed = writeContext.anyVariablesWritten;
            serializer.EndSerialize(&writeContext);
            if (changed) {
                MafiaNet::VariableDeltaSerializer::DeserializationContext readContext;
                serializer.BeginDeserialize(&readContext, &stream);
                Framework::Networking::Replication::FieldSerializer reader(&serializer, &readContext, false);
                destination.SerializeFields(reader);
                serializer.EndDeserialize(&readContext);
            }
            return changed;
        }
    };

    inline void Force(KCDC::Shared::Entities::NpcEntity &server, KCDC::Shared::Entities::NpcEntity &simulator) {
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        server.SerializeForcedState(writer);
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        simulator.SerializeForcedState(reader);
    }
} // namespace NpcReplicationTests

MODULE(npc_replication, {
    IT("delivers immediate arrivals after hold, move and follow orders", {
        NpcReplicationTests::Channel channel;
        KCDC::Shared::Entities::NpcEntity simulator, server;
        simulator.intentStatus   = KCDC::Shared::Game::NpcIntentStatus::Reached;
        simulator.statusRevision = simulator.intentRevision;
        EQUALS(channel.Send(simulator, server, true), true);

        // A nearby guard reaches each new order without an intervening
        // Running report. The status repeats, but its revision changes.
        for (const auto kind : {KCDC::Shared::Game::NpcIntentKind::Hold, KCDC::Shared::Game::NpcIntentKind::MoveTo, KCDC::Shared::Game::NpcIntentKind::Follow}) {
            server.intent.kind    = kind;
            server.intentRevision = KCDC::Shared::Entities::NpcEntity::AdvanceRevision(server.intentRevision);
            NpcReplicationTests::Force(server, simulator);
            simulator.statusRevision = simulator.intentRevision;
            EQUALS(channel.Send(simulator, server), true);
            EQUALS(server.intentStatus == KCDC::Shared::Game::NpcIntentStatus::Reached, true);
            EQUALS(server.statusRevision, server.intentRevision);
        }
    });

    IT("leaves the simulator's report alone at a handover and relays the next simulator's", {
        NpcReplicationTests::Channel channel;
        KCDC::Shared::Entities::NpcEntity simulator, server;
        simulator.intentStatus   = KCDC::Shared::Game::NpcIntentStatus::Reached;
        simulator.statusRevision = simulator.intentRevision;
        EQUALS(channel.Send(simulator, server, true), true);
        const std::uint32_t before = server.intentRevision;
        server.busy                = true;
        server.OnSimulatorChanged(static_cast<MafiaNet::PeerGuid>(1), static_cast<MafiaNet::PeerGuid>(2));

        // The server bumps the revision so the old report no longer matches,
        // and writes nothing into the report itself: a finished order's
        // outcome lives on the server's side, and is not re-announced (see
        // the NPC order tests).
        EQUALS(server.intentRevision, KCDC::Shared::Entities::NpcEntity::AdvanceRevision(before));
        EQUALS(server.intentStatus == KCDC::Shared::Game::NpcIntentStatus::Reached, true);
        EQUALS(server.statusRevision, before);
        EQUALS(server.busy, false);

        NpcReplicationTests::Force(server, simulator);
        simulator.statusRevision = simulator.intentRevision;
        EQUALS(channel.Send(simulator, server), true);
        EQUALS(server.statusRevision, server.intentRevision);
    });

    IT("delivers repeated blocked and failed results for new revisions", {
        for (const auto status : {KCDC::Shared::Game::NpcIntentStatus::Blocked, KCDC::Shared::Game::NpcIntentStatus::Failed}) {
            NpcReplicationTests::Channel channel;
            KCDC::Shared::Entities::NpcEntity simulator, server;
            simulator.intentStatus   = status;
            simulator.statusRevision = 7;
            EQUALS(channel.Send(simulator, server, true), true);
            simulator.statusRevision = 8;
            EQUALS(channel.Send(simulator, server), true);
            EQUALS(server.intentStatus == status, true);
            EQUALS(server.statusRevision, 8U);
        }
    });

    IT("carries how much route lies beyond a corner to the simulator", {
        KCDC::Shared::Entities::NpcEntity server, simulator;
        server.intent.kind       = KCDC::Shared::Game::NpcIntentKind::MoveTo;
        server.intent.pathBeyond = 23.5F;
        NpcReplicationTests::Force(server, simulator);
        EQUALS(simulator.intent.pathBeyond, 23.5F);
        EQUALS(server.intent.IsSane(), true);
        server.intent.pathBeyond = -1.0F;
        EQUALS(server.intent.IsSane(), false);
    });

    IT("tells a simulator a teleport from an intent revision by the teleport sequence alone", {
        KCDC::Shared::Entities::NpcEntity server, simulator;
        // An intent revision forces the state, transform and all, but leaves the sequence where it was.
        server.intentRevision = KCDC::Shared::Entities::NpcEntity::AdvanceRevision(server.intentRevision);
        NpcReplicationTests::Force(server, simulator);
        EQUALS(simulator.intentRevision, server.intentRevision);
        EQUALS(simulator.teleportSequence, 0U);
        // A teleport is the one write that moves it, and the forced state carries it.
        server.teleportSequence = KCDC::Shared::Entities::NpcEntity::AdvanceRevision(server.teleportSequence);
        NpcReplicationTests::Force(server, simulator);
        EQUALS(simulator.teleportSequence, 1U);
        // Observers read it off the field channel too, so a later simulator does not replay it.
        NpcReplicationTests::Channel channel;
        KCDC::Shared::Entities::NpcEntity observer;
        EQUALS(channel.Send(server, observer, true), true);
        EQUALS(observer.teleportSequence, 1U);
    });
    IT("carries bow orders and delegated shooting phases across the field channel", {
        KCDC::Shared::Entities::NpcEntity server, simulator, observer;
        server.intent.kind            = KCDC::Shared::Game::NpcIntentKind::Attack;
        server.intent.targetNetworkId = 42;
        server.intent.bow             = true;
        server.intent.radius          = 18.0F;
        NpcReplicationTests::Force(server, simulator);
        EQUALS(simulator.intent.bow, true);
        EQUALS(simulator.intent.radius, 18.0F);
        NpcReplicationTests::Channel channel;
        simulator.missile.phase = KCDC::Shared::RPC::MissilePhase::Charging;
        EQUALS(channel.Send(simulator, observer, true), true);
        EQUALS(observer.intent.bow, true);
        EQUALS(observer.missile.phase == KCDC::Shared::RPC::MissilePhase::Charging, true);
        simulator.missile.phase = KCDC::Shared::RPC::MissilePhase::Idle;
        EQUALS(channel.Send(simulator, observer), true);
        EQUALS(observer.missile.phase == KCDC::Shared::RPC::MissilePhase::Idle, true);
    });

    IT("carries a revive's new life to the simulator and to every observer", {
        // Every combat report about an NPC is fenced on its life, so a client
        // left on the old one has each blow by or on the revived body refused.
        KCDC::Shared::Entities::NpcEntity server, simulator, observer;
        NpcReplicationTests::Channel channel;
        EQUALS(channel.Send(server, observer, true), true);
        server.spawnEpoch = KCDC::Shared::Entities::NpcEntity::AdvanceRevision(server.spawnEpoch);
        NpcReplicationTests::Force(server, simulator);
        EQUALS(simulator.spawnEpoch, server.spawnEpoch);
        EQUALS(channel.Send(server, observer), true);
        EQUALS(observer.spawnEpoch, server.spawnEpoch);
    });
});

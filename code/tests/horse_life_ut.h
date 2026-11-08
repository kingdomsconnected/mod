#pragma once

#include "shared/features/horse/horse_entity.h"

#include <mafianet/bit_stream.h>
#include <mafianet/variable_delta_serializer.h>

namespace HorseLifeTests {
    /** One horse's state fields, written by one peer and read by another, as the delta channel does. */
    inline void Send(KCDC::Shared::Entities::HorseEntity &source, KCDC::Shared::Entities::HorseEntity &destination, bool serverAuthored) {
        MafiaNet::VariableDeltaSerializer serializer;
        MafiaNet::BitStream stream;
        MafiaNet::VariableDeltaSerializer::SerializationContext writeContext;
        serializer.OnPreSerializeTick();
        serializer.BeginIdenticalSerialize(&writeContext, true, &stream);
        Framework::Networking::Replication::FieldSerializer writer(&serializer, &writeContext, serverAuthored);
        source.SerializeFields(writer);
        serializer.EndSerialize(&writeContext);

        MafiaNet::VariableDeltaSerializer::DeserializationContext readContext;
        serializer.BeginDeserialize(&readContext, &stream);
        Framework::Networking::Replication::FieldSerializer reader(&serializer, &readContext, serverAuthored);
        destination.SerializeFields(reader);
        serializer.EndDeserialize(&readContext);
    }
} // namespace HorseLifeTests

MODULE(horse_life, {
    IT("accepts navigation orders only from the server while accepting simulator progress", {
        KCDC::Shared::Entities::HorseEntity simulator;
        simulator.navigationIntent.kind    = KCDC::Shared::Game::NpcIntentKind::MoveTo;
        simulator.navigationIntent.point   = glm::vec3(999.0F);
        simulator.navigationRevision       = 99;
        simulator.seatedInstantly          = true;
        simulator.dismountSide             = 2;
        simulator.navigationStatus         = KCDC::Shared::Game::NpcIntentStatus::Reached;
        simulator.navigationStatusRevision = 7;
        KCDC::Shared::Entities::HorseEntity server;
        server.navigationIntent.point = glm::vec3(1.0F);
        server.navigationRevision     = 7;
        HorseLifeTests::Send(simulator, server, false);
        EQUALS(server.navigationRevision, 7U);
        EQUALS(server.seatedInstantly, false);
        EQUALS(server.dismountSide, 0);
        EQUALS(server.navigationIntent.point.x, 1.0F);
        EQUALS(static_cast<int>(server.navigationIntent.kind), static_cast<int>(KCDC::Shared::Game::NpcIntentKind::Hold));
        EQUALS(server.navigationStatusRevision, 7U);
        EQUALS(static_cast<int>(server.navigationStatus), static_cast<int>(KCDC::Shared::Game::NpcIntentStatus::Reached));
    });

    IT("carries the horse route through owner handover and observer fields", {
        KCDC::Shared::Entities::HorseEntity server;
        server.navigationIntent.kind       = KCDC::Shared::Game::NpcIntentKind::MoveTo;
        server.navigationIntent.point      = glm::vec3(4.0F, 5.0F, 6.0F);
        server.navigationIntent.pathBeyond = 12.0F;
        server.navigationRevision          = 8;
        server.seatedInstantly             = true;
        server.dismountSide                = 2;
        server.jumpSequence                = 17;
        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        server.SerializeForcedState(writer);
        KCDC::Shared::Entities::HorseEntity simulator;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        simulator.SerializeForcedState(reader);
        EQUALS(simulator.navigationRevision, 8U);
        EQUALS(simulator.seatedInstantly, true);
        EQUALS(simulator.dismountSide, 2);
        EQUALS(simulator.navigationIntent.point.y, 5.0F);
        EQUALS(simulator.navigationIntent.pathBeyond, 12.0F);
        KCDC::Shared::Entities::HorseEntity observer;
        HorseLifeTests::Send(server, observer, true);
        EQUALS(observer.navigationRevision, 8U);
        EQUALS(observer.seatedInstantly, true);
        EQUALS(observer.dismountSide, 2);
        EQUALS(observer.jumpSequence, 17);
        EQUALS(observer.navigationIntent.point.z, 6.0F);
    });

    // The simulator held `alive` from a forced state pushed for the rider's
    // dismount, which beat its own death report to the server. Echoing it
    // upstream revived the horse on every other copy.
    IT("never takes a horse's verdict from its simulator's upstream state", {
        KCDC::Shared::Entities::HorseEntity simulator;
        simulator.alive          = true;
        simulator.riderNetworkId = 11;
        simulator.vitalsRevision = 3;
        simulator.health         = 0.0F;

        KCDC::Shared::Entities::HorseEntity server;
        server.alive           = false;
        server.riderNetworkId  = 0;
        server.killerNetworkId = 9;
        server.vitalsRevision  = 4;
        server.health          = 50.0F;

        HorseLifeTests::Send(simulator, server, false);
        EQUALS(server.alive, false);
        EQUALS(server.riderNetworkId, 0ULL);
        EQUALS(server.killerNetworkId, 9ULL);
        EQUALS(server.vitalsRevision, 4U);
        EQUALS(server.health, 0.0F);
    });

    IT("relays the server's verdict to every other copy", {
        KCDC::Shared::Entities::HorseEntity server;
        server.alive           = false;
        server.killerNetworkId = 9;
        server.deathReason     = 1;
        server.vitalsRevision  = 4;

        KCDC::Shared::Entities::HorseEntity observer;
        HorseLifeTests::Send(server, observer, true);
        EQUALS(observer.alive, false);
        EQUALS(observer.killerNetworkId, 9ULL);
        EQUALS(observer.deathReason, 1);
        EQUALS(observer.vitalsRevision, 4U);
    });

    IT("carries the simulator's knockout to the server and on to every other copy", {
        KCDC::Shared::Entities::HorseEntity simulator;
        simulator.unconscious = true;

        KCDC::Shared::Entities::HorseEntity server;
        HorseLifeTests::Send(simulator, server, false);
        EQUALS(server.unconscious, true);

        KCDC::Shared::Entities::HorseEntity observer;
        HorseLifeTests::Send(server, observer, true);
        EQUALS(observer.unconscious, true);
    });

    IT("hands the simulator the verdict through the forced state", {
        KCDC::Shared::Entities::HorseEntity server;
        server.alive          = false;
        server.vitalsRevision = 4;

        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        server.SerializeForcedState(writer);

        KCDC::Shared::Entities::HorseEntity simulator;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        simulator.SerializeForcedState(reader);
        EQUALS(simulator.alive, false);
        EQUALS(simulator.vitalsRevision, 4U);
    });

    // A new simulator never sends a value equal to its own last report, so
    // the server kept the previous simulator's knockout. The forced state
    // hands the new one the server's.
    IT("hands a new simulator the knockout through the forced state", {
        KCDC::Shared::Entities::HorseEntity server;
        server.unconscious = true;

        MafiaNet::BitStream stream;
        Framework::Networking::Replication::FieldSerializer writer(&stream, true);
        server.SerializeForcedState(writer);

        KCDC::Shared::Entities::HorseEntity simulator;
        Framework::Networking::Replication::FieldSerializer reader(&stream, false);
        simulator.SerializeForcedState(reader);
        EQUALS(simulator.unconscious, true);
    });
});

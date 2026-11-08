#pragma once

#include "shared/features/prop/prop_entity.h"
#include "shared/features/prop/prop_rpc.h"
#include "shared/features/world/world_query_rpc.h"

#include <mafianet/bit_stream.h>

#include <cmath>
#include <limits>
#include <string>

MODULE(prop_physics, {
    IT("carries a push to a rigid prop's body and reads it back", {
        KCDC::Shared::RPC::PropMotion push;
        push.propNetworkId = 42;
        push.kind          = static_cast<std::uint8_t>(KCDC::Shared::RPC::PropMotionKind::Impulse);
        push.linear        = {2500.0F, 0.0F, 0.0F};
        push.angular       = {0.0F, 0.0F, 10.0F};
        push.hasPoint      = true;
        push.point         = {582.0F, 340.2F, 118.5F};
        EQUALS(push.IsSane(), true);

        MafiaNet::BitStream stream;
        push.Serialize(&stream, true);

        KCDC::Shared::RPC::PropMotion read;
        read.Serialize(&stream, false);
        EQUALS(read.IsSane(), true);
        EQUALS(read.propNetworkId, 42ULL);
        EQUALS(read.Kind() == KCDC::Shared::RPC::PropMotionKind::Impulse, true);
        EQUALS(read.linear.x, 2500.0F);
        EQUALS(read.angular.z, 10.0F);
        EQUALS(read.hasPoint, true);
        EQUALS(read.point.z, 118.5F);
    });

    IT("refuses a motion no body could take", {
        KCDC::Shared::RPC::PropMotion motion;
        motion.propNetworkId = 7;
        motion.kind          = static_cast<std::uint8_t>(KCDC::Shared::RPC::PropMotionKind::Impulse);
        motion.linear        = {std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F};
        EQUALS(motion.IsSane(), false);

        motion.linear = {KCDC::Shared::RPC::PropMotion::kMaxImpulse * 2.0F, 0.0F, 0.0F};
        EQUALS(motion.IsSane(), false);

        // The same number is a fine push and an impossible speed.
        motion.linear = {1000.0F, 0.0F, 0.0F};
        EQUALS(motion.IsSane(), true);
        motion.kind = static_cast<std::uint8_t>(KCDC::Shared::RPC::PropMotionKind::Velocity);
        EQUALS(motion.IsSane(), false);
        motion.linear = {20.0F, 0.0F, 0.0F};
        EQUALS(motion.IsSane(), true);

        motion.hasPoint = true;
        motion.point    = {1.0e6F, 0.0F, 0.0F};
        EQUALS(motion.IsSane(), false);
        motion.hasPoint = false;

        motion.kind = 200;
        EQUALS(motion.IsSane(), false);

        motion.kind          = static_cast<std::uint8_t>(KCDC::Shared::RPC::PropMotionKind::Sleep);
        motion.propNetworkId = 0;
        EQUALS(motion.IsSane(), false);
    });

    IT("hands a prop to a simulator only while it is rigid", {
        KCDC::Shared::Entities::PropEntity prop;
        EQUALS(prop.GetDelegationPolicy() == nullptr, true);
        prop.physics = KCDC::Shared::Entities::PropPhysics::Rigid;
        EQUALS(prop.GetDelegationPolicy() != nullptr, true);
        EQUALS(prop.GetDelegationPolicy()->acquireRange, 60.0F);
        EQUALS(prop.GetDelegationPolicy()->releaseRange > prop.GetDelegationPolicy()->acquireRange, true);
        EQUALS(prop.CanOwnerDestroy(), false);
        prop.physics = KCDC::Shared::Entities::PropPhysics::None;
        EQUALS(prop.GetDelegationPolicy() == nullptr, true);
    });

    IT("takes a simulator's rest and pose but never its build", {
        // What the simulating client writes upstream: the build is the server's, so it is not in the stream at all.
        KCDC::Shared::Entities::PropEntity simulated;
        simulated.physics      = KCDC::Shared::Entities::PropPhysics::Rigid;
        simulated.scale        = 2.0F;
        simulated.mass         = 100.0F;
        simulated.resting      = true;
        simulated.restPosition = {585.1F, 344.0F, 112.4F};
        simulated.restRotation = {0.70710677F, 0.0F, 0.70710677F, 0.0F};
        MafiaNet::BitStream upstream;
        Framework::Networking::Replication::FieldSerializer write(&upstream, true, /*serverAuthored=*/false);
        simulated.SerializeFields(write);

        KCDC::Shared::Entities::PropEntity server;
        server.physics = KCDC::Shared::Entities::PropPhysics::Rigid;
        server.scale   = 5.0F;
        server.mass    = 500.0F;
        server.resting = false;
        Framework::Networking::Replication::FieldSerializer read(&upstream, false, /*serverAuthored=*/false);
        server.SerializeFields(read);
        EQUALS(read.Good(), true);
        EQUALS(server.scale, 5.0F);
        EQUALS(server.mass, 500.0F);
        EQUALS(server.resting, true);
        EQUALS(server.restPosition.y, 344.0F);
        EQUALS(server.restRotation.w, simulated.restRotation.w);

        // The server's own stream carries the build too, which is how a scale change reaches every client.
        MafiaNet::BitStream downstream;
        Framework::Networking::Replication::FieldSerializer serverWrite(&downstream, true);
        server.SerializeFields(serverWrite);
        KCDC::Shared::Entities::PropEntity observer;
        Framework::Networking::Replication::FieldSerializer observerRead(&downstream, false);
        observer.SerializeFields(observerRead);
        EQUALS(observer.scale, 5.0F);
        EQUALS(observer.mass, 500.0F);
        EQUALS(observer.physics == KCDC::Shared::Entities::PropPhysics::Rigid, true);
    });

    IT("carries a weapon's hit on a tree with the level object it is", {
        KCDC::Shared::RPC::WeaponHitReport report;
        report.levelHash          = 0x1234ABCDU;
        report.hit.kind           = KCDC::Shared::RPC::WorldHitKind::Vegetation;
        report.hit.model          = "objects/natural/vegetation/trees/normal_trees/betula_pendula/betula_pendula_e.cgf";
        report.hit.surface        = "mat_wood";
        report.hit.pointX         = 582.1F;
        report.hit.pointY         = 340.0F;
        report.hit.pointZ         = 113.4F;
        report.hit.normalY        = -1.0F;
        report.hit.distance       = 1.6F;
        report.hit.levelObject    = true;
        report.hit.objectAt       = {582.02F, 340.19F, 112.03F};
        report.hit.objectRotation = {0.0F, 0.0F, 0.0F, 1.0F};
        report.hit.objectScale    = {1.0F, 1.0F, 1.0F};
        EQUALS(report.IsSane(), true);

        MafiaNet::BitStream stream;
        report.Serialize(&stream, true);
        KCDC::Shared::RPC::WeaponHitReport read;
        read.Serialize(&stream, false);
        EQUALS(read.IsSane(), true);
        EQUALS(read.levelHash, 0x1234ABCDU);
        EQUALS(read.hit.model, report.hit.model);
        EQUALS(read.hit.surface, std::string("mat_wood"));
        EQUALS(read.hit.levelObject, true);
        EQUALS(read.hit.objectAt[0], 582.02F);

        // Between levels a client has nothing to stamp a report with.
        read.levelHash = 0;
        EQUALS(read.IsSane(), false);
    });
});

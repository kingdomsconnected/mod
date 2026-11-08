#include "shared/features/stash/stash_entity.h"
#include "shared/features/stash/stash_lid_motion.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
    void RequireStashLid(bool condition, const char *message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    void StashLidReplication() {
        KCDC::Shared::Entities::StashEntity server;
        server.stashId     = 12;
        server.isOpen      = true;
        server.hasContents = true;
        MafiaNet::BitStream construction;
        Framework::Networking::Replication::FieldSerializer constructionWriter(&construction, true, true);
        server.OnSerializeConstruction(constructionWriter);
        KCDC::Shared::Entities::StashEntity lateJoiner;
        Framework::Networking::Replication::FieldSerializer constructionReader(&construction, false, true);
        lateJoiner.OnSerializeConstruction(constructionReader);
        RequireStashLid(constructionReader.Good() && lateJoiner.stashId == 12 && lateJoiner.isOpen && lateJoiner.hasContents, "a late joiner receives the open lid and whether it holds anything");

        KCDC::Shared::Entities::StashEntity forged;
        forged.isOpen      = false;
        forged.hasContents = false;
        MafiaNet::BitStream upstream;
        Framework::Networking::Replication::FieldSerializer upstreamWriter(&upstream, true, false);
        forged.SerializeFields(upstreamWriter);
        RequireStashLid(upstream.GetNumberOfBitsUsed() == 0, "no client can serialize the lid or its contents flag upstream");
        Framework::Networking::Replication::FieldSerializer upstreamReader(&upstream, false, false);
        server.SerializeFields(upstreamReader);
        RequireStashLid(server.isOpen && server.hasContents, "a client cannot close a held container or empty its prompt");

        server.isOpen = false;
        MafiaNet::BitStream released;
        Framework::Networking::Replication::FieldSerializer releaseWriter(&released, true, true);
        server.SerializeFields(releaseWriter);
        Framework::Networking::Replication::FieldSerializer releaseReader(&released, false, true);
        lateJoiner.SerializeFields(releaseReader);
        RequireStashLid(releaseReader.Good() && !lateJoiner.isOpen, "a release closes the lid for a streamed observer");

        KCDC::Shared::StashLidMotion motion;
        RequireStashLid(motion.Update(true, 1000, 2.0F).value() == 1.0F, "late join poses an open lid immediately without replaying an action");
        RequireStashLid(motion.Update(false, 1000, 2.0F).value() == 1.0F, "close begins at the visible open pose");
        RequireStashLid(motion.Update(false, 1500, 2.0F).value() == 0.75F, "close advances using the authored clip duration");
        RequireStashLid(motion.Update(true, 1500, 1.0F).value() == 0.75F, "a new holder reverses an unfinished close without snapping");
        RequireStashLid(motion.Update(true, 1750, 1.0F).value() == 1.0F, "opening reaches the final pose at the authored speed");
        RequireStashLid(motion.Update(true, 9000, 1.0F).value() == 1.0F, "an idle open chest holds its pose without retriggering callbacks");
        RequireStashLid(!motion.Update(false, 9100, 0.0F).has_value() && !motion.Update(false, 9100, std::numeric_limits<float>::quiet_NaN()).has_value(), "invalid native clip durations cannot produce an unbounded pose");
        motion.Reset();
        RequireStashLid(motion.Update(false, 10000, 1.0F).value() == 0.0F, "stream reload discards the old animation timeline");
    }
} // namespace

void StashLidTests() {
    StashLidReplication();
}

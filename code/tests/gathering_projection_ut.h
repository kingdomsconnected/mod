#pragma once

#include "features/gathering/gathering_module.h"
#include "shared/features/gathering/gathering_projection.h"

MODULE(gathering_projection, {
    IT("recovers a lost bulk restore without orphaning native claims or applying stale hides", {
        using KCDC::Shared::Gathering::ProjectedSource;
        using KCDC::Shared::Gathering::Projection;
        Projection projection;
        projection.ChangeWorld(7);
        const ProjectedSource removed {0, 2, {100, 200, 30}};
        const ProjectedSource unprojected {0, 2, {101, 200, 30}};
        const ProjectedSource restoring {0, 2, {102, 200, 30}};
        const auto removedKey     = Projection::KeyOf(removed.kind, removed.position);
        const auto unprojectedKey = Projection::KeyOf(unprojected.kind, unprojected.position);
        const auto restoringKey   = Projection::KeyOf(restoring.kind, restoring.position);
        projection.depletions.emplace(removedKey, removed);
        projection.depletions.emplace(unprojectedKey, unprojected);
        projection.pending.push_back(unprojectedKey);
        projection.claimed.insert(removedKey);
        projection.claimed.insert(restoringKey);
        projection.pendingRespawns.push_back(restoring);
        const auto lostGeneration = projection.syncGeneration;
        projection.CompleteSync(lostGeneration);

        // The restore for removed was dropped. It must be restored locally
        // even when outside the nearby sources included by the fresh replay.
        projection.RecoverOverflow();
        EQUALS(projection.virtualWorld.value(), std::uint32_t(7));
        EQUALS(projection.depletions.empty(), true);
        EQUALS(projection.pending.empty(), true);
        EQUALS(projection.pendingRespawns.size(), std::size_t(2));
        EQUALS(projection.pendingRespawns[0].position == restoring.position, true);
        EQUALS(projection.pendingRespawns[1].position == removed.position, true);
        EQUALS(projection.claimed.size(), std::size_t(2));
        EQUALS(projection.claimed.contains(unprojectedKey), false);
        EQUALS(projection.fullSyncRequired, true);
        EQUALS(projection.AcceptDelivery(std::nullopt), false);
        EQUALS(projection.AcceptDelivery(lostGeneration), false);
        EQUALS(projection.CompleteSync(lostGeneration), false);
        EQUALS(projection.replayPending, true);

        // Every replay chunk is correlated, while full marks only the final
        // chunk. Later ordered deltas become eligible after its completion.
        const auto replay = projection.syncGeneration;
        EQUALS(projection.AcceptDelivery(replay), true);
        EQUALS(projection.replayPending, true);
        EQUALS(projection.AcceptDelivery(std::nullopt), false);
        EQUALS(projection.CompleteSync(replay), true);
        EQUALS(projection.AcceptDelivery(std::nullopt), true);
        EQUALS(projection.replayPending, false);
        // Receiving a snapshot does not discard restoration ownership before
        // ProjectPending or session cleanup can release the native claims.
        EQUALS(projection.pendingRespawns.size(), std::size_t(2));
        EQUALS(projection.claimed.contains(removedKey), true);
    });

    IT("restarts interrupted overflow recovery in the server's new world", {
        using KCDC::Shared::Gathering::Projection;
        Projection projection;
        projection.ChangeWorld(7);
        projection.RecoverOverflow();
        const auto oldReplay = projection.syncGeneration;
        const auto key       = Projection::KeyOf(2, {100, 200, 30});
        EQUALS(projection.AcceptDelivery(oldReplay), true);
        projection.depletions.emplace(key, KCDC::Shared::Gathering::ProjectedSource {0, 2, {100, 200, 30}});
        projection.claimed.insert(key);
        projection.ChangeWorld(8);
        EQUALS(projection.AcceptDelivery(oldReplay), false);
        EQUALS(projection.CompleteSync(oldReplay), false);
        EQUALS(projection.AcceptDelivery(std::nullopt), false);
        EQUALS(projection.depletions.empty(), true);
        EQUALS(projection.pendingRespawns.size(), std::size_t(1));
        EQUALS(projection.fullSyncRequired, true);
        EQUALS(projection.AcceptDelivery(projection.syncGeneration), true);
        EQUALS(projection.CompleteSync(projection.syncGeneration), true);
        EQUALS(projection.AcceptDelivery(std::nullopt), true);
    });

    IT("retains the replay barrier across repeated loss and native reload", {
        KCDC::Shared::Gathering::Projection projection;
        projection.ChangeWorld(7);
        projection.RecoverOverflow();
        const auto first = projection.syncGeneration;
        projection.RecoverOverflow();
        const auto second = projection.syncGeneration;
        EQUALS(projection.AcceptDelivery(first), false);
        projection.Forget();
        projection.ChangeWorld(7);
        EQUALS(projection.AcceptDelivery(second), false);
        EQUALS(projection.AcceptDelivery(std::nullopt), false);
        EQUALS(projection.CompleteSync(second), false);
        EQUALS(projection.CompleteSync(projection.syncGeneration), true);
        EQUALS(projection.AcceptDelivery(std::nullopt), true);
    });

    IT("retries a full replay when a pre-load completion arrives after native reset", {
        KCDC::Shared::Gathering::Projection projection;
        const auto beforeLoad = projection.syncGeneration;
        projection.ChangeWorld(0);
        projection.Forget();
        const auto afterLoad = projection.syncGeneration;
        EQUALS(afterLoad != beforeLoad, true);
        // The old final chunk can be queued before the new full request is sent.
        // Its world tag must not make its completion current again.
        projection.ChangeWorld(0);
        EQUALS(projection.CompleteSync(beforeLoad), false);
        EQUALS(projection.fullSyncRequired, true);
        EQUALS(projection.syncGeneration, afterLoad);
        // Throttled requests can be retried with this same generation. An empty
        // successful replay has the same acknowledgement as a populated one.
        EQUALS(projection.CompleteSync(afterLoad), true);
        EQUALS(projection.fullSyncRequired, false);
        projection.Forget();
        EQUALS(projection.CompleteSync(afterLoad), false);
        EQUALS(projection.fullSyncRequired, true);
    });

    IT("invalidates replay completion on world changes but preserves first-world correlation", {
        KCDC::Shared::Gathering::Projection projection;
        const auto initial = projection.syncGeneration;
        projection.ChangeWorld(7);
        EQUALS(projection.syncGeneration, initial);
        EQUALS(projection.CompleteSync(initial), true);
        EQUALS(projection.ChangeWorld(7), false);
        EQUALS(projection.fullSyncRequired, false);
        projection.ChangeWorld(8);
        EQUALS(projection.CompleteSync(initial), false);
        EQUALS(projection.fullSyncRequired, true);
        const auto otherWorld = projection.syncGeneration;
        projection.ChangeWorld(7);
        EQUALS(projection.CompleteSync(otherWorld), false);
        EQUALS(projection.CompleteSync(initial), false);
        EQUALS(projection.fullSyncRequired, true);
        EQUALS(projection.CompleteSync(projection.syncGeneration), true);
    });

    IT("reads full replay generations without truncation and rejects invalid types", {
        KCDC::Features::Gathering::GatheringRequest request;
        EQUALS(KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"sync","full":true,"syncGeneration":18446744073709551615})", request), true);
        EQUALS(request.full, true);
        EQUALS(request.syncGeneration, std::uint64_t(-1));
        EQUALS(KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"sync","full":true,"syncGeneration":-1})", request), false);
        EQUALS(KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"sync","full":true,"syncGeneration":1.5})", request), false);
        EQUALS(KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"sync","full":true,"syncGeneration":"1"})", request), false);
        EQUALS(KCDC::Features::Gathering::GatheringModule::ReadRequest(R"({"action":"sync","full":true})", request), true);
        EQUALS(request.syncGeneration, std::uint64_t(0));
    });

    IT("keeps sub-centimetre authored neighbours distinct", {
        KCDC::Shared::Gathering::Projection projection;
        const glm::vec3 first {100.001F, 200.0F, 30.0F};
        const glm::vec3 second {100.004F, 200.0F, 30.0F};
        const auto a = KCDC::Shared::Gathering::Projection::KeyOf(2, first);
        const auto b = KCDC::Shared::Gathering::Projection::KeyOf(2, second);
        projection.depletions.emplace(a, KCDC::Shared::Gathering::ProjectedSource {0, 2, first});
        projection.depletions.emplace(b, KCDC::Shared::Gathering::ProjectedSource {0, 2, second});
        EQUALS(projection.depletions.size(), std::size_t(2));
        EQUALS(projection.depletions.erase(a), std::size_t(1));
        EQUALS(projection.depletions.contains(b), true);
        EQUALS(KCDC::Shared::Gathering::Projection::KeyOf(2, {0.0F, 0.0F, 0.0F}) == KCDC::Shared::Gathering::Projection::KeyOf(2, {-0.0F, 0.0F, 0.0F}), true);
    });

    IT("restores only applied sources and discards stale hides on a world change", {
        KCDC::Shared::Gathering::Projection projection;
        EQUALS(projection.ChangeWorld(0), true);
        const auto a = KCDC::Shared::Gathering::Projection::KeyOf(2, {100, 200, 30});
        const auto b = KCDC::Shared::Gathering::Projection::KeyOf(2, {101, 200, 30});
        projection.depletions.emplace(a, KCDC::Shared::Gathering::ProjectedSource {0, 2, {100, 200, 30}});
        projection.depletions.emplace(b, KCDC::Shared::Gathering::ProjectedSource {0, 2, {101, 200, 30}});
        projection.claimed.insert(a);
        projection.pending.push_back(b);
        EQUALS(projection.ChangeWorld(1), true);
        EQUALS(projection.depletions.empty(), true);
        EQUALS(projection.pending.empty(), true);
        EQUALS(projection.pendingRespawns.size(), std::size_t(1));
        EQUALS(projection.pendingRespawns.front().position == glm::vec3(100, 200, 30), true);
        EQUALS(projection.claimed.contains(a), true);
        EQUALS(projection.ChangeWorld(1), false);
        EQUALS(projection.pendingRespawns.size(), std::size_t(1));
        // A second transition before the native tick must retain the first
        // world's restoration, even though its source map was cleared.
        EQUALS(projection.ChangeWorld(2), true);
        EQUALS(projection.pendingRespawns.size(), std::size_t(1));
        projection.Forget();
        EQUALS(projection.pendingRespawns.empty(), true);
        EQUALS(projection.claimed.empty(), true);
        EQUALS(projection.virtualWorld.has_value(), false);
    });

    IT("keeps optimistic state when the server first identifies the world", {
        KCDC::Shared::Gathering::Projection projection;
        const auto key = KCDC::Shared::Gathering::Projection::KeyOf(2, {100, 200, 30});
        projection.depletions.emplace(key, KCDC::Shared::Gathering::ProjectedSource {3600000, 2, {100, 200, 30}});
        projection.pending.push_back(key);
        EQUALS(projection.ChangeWorld(7), true);
        EQUALS(projection.depletions.contains(key), true);
        EQUALS(projection.pending.size(), std::size_t(1));
        EQUALS(projection.pendingRespawns.empty(), true);
    });
});

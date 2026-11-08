#pragma once

#include "core/feature_registry.h"

#include "shared/features/quest/quest_rpc.h"

#include "sdk/types.h"
#include "sdk/wh/guimodule/c_compass_mark.h"
#include "sdk/wh/guimodule/e_compass_mark_type.h"
#include "sdk/wh/guimodule/s_quest_marker_entry.h"
#include "sdk/wh/questmodule/c_log_base.h"
#include "sdk/wh/questmodule/c_objective.h"
#include "sdk/wh/questmodule/c_quest.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace KCDC::Features::Quest {
    /**
     * Every server quest this machine is showing, and the detours that put
     * them in front of the game's own quest UI: the journal, the tracker and
     * the quest markers on the map and compass.
     *
     * `C_QuestManager` is left alone, because it is what the single-player
     * save walks. The two functions every reader of its list goes through are
     * detoured instead, so what the game shows is one list and what it saves
     * is its own.
     *
     * Game thread only, except `NativeContextChanged` and `QueueDestroy`.
     */
    class QuestService final: public KCDC::Core::IFeatureService {
      public:
        /** A resource that leaks quests should stop adding them rather than make every journal open walk an unbounded list. */
        static constexpr std::size_t kMaxLiveQuests = 64;

        /** Installs the two detours and the announcement handler. Called once at startup. */
        static void Install();

        /** Takes an announcement off the wire. Peer pump: it only queues. */
        void Receive(const Shared::RPC::QuestAnnounced &announced);

        /** Takes a track instruction off the wire. Peer pump: it only queues. */
        void Receive(const Shared::RPC::QuestTrackRequest &request);

        /**
         * The text behind one of this feature's `@kcdc_quest_...` labels.
         *
         * The journal will not show a sentence: everything it draws goes
         * through the localiser, which turns text into one `@word` key per
         * word. So the node carries a key of our own, and this answers it.
         * The key names the quest's NetworkID, so the text is read off the
         * replica when the UI asks and never has to be kept in step.
         */
        static bool ResolveLabel(const char *label, SDK::CryStringT<char> &out);

        /** The label a quest's title, description or Nth objective is written as. */
        static std::string TitleLabel(std::uint64_t questNetworkId);
        static std::string DescriptionLabel(std::uint64_t questNetworkId);
        static std::string ObjectiveLabel(std::uint64_t questNetworkId, std::size_t index);

        void Update() override;
        void Reset() override;
        void NativeContextChanged() override;

        /** Whether another quest fits. Says so in the log the first time it does not. */
        bool HasRoom();

        /** Takes a built node into the register. The node keeps its own reference; this does not add one. */
        void Register(SDK::wh::questmodule::C_Quest *quest);
        void Unregister(SDK::wh::questmodule::C_Quest *quest);

        // --- What the detours ask. Game thread, from inside the detour. ---

        /** Appends every registered quest, taking a reference on each the way the game's own collection does. */
        void AppendTo(SDK::StdVector<SDK::wh::questmodule::C_Quest *> &collected);

        /** The registered quest of that node name, or null. No reference is taken. */
        SDK::wh::questmodule::C_Quest *FindByName(const SDK::CryStringT<char> &name) const;

        /** Whether the journal can safely be handed this node: the row builder reads every objective without checking any of them. */
        static bool IsRenderable(const SDK::wh::questmodule::C_Quest *quest);

        /** Whether this node is one of ours. Game thread, from inside a detour. */
        bool Owns(const SDK::wh::questmodule::C_Quest *quest) const;

        /**
         * Reports that the game started or stopped following one of our
         * quests: raises the local scripting event and tells the server.
         *
         * Whether this is a change is the caller's to establish -- the natives
         * are not edge-triggered, and the game's own table is the only honest
         * answer to what was being followed a moment ago.
         */
        void NoteTracking(const SDK::wh::questmodule::C_Quest *quest, bool tracked);

        /**
         * Adds the map entries for every positioned, active objective of a
         * server quest this machine is following. Game thread, from inside the
         * detour on `C_UIQuestLog::CollectQuestMarkers`.
         */
        void AppendMarkerEntries(SDK::StdVector<SDK::wh::guimodule::S_QuestMarkerEntry> &entries) const;

        /** Defers a release to the next tick: a replica dies on the peer pump, where the concept registry is not safe to touch. Either thread. */
        void QueueDestroy(SDK::wh::questmodule::C_Quest *quest, std::vector<SDK::wh::questmodule::C_Objective *> &&objectives, std::vector<SDK::wh::questmodule::C_LogBase *> &&logs);

      private:
        /** A track instruction that arrived before the quest it names was built. */
        struct PendingTrack {
            Shared::RPC::QuestTrackRequest request;
            std::chrono::steady_clock::time_point deadline;
        };

        /** How long a track instruction waits for its quest; the replica and the RPC travel separately. */
        static constexpr std::chrono::seconds kTrackRequestLifetime {15};

        /** The least time between two map marker refreshes, so a marker a script moves every tick does not redraw every tick. */
        static constexpr std::chrono::milliseconds kMarkerRefreshInterval {250};

        /**
         * What a map quest marker reads its position from, in place of the
         * `C_LinkableObject` a shipped objective's marker follows. The row
         * builder calls slot 2 for the position and reads a WUID at +8, and
         * that is all: nothing attaches to it, which a compass mark would.
         *
         * Never freed while the service lives, only recycled: the map keeps
         * its own copy of the entry list, pointers included, and reads it
         * again when the player switches maps.
         */
        class MarkerSource final {
          public:
            virtual ~MarkerSource() = default; // slot 0
            virtual void Unused() {}           // slot 1

            virtual const SDK::Vec3 *GetWorldPosition() const { // slot 2
                return &position;
            }

            // Read at +8 as the object's WUID: for the row's id, the area test
            // and the carried-item test. The mod's own range keeps it out of
            // the area types (7, 15, 20, 21) and off every real item.
            std::uint64_t wuid = 0;
            SDK::Vec3 position {};
        };

        /** One positioned objective of a followed server quest. Game thread only. */
        struct ObjectiveMarker {
            std::uint64_t questNetworkId = 0;
            std::size_t index            = 0;
            std::string questKey;
            std::string text;
            SDK::wh::guimodule::E_CompassMarkType markType = SDK::wh::guimodule::E_CompassMarkType::Side;
            std::int32_t trackingId                        = -1;
            MarkerSource *source                           = nullptr;
            SDK::wh::guimodule::CompassMarkRef compassMark;

            /** The map's row no longer matches: it has to drop out for one refresh to be drawn again. */
            bool stale = false;
        };

        /** Applies the track instructions whose quests exist, and drops the ones that waited too long. */
        void DrainTrackRequests();

        /** Brings the objective markers in line with what is followed, and the compass with them. */
        void UpdateMarkers();

        /** Redraws the map's quest markers when the set changed, dropping stale rows first. */
        void RefreshMapMarkers();

        void AddCompassMark(ObjectiveMarker &marker);
        void RemoveCompassMark(ObjectiveMarker &marker);
        MarkerSource *AcquireSource();
        void ReleaseMarkers();

        /** What a queued teardown carries: the node, and the objectives the replica held its own reference on. */
        struct PendingDestroy {
            SDK::wh::questmodule::C_Quest *quest;
            std::vector<SDK::wh::questmodule::C_Objective *> objectives;
            std::vector<SDK::wh::questmodule::C_LogBase *> logs;
        };

        void DrainDestroys();

        /** Raises the queued toasts through the HUD's own quest interface. Game thread. */
        void DrainAnnouncements();

        /** Drops the register when the level the nodes belonged to has gone. Game thread. */
        void ObserveNativeContext();

        std::vector<SDK::wh::questmodule::C_Quest *> _quests;

        // Bumped off the game thread when the world changes, observed on it.
        std::atomic<std::uint64_t> _nativeContext {0};
        std::uint64_t _observedContext = 0;

        std::mutex _pendingDestroysMutex;
        std::vector<PendingDestroy> _pendingDestroys;

        std::mutex _pendingAnnouncementsMutex;
        std::vector<Shared::RPC::QuestAnnounced> _pendingAnnouncements;

        std::mutex _pendingTracksMutex;
        std::vector<PendingTrack> _pendingTracks;

        // Game thread: instructions still waiting for their quest.
        std::vector<PendingTrack> _heldTracks;

        std::vector<ObjectiveMarker> _markers;
        std::vector<std::unique_ptr<MarkerSource>> _sources;
        std::vector<MarkerSource *> _freeSources;
        std::uint64_t _nextSourceSerial = 0;

        bool _mapRefreshWanted = false;
        std::chrono::steady_clock::time_point _lastMapRefresh {};

        // Set only across the first of a two-step refresh, so the collector
        // leaves the stale rows out and the map removes them.
        bool _leaveOutStale = false;

        // Both logged once rather than once per journal open.
        bool _warnedAtBudget    = false;
        bool _warnedAtPageLimit = false;
    };
} // namespace KCDC::Features::Quest

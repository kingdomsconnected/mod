#pragma once

#include "sdk/cryengine/animation/i_character_instance.h"
#include "shared/game/mod_item_tables.h"
#include "sdk/cryengine/system/i_system_event_listener.h"
#include "sdk/cryengine/system/xml_node_ref.h"
#include "sdk/wh/fsw_bank.h"

#include "streamed_asset_settings.h"

#include "shared/streamed_assets/streamed_asset_policy.h"

#include <utils/streamed_assets/pak_archive.h>

#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Framework::Integrations::Client {
    struct ResourceAssetPak;
} // namespace Framework::Integrations::Client

namespace KCDC::Core::StreamedAssets {
    /**
     * What KCDC does with the asset paks a server's resources ship. They arrive with the resource
     * packages, through the Framework's own asset download: announced in each resource's
     * ResourceInfo, fetched into the server's cache by the same delta transfer, refreshed by the same
     * hot reload (Framework::Integrations::Client::Instance::GetResourceAssetPaks).
     *
     * Every pak is checked against the policy again on this machine and mounted
     * through CryPak -- before the level is asked for when joining, so the level load's own flush
     * means the engine's loaders pick everything up with nothing registered. What needs the engine
     * told is told natively:
     *
     *  - ATL controls (`sounds/kcdc/<resource>/atl/*.xml`) are parsed at scope 2 once the level is
     *    up, the scope `UnLoadLevel` clears;
     *  - FMOD banks register with the game's FMOD wrapper, the mod's own master and strings banks
     *    first, so their events reach the same path map the game's do;
     *  - clips get a `KCDC_StreamedClip` fragment entry in the human Mannequin database, which is
     *    read again whenever the clips in use differ from the ones it holds, played with the
     *    game's own MovementControlMethod, and join a body's animation set the first time it plays
     *    one;
     *  - particle libraries load by name the first time FindEffect asks, like the game's own;
     *  - a KCD2 mod's table patches (`libs/tables/.../<table>__<id>.xml`, replace lane) are read
     *    by the game's own database reload, and its string patches (localization lane) are merged
     *    into the string table by the loader the game runs for an installed mod's.
     *
     * Paks a resource ships while the player is connected -- it started, or restarted with changed
     * files -- are applied in place: new paks mount one per frame, everything above is registered
     * for them, and a changed file is reloaded where it is resident. Paks the session no longer names
     * are disabled at the next level edge (system event 35) -- the level they fed stays up after a
     * disconnect.
     *
     * Game thread only.
     */
    class StreamedAssetService final: public SDK::ISystemEventListener {
      public:
        enum class JoinStatus {
            Downloading,
            AwaitingConsent,
            Ready,
            Failed,
        };

        /** A new connection starts: nothing it streams is mounted yet. */
        void BeginJoin();

        /** Mounts the session's assets once they are downloaded. The level must not be asked for before Ready. */
        JoinStatus PollJoin();

        /** The Framework's asset pak list changed; applied in place once the session's first set is mounted. */
        void OnResourceAssetPaksChanged();

        const std::string &GetError() const {
            return _error;
        }

        /** Per-frame work: the settings, and paks shipped mid-session. */
        void Update();

        /** Leaves the profile manager. Must run before the mod unloads. */
        void Shutdown();

        StreamedAssetSettings &GetSettings() {
            return _settings;
        }

        /** Whether a mounted stream-lane pak carries this normalised path. */
        bool HasEntry(std::string_view path) const;

        /**
         * Readies a streamed clip on a character and answers the option of `KCDC_StreamedClip` that
         * plays it. False for a clip no pak in use carries.
         */
        bool PrepareClip(SDK::ICharacterInstance *character, const std::string &clip, int &option);

        /** Called while the human Mannequin files load, on the editable copy: declares the fragment and its entries. */
        void ExtendMannequinFile(const char *file, SDK::XmlNodeRef &document);

        void OnSystemEventAnyThread(SDK::ESystemEvent event, std::uint64_t wparam, std::uint64_t lparam) override;
        void OnSystemEvent(SDK::ESystemEvent event, std::uint64_t wparam, std::uint64_t lparam) override;

      private:
        struct MountedPak {
            std::string resource;
            std::string lane;
            std::string path;
            std::vector<Framework::Utils::StreamedAssets::PakEntry> entries;
            bool accessible = false;
            // Banks this pak registered with the FMOD wrapper, and the raw master and strings banks
            // it loaded itself; released when the pak retires.
            std::vector<SDK::wh::FSW_BANK *> eventBanks;
            std::vector<void *> rawBanks;
            // Whether what the banks carry has been logged since they linked.
            bool banksReported = false;
            // The item classes the pak's `item__<id>.xml` patches add, read when it mounts and
            // registered with the catalog while it stays mounted.
            std::vector<KCDC::Shared::Game::ModItemClass> itemClasses;
        };

        void EnsureListening();
        bool Mount(const Framework::Integrations::Client::ResourceAssetPak &pak, bool joining);
        // The paths of the Framework's current pak list.
        std::unordered_set<std::string> CurrentPaths() const;
        void LoadBanks(MountedPak &mounted);
        void ReleaseBanks(MountedPak &mounted);
        void ReportLinkedBanks(MountedPak &mounted);
        void ParseAudioControls(const MountedPak &mounted);
        void ReloadChanged(const MountedPak &previous, const MountedPak &current);
        void RebuildClipDatabase();
        // Brings the string table, and at the level edge the databases, in line with the patches the
        // answering paks carry.
        void SyncModData(bool levelEdge);
        // Reads a freshly mounted pak's item table patches.
        void ReadItemTables(MountedPak &mounted);
        // Registers the item classes of every mounted pak with the catalog and drops the rest.
        void SyncModItems();
        void Retire();
        void Fail(const std::string &error);

        StreamedAssetSettings _settings;

        std::string _error;
        bool _listening   = false;
        bool _levelLoaded = false;

        // Every pak this process has mounted, by cache path. CryPak answers a second mount of the same
        // path with an early true, so a pak coming back is re-enabled rather than mounted again.
        std::unordered_map<std::string, MountedPak> _mounted;
        // The paths of the set in use; everything else retires at the next level edge.
        std::unordered_set<std::string> _current;
        // Whether the session's first set is mounted, after which changes are applied in place.
        bool _joined = false;

        // A changed set, mounted a pak per frame: indices into the Framework's list, which only
        // changes again through OnResourceAssetPaksChanged.
        std::deque<std::size_t> _pending;
        bool _applying = false;

        // Clips the human animation database was last built with, in option order.
        std::vector<std::string> _databaseClips;
        std::vector<std::string> _clips;
        // Clips added to an animation set this level, keyed by set and clip.
        std::unordered_set<std::string> _preparedClips;
        // Table patches the databases were last read with, and string patches merged, each by pak
        // and entry.
        std::set<std::string> _appliedTablePatches;
        std::set<std::string> _appliedStringPatches;
        // Paks whose item classes the catalog currently holds.
        std::set<std::string> _modItemSources;
        // Whether the table patches in use differ from the ones the databases were read with.
        bool _tablesPending = false;

        // ATL folders parsed into this level.
        std::unordered_set<std::string> _parsedControls;

        // Banks registered with the wrapper this process; it keeps them for good.
        std::unordered_map<std::string, SDK::wh::FSW_BANK *> _registeredBanks;
    };
} // namespace KCDC::Core::StreamedAssets

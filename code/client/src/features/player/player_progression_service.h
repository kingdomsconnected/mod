#pragma once

#include "core/feature_registry.h"
#include "features/player/player_duel_roster.h"

#include "shared/features/player/player_progression.h"

#include "sdk/types.h"
#include "sdk/wh/rpgmodule/c_perk_learning_state.h"
#include "sdk/wh/rpgmodule/c_perk_list.h"
#include "sdk/wh/rpgmodule/c_rpg_inventory.h"
#include "sdk/wh/rpgmodule/c_skill_xp_effect.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/c_stat_xp_effect.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Features::Player {
    /**
     * The local player's skills, stats and perks, as the server decides them.
     *
     * The server authorizes the input and this client's engine computes the
     * result. An XP gain the game produces for the local soul is withheld at
     * the effect's `Apply` and asked for; the server answers with an order
     * this service applies through the game's own effect path, so the curve,
     * the perk multipliers, the perk-point grants and the level-up popup stay
     * native. A perk confirmed on the perk screen is refused at `LearnPerk`
     * until the server orders the same learn. Back the other way goes a
     * digest of the whole of the local body's progression.
     *
     * Remote bodies are never given perks -- that would build every buff perk
     * a second time on top of the stats `HumanSoulState` already carries.
     * `C_Soul::HasPerk` on a remote copy is answered from the server's
     * replicated `ProgressionView` instead.
     */
    class PlayerProgressionService final: public KCDC::Core::IFeatureService {
      public:
        /** Registers the order, refusal and policy RPCs. Call once at startup. */
        static void Install();

        /** Game thread: send what was withheld, apply orders, and publish the digest when a hook said it changed. */
        void Update() override;

        /** Clears the session's orders, policy, digest and roster. */
        void Reset() override;

        /**
         * Whether the skill effect's `Apply` should be skipped, having been
         * recorded as a request. Any thread: the detour runs wherever the
         * event is processed.
         */
        bool WithholdsSkillGain(SDK::wh::rpgmodule::C_SkillXPEffect *effect);
        bool WithholdsStatGain(SDK::wh::rpgmodule::C_StatXPEffect *effect);

        /**
         * While set, the local soul's gains are withheld and not asked for: the
         * server grants them itself with the action that made them, so none
         * outlives a refusal. Set around a drying rack's, smokehouse's or
         * fire's conversion, which gives its survival XP in the same event.
         * Per thread, as the event is processed on the thread that made it.
         */
        void SetServerGrantsGains(bool granted) {
            _serverGrantsGains = granted;
        }

        /**
         * Whether the perk screen's learn should be dropped, having been
         * recorded as a request. Gated at `C_RPGInventory::ApplyInventoryPerk`,
         * before any event exists: refused inside the event, the screen would
         * report the learn as failed.
         */
        bool RefusesPerkScreenLearn(const SDK::wh::rpgmodule::I_Soul *soul, const SDK::CryGUID &perk);

        /** Whether an add the engine made on its own is one this server blocks. */
        bool RefusesAdd(SDK::wh::rpgmodule::C_PerkList *list, const SDK::CryGUID &perk);

        /** Answers `HasPerk` for a remote copy's soul. False when `soul` is not one; `owned` is the answer when true. Any thread. */
        bool AnswersRemotePerk(const SDK::wh::rpgmodule::I_Soul *soul, const SDK::CryGUID &perk, bool &owned) const;

        /** Whether the perk screen should draw `perk` as blocked for `soul`: the local player's, and a perk the server blocks. */
        bool ShowsBlocked(const SDK::wh::rpgmodule::I_Soul *soul, const SDK::CryGUID &perk);

        /**
         * A perk was added to or removed from a list. Marks the digest stale
         * when the list is the local soul's. Any thread.
         */
        void NotePerkListChanged(const SDK::wh::rpgmodule::C_PerkList *list);

        /**
         * The RPG module dispatched an event to a perk list, which is where a
         * level becomes perk points. Releases the perk orders waiting on that
         * drain, and marks the digest stale when the banks moved. Any thread.
         */
        void NotePerkEventDispatched(const SDK::wh::rpgmodule::C_PerkList *list, bool pointsChanged);

        /** A remote player's replicated perks arrived. The roster is republished on the next tick when they changed. Game thread. */
        void NoteRemotePerks(std::uint64_t networkId, const Shared::RPC::PerkSet &perks);

        /** A remote player's body is gone. Game thread. */
        void ForgetRemote(std::uint64_t networkId);

        /** The local body's progression as last read, for the client's own scripting handle. */
        const Shared::RPC::ProgressionDigest &LocalDigest() const {
            return _published;
        }

        bool HasLocalDigest() const {
            return _revision != 0;
        }

      private:
        /** A remote player's replicated perks, published whole for readers on other threads. */
        struct RemotePerkRoster {
            static constexpr std::size_t kCapacity = 64;

            std::array<std::uint64_t, kCapacity> networkIds {};
            std::array<Shared::RPC::PerkSet, kCapacity> perks {};
            std::size_t count = 0;
        };

        void SendWithheldGains();
        void SendLearnRequests();
        void ApplyOrders(SDK::wh::rpgmodule::C_Soul *soul);
        bool ApplyOrder(SDK::wh::rpgmodule::C_Soul *soul, const Shared::RPC::ProgressionOrder &order);
        bool ApplyXp(SDK::wh::rpgmodule::C_Soul *soul, std::uint8_t track, std::uint32_t xpQ7, bool scaled);
        bool ApplyExactXp(SDK::wh::rpgmodule::C_Soul *soul, std::uint8_t track, std::uint32_t totalQ7);
        void BuildDigest(const SDK::wh::rpgmodule::C_Soul *soul, Shared::RPC::ProgressionDigest &digest) const;
        void Publish(const Shared::RPC::ProgressionDigest &digest);
        void EmitLocalEvents(const Shared::RPC::ProgressionDigest &previous, const Shared::RPC::ProgressionDigest &current) const;
        void PublishRemotes();
        bool IsLocalSoul(const SDK::wh::rpgmodule::C_Soul *soul) const;
        void QueueGain(std::uint8_t track, std::uint32_t xpQ7, bool scaled, const SDK::wh::rpgmodule::C_Effect *effect);

        // Set around this service's own orders, so the gates let them
        // through. Per thread: `ProcessEvent` applies the effect on the
        // thread that asked, and a gain on a worker must not see it.
        static thread_local bool _applyingOrder;
        static thread_local bool _serverGrantsGains;

        // Whose gains to withhold. The detours compare the effect's subject
        // against it from any thread.
        std::atomic<std::uint64_t> _localSoulWuid {0};

        // Written from the detours, drained on the mod's own tick.
        std::mutex _requestMutex;
        std::vector<Shared::RPC::XpGain> _withheldGains;
        std::vector<std::uint16_t> _learnRequests;
        std::uint64_t _lastGainSendAt = 0;

        std::vector<Shared::RPC::ProgressionOrder> _pendingOrders;
        std::vector<Shared::RPC::PerkRefused> _pendingRefusals;
        std::uint32_t _appliedOrder = 0;

        // The policy: read by the detours as well as the tick.
        std::atomic<bool> _nativeXp {true};
        Shared::RPC::PerkSet _blocked;
        std::mutex _policyMutex;

        // Set by every hook that changes the local soul's progression, and by
        // this service's own orders; the digest is read only then. Every
        // native change passes one: XP only moves through orders, perks
        // through `AddPerk`/`RemovePerk`, points through `C_PerkList::OnEvent`.
        std::atomic<bool> _digestDirty {true};

        // Set by an XP or level order and cleared once the RPG module has
        // drained its queue through the local `C_PerkList::OnEvent`; perk
        // orders wait for it.
        std::atomic<bool> _awaitingDispatch {false};

        Shared::RPC::ProgressionDigest _published;
        std::uint32_t _revision      = 0;
        std::uint64_t _lastPublishAt = 0;

        // What the server last replicated for each remote player, the source
        // the roster is rebuilt from when one of them changes.
        std::vector<std::pair<std::uint64_t, Shared::RPC::PerkSet>> _remotePerks;
        bool _remotesDirty = false;
        RosterRing<RemotePerkRoster, 3> _remotes;
    };
} // namespace KCDC::Features::Player

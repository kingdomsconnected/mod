#pragma once

#include "core/feature_registry.h"

#include "shared/features/player/player_buff.h"

#include "sdk/types.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/s_buff_definition.h"

#include <cstdint>
#include <variant>
#include <vector>

namespace KCDC::Features::Player {
    /**
     * Server-decided status effects on the local player's body.
     *
     * An intent channel rather than replicated state: the server says a buff
     * goes on or comes off and this client applies it through the game's own
     * `C_BuffManager`. Observers run nothing -- they already wear the
     * consequences through `HumanSoulState`. Back the other way go a digest of
     * what this body has and the refusals the claim gate made.
     */
    class PlayerBuffService final: public KCDC::Core::IFeatureService {
      public:
        /** Registers the order and policy RPCs. Call once at startup. */
        static void Install();

        /** Game thread: apply pending orders, report refusals, then publish the digest. */
        void Update() override;

        /** Clears the session's orders, policy and published digest. */
        void Reset() override;

        /**
         * Whether the create funnel should turn this add down, recording the
         * refusal for the server when it does. Called from the detour, which
         * fires for every soul in the world, so the cheap checks come first.
         */
        bool RefusesLocalAdd(const SDK::wh::rpgmodule::C_Soul *soul, const SDK::CryGUID &requested);

        /** The digest as last published, with when each of its buffs turned up, for the client's own scripting handle. */
        const std::vector<Shared::RPC::ObservedBuff> &LocalBuffs() const {
            return _published;
        }

      private:
        /** A definition the gate turned down, and when the server was last told. */
        struct Refusal {
            Shared::RPC::BuffId buff;
            std::uint64_t reportedAt = 0;
        };

        void ApplyOrders(SDK::wh::rpgmodule::C_Soul *soul);
        void BuildDigest(SDK::wh::rpgmodule::C_Soul *soul);
        /** Whether the digest carries an instance of `definition`, reported as `buff`. */
        bool IsReportable(const SDK::wh::rpgmodule::S_BuffDefinition &definition, const Shared::RPC::BuffId &buff) const;
        void PruneAttribution();
        void Publish();
        void ReportRefusals();

        std::uint32_t _claimedClasses = 0;

        std::vector<std::variant<Shared::RPC::ApplyBuff, Shared::RPC::RetractBuff, Shared::RPC::ClearBuffsByTag>> _pendingOrders;

        // Written from inside the create detour, drained on the mod's own tick.
        std::vector<Shared::RPC::BuffId> _pendingRefusals;
        std::vector<Refusal> _recentRefusals;

        // What the server asked for and this client put on. Handles cover the
        // ordinary case; definitions cover a stack the engine merged into an
        // instance we never got a handle for, and make an ordered buff
        // reportable whatever class it belongs to.
        std::vector<std::uint64_t> _serverHandles;
        std::vector<Shared::RPC::BuffId> _serverDefinitions;

        // Rebuilt in place every tick rather than allocated: this runs on the
        // game thread on every frame of a session.
        std::vector<Shared::RPC::ObservedBuff> _digest;

        std::vector<Shared::RPC::ObservedBuff> _published;
        std::uint32_t _revision = 0;
        bool _overflowLogged    = false;

        // The definition being applied, so the gate lets our own order through.
        // A GUID rather than a depth counter: an add can set off further adds,
        // and only the ordered one is exempt.
        Shared::RPC::BuffId _applyingBuff;
    };
} // namespace KCDC::Features::Player

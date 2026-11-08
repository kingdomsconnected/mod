#pragma once

#include <unordered_map>

#include "shared/features/player/player_combat_pricing.h"
#include "shared/features/player/player_damage.h"
#include "shared/features/player/player_entity.h"

#include <scripting/builtins/player.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace KCDC::Scripting {
    // The contract's spelling of a damage reason: a string-literal union of every name `Shared::Game::DamageReasonName` gives, `unknown` included. Shared by every event that carries one.
    std::string DamageReasonType();

    // Resolves the player by network ID. Body commands request changes from
    // the authoritative client.
    class Player final: public Framework::Scripting::Builtins::Player {
      public:
        Player(std::uint64_t networkId): Framework::Scripting::Builtins::Player(networkId) {}

        // The live body, or nullptr once the player is gone.
        Shared::Entities::HumanEntity *ResolveHuman() const;

        // The horse this player is in the saddle of, as a handle, or JS null.
        v8::Local<v8::Value> GetHorseHandle(v8::Isolate *isolate) const;

        // The dog belonging to this player, or JS null when they have none.
        v8::Local<v8::Value> GetDogHandle(v8::Isolate *isolate) const;

        bool IsMounted() const;
        bool Revive();
        bool CancelCombat(Player *opponent);

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Player> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Player>>> _classes;
    };

    // What this feature tells loaded resources happened to a player. Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        void PlayerAttack(std::uint64_t playerNetworkId, std::uint64_t opponentNetworkId);
        void PlayerCombatCancelled(std::uint64_t first, std::uint64_t second);
        // The connecting player's body exists. It has no pose yet.
        void PlayerConnected(std::uint64_t playerNetworkId);

        // Raised while the body is still resolvable, so a handler can read it.
        void PlayerDisconnected(std::uint64_t playerNetworkId);

        // `killerNetworkId` is 0 for no player; `reason` is the fatal write's.
        void PlayerDied(std::uint64_t playerNetworkId, std::uint64_t killerNetworkId, Shared::Game::DamageReason reason);

        // `amount` health came off the player's body: a blow another player
        // landed, as the combat referee ruled it, or anything else, as the
        // player's own client took it. Raised for blows only; drains are live
        // numbers already.
        void PlayerDamaged(std::uint64_t playerNetworkId, std::uint64_t attackerNetworkId, float amount, std::uint8_t bodyPart, Shared::Game::DamageReason reason);

        /** One blow another player landed, as the gamemode is shown it before the referee rules. */
        struct PlayerHitProposal {
            float damage                      = 0.0F;
            float stamina                     = 0.0F;
            std::uint8_t bodyPart             = 0;
            Shared::Game::DamageReason reason = Shared::Game::DamageReason::Unknown;
            bool missile                      = false;
            std::uint8_t flags                = 0;

            /** What the resolving engine took; `damage` is the server's own price where it has one. */
            float engineDamage = 0.0F;

            /** Whether the server priced the blow, and the steps of the price. */
            bool priced = false;
            std::array<float, 3> attack {};
            std::array<float, 3> armor {};
            float block               = 0.0F;
            float bodyPartCoefficient = 1.0F;

            /** The attacker's weapon as 32 hex digits, empty for a bare hand. */
            std::string weapon;

            /** In: 1 throughout. Out: what the handlers scaled the price by. */
            Shared::Game::CombatPricingScales modifiers;

            /** Out: a handler set `damage` itself, which then stands as set. */
            bool damageSet = false;
        };

        // A blow another player landed, accepted by the combat referee and
        // not yet taken. A handler may refuse it (literal false) or change
        // `hit.damage`, which `hit` carries back. False when refused.
        bool PlayerHit(std::uint64_t playerNetworkId, std::uint64_t attackerNetworkId, PlayerHitProposal &hit);

        // A limb injury appeared on, or left, a player's body. Derived from
        // the buff report the client already sends; no wire of its own.
        void PlayerInjured(std::uint64_t playerNetworkId, std::uint8_t bodyPart);
        void PlayerInjuryHealed(std::uint64_t playerNetworkId, std::uint8_t bodyPart);

        // Dispatched inside the request, so player.spawn() from a handler
        // names the placement rather than chasing one already sent.
        void PlayerSpawning(std::uint64_t playerNetworkId);

        // Raised off the spawn barrier: the body is standing where the spawn
        // put it, with real ground under it.
        void PlayerSpawned(std::uint64_t playerNetworkId);

        // The player's own client says they can play: the game took its
        // loading screen down. Once per connection, after PlayerSpawned.
        void PlayerReady(std::uint64_t playerNetworkId);

        // A player's own game picked up another player's body, before the
        // server accepts it. False when a handler refused it.
        bool PlayerCarrying(std::uint64_t carrierNetworkId, std::uint64_t carriedNetworkId);

        // The server accepted that carry; everybody else now sees it.
        void PlayerCarried(std::uint64_t carrierNetworkId, std::uint64_t carriedNetworkId);

        // The carry is over, however it ended.
        void PlayerPutDown(std::uint64_t carrierNetworkId, std::uint64_t carriedNetworkId);

        // A player's own game took an item into their arms, before the server
        // accepts it. False when a handler refused it.
        bool PlayerCarryingItem(std::uint64_t playerNetworkId, const Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId);

        // The server accepted that item carry; everybody else now sees it.
        void PlayerCarriedItem(std::uint64_t playerNetworkId, const Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId);

        // The item carry is over, however it ended.
        void PlayerPutDownItem(std::uint64_t playerNetworkId, const Shared::RPC::ItemClassId &itemClass, std::uint64_t groundItemNetworkId);

        // The animation a `playAnimation` request made the player play has
        // ended, once per request; `fragment` is the request's.
        void PlayerAnimationEnded(std::uint64_t playerNetworkId, const std::string &fragment, Shared::Game::AnimationEndReason reason);
    } // namespace Events

    /**
     * The `hit` every playerHit handler is given.
     *
     * Each resource runs in an isolate of its own and its handlers are handed
     * a copy of the event's arguments, so the object a handler sets `damage`
     * or `modifiers` on is never the one the server built. Both are therefore
     * accessors onto the one proposal the server is ruling on, and `hit`
     * crosses into a resource as a handle to it rather than as its values:
     * every handler, in whichever resource, edits that proposal and sees what
     * the handlers before it left.
     *
     * A handle is live for one dispatch. A `hit` kept past it reads `damage`
     * and `modifiers` as undefined and changes nothing, and one copied after
     * it arrives as null.
     */
    class PlayerHitHandles final {
      public:
        static PlayerHitHandles &Get();

        // Makes `hit` reachable for as long as this lives: one dispatch.
        class Scope final {
          public:
            explicit Scope(Events::PlayerHitProposal &hit);
            ~Scope();

            Scope(const Scope &)            = delete;
            Scope &operator=(const Scope &) = delete;

            std::uint32_t GetHandle() const {
                return _handle;
            }

          private:
            std::uint32_t _handle;
        };

        // The `hit` object for a live handle, built in `isolate`, or JS null
        // once the handle is gone. Empty, with an exception pending, when the
        // isolate cannot build it.
        v8::MaybeLocal<v8::Value> Wrap(v8::Isolate *isolate, v8::Local<v8::Context> context, std::uint32_t handle);

        // The handle a `hit` stands for, or nothing for any other object.
        // Asked of every object with an internal field that crosses into a
        // resource, so it only reads.
        static std::optional<std::uint32_t> HandleOf(v8::Isolate *isolate, v8::Local<v8::Object> object);

        // Forgets an isolate's templates: a resource's as its runtime is
        // disposed, and every one as the server shuts down, since the host
        // runtime goes without telling anybody.
        void UnregisterIsolate(v8::Isolate *isolate);
        void Clear();

        // Teaches the value transfer to carry a `hit` into another resource
        // as its handle.
        static void RegisterTransfer();

      private:
        // One isolate's templates, built on its first blow. Eternal, so that
        // letting go of them never touches an isolate that may be gone.
        struct Templates {
            v8::Eternal<v8::ObjectTemplate> hit;
            v8::Eternal<v8::ObjectTemplate> modifiers;
        };

        Events::PlayerHitProposal *Find(std::uint32_t handle) const;
        Templates &TemplatesFor(v8::Isolate *isolate);

        // The live proposal an accessor's receiver stands for: a `hit`, or
        // the `modifiers` read off one. Null for another receiver, or for a
        // dispatch that is over.
        static Events::PlayerHitProposal *Receiver(const v8::FunctionCallbackInfo<v8::Value> &info);
        static Events::PlayerHitProposal *ModifiersReceiver(const v8::FunctionCallbackInfo<v8::Value> &info);

        static void GetDamage(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void SetDamage(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void GetModifiers(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void SetModifiers(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void GetModifier(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void SetModifier(const v8::FunctionCallbackInfo<v8::Value> &info);

        std::unordered_map<std::uint32_t, Events::PlayerHitProposal *> _live;
        std::uint32_t _nextHandle = 0;
        std::unordered_map<v8::Isolate *, Templates> _templates;
    };
} // namespace KCDC::Scripting

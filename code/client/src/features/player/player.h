#pragma once

#include "features/player/player_nametag_policy.h"

#include "shared/features/player/player_appearance.h"
#include "shared/features/player/player_combat_attack.h"
#include "shared/features/player/player_disguise.h"
#include "shared/features/player/player_entity.h"
#include "shared/features/player/player_respawn.h"
#include "shared/features/player/player_set_resource.h"
#include "shared/features/player/player_soul_query.h"
#include "shared/features/player/station_tools.h"

#include "common/replication/body_life.h"
#include "common/replication/pose_snapshot.h"
#include "common/spawn/actor_spawner.h"
#include "sdk/cryengine/action/i_actor.h"
#include "sdk/cryengine/action/i_action_controller.h"
#include "sdk/cryengine/action/i_actor.h"
#include "sdk/cryengine/action/i_mannequin_action.h"
#include "sdk/cryengine/animation/i_character_instance.h"
#include "sdk/cryengine/animation/i_attachment_object.h"
#include "sdk/cryengine/entitysystem/i_entity.h"
#include "sdk/cryengine/physics/pe_player_dimensions.h"
#include "sdk/cryengine/system/xml_node_ref.h"
#include "sdk/types.h"
#include "sdk/wh/combatmodule/c_combat_actor.h"
#include "sdk/wh/entitymodule/c_actor.h"
#include "sdk/wh/entitymodule/c_actor_model.h"
#include "sdk/wh/entitymodule/c_actor_movement_controller.h"
#include "sdk/wh/entitymodule/c_human.h"
#include "sdk/wh/entitymodule/c_item.h"
#include "sdk/wh/entitymodule/c_player.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/c_soul_state_storage.h"
#include "sdk/wh/rpgmodule/s_soul_query_context.h"

#include <mafianet/replica_manager3.h>
#include <mafianet/types.h>

#include <function2/function2.hpp>
#include <glm/glm.hpp>

#include <array>
#include <atomic>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace KCDC::Features::Player {
    // Captures the local player's state or presents a remote player's native
    // body.
    class Human final
        : public Shared::Entities::HumanEntity
        , public KCDC::Common::Replication::IReplicatedLife {
      public:
        bool HasStationPose() const;
        ~Human() override;

        // --- Replica3 ---------------------------------------------------------

        void OnConstructed() override;
        void OnDeserialized(bool transformUpdated) override;
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        // --- This body --------------------------------------------------------

        /** Capture if we own it, present it if we do not. */
        void Frame();
        void ReleaseHeldDecoy();

        /**
         * A smithing step this body replays raised a tool event: the
         * smithery's own hammer or tongs goes into the hand the native puts it
         * in, or back where it rests. Ignored for an action this body does not
         * replay. Main thread.
         */
        void ApplyStationToolEvent(const SDK::IAction *action, Shared::Game::StationToolEvent event);

        /** Game-thread application of a server permission for this body. */
        void Respawn(const Shared::RPC::RespawnPlayer &permission);
        Shared::RPC::PlayerDied CaptureDeathReport();
        void NativeRevived();

        // --- Life (IReplicatedLife) --------------------------------------------
        //
        // Only a copy is bound to the shared layer: the owner's body is the
        // local player, whose own soul and `LocalEvents` already decide and
        // report its life -- and in PvP it must keep the blows a remote
        // attacker's replayed swing lands, which the shared filter would drop.
        // A copy's writes never reach that filter either: the snapshot
        // mirror replays its owner's numbers first. What the layer gives a
        // copy is the one way every body dies and gets its ragdoll.

        std::uint64_t LifeNetworkId() const override {
            return const_cast<Human *>(this)->GetNetworkID();
        }

        bool DecidesOwnLife() const override {
            return false;
        }

        /** PvP is resolved by the defender's own client, so nothing is handed on. */
        void OnLocalBlow(float, std::uint8_t) override {}

        void OnOwnDamage(std::uint64_t, float, std::uint8_t) override {}

        /** Game-thread application of a server heal: raises the local body's health, never lowers it. */
        void Heal(float health);
        void SetResource(const Shared::RPC::SetPlayerResource &request);

        SDK::EntityId GetEntityId() const {
            return _entityId;
        }

        /** The live actor behind this replica, or null across level loads. */
        SDK::wh::entitymodule::C_Actor *ResolveActor() const;

        /** This player's body on this machine: the local player for the owner, whose replica builds none, and the copy for anyone else. */
        SDK::wh::entitymodule::C_Actor *ResolveBody() const;
        SDK::EntityId ResolveBodyEntityId() const;

        /** The replica of the player sitting at this machine, or nullptr before the session has one. */
        static Human *FindLocal();

        /**
         * The moment, on this machine's clock, this remote body was last drawn
         * at -- its pose buffer's presentation clock -- or zero while nothing
         * has been drawn. Events about the body are played once it reaches
         * them, so they happen where the body is.
         */
        MafiaNet::Time PresentedPoseTime() const {
            return _presentedPoseTime;
        }
        SDK::wh::entitymodule::C_Player *GetPlayer() const {
            return _nativePlayer;
        }

        /** The engine is releasing this replica's body; see NativePlayerModule::SetRemoteReleasedListener. */
        void ForgetNativePlayer() {
            _nativePlayer = nullptr;
        }

        // --- Nametag ----------------------------------------------------------

        /**
         * What the nametag pass remembers about this body between frames.
         *
         * The pass rations its occlusion rays, so a body is not necessarily
         * re-traced on the frame it is drawn. `clear` only authorises a tag
         * while `checkedAt` is recent, which is what keeps that safe.
         */
        NametagVisibility nametagVisibility {};

        // --- Soul -------------------------------------------------------------

        // Records or replays attacker-soul queries around one native
        // calculation. Replay restores resources without notifications.
        // Scopes must unwind in reverse order.
        class SoulActionScope {
          public:
            SoulActionScope(Shared::Game::HumanSoulAction &action, SDK::wh::rpgmodule::I_Soul *source, SDK::wh::rpgmodule::I_Soul *target, const void *weapon, const void *ammo, const void *defense, bool replay);
            ~SoulActionScope();
            SoulActionScope(const SoulActionScope &)            = delete;
            SoulActionScope &operator=(const SoulActionScope &) = delete;

            /** False once the record stopped describing the calculation. */
            bool Good() const {
                return _good;
            }

          private:
            friend class Human;

            // Index order is HumanSoulQuery's reference roles: none, attacker,
            // victim, weapon, ammo, defense item.
            static constexpr std::size_t kReferenceCount = 6;

            Shared::Game::HumanSoulAction &_action;
            SDK::wh::rpgmodule::I_Soul *_source;
            std::array<const void *, kReferenceCount> _references;
            std::array<float, Shared::Game::HumanSoulState::kResourceCount> _savedResources {};
            SoulActionScope *_previous;
            bool _replay;
            bool _good = true;
        };

        static bool ReplaySoulQuery(const SDK::wh::rpgmodule::I_Soul *source, Shared::Game::HumanSoulQueryKind kind, std::uint32_t selector, float base, const SDK::wh::rpgmodule::S_SoulQueryContext *context, float &value);
        static void CaptureSoulQuery(const SDK::wh::rpgmodule::I_Soul *source, Shared::Game::HumanSoulQueryKind kind, std::uint32_t selector, float base, const SDK::wh::rpgmodule::S_SoulQueryContext *context, float value);

        // Finds a remote player's body by native soul. Rejects stale bindings; safe
        // for concurrent query readers.
        static Human *FindRemoteSoul(const SDK::wh::rpgmodule::I_Soul *soul);

        // Finds a remote player's body by its soul's WUID. A walk of the
        // binding table, for event-rate callers (damage, death) on the game
        // thread; the soul hooks use FindRemoteSoul.
        static Human *FindRemoteSoulWuid(std::uint64_t soulWuid);

        // The same lookup by the soul's WUID, which is how a damage effect
        // names its attacker. Null for the local body and for any soul that is
        // not a bound player copy. Game thread only.

        // The owned body whose snapshot describes the local player's soul,
        // or null while none does. Game thread only.
        static Human *FindCapturedLocal();

        static bool TryGetRemoteState(const SDK::wh::rpgmodule::C_SoulStateStorage18070F3E8 *storage, std::uint32_t state, float &value);

        /** Whether this copy is bound and wearing the published snapshot. */
        bool HasAppliedSoulState() const;

        /** Whether the local player's body is the one the snapshot describes. */
        bool HasCapturedLocalSoul() const;

        const Shared::Game::HumanSoulState &AppliedSoulState() const {
            return _appliedSoul;
        }

        /** True while ApplySoulState is writing, so its own writes pass. */
        bool ApplyingSoulState() const {
            return _applyingSoul;
        }

        /** The replicated answer to a context-free soul query, if we have one. */
        bool TryGetSoulQuery(Shared::Game::HumanSoulQueryKind kind, std::uint32_t selector, float &value) const;

        // --- Held items -------------------------------------------------------

        /**
         * Moves the receiver-local held-item shadow into world inventory for
         * the replicated drop that retires it.
         */
        SDK::wh::entitymodule::C_Item *ReleaseHeldShadowForGroundDrop(const Shared::RPC::ItemClassId &droppedClass, bool retiresHeldItem);

        /** Which hand's shadow holds this class, or kNoHand for neither. */
        std::size_t FindHandShadow(const Shared::RPC::ItemClassId &itemClass) const {
            const auto holds = [&](std::size_t hand) {
                return itemClass == _shadowHandClass[hand] && _shadowHandWuid[hand] != 0;
            };
            if (!itemClass.IsValid()) {
                return kNoHand;
            }
            if (holds(kRightHand)) {
                return kRightHand;
            }
            return holds(kLeftHand) ? kLeftHand : kNoHand;
        }

        bool HasHeldShadow(const Shared::RPC::ItemClassId &itemClass) const {
            return FindHandShadow(itemClass) != kNoHand;
        }

        // --- Combat -----------------------------------------------------------

        enum class RemoteAttackResult : std::uint8_t {
            Applied,
            Deferred,
            Obsolete,
        };

        /** Applies one exact accepted attack edge, or asks the stream to retry it. */
        RemoteAttackResult PerformRemoteAttack(const Shared::RPC::CombatAttack &attack);

        /** Retires a held generation whose ordered terminal can no longer run. */
        void CancelRemoteAttackGeneration(std::uint32_t attackGeneration);
        /** Server cancellation also stops a held Press that has not opened a swing yet. */
        void CancelRemoteCombatAttack();

        bool KeepsCombatStance() const {
            return fistsUp && !_combatPresentationSuppressed;
        }

        /** The engine took this copy's fists down; allow the durable state to reassert it. */
        void NoteStanceLost() {
            _stanceRequested = false;
        }

        // --- Ledges -----------------------------------------------------------

        // Runs the native vault query at the copy's pose. The caller may
        // retry if pose replication has not reached the ledge.
        bool PerformRemoteLedgeGrab(bool jumpRequested, bool crouchRequested);

        // --- Riding -----------------------------------------------------------

        /**
         * Seats this body on `horseEntityId`, or unseats it when 0. Idempotent.
         *
         * `playGetOn` is a mount this machine watched -- the horse's seat
         * filling while it stood here -- that was asked for as a get-on: it
         * plays the game's own get-on, as the owner did, provided this body
         * was here on foot to start it. Anything else is taken straight into
         * the saddle: an instant seat, or a ride this machine joins already
         * under way, whose get-on happened before it arrived.
         */
        void ApplyRide(SDK::EntityId horseEntityId, bool playGetOn);

        // Unseats immediately before horse destruction; animated dismount
        // requires the horse to remain alive.
        void AbandonRide();

        /**
         * Ends this body's ride with the game's forced get-off -- Lua's
         * `ForceDismount` -- for the cases a get-off cannot be played: a dead
         * body, both flanks blocked, a horse that is going away. Asked once;
         * the seat goes in the rider action's Exit a queue update later.
         */
        void ForceOffSaddle(SDK::wh::entitymodule::C_Actor *actor);

        // --- Carrying ---------------------------------------------------------

        /**
         * Puts the body the server says this player carries on this copy's
         * shoulder, or takes it off, through the same carry action the owner's
         * game ran. The carried body is whichever copy of that player this
         * machine has -- a copy, or the local player's own.
         */
        void ApplyCarry(SDK::wh::entitymodule::C_Actor *actor);

        /**
         * Has this copy carry the item the server says this player carries,
         * or put it down, through the same carry-item action the owner's game
         * ran -- with an item this machine makes for it, since a carried item
         * belongs to nobody's inventory.
         */
        void ApplyCarriedItem(SDK::wh::entitymodule::C_Actor *actor);

        // --- Speech -----------------------------------------------------------

        /** Silent for the local player, which drives the game's own actor. */
        void Talk(const std::string &text);

        // --- Appearance -------------------------------------------------------

        /**
         * Adopts a server-chosen appearance on the body this client owns.
         * Published only once this body is wearing it: publishing one the
         * owner cannot wear leaves everyone else drawing a body its owner
         * does not see.
         */
        bool AdoptAppearance(const Shared::Game::CharacterAppearance &wanted);

        /** One script property a body of a gender is given. */
        struct BodyProperty {
            const char *key;
            const char *value;
        };

        /**
         * The Properties that stand a body on its gender's skeleton: the
         * model pair `ReadActorScriptProperties` (0x180AE5C68) records the
         * model from, and the clothing config -- the underwear --
         * `RefreshClothingSupport` reads live. The skeleton decides the bind
         * pose every skin is built against; a woman's skin on `male.chr`
         * bends around joints a few centimetres off.
         */
        static std::array<BodyProperty, 3> BodyProperties(Shared::Game::AppearanceGender gender);

        /**
         * Marks a body as a woman's for Mannequin, or clears the mark.
         *
         * Every human body runs `kcd_male_database.adb`, which carries every
         * fragment of `wh_female_database.adb` with the `female` tag added to
         * each of its variants (player_female_body.cpp), so a body with
         * the tag plays a woman's fragment wherever the game made one and the
         * man's only where it made none -- riding, weapon draws, combat,
         * deaths. A body's controller is marked as it is built
         * (`MarkWomansController`); this is for a body that changes skeleton
         * under a controller that stays.
         */
        static void ApplyGenderTag(SDK::wh::entitymodule::C_Actor *actor, Shared::Game::AppearanceGender gender);

        /** After `FemaleBody::AdoptHumanMannequin`: the controller just built for a woman's body carries the `female` tag. */
        static void MarkWomansController(SDK::IActionController *controller);

        // --- Disguise ---------------------------------------------------------

        /**
         * Adopts a server-chosen disguise on the body this client owns and
         * publishes it. Unlike an appearance nothing can refuse it here: the
         * mesh was checked against the catalog on the way in, and a body can
         * always stop rendering.
         */
        void AdoptDisguise(const Shared::Game::PlayerDisguise &wanted);

        // --- Visibility -------------------------------------------------------

        /**
         * Adopts the server's answer to whether the body this client owns is
         * drawn at all, and publishes it. Nothing can refuse it: a body can
         * always stop drawing.
         */
        void AdoptVisibility(bool visible);

      private:
        void SuspendBodyActions(SDK::wh::entitymodule::C_Actor *actor);
        bool PlaceAtStation(SDK::IEntity *entity) const;
        // --- Owner capture ----------------------------------------------------

        void CaptureLocalState();
        void CaptureSoulState();
        void CaptureLocomotionTags();
        void CaptureHeadLook(SDK::wh::entitymodule::C_Actor *actor);
        void CaptureCombat();

        // Publishes ladder EntityGUID and climb input. Ladders without a
        // stable GUID are not replicated.
        void CaptureLadder();

        // Publishes the battlement or window the local player leans out of,
        // and the slot. One without a stable GUID is not replicated.
        void CaptureLean();

        /**
         * Publishes the body's stance off its own actor model, and the last
         * body action the local player's hooks caught, if one is new.
         */
        void CaptureBodyActions(SDK::wh::entitymodule::C_Actor *actor);
        void CaptureStation(SDK::wh::entitymodule::C_Actor *actor);

        /** At a grindstone: states the wheel as the station and samples the blade and wheel into `sharpening`. */
        void CaptureSharpening(SDK::wh::entitymodule::C_Actor *actor, Shared::Game::StationState &captured);

        static std::string CaptureWornEquipment();

        /** The same slots as one value, for deciding whether to rebuild it. */
        static std::uint64_t FingerprintWornEquipment();

        // --- Presentation -----------------------------------------------------

        void PresentRemoteState();
        /** Resolves the body once, when it is spawned; released bodies are forgotten by event. */
        void BindNativePlayer();
        /**
         * Asks the spawner for a body, at most one request in flight. The body
         * arrives in `OnSpawned` on a later frame.
         */
        void RequestSpawn();
        void OnSpawned(KCDC::Common::Spawn::SpawnTicket &ticket, bool spawned);

        /** Cancels a request that has not run, or forgets one that has. */
        void ReleaseSpawnTicket();
        void Despawn();

        /** How far ApplyLook got, and whether asking again could do better. */
        enum class LookResult {
            Applied,
            /** Not this frame, but nothing says it cannot be the next one. */
            Retry,
            /** The engine refused; retrying would fail the same way. */
            Refused,
        };
        LookResult ApplyLook();

        void ApplyOutfit(SDK::wh::entitymodule::C_Actor *actor);

        /**
         * Writes the replicated appearance into a soul's look-as layer, part
         * by part, writing only what changed. False when the soul cannot
         * wear it at all -- a gender its archetype has no counterpart for,
         * or a beard nobody modelled for the face it would sit on.
         */
        bool ApplyAppearance(SDK::wh::rpgmodule::I_Soul *soul);

        /**
         * The head this appearance will leave the soul wearing: the one it
         * asks for, or the one already resolving when it asks for none. Only
         * answerable here -- this client's body is Henry, a copy's is
         * whatever its blank soul falls back to.
         */
        std::string EffectiveHead(const SDK::wh::rpgmodule::I_Soul *soul) const;

        static bool SoulWearsGender(const SDK::wh::rpgmodule::I_Soul *soul, Shared::Game::AppearanceGender gender);

        /**
         * The archetype row that is this one with the other gender, or -1.
         * A table because the game pairs its human archetypes by hand.
         */
        static int PairedGenderArchetype(int archetypeId, Shared::Game::AppearanceGender wanted);

        /**
         * Brings `entity` -- this body's own, the local player's for the owner
         * and the copy everywhere else -- in line with `disguise`, and holds
         * it there. Every frame, because the engine undoes both halves on its
         * own: re-dressing rebuilds the character in slot 0 drawing again,
         * and a stance change re-states the collider.
         */
        void ApplyDisguise(SDK::IEntity *entity);

        /** Puts `entity` back in its own body: the character draws again, the mesh goes, and the collider it had returns. */
        void WithdrawDisguise(SDK::IEntity *entity);

        /** Drops what ApplyDisguise holds without touching an entity, for a body that is already gone. */
        void ForgetDisguise();

        /**
         * Stops the character drawing, keeping it animated; see
         * `CS_FLAG_DRAW_MODEL` and `CS_FLAG_UPDATE_ALWAYS`.
         *
         * Two features ask for this and either one holds it: a disguise, which
         * draws a mesh in the body's place, and a hidden body, which draws
         * nothing. The caller states its own reason before asking.
         */
        void SuppressCharacterDraw(SDK::IEntity *entity);

        /** Lets the character draw again, once neither the disguise nor the hide holds it, and hands back only what was taken. */
        void ReleaseCharacterDraw(SDK::IEntity *entity);

        /**
         * Brings `entity` in line with `bodyHidden` and holds it there, beside
         * `ApplyDisguise` and for the same reason: the engine puts a body's
         * drawing back on its own, and a weapon drawn or holstered while
         * hidden arrives visible.
         *
         * The body is three things to hide. The character gate takes the body
         * and everything the character draws with it -- clothes, armour, the
         * blades sheathed on it. `IActor::HideAllAttachments` is the game's
         * own call for what the body carries, and the one `C_Human` makes for
         * itself when an entity is hidden. Neither reaches a socket holding an
         * *entity*, which KCD2's holsters and hands do (`CharHolsterItem` and
         * pickable items), so those are taken out of their own slots' render
         * flag. A disguise mesh rides a slot of this entity too, and goes the
         * same way.
         */
        void ApplyBodyVisibility(SDK::IEntity *entity, SDK::wh::entitymodule::C_Human *human);

        /** Takes the render flag off the disguise mesh's slot, which the character gate does not cover, and hands it back. */
        void HoldDisguiseSlotHidden(SDK::IEntity *entity);
        void ShowDisguiseSlot(SDK::IEntity *entity);

        /**
         * Takes the render flag off every slot of every entity this body
         * carries, and gives it back to anything that has since left the body
         * -- a torch dropped while hidden is nobody's secret.
         */
        void HoldCarriedEntitiesHidden(SDK::IEntity *entity);

        /** Hands back every slot `HoldCarriedEntitiesHidden` took, for a body that is drawing again or gone. */
        void ShowCarriedEntities();

        /**
         * Sizes the living entity's cylinder around the mesh, from the mesh's
         * own bounds. False until the mesh is in, or while the physics has no
         * dimensions to read.
         */
        bool FitDisguiseCollider(SDK::IEntity *entity);

        // Rebinds Mannequin Char3P when the native character changes; copy
        // initialization may occur before the model exists.
        void RefreshAnimationBinding(SDK::wh::entitymodule::C_Actor *actor);

        // --- Soul binding -----------------------------------------------------

        bool ApplySoulState(SDK::wh::entitymodule::C_Actor *actor);

        void ApplyDeathState(SDK::wh::entitymodule::C_Actor *actor);

        /** Claims a registry slot for this native soul. False when full. */
        bool BindSoul(SDK::wh::rpgmodule::I_Soul *nativeSoul);
        void UnbindSoul();

        // Checks binding identity without retiring stale bindings.
        bool IsSoulBindingLive() const;

        static Shared::Game::HumanSoulQuery MakeSoulQuery(Shared::Game::HumanSoulQueryKind kind, std::uint32_t selector, float base, const SDK::wh::rpgmodule::S_SoulQueryContext *context);

        /** Logs a replay miss beside the near misses the attacker recorded. */
        static void ReportMissingSoulQuery(const Shared::Game::HumanSoulQuery &wanted);

        // --- Applying to the copy -------------------------------------------

        // Applies replicated guard and aim. During full-body interactions,
        // reports whether the native stance has lowered.
        bool ApplyCombat(SDK::wh::entitymodule::C_Actor *actor, bool suppress);

        // Applies ladder state and climb input. Returns true while ladder
        // animation owns the body. The incoming position anchors dismount.
        bool ApplyLadder(SDK::wh::entitymodule::C_Actor *actor, const glm::vec3 &position);

        // Starts the engine's own lean at the owner's slot and sends the
        // copy back out when the owner leaves it. Returns true while the
        // lean owns the body.
        bool ApplyLean(SDK::wh::entitymodule::C_Actor *actor);

        /**
         * How fast this body's position correction may move it, in m/s: held
         * inside the travel speeds of the locomotion set its owner's input
         * plays (Shared::Game::CorrectionSpeedLimit), measured against the
         * travel speed its animated character asked of the blendspace.
         * `lerpSpeed` is what the correction wants, for the debug report of a
         * cap that bit.
         */
        float RemoteCorrectionSpeedLimit(SDK::wh::entitymodule::C_Actor *actor, const Shared::Entities::PlayerInput &input, float errorLength, float snapDistance, float lerpSpeed);

        /**
         * Plays the owner's new body action and puts the copy's actor model
         * in the owner's stance, whose idle the copy then plays by itself.
         * True while either owns the body, when locomotion must stand aside.
         */
        bool ApplyBodyActions(SDK::wh::entitymodule::C_Actor *actor);
        void ApplyHeldDecoy(SDK::wh::entitymodule::C_Actor *actor);

        /** Ends whatever body action is playing and starts the new one. */
        void PlayBodyAction(SDK::wh::entitymodule::C_Actor *actor);

        /** Queues the owner's fragment on this body's own controller. */
        void PlayBodyActionFragment(SDK::wh::entitymodule::C_Actor *actor);

        /** Has this client's copy of the praised dog play the pair with this body. */
        void PlayPraise(SDK::wh::entitymodule::C_Actor *actor);

        /**
         * Reads the way the game's NPCs read: their book in the right hand,
         * then ReadingBookIn and the ReadingBook loop, and ReadingBookOut
         * once the owner closes it. True while the reading owns the body.
         */
        bool ApplyReading(SDK::wh::entitymodule::C_Actor *actor);

        /** Takes the reading book back off the body and out of its inventory. */
        void DropReadingBook(SDK::wh::entitymodule::C_Actor *actor);

        /**
         * Queues each minigame action the owner's body has queued since, in
         * its order, and ends each one it has ended, once this body is drawn
         * where its owner is. True while one of them owns the body.
         */
        bool ApplyMinigameActions(SDK::wh::entitymodule::C_Actor *actor);

        /**
         * Puts the smithery tool this body holds in `hand` back where it was
         * taken from, with its physics as it was.
         */
        void ReleaseStationTool(std::size_t hand);
        void ReleaseStationTools();

        /**
         * While this body replays a grindstone's steps, writes the owner's
         * blade coordinates into its blend and turns the wheel as the owner's
         * game turns it; otherwise lets go of a wheel it was turning.
         */
        void ApplySharpening(SDK::wh::entitymodule::C_Actor *actor, bool replaying);

        /** Stops turning the wheel this body turned, as the native leaves it: still. */
        void ReleaseGrindstone();

        /**
         * Drops what ApplyBodyActions holds, for a body that is gone. Nothing
         * is written through the old body; releasing the action's reference is
         * safe whether or not the engine still has it.
         */
        void ForgetBodyActions();

        /** Ends any replicated held attack through the native AbortAttack state. */
        void CancelRemoteAttack(SDK::wh::combatmodule::C_CombatActor *combatActor);

        /** Brings each hand's replicated item out, or puts it away. */
        void ApplyHandItems(SDK::wh::entitymodule::C_Human *human);
        void ApplyHandItem(SDK::wh::entitymodule::C_Human *human, std::size_t hand);
        void ResetHandState(std::size_t hand);

        // Queues one native weapon draw or holster action covering both
        // hands; a body an animation holds still puts its set away at once
        // instead.
        bool IssueWeaponAction(SDK::wh::entitymodule::C_Human *human, std::size_t hand, SDK::wh::entitymodule::C_Item *item, bool wantDrawn);

        /** Fills a pose's look pitch from the replicated head look; level before any look has arrived. */
        void SampleLookPitch(KCDC::Common::Replication::PoseSnapshot &pose) const;

        /** Feeds interpolated network movement into the native player controller. */
        void ApplyNetworkMovement(const KCDC::Common::Replication::PoseSnapshot &pose, bool riding = false);

        /** Hands the owner's gallop, spur, brake and walk toggle to this body's rider input. */
        void ReplayRiderActions(SDK::wh::entitymodule::C_Player *player, const KCDC::Common::Replication::PoseSnapshot &pose);

        /** Releases interpolated movement while a native action owns the body. */
        void WithdrawLocomotion();

        /** Whether `player`'s living physics is off the ground. */
        static bool IsBodyAirborne(SDK::wh::entitymodule::C_Player *player);

        // Tests the horse's accepted seat state, not the broader lifetime of
        // a queued ride action -- or a stay in a cart, from the walk to the
        // seat to the climb down, since the cart action moves the body itself
        // for all of it.
        bool IsSeated() const;

        // --- Soul binding registry --------------------------------------------

        // An open-addressed table from native soul to the replica that owns
        // it, read lock-free from the soul hooks on every thread and written
        // only on the game thread. The hooks run for every soul in the world,
        // so a miss -- every NPC -- must cost one probe, not a scan.
        //
        // A writer publishes the owner, then the soul with release ordering,
        // and retires a slot by writing RetiredSoul() over the soul, which
        // keeps later entries of the same probe chain reachable; an insert
        // reuses the first retired slot it passes. Readers acquire the soul
        // before reading the owner and confirm the owner still holds that
        // soul, so a slot reused under them never answers for another body.
        // Only the game thread retires: a reader on another thread that finds
        // a stale binding declines it and leaves it be.
        struct SoulBinding {
            std::atomic<const SDK::wh::rpgmodule::I_Soul *> soul {nullptr};
            std::atomic<Human *> human {nullptr};
        };

        // A power of two, with room for every body a client can stream many
        // times over, so probe chains stay a slot or two long.
        static constexpr std::size_t kSoulTableSize = 4096;
        static std::array<SoulBinding, kSoulTableSize> _soulBindings;

        // Bindings currently published; zero lets a lookup return at once.
        static std::atomic<std::size_t> _soulBindingCount;

        /** The marker a retired slot holds; never the address of a soul. */
        static const SDK::wh::rpgmodule::I_Soul *RetiredSoul() {
            return reinterpret_cast<const SDK::wh::rpgmodule::I_Soul *>(alignof(std::max_align_t));
        }

        /** The first slot of `soul`'s probe chain. */
        static std::size_t SoulSlot(const SDK::wh::rpgmodule::I_Soul *soul) {
            const auto bits = reinterpret_cast<std::uintptr_t>(soul) >> 4;
            return static_cast<std::size_t>((bits * 0x9E3779B97F4A7C15ULL) >> 52) & (kSoulTableSize - 1);
        }

        // The capture or replay in progress on this thread. Thread local
        // because the hooks reach it from whichever thread ran the calculation.
        static thread_local SoulActionScope *_soulAction;

        // --- State ------------------------------------------------------------

        SDK::EntityId _entityId                        = 0;
        SDK::wh::entitymodule::C_Player *_nativePlayer = nullptr;

        // The owner's jump count this body last started a jump for. A new one
        // stays pending until the body leaves the ground, so a jump taken the
        // moment the owner landed waits out this body's own landing.
        std::uint8_t _appliedJumpCount   = 0;
        bool _jumpCountKnown             = false;

        // The owner's sprint presses in the saddle this body's rider input
        // was last handed, and, on the owner, the press stamp last counted.
        std::uint8_t _appliedSprintPresses   = 0;
        std::uint8_t _appliedSlowdownPresses = 0;
        bool _riderPressesKnown              = false;
        float _capturedSprintPressTime     = 0.0F;
        bool _wasJumping                 = false;
        MafiaNet::Time _jumpPendingUntil = 0;
        // A replayed jump whose takeoff has not been given the owner's velocity yet.
        bool _jumpLaunchPending = false;

        // The position correction stored this frame and in the three
        // before it, newest first; see PresentRemoteState.
        std::array<glm::vec2, 4> _correctionSteps {};
        // When a capped correction was last reported, so the report is
        // rate-limited per body.
        MafiaNet::Time _correctionReportedAt = 0;

        // When a refused crouch toggle or block press may be replayed again,
        // and when this body was last presented, for frame-rate independent
        // easing.
        MafiaNet::Time _nextGuardPressAt    = 0;
        MafiaNet::Time _nextStanceRequestAt = 0;
        MafiaNet::Time _lastPresentedAt     = 0;
        MafiaNet::Time _presentedPoseTime   = 0;

        bool _deathHandled                             = false;
        bool _deathStateDirty                          = false;

        // A forced get-off whose seat has not been released yet; see
        // ForceOffSaddle.
        bool _forcedOffSaddle = false;

        // The owner's falls already played on this body, known whenever the
        // jump count is; see fallSequence.
        std::uint8_t _appliedFallSequence = 0;

        // Until when a replayed fall still owns this body's position: its
        // ragdoll moves the body, then the gap it leaves is walked off rather
        // than snapped. Moved on every frame the ragdoll lasts.
        MafiaNet::Time _fallRecoveryUntil = 0;

        // Whether this body was put out cold for its owner's knockout.
        bool _knockedOut = false;

        // Whether the owner's head look is on the body as a look target -- in a
        // cart, in the saddle, on a seat or leaning out -- to be cleared once
        // none holds it.
        bool _lookTargetApplied = false;
        // The head's look target, eased toward the owner's look (PresentRemoteState).
        glm::vec3 _easedLook {0.0F};
        std::uint64_t _easedLookAt = 0;
        // Whether the body holds its owner's look without a looking clip of
        // its own -- in the saddle, seated or leaning out
        // (NativePlayerModule::SetLookHeld); let go on the frame that ends.
        bool _lookClipRequested = false;

        // Whether this body has stood here out of the saddle since it came
        // up: a mount can only have been watched by a machine that saw it
        // on foot first.
        bool _seenOnFoot = false;

        // Whether this body was in a saddle last frame, for the unseat edge.
        bool _wasSeated = false;

        // The horse the wire has this body riding, as ApplyRide last stated
        // it, whether or not the engine has seated the body on it yet.
        SDK::EntityId _wireHorseEntityId = 0;

        // Whether ApplyCarry has looked at this body yet, and whether a carry
        // was already under way when it first did: that one is joined on the
        // shoulder, without the pick-up clip the owner played long ago.
        bool _carrySeen       = false;
        bool _carryStreamedIn = false;

        // The body the last grab was asked for and when, so a grab still
        // queued is not asked for again before its action enters.
        SDK::EntityId _carryGrabAskedFor = 0;
        MafiaNet::Time _carryGrabAskedAt = 0;

        // When the last item carry was asked for, so one whose action has not
        // entered is not asked for again -- with another item -- straight away.
        MafiaNet::Time _itemCarryAskedAt = 0;

        // How many times the current carry has been asked for, and which it is.
        std::uint8_t _itemCarryAttempts = 0;
        Shared::RPC::ItemClassId _itemCarryAttemptClass;

        // Whether the running item carry has been asked to put its item down.
        bool _itemPutDownAsked = false;

        // What ApplyBodyActions last put this body's actor model in, and the
        // body action it last played. The first revision this body sees is
        // recorded rather than played: it happened before this client was
        // watching.
        Shared::Game::BodyStanceState _appliedBodyStance;
        std::uint32_t _appliedBodyActionRevision = 0;
        bool _bodyActionSeen                     = false;
        SDK::IAction *_bodyActionPlaying         = nullptr; // one reference held
        SDK::IAttachmentObject *_heldDecoy       = nullptr; // borrowed, owned by socket
        bool _decoyReleased                      = false;

        // How far ApplyReading has got: reading, or closing the book.
        enum class ReadingStage : std::uint8_t {
            None,
            Reading,
            Closing,
        };
        ReadingStage _readingStage     = ReadingStage::None;
        std::uint64_t _readingBookWuid = 0;
        SDK::IAction *_readingIntro    = nullptr; // ReadingBookIn; one reference held
        SDK::IAction *_readingAction   = nullptr; // the loop, then ReadingBookOut; one reference held

        // The owner's minigame actions this body has queued, with the one
        // reference held on each (none for a fragment its database lacks),
        // and since when a first one has waited for the body to arrive.
        std::vector<std::pair<Shared::Game::MinigameActions::Action, SDK::IAction *>> _minigamePlaying;
        MafiaNet::Time _minigameWaitingSince = 0;

        // A smithery tool this body holds for a replayed smithing step: the
        // smithery's own entity, carried on a socket of ours, with the world
        // transform and physics it had where it rested.
        struct StationToolHold {
            SDK::EntityId tool = 0;
            SDK::Matrix34 rest {};
            bool physics = false;
        };
        StationToolHold _stationTools[Shared::Entities::HumanEntity::kHandCount] {};

        // The grindstone this body turns for a replayed sharpening, and the
        // spinning clip it started on the wheel's layer 0 (-1 for none yet).
        SDK::EntityId _grindstone        = 0;
        std::int32_t _grindstoneClip     = -1;
        float _grindstoneSpeedMultiplier = 0.0F;

        // The owner's seat, as its model's WUID and the level EntityGUID that
        // WUID resolved to, so the lookup runs when the seat changes.
        std::uint64_t _capturedSeatWuid = SDK::wh::entitymodule::C_ActorModel::kNoStanceObject;
        std::uint64_t _capturedSeatGuid = 0;

        // Native soul identity includes actor, WUID, owner and epoch to
        // detect reused addresses.
        SoulBinding *_boundBinding             = nullptr;
        SDK::wh::rpgmodule::I_Soul *_boundSoul = nullptr;
        std::uint64_t _boundSoulWuid           = 0;
        std::uint8_t _boundEpoch               = 0;
        MafiaNet::PeerGuid _boundOwner         = MafiaNet::UNASSIGNED_PEER_GUID;

        // The owner's side of the same idea: which native body the published
        // snapshot was captured from.
        std::uint64_t _captureSoulWuid = 0;
        SDK::EntityId _captureActorId  = 0;

        Shared::Game::HumanSoulState _appliedSoul;
        bool _applyingSoul = false;

        // When the parts of the snapshot that raise no change signal are next
        // due to be read. See CaptureSoulState.
        MafiaNet::Time _soulResampleAt = 0;
        // A resource moved since the last snapshot went out; held while it is only drift.
        bool _soulChangePending = true;

        // Presentation identity. Revival may retain the native body across
        // incarnations.
        std::uint32_t _presentationIncarnation = 0;
        MafiaNet::PeerGuid _presentationOwner  = MafiaNet::UNASSIGNED_PEER_GUID;
        std::uint8_t _presentationEpoch        = 0;

        // Rebind only when the character changes; repeating the native
        // operation can clear its binding flag.
        SDK::ICharacterInstance *_boundCharacter = nullptr;

        // Cleared after replicated equipment is applied successfully.
        bool _equipmentDirty = false;
        // A weapon-slot change deferred behind a hand action or ground
        // transfer; ApplyOutfit retries it no sooner than _weaponRetryAt.
        bool _weaponsPending          = false;
        MafiaNet::Time _weaponRetryAt = 0;

        // What this module last wrote into the body's look-as layer, cleared
        // with the body because the layer went with the soul. Tracked rather
        // than asked: IsOverriddenBodyDescriptionPart cannot tell "not set"
        // from "not ours".
        Shared::Game::CharacterAppearance _appliedAppearance;

        // What the look-as layer holds part by part, which is the appearance
        // plus the defaults a soul worn as the other gender is given; the
        // write loop diffs against this rather than `_appliedAppearance`.
        std::array<std::string, Shared::Game::CharacterAppearance::kPartCount> _lookAsParts;

        // The gender the soul's own parts were authored for, read before the
        // first archetype change.
        Shared::Game::AppearanceGender _soulGender = Shared::Game::AppearanceGender::Male;
        bool _soulGenderKnown                      = false;
        bool _appearanceDirty                      = true;

        // A refusal belongs to (this body, this appearance), so it is retried
        // the moment either changes.
        bool _appearanceRefused = false;

        // What ApplyDisguise put on the entity it names. A different entity
        // is a different body, which starts undisguised.
        Shared::Game::PlayerDisguise _appliedDisguise;
        SDK::EntityId _disguiseEntityId = 0;

        // The slot the mesh went into, or -1 for none -- also after the
        // engine refused to load it, which is not retried for that mesh.
        int _disguiseSlot = -1;

        // The character the body stopped drawing and held animated, and what
        // it had of both before -- only what was taken is handed back.
        // Compared against the entity's live character before any use, never
        // dereferenced on its own.
        SDK::ICharacterInstance *_suppressedCharacter = nullptr;
        bool _suppressedCharacterDrew                 = false;
        bool _suppressedCharacterUpdatedAlways        = false;

        // Why it is not drawing. Either reason holds the gate shut, so the
        // one that lets go does not speak for the other.
        bool _drawHeldByDisguise = false;
        bool _drawHeldByHide     = false;

        // The cylinder the body had before the disguise, to hand back, and
        // the one the disguise asked for, to hold against stance changes.
        bool _ownDimensionsSaved      = false;
        bool _disguiseColliderFitted  = false;
        bool _disguiseColliderRefused = false;
        SDK::pe_player_dimensions _ownDimensions;
        SDK::pe_player_dimensions _disguiseDimensions;

        // One slot of one entity this body carries, which the hide stopped
        // drawing. Kept per slot rather than per entity so showing it again
        // gives back exactly what was taken: a slot that was already not
        // drawing is never recorded, which is also what keeps a second pass
        // from recording it twice.
        struct CarriedSlot {
            SDK::EntityId entityId = 0;
            int slot               = 0;
        };

        // What ApplyBodyVisibility holds on the entity it names, and when the
        // carried half was last re-stated. A different entity is a different
        // body, which starts drawn.
        bool _appliedBodyHidden         = false;
        SDK::EntityId _visibilityEntityId = 0;
        MafiaNet::Time _carriedHeldAt     = 0;
        std::vector<CarriedSlot> _hiddenCarriedSlots;

        // Last replicated clothing payload inspected by OnDeserialized. Held
        // items have their own state and are excluded from this string.
        std::string _receivedEquipment;

        // Owner-side: what the worn slots hashed to when the string above was
        // last built from them.
        std::uint64_t _wornEquipmentFingerprint = 0;

        // Last replicated wear inspected by OnDeserialized, and whether this
        // body's own pieces have yet to be given it.
        std::string _receivedEquipmentWear;
        bool _wearDirty = false;

        // Owner-side: when the worn pieces' wear was last read.
        MafiaNet::Time _wearCapturedAt = 0;

        // The gender whose skeleton the body was spawned on. The skeleton is
        // chosen by the spawn Properties, so a body that no longer matches its
        // appearance is rebuilt rather than re-dressed.
        Shared::Game::AppearanceGender _bodyGender = Shared::Game::AppearanceGender::Male;

        // Set when the spawn failed for a reason retrying cannot fix.
        bool _spawnRefused               = false;
        MafiaNet::Time _nextSpawnAttempt = 0;

        KCDC::Common::Spawn::SpawnTicket *_spawnTicket = nullptr;

        // Per-hand receiver state, indexed by the engine's hand. Both hands
        // transition independently.
        Shared::RPC::ItemClassId _shadowHandClass[kHandCount];
        std::uint64_t _shadowHandWuid[kHandCount] = {};

        // When a ground drop consumed that hand's shadow. Its class is held
        // back until the owner's hand update moves on, but never longer than
        // kRetiredShadowHoldMs: a same-class redraw inside one replication
        // interval never shows the empty hand in between.
        MafiaNet::Time _shadowRetiredAt[kHandCount] = {};

        // CreateItems reports failure from a path that runs after the add, so a
        // null result must not drive an unbounded retry.
        int _handCreateAttempts[kHandCount] = {};

        bool _receivedHandStateKnown[kHandCount] = {};
        Shared::RPC::ItemClassId _receivedHandClass[kHandCount];
        std::uint8_t _remoteHandStage[kHandCount] = {};

        // The WUID this module last got the engine to accept into that hand.
        std::uint64_t _appliedHandWuid[kHandCount] = {};

        // Avoid another weapon request before the queued animation updates
        // the hands.
        MafiaNet::Time _weaponActionAt = 0;

        // Bounds repeated ladder-grab attempts.
        static constexpr std::uint16_t kMaxLadderGrabAttempts = 120;
        std::uint64_t _ladderGrabGuid                         = 0;
        std::uint16_t _ladderGrabAttempts                     = 0;

        // Avoid queueing another grab before the first action enters.
        static constexpr MafiaNet::Time kLadderActionTimeoutMs = 500;
        MafiaNet::Time _ladderGrabAt                           = 0;

        // Request ladder exit once; repeating it recomputes placement.
        bool _ladderExitRequested = false;

        // The lean the copy was last started for, so a refused start is not
        // asked again every frame.
        std::uint64_t _leanStartGuid = 0;
        std::uint8_t _leanStartSlot  = 0;
        // Whether the copy's lean was running at the last look, so a copy
        // that never leant skips the minigame lookup.
        bool _leanStarted = false;

        // The stance last asked of this copy; NotifyCombatStanceLost clears
        // it when the engine takes the fists down.
        bool _stanceRequested = false;

        // Tracks stance suppression during a full-body interaction without
        // changing replicated fistsUp.
        bool _combatPresentationSuppressed = false;

        // A replicated Press owns the native attack request until its Commit,
        // Cancel, or an exclusive presentation explicitly cancels it.
        std::int32_t _remoteAttackInputClass  = -1;
        std::uint32_t _remoteAttackGeneration = 0;
        bool _remoteAttackActive              = false;

        SDK::wh::combatmodule::C_CombatActor::GuardRequest _appliedGuard = SDK::wh::combatmodule::C_CombatActor::GUARD_REQUEST_NONE;

        /**
         * The owner's fists, guard and zone, each change stamped with the
         * relay time of the update that carried it.
         *
         * They arrive on the reliable channel the moment they change, while
         * the body they belong to is drawn behind the wire -- and so are its
         * swings (`PresentationModule::IsDueOnActorClock`). Applied on
         * arrival, the zone of the owner's next swing was already twisting
         * this body while it still played the previous one. So they are
         * applied on the same clock as the swings: at once when the fight is
         * with this machine's own body, at the drawn pose otherwise.
         */
        struct CombatSample {
            MafiaNet::Time at         = 0;
            bool fistsUp              = false;
            std::uint8_t guardRequest = 0;
            std::int16_t combatZone   = -1;
        };
        static constexpr std::size_t kCombatTimelineCapacity = 32;
        std::array<CombatSample, kCombatTimelineCapacity> _combatTimeline {};
        std::size_t _combatTimelineHead  = 0; // the sample in force
        std::size_t _combatTimelineCount = 0;

        // The owner's block presses the drawn timeline went past that this
        // body has not pressed yet, and when the first of them was owed. A
        // press drawn in the same frame as its release, or refused by a busy
        // body until its release was drawn, is still pressed, once each, as
        // long as it is not older than `kGuardPressBudgetMs`.
        std::uint8_t _owedGuardPresses   = 0;
        MafiaNet::Time _guardPressOwedAt = 0;

        void RecordCombatSample();
        CombatSample DrawnCombat();

        // Buffered owner snapshots for interpolation.
        Framework::Utils::SnapshotBuffer<KCDC::Common::Replication::PoseSnapshot, KCDC::Common::Replication::PoseSnapshotPolicy> _snapshots {&KCDC::Common::Replication::PoseBufferConfig()};
    };
} // namespace KCDC::Features::Player

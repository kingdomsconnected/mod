#pragma once

#include "core/feature_registry.h"
#include "features/prop/prop_ghost.h"

#include "shared/features/siege/siege_ballistics.h"
#include "shared/features/siege/siege_rpc.h"

#include "sdk/cryengine/action/i_action_filter.h"
#include "sdk/cryengine/action/i_action_map.h"
#include "sdk/cryengine/action/i_action_map_manager.h"
#include "sdk/cryengine/action/i_game_object.h"
#include "sdk/cryengine/system/xml_node_ref.h"
#include "sdk/types.h"

#include <glm/glm.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Features::Siege {
    class SiegeEngine;

    /**
     * Siege engines on this machine: the stones in the air, where they come
     * down and what that does to the local player, and the local player's
     * hands on an engine.
     *
     * A shot is launched here at the server clock the engine says it is let
     * go at -- from the engine's own copy when this client has the engine,
     * so the stone leaves the sling as the arm lets it go, or from the
     * `SiegeShot` sent to players near the target who do not. Every frame the
     * stone is put where the arc says and the step since the last frame is
     * traced against the world; the first thing it meets is where it lands
     * here -- an impact is seen at once rather than a round trip later. The
     * one client the shot names reports that point. The server's verdict
     * (`SiegeImpact`) then lands any stone still flying, plays the impact for
     * anyone who did not see one, and is the only thing that hurts: each
     * client takes its own player's share of the blast through the game's own
     * damage, which reports it like any other blow.
     *
     * Near a `usable` engine the game's own prompt offers its place, or a
     * winch when it needs a crew. In the engine's place the attack, block,
     * torch and use keys are the operator's -- fire, lay it where the player
     * looks, load, step away -- shown as the game's own hints, with the
     * game's own answers to those keys filtered away for as long as it lasts,
     * and a red beam along the arc to where the shot would come down.
     *
     * The engines themselves are `SiegeEngine` replicas, ticked from here.
     *
     * Game thread only, apart from the RPC handlers, which queue.
     */
    class SiegeModule final
        : public KCDC::Core::IFeatureService
        , public SDK::IActionListener {
      public:
        static SiegeModule &Get();

        void Update() override;
        void Reset() override;

        /** Raised on the loading thread; the tick forgets every native handle. */
        void NativeContextChanged() override;

        /** The operator's aim key, from the engine's input dispatch; latched for the tick. */
        void OnAction(const SDK::CCryName &action, int activationMode, float value) override;
        void AfterAction() override {}

        /**
         * Gives the engines' sounds the range the catalog sets, as the audio
         * system loads an ATL control document; false when the document has
         * none of them. Runs once per control file, before any can play.
         */
        static bool ExtendGameAudio(SDK::XmlNodeRef &document);

        /** Draws an entity a hundred times as far as its size would: a stone in the air is small and far off. */
        static void DrawFar(SDK::EntityId entityId);

        /** A catalog particle effect at a point, kept for `durationMs`; nothing for an empty name. */
        void PlayEffect(const char *name, const glm::vec3 &point, std::uint32_t durationMs);

        /**
         * An ATL trigger at a point, heard as late as sound takes to get from
         * there to the listener: a blast a kilometre off is seen three seconds
         * before it is heard. Nothing for an empty name.
         */
        void PlaySound(const char *trigger, const glm::vec3 &point);

        /** Shakes the local camera with `strength` at `point`, fading with the square of the distance out to `range`. */
        void Shake(const glm::vec3 &point, float strength, float range);

      private:
        struct Flight {
            Shared::RPC::SiegeShot shot;

            /** This machine's clock at launch. */
            std::int64_t launchLocalMs = 0;

            /**
             * How far into its flight the stone already was when this machine
             * launched it, in seconds. An impact before that happened before
             * this client could see it, and lands the stone without a sound.
             */
            float joinedSeconds = 0.0F;

            SDK::EntityId stoneId = 0;
            glm::vec3 lastPoint {0.0F};
            float lastSeconds = 0.0F;

            bool landed  = false;
            bool whizzed = false;
            glm::vec3 landedAt {0.0F};

            /** When the stone is cleared away. */
            std::uint64_t clearAtMs = 0;
        };

        /** An impact this machine has already shown, so the server's verdict does not show it twice. */
        struct Shown {
            std::uint32_t shotId = 0;
            glm::vec3 point {0.0F};
        };

        /** A sound waiting for its delay to run out. */
        struct DelayedSound {
            std::uint64_t dueMs = 0;
            std::string trigger;
            glm::vec3 point {0.0F};
        };

        void InstallRPCs();
        void DrainInbox(std::uint64_t now);

        /** Launches each engine's latest shot from its own copy once its moment comes, unless it is already flying here. */
        void LaunchDueShots(std::uint64_t now, std::int64_t serverClockOffset, bool serverClockKnown);

        void Launch(const Shared::RPC::SiegeShot &shot, std::uint64_t now);
        void Settle(const Shared::RPC::SiegeImpact &impact, std::uint64_t now);

        /** Whether this machine has launched a shot, flying or since landed. */
        Flight *FindFlight(std::uint32_t shotId);
        bool HasLaunched(std::uint32_t shotId) const;

        /** Moves every stone along its arc and stops the ones that met something. */
        void Fly(std::uint64_t now);

        /** Ends a flight at `point`: the stone goes, and the impact is shown unless it already was nearby, or happened before this machine joined it. */
        void Land(Flight &flight, const glm::vec3 &point, float seconds, std::uint64_t now);

        void ReportImpact(std::uint32_t shotId, const glm::vec3 &point);
        void ShowImpact(std::uint32_t shotId, std::uint8_t assetIndex, const glm::vec3 &point, bool struckEngine);

        /** The local player's share of a blast, taken through the game's own damage. */
        void HurtLocalPlayer(const Shared::RPC::SiegeImpact &impact);

        void ClearFlights(bool removeEntities);
        void PlayDueSounds(std::uint64_t now);

        bool _rpcsInstalled = false;

        // --- The local player's hands on an engine -------------------------

        /** What the local player holds of an engine. */
        enum class Role : std::uint8_t {
            None,
            Operator,
            Crew
        };

        /**
         * The prompt for the nearest `usable` engine within reach -- its place
         * while it has nobody in it, a free winch when it needs hands and the
         * player stands at one -- chosen again every `kPromptIntervalMs`,
         * whose press sends `SiegeUse` for what it showed.
         */
        void UpdatePrompt(std::uint64_t now);
        void HidePrompt();

        /** Takes up or puts down the operator's or the crewman's keys as the engines say what the local player holds. */
        void UpdateRole(std::uint64_t now, std::int64_t serverClockOffset, bool serverClockKnown);
        void EnterRole(Role role, SiegeEngine &engine);
        void LeaveRole();

        /** The operator's hints, kept to what the engine can do now; Fire says why it cannot yet, or how far it would throw. */
        void UpdateOperatorHints(SiegeEngine &engine, std::uint64_t now, std::int64_t serverClockOffset, bool serverClockKnown);

        /**
         * While the aim key is held, lays the engine where the player looks:
         * here at once, every frame, the way the server will lay it, and on the
         * server a few times a second. Once the key is let go the server's aim
         * takes over again as soon as it has caught up.
         */
        void UpdateAim(SiegeEngine &engine, std::uint64_t now);

        /**
         * The beam along the engine's aim, every frame: the path the projectile
         * takes from where it lies now -- a trebuchet's stone swings out of the
         * sling on the clip's own path before it flies -- along the arc the aim
         * solves to, traced through this world, as a translucent red tube to
         * where it comes down, and a ball there. A gun's shot is all but
         * straight, so it reads as a beam of light; a trebuchet's is the curve
         * a thrown stone takes. Built once, then only moved, and traced again
         * only when the engine or its aim has moved.
         */
        void UpdateMarker(SiegeEngine &engine);
        void HideMarker();

        /** The operator's keys and the filter that takes the game's own answers to them away. */
        bool AcquireInput();
        void ReleaseInput();

        void SendUse(std::uint64_t engineNetworkId, Shared::RPC::SiegeUseAction action);

        std::uint32_t _prompt                     = 0;
        std::uint64_t _promptEngine               = 0;
        Shared::RPC::SiegeUseAction _promptAction = Shared::RPC::SiegeUseAction::Operate;
        std::uint64_t _nextPromptCheck            = 0;

        Role _role               = Role::None;
        std::uint64_t _roleEngine = 0;

        std::uint32_t _fireHint  = 0;
        std::uint32_t _aimHint   = 0;
        std::uint32_t _loadHint  = 0;
        std::uint32_t _leaveHint = 0;
        std::string _fireText;

        SDK::IActionMapManager *_inputManager = nullptr;
        SDK::IActionMap *_inputMap            = nullptr;
        SDK::IActionFilter *_filter           = nullptr;
        bool _listening                       = false;

        /** Latched from `OnAction`, read on the tick. */
        std::atomic<bool> _aimHeld = false;

        std::optional<glm::vec3> _aimSent;
        std::uint64_t _nextAimAt = 0;

        /** The point this client laid the engine on while the aim key was held, until the server's aim catches up; and when the key was let go. */
        std::optional<glm::vec3> _localAim;
        std::uint64_t _aimReleasedAt = 0;

        /** The beam's ghost, built once and moved every frame, and where it comes down for the Fire hint's distance. */
        std::uint32_t _marker = 0;
        bool _markerShown     = false;
        std::optional<glm::vec3> _markerPoint;

        /**
         * What the beam was last traced from -- the engine, where it stood,
         * the aim, whether it was firing and showed its guide -- so a beam
         * that has not moved is not solved and traced again; and the path and
         * parts it was built from, kept so a frame that does trace again
         * allocates nothing.
         */
        bool _markerTraced          = false;
        std::uint64_t _markerEngine = 0;
        bool _markerFiring          = false;
        bool _markerGuide           = false;
        std::optional<glm::vec3> _markerTarget;
        glm::vec3 _markerFrom {0.0F};
        std::vector<glm::vec3> _markerPath;
        std::vector<KCDC::Features::Prop::GhostPart> _markerParts;

        // --- Shared state ----------------------------------------------------

        std::mutex _inboxMutex;
        std::vector<Shared::RPC::SiegeShot> _shotInbox;
        std::vector<Shared::RPC::SiegeImpact> _impactInbox;

        std::vector<Flight> _flights;
        std::vector<Shown> _shown;

        /** Shots this machine has launched, newest last, so neither copy of one launches it twice. */
        std::deque<std::uint32_t> _launched;

        std::vector<DelayedSound> _sounds;

        /** The clock of the last tick, for how far a laid barrel moves in one. */
        std::uint64_t _lastFrameMs = 0;

        /** Set from NativeContextChanged, acted on by the tick. */
        std::atomic<bool> _nativeContextLost = false;
    };
} // namespace KCDC::Features::Siege

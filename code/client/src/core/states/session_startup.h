#pragma once

#include <utils/states/state.h>

#include <cstdint>

namespace KCDC::Core::States {
    /**
     * The readiness category the session's hold registers in. Every load
     * completion waits on mask 0x3F93 (`C_ReadinessObserver::kLoadMask`);
     * 0x400 is inside it, outside the save requests' masks (0x2A, 0x4022) and
     * the 0x3B91/0x3B97/0x81 waits, and no native task uses it -- so the hold
     * delays nothing but a load completion (and skip-time's 0x3F91, which
     * cannot run during a join).
     */
    constexpr std::uint32_t kSessionHoldCategory = 0x400;

    /**
     * How long a join stage may wait on the server before the join is
     * refused. Every stage that waits on it answers in milliseconds on a
     * working server; a silent one -- most often a build whose RPC salt
     * differs, so none of its RPCs arrive -- is refused while the game's
     * loading screen is still held, long before the game's own 50 s
     * readiness timeout would show the world.
     */
    constexpr std::uint64_t kServerAnswerTimeoutMs = 10000;

    /** The join's reason when the server goes quiet. */
    constexpr const char *kServerSilent = "The server stopped answering while you were joining. It may be running a different build of the mod.";

    // Facts retained across the existing machine's Exit/Next/Enter ticks.
    // This owns no phases, transitions, work queue or update loop: the
    // Framework's connection phase and deferred asset generation are read
    // from the Instance rather than mirrored here.
    struct SessionLoadContext {
        const char *error    = nullptr;
        bool active          = false;
        /** The session level was asked for: a native load completion is owed. */
        bool levelRequested  = false;
        /** A load-completion observer began waiting on the session's hold. */
        bool levelLoaded     = false;
        /** ESYSTEM_EVENT_LEVEL_GAMEPLAY_START since this attempt began. */
        bool gameplayStarted = false;
        /** The game's own session latch (module message 52) since this attempt began. */
        bool gameSessionStarted = false;
    };

    // The currently entered startup state receives engine/network edges.
    // OnExit detaches it; edges between states remain in SessionLoadContext
    // (or the Framework's own latches) for the next state's OnEnter.
    class SessionStartupState: public Framework::Utils::States::IState {
      public:
        /** Refuses the join once the stage has waited kServerAnswerTimeoutMs on the server. */
        bool OnUpdate(Framework::Utils::States::Machine *) override;
        bool OnExit(Framework::Utils::States::Machine *) override;
        virtual void OnLevelAndAssetsReady() {}
        virtual void OnReplicationReady() {}
        virtual void OnSpawnAnswered(std::uint64_t) {}
        virtual void OnPlacementSettled(std::uint64_t, bool) {}
        virtual void OnConnectionReady() {}

      protected:
        bool Enter(Framework::Utils::States::Machine *machine);
        Framework::Utils::States::Machine *_machine = nullptr;
        std::uint64_t _enteredAtMs                  = 0;
    };

    class SessionResourcesState final: public SessionStartupState {
      public:
        const char *GetName() const override {
            return "SessionResources";
        }
        int32_t GetId() const override;
        bool OnEnter(Framework::Utils::States::Machine *) override;
    };

    class SessionReplicationState final: public SessionStartupState {
      public:
        const char *GetName() const override {
            return "SessionReplication";
        }
        int32_t GetId() const override;
        bool OnEnter(Framework::Utils::States::Machine *) override;
        void OnReplicationReady() override;
    };

    class SessionSpawnState final: public SessionStartupState {
      public:
        const char *GetName() const override {
            return "SessionSpawn";
        }
        int32_t GetId() const override;
        bool OnEnter(Framework::Utils::States::Machine *) override;
        void OnSpawnAnswered(std::uint64_t placement) override;
        void OnPlacementSettled(std::uint64_t placement, bool success) override;

      private:
        std::uint64_t _placement = 0;
        bool _answered           = false;
        bool _settled            = false;
    };

    class SessionFinalizeState final: public SessionStartupState {
      public:
        const char *GetName() const override {
            return "SessionFinalize";
        }
        int32_t GetId() const override;
        bool OnEnter(Framework::Utils::States::Machine *) override;
        void OnConnectionReady() override;
    };
} // namespace KCDC::Core::States

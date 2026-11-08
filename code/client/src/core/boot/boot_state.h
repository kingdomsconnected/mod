#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

namespace KCDC::Core::Boot {
    /**
     * The shell's own boot state, in one declared owner.
     *
     * Section 4 of `docs/project_architecture.md` allows exactly one escape
     * from "state belongs to an object": a single process-wide owner published
     * as `extern` in its header, declared, reachable and documented. This is
     * that escape, and these five members are why it is taken. Every one of
     * them is written from an edge that runs before `gApplication` is
     * constructed -- `InitFunction::RunAll()` and `MH_EnableHook` both precede
     * it in `main.cpp`, so the loading pump, the swap-chain bootstrap and the
     * window procedure are all live first -- and the three render-device
     * members are additionally touched from the render thread, which the
     * application does not own either. None of them can therefore sit on
     * `Application`.
     */
    struct BootState {
        // Written by `CSystem_SynchronousLoadingTick_Hook` in loading_tick.cpp:
        // when the peer was last pumped during a level load.
        std::uint64_t lastPumpMs = 0;

        // Written by `CCryAction_PostUpdate_Hook` in engine.cpp: the thread the
        // engine ticks the game on, which is therefore the thread that owns the
        // peer and everything reached through it. Read by the loading pump,
        // which the engine also enters from its streaming threads.
        std::atomic<std::uint32_t> gameThreadId {0};

        // Written by `CCryAction_PostUpdate_Hook` in engine.cpp around the
        // application's own tick, which a synchronous asset load inside it --
        // a body's models, while the spawner builds it -- must not re-enter
        // through the loading pump: the peer would delete replicas the tick is
        // in the middle of. Only touched on the game thread.
        bool inApplicationTick = false;

        // Held by the Present and ResizeBuffers detours in render_device.cpp: a
        // Present racing a ResizeBuffers would draw into a freed back buffer.
        std::mutex presentResizeMutex;

        // Written once by `CCryDX12SwapChain_Present_Hook` in render_device.cpp,
        // which bootstraps the native hooks off the engine's own swap chain.
        std::atomic_bool bootstrapped {false};

        // Written by `IDXGISwapChain_Present_Hook` in render_device.cpp, and only
        // once RenderInit fully succeeded; a half-initialized renderer must
        // never reach D3D12Backend::Begin().
        std::atomic_bool overlayReady {false};

        // Written by `ConsumeLockedEscape` in window.cpp while the window
        // procedure swallows an Escape press the controls lock refused.
        bool lockedEscapeHeld = false;
    };

    extern BootState gBootState;
} // namespace KCDC::Core::Boot

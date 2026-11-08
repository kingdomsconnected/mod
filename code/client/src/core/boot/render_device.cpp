#include <utils/safe_win32.h>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <MinHook.h>

#include <graphics/backend/d3d12.h>
#include <logging/logger.h>
#include <utils/hooking/hook_function.h>

#include <atomic>
#include <cstddef>
#include <mutex>

#include "core/application.h"
#include "core/boot/boot_state.h"
#include "features/prop/prop_editor_capture.h"

#include "sdk/cryengine/renderer/c_cry_dx12_swap_chain.h"
#include "sdk/patterns.h"

/*
 * CCryDX12SwapChain::Present hands us a live engine-owned swap chain
 * (this+0x18 -> NCryDX12::CSwapChain, +0xA8 -> the real IDXGISwapChain), so the
 * native hooks below are derived from real objects instead of a throwaway
 * device. It fires once, purely to bootstrap.
 *
 * The overlay itself draws from the *native* Present, which the engine only
 * reaches after the renderer has flushed the frame's command lists.
 */

namespace KCDC::Core::Boot {
    namespace {
        // Fixed by the COM interface definitions, not by the game build.
        constexpr std::size_t kSwapChainPresentSlot       = 8;  // IDXGISwapChain::Present
        constexpr std::size_t kSwapChainResizeBuffersSlot = 13; // IDXGISwapChain::ResizeBuffers
        constexpr std::size_t kCommandQueueExecuteSlot    = 10; // ID3D12CommandQueue::ExecuteCommandLists

        using IDXGISwapChain_Present_t       = HRESULT(__fastcall *)(IDXGISwapChain *, UINT, UINT);
        using IDXGISwapChain_ResizeBuffers_t = HRESULT(__fastcall *)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
        using ID3D12CommandQueue_Execute_t   = void(__fastcall *)(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);
        using CCryDX12SwapChain_Present_t    = HRESULT(__fastcall *)(SDK::CCryDX12SwapChain *, UINT, UINT);

        IDXGISwapChain_Present_t g_presentOriginal             = nullptr;
        IDXGISwapChain_ResizeBuffers_t g_resizeBuffersOriginal = nullptr;
        ID3D12CommandQueue_Execute_t g_executeOriginal         = nullptr;
        CCryDX12SwapChain_Present_t g_enginePresentOriginal    = nullptr;

        // Resolved per call because a mode change swaps the whole chain out. The
        // reference is dropped immediately: CryEngine owns it, we only borrow.
        IDXGISwapChain3 *ResolveSwapChain3(IDXGISwapChain *swapChain) {
            IDXGISwapChain3 *resolved = nullptr;
            if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&resolved))) || !resolved) {
                return nullptr;
            }
            resolved->Release();
            return resolved;
        }

        void __fastcall ID3D12CommandQueue_ExecuteCommandLists_Hook(ID3D12CommandQueue *queue, UINT numCommandLists, ID3D12CommandList *const *commandLists) {
            // The engine's queue lives inside NCryDX12::CDevice's command-list
            // pools and is not reachable from the swap chain, so capture it here.
            // Our own backend re-enters this; the null check makes that a no-op.
            if (!gGlobals.commandQueue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
                gGlobals.commandQueue = queue;
                Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS)->info("Captured engine DIRECT command queue {}", fmt::ptr(queue));
            }

            g_executeOriginal(queue, numCommandLists, commandLists);
        }

        HRESULT __fastcall IDXGISwapChain_ResizeBuffers_Hook(IDXGISwapChain *swapChain, UINT bufferCount, UINT width, UINT height, DXGI_FORMAT newFormat, UINT swapChainFlags) {
            std::lock_guard<std::mutex> lock(KCDC::Core::Boot::gBootState.presentResizeMutex);
            if (!KCDC::Features::Prop::EditorCapture::Get().PrepareResize()) {
                return DXGI_ERROR_WAS_STILL_DRAWING;
            }
            return g_resizeBuffersOriginal(swapChain, bufferCount, width, height, newFormat, swapChainFlags);
        }

        HRESULT __fastcall IDXGISwapChain_Present_Hook(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags) {
            if ((flags & DXGI_PRESENT_TEST) != 0) {
                return g_presentOriginal(swapChain, syncInterval, flags);
            }
            auto *app      = gApplication.get();
            auto *renderer = app ? app->GetRenderer() : nullptr;

            // Fires before the application is wired up and during teardown.
            if (!renderer) {
                return g_presentOriginal(swapChain, syncInterval, flags);
            }

            // A shut-down renderer reads below as one that never came up.
            if (app->IsShuttingDown()) {
                KCDC::Features::Prop::EditorCapture::Get().Shutdown();
                return g_presentOriginal(swapChain, syncInterval, flags);
            }

            auto *swapChain3 = ResolveSwapChain3(swapChain);
            if (!swapChain3) {
                return g_presentOriginal(swapChain, syncInterval, flags);
            }

            if (!KCDC::Core::Boot::gBootState.overlayReady.load(std::memory_order_acquire)) {
                // renderer->IsInitialized() here means RenderInit ran and failed
                // partway; retrying would just walk into the broken backend.
                if (renderer->IsInitialized() || !gGlobals.commandQueue) {
                    return g_presentOriginal(swapChain, syncInterval, flags);
                }

                auto &opts                              = app->GetOptions();
                opts.rendererOptions.windowHandle       = gGlobals.window;
                opts.rendererOptions.d3d12.device       = gGlobals.device;
                opts.rendererOptions.d3d12.commandQueue = gGlobals.commandQueue;
                opts.rendererOptions.d3d12.swapchain    = swapChain3;
                gGlobals.swapChain                      = swapChain3;

                if (auto result = app->RenderInit(); !result) {
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Rendering subsystems failed to initialize: {}", result.GetError().message);
                }
                else {
                    KCDC::Core::Boot::gBootState.overlayReady.store(true, std::memory_order_release);
                    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("ImGui overlay ready");
                }

                return g_presentOriginal(swapChain, syncInterval, flags);
            }

            {
                std::lock_guard<std::mutex> lock(KCDC::Core::Boot::gBootState.presentResizeMutex);

                // Again under the lock: Shutdown() drains this mutex behind it.
                if (!app->IsShuttingDown()) {
                    auto *backend = renderer->GetD3D12Backend();

                    // Begin() re-acquires the back buffer each frame, so a replaced
                    // chain only needs repointing.
                    if (swapChain3 != backend->GetSwapChain()) {
                        backend->SetSwapChain(swapChain3);
                        gGlobals.swapChain = swapChain3;
                    }

                    app->Render();

                    auto &capture = KCDC::Features::Prop::EditorCapture::Get();
                    capture.Poll();
                    // The capture uses its own list on the same DIRECT queue.
                    // Outside Begin/End the buffer is in PRESENT state, so it
                    // never depends on an unsubmitted overlay transition.
                    capture.Record(*backend, false);
                    backend->Begin();
                    const bool overlayFrame = backend->GetCurrentBackBuffer() != nullptr;
                    app->RenderWebViews();
                    if (app->GetImGUI() && app->GetImGUI()->IsInitialized()) {
                        app->GetImGUI()->Render();
                    }
                    backend->End();
                    if (overlayFrame) {
                        capture.Record(*backend, true);
                    }
                }
            }

            return g_presentOriginal(swapChain, syncInterval, flags);
        }

        void BootstrapNativeHooks(SDK::CCryDX12SwapChain *engineSwapChain) {
            const auto log = Framework::Logging::GetLogger(FRAMEWORK_INNER_GRAPHICS);

            auto *nativeWrapper   = engineSwapChain ? engineSwapChain->GetDX12SwapChain() : nullptr;
            auto *nativeSwapChain = nativeWrapper ? static_cast<IDXGISwapChain *>(nativeWrapper->GetNativeSwapChain()) : nullptr;
            if (!nativeSwapChain) {
                log->error("CCryDX12SwapChain carried no native swap chain; DX12 overlay disabled");
                return;
            }

            IDXGISwapChain3 *swapChain3 = nullptr;
            if (FAILED(nativeSwapChain->QueryInterface(IID_PPV_ARGS(&swapChain3))) || !swapChain3) {
                log->error("Engine swap chain does not expose IDXGISwapChain3; DX12 overlay disabled");
                return;
            }

            DXGI_SWAP_CHAIN_DESC desc {};
            ID3D12Device *device           = nullptr;
            ID3D12CommandQueue *probeQueue = nullptr;
            bool ok                        = false;

            do {
                // Cheap plausibility check on the offsets we came through: a real
                // swap chain always describes a real window.
                if (FAILED(swapChain3->GetDesc(&desc)) || !::IsWindow(desc.OutputWindow)) {
                    log->error("Engine swap chain description is not plausible; DX12 overlay disabled");
                    break;
                }

                if (FAILED(swapChain3->GetDevice(IID_PPV_ARGS(&device))) || !device) {
                    log->error("Could not resolve the D3D12 device from the engine swap chain");
                    break;
                }

                // ExecuteCommandLists is not reachable from the swap chain; read
                // its address off a throwaway queue created on the game's own
                // device, which shares the runtime and therefore the vtable.
                D3D12_COMMAND_QUEUE_DESC queueDesc {};
                queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
                if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&probeQueue))) || !probeQueue) {
                    log->error("Could not create the probe command queue used to locate ExecuteCommandLists");
                    break;
                }

                auto **swapChainVtable = *reinterpret_cast<void ***>(swapChain3);
                auto **queueVtable     = *reinterpret_cast<void ***>(probeQueue);

                struct HookSpec {
                    const char *name;
                    LPVOID target;
                    LPVOID detour;
                    LPVOID *original;
                };

                const HookSpec hooks[] = {
                    {"IDXGISwapChain::Present", swapChainVtable[kSwapChainPresentSlot], reinterpret_cast<LPVOID>(&IDXGISwapChain_Present_Hook), reinterpret_cast<LPVOID *>(&g_presentOriginal)},
                    {"IDXGISwapChain::ResizeBuffers", swapChainVtable[kSwapChainResizeBuffersSlot], reinterpret_cast<LPVOID>(&IDXGISwapChain_ResizeBuffers_Hook), reinterpret_cast<LPVOID *>(&g_resizeBuffersOriginal)},
                    {"ID3D12CommandQueue::ExecuteCommandLists", queueVtable[kCommandQueueExecuteSlot], reinterpret_cast<LPVOID>(&ID3D12CommandQueue_ExecuteCommandLists_Hook), reinterpret_cast<LPVOID *>(&g_executeOriginal)},
                };

                ok = true;
                for (const auto &hook : hooks) {
                    const auto createStatus = MH_CreateHook(hook.target, hook.detour, hook.original);
                    if (createStatus != MH_OK) {
                        log->error("Could not hook {} at {}: {}", hook.name, fmt::ptr(hook.target), MH_StatusToString(createStatus));
                        ok = false;
                        continue;
                    }

                    const auto enableStatus = MH_EnableHook(hook.target);
                    if (enableStatus != MH_OK) {
                        log->error("Could not enable the {} hook: {}", hook.name, MH_StatusToString(enableStatus));
                        ok = false;
                        continue;
                    }

                    log->info("Hooked {} at {}", hook.name, fmt::ptr(hook.target));
                }
            } while (false);

            if (ok) {
                // Borrowed: the engine owns both, so only the raw pointers are kept.
                gGlobals.window = desc.OutputWindow;
                gGlobals.device = device;

                log->info("DX12 overlay attached (window {}, device {}, {}x{}, format {})", fmt::ptr(desc.OutputWindow), fmt::ptr(device), desc.BufferDesc.Width, desc.BufferDesc.Height, static_cast<int>(desc.BufferDesc.Format));

                // D3D12 requires the PSO's render-target format to match the
                // bound RTV exactly, and the framework builds ImGui's pipeline
                // for R8G8B8A8_UNORM.
                if (desc.BufferDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM) {
                    log->warn("Back buffer format {} does not match the framework's R8G8B8A8_UNORM ImGui pipeline; the overlay may not draw", static_cast<int>(desc.BufferDesc.Format));
                }
            }

            if (probeQueue) {
                probeQueue->Release();
            }
            if (device) {
                device->Release();
            }
            swapChain3->Release();
        }

        HRESULT __fastcall CCryDX12SwapChain_Present_Hook(SDK::CCryDX12SwapChain *engineSwapChain, UINT syncInterval, UINT flags) {
            bool expected = false;
            if (KCDC::Core::Boot::gBootState.bootstrapped.compare_exchange_strong(expected, true)) {
                BootstrapNativeHooks(engineSwapChain);
            }

            return g_enginePresentOriginal(engineSwapChain, syncInterval, flags);
        }
    } // namespace
} // namespace KCDC::Core::Boot

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CCryDX12SwapChain__Present), reinterpret_cast<LPVOID>(&KCDC::Core::Boot::CCryDX12SwapChain_Present_Hook), reinterpret_cast<LPVOID *>(&KCDC::Core::Boot::g_enginePresentOriginal));
    },
    "RenderDevice");

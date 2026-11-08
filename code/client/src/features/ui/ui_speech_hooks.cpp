#include <utils/safe_win32.h>

#include <MinHook.h>

#include <utils/hooking/hook_function.h>

#include "sdk/patterns.h"
#include "sdk/types.h"

#include <cstdint>

namespace KCDC::Features::UI {
    namespace {
        using SetBubblePosition_t = void(__fastcall *)(void *, std::uint32_t, const SDK::Vec3 *, float);

        SetBubblePosition_t g_setBubblePositionOriginal = nullptr;

        // The engine anchors a bubble 12 to 16 cm above the eyes, the band
        // this mod draws a nametag in. Every bubble moves, not just players':
        // telling those apart needs an entity-keyed index, and streaming
        // churns one exactly where it would be read.
        constexpr float kBubbleLift = 0.45F;

        void __fastcall SetBubblePosition_Hook(void *bubbles, std::uint32_t bubbleId, const SDK::Vec3 *position, float scale) {
            // Adjusted, not recomputed: it arrives already smoothed.
            const SDK::Vec3 lifted {position->x, position->y, position->z + kBubbleLift};
            g_setBubblePositionOriginal(bubbles, bubbleId, &lifted, scale);
        }
    } // namespace
} // namespace KCDC::Features::UI

static InitFunction init(
    []() {
        MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.C_UIHudBubbles__SetBubblePosition), reinterpret_cast<LPVOID>(&KCDC::Features::UI::SetBubblePosition_Hook), reinterpret_cast<LPVOID *>(&KCDC::Features::UI::g_setBubblePositionOriginal));
    },
    "Speech");

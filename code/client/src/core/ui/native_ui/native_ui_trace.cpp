#include "sdk/patterns.h"

// Load Winsock2 before MinHook includes Windows.h.
#include <utils/safe_win32.h>

#include <MinHook.h>
#include <cstring>
#include <logging/logger.h>
#include <utils/hooking/hook_function.h>

namespace KCDC::Core {
    /** Observes only original probe commands without altering dispatch. */
    class NativeUITrace final {
      public:
        static NativeUITrace &Get() {
            static NativeUITrace instance;
            return instance;
        }

        static void Install() {
            const MH_STATUS status = MH_CreateHook(reinterpret_cast<LPVOID>(SDK::gPatterns.CryGFxFSCommandHandler__Callback), reinterpret_cast<LPVOID>(&Callback), reinterpret_cast<LPVOID *>(&Get()._original));
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Native UI callback trace installation: {}", MH_StatusToString(status));
        }

      private:
        // These native objects are identity-only: no layout or slots are read.
        using CallbackFunction     = void(__fastcall *)(void *, void *, const char *, const char *);
        CallbackFunction _original = nullptr;

        static void __fastcall Callback(void *handler, void *movie, const char *command, const char *arguments) { Get().Dispatch(handler, movie, command, arguments); }

        void Dispatch(void *handler, void *movie, const char *command, const char *arguments) {
            if (std::strncmp(command, "NativeUI", 8) == 0) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Native UI adapter callback {} movie {}", command, movie);
            }
            _original(handler, movie, command, arguments);
        }
    };
} // namespace KCDC::Core

static InitFunction init(
    []() {
        KCDC::Core::NativeUITrace::Install();
    },
    "Native UI callback trace");

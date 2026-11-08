#include "i_flash_player.h"

#include "../../patterns.h"

#include <logging/logger.h>
#include <utils/hooking/hooking.h>

#include <cstdint>

namespace SDK {
    namespace {
        /** A path the movie root always has, and one it cannot have. */
        constexpr const char *kPresentPath = "_root";
        constexpr const char *kAbsentPath  = "_root.kcdc_probe_that_cannot_exist";
    } // namespace

    bool IFlashPlayer::VerifySlots() const {
        const auto *const *vtable = *reinterpret_cast<const void *const *const *>(this);

        struct Expected {
            int slot;
            std::uint64_t address;
            const char *name;
        };

        const Expected expected[] = {
            {39, gPatterns.CFlashPlayer__SetVariable, "SetVariable"},
            {42, gPatterns.CFlashPlayer__IsAvailable, "IsAvailable"},
            {46, gPatterns.CFlashPlayer__Invoke, "Invoke"},
        };

        for (const Expected &entry : expected) {
            const auto actual = reinterpret_cast<std::uint64_t>(vtable[entry.slot]);
            if (actual != entry.address) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("IFlashPlayer slot {} is not {}: the vtable holds {:#x}, the signature resolved {:#x}", entry.slot, entry.name, actual, entry.address);
                return false;
            }
        }

        // Two probes with opposite answers, so a neighbouring function that
        // happens to return a constant cannot pass.
        const bool presentByDispatch = IsAvailable(kPresentPath);
        const bool presentByAddress  = hook::this_call<bool>(gPatterns.CFlashPlayer__IsAvailable, this, kPresentPath);
        const bool absentByDispatch  = IsAvailable(kAbsentPath);
        const bool absentByAddress   = hook::this_call<bool>(gPatterns.CFlashPlayer__IsAvailable, this, kAbsentPath);

        if (presentByDispatch != presentByAddress || absentByDispatch != absentByAddress) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("IFlashPlayer dispatch disagrees with its slot map: present {}/{}, absent {}/{}", presentByDispatch, presentByAddress, absentByDispatch, absentByAddress);
            return false;
        }

        if (!presentByAddress || absentByAddress) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("IFlashPlayer::IsAvailable answered {} for {} and {} for a path that cannot exist", presentByAddress, kPresentPath, absentByAddress);
            return false;
        }

        return true;
    }
} // namespace SDK

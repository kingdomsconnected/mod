#pragma once

#include <utils/safe_win32.h>

#include "core/feature_registry.h"
#include "sdk/types.h"
#include "sdk/wh/rpgmodule/c_soul.h"
#include "sdk/wh/rpgmodule/s_buff_definition.h"
#include "shared/game/custom_buff_registry.h"
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace KCDC::Features::Buff {
    /** Registry installation and native definition lifetime, on the game thread. */
    class CustomBuffModule final: public KCDC::Core::IFeatureService {
      public:
        static CustomBuffModule &Get();
        static void Install();
        static void InstallHooks();
        void Update() override;
        void Reset() override;
        SDK::CryGUID ToNative(const KCDC::Shared::RPC::BuffId &id) const;
        KCDC::Shared::RPC::BuffId ToLogical(const SDK::CryGUID &id) const;
        bool ResolveLabel(const char *label, SDK::CryStringT<char> &out) const;
        float HealthMaximum(const SDK::wh::rpgmodule::I_Soul *soul, float base) const;
        /** Whether any installed definition moves the health maximum. */
        bool HasHealthModifiers() const {
            return _hasHealthModifiers.load(std::memory_order_acquire);
        }

      private:
        struct NativeDefinition {
            SDK::ScopedCryString name;
            SDK::ScopedCryString description;
            SDK::ScopedCryString uiName;
            SDK::ScopedCryString implementation;
            SDK::ScopedCryString factoryClassName;
            SDK::ScopedCryString parameters;
            SDK::wh::rpgmodule::S_BuffDefinition row {};
            NativeDefinition(const KCDC::Shared::Game::CustomBuffDefinition &definition, const SDK::wh::rpgmodule::S_BuffDefinition &prototype, const SDK::wh::rpgmodule::S_BuffDefinition &visual);
        };
        void Accept(const nlohmann::json &message);
        static const SDK::wh::rpgmodule::S_BuffDefinition *Lookup(void *database, const SDK::CryGUID *id);
        const SDK::wh::rpgmodule::S_BuffDefinition *FindNative(const SDK::CryGUID &id) const;
        const SDK::wh::rpgmodule::S_BuffDefinition *(__fastcall *_lookupOriginal)(void *, const SDK::CryGUID *) = nullptr;
        mutable std::mutex _mutex;
        std::vector<std::string> _incoming;
        std::atomic<bool> _hasHealthModifiers = false;
        std::map<std::string, std::unique_ptr<NativeDefinition>, std::less<>> _installed;
        std::map<std::string, std::string, std::less<>> _labels;
        std::map<std::pair<std::uint64_t, std::uint64_t>, KCDC::Shared::RPC::BuffId> _logical;
        std::map<std::pair<std::uint64_t, std::uint64_t>, const SDK::wh::rpgmodule::S_BuffDefinition *> _nativeRows;
        std::map<const SDK::wh::rpgmodule::S_BuffDefinition *, KCDC::Shared::Game::CustomBuffMaximum> _healthModifiers;
    };
} // namespace KCDC::Features::Buff

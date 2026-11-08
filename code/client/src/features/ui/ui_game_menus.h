#pragma once

#include "core/feature_registry.h"
#include "sdk/wh/framework/e_ui_apse_view.h"
#include "sdk/wh/framework/i_ui_apse.h"
#include "sdk/wh/guimodule/c_ui_apse_camera.h"

#include <array>
#include <set>
#include <string>
#include <string_view>

namespace KCDC::Features::UI {
    /** Resource-owned permission to open the game's tabbed menus. Game thread only. */
    class GameMenuPolicy {
      public:
        enum class Menu {
            Inventory,
            Player,
            Map,
            Codex,
            Journal,
            Crafting,
            Count
        };
        static constexpr std::array<std::string_view, static_cast<std::size_t>(Menu::Count)> Names {"inventory", "player", "map", "codex", "journal", "crafting"};

        static bool Parse(std::string_view name, Menu &menu) {
            for (std::size_t index = 0; index < Names.size(); ++index) {
                if (Names[index] == name) {
                    menu = static_cast<Menu>(index);
                    return true;
                }
            }
            return false;
        }

        void SetEnabled(const std::string &resource, Menu menu, bool enabled) {
            auto &owners = _owners[static_cast<std::size_t>(menu)];
            if (enabled) {
                owners.erase(resource);
            }
            else {
                owners.insert(resource);
            }
        }

        bool IsEnabled(Menu menu) const {
            return _owners[static_cast<std::size_t>(menu)].empty();
        }

        bool IsViewEnabled(SDK::wh::framework::E_UIApseView::Type view) const {
            switch (view) {
            case SDK::wh::framework::E_UIApseView::Type::Inventory:
            case SDK::wh::framework::E_UIApseView::Type::InventoryInfo: return IsEnabled(Menu::Inventory);
            case SDK::wh::framework::E_UIApseView::Type::Player:
            case SDK::wh::framework::E_UIApseView::Type::PlayerInfo: return IsEnabled(Menu::Player);
            case SDK::wh::framework::E_UIApseView::Type::Map:
            case SDK::wh::framework::E_UIApseView::Type::MapLegend: return IsEnabled(Menu::Map);
            case SDK::wh::framework::E_UIApseView::Type::Codex: return IsEnabled(Menu::Codex);
            case SDK::wh::framework::E_UIApseView::Type::QuestLog:
            case SDK::wh::framework::E_UIApseView::Type::QuestDiary: return IsEnabled(Menu::Journal);
            case SDK::wh::framework::E_UIApseView::Type::Crafting: return IsEnabled(Menu::Crafting);
            default: return true; // None is also used during native teardown.
            }
        }

        void Release(const std::string &resource) {
            for (auto &owners : _owners) {
                owners.erase(resource);
            }
        }

        void Reset() {
            for (auto &owners : _owners) {
                owners.clear();
            }
        }

        bool HasRestrictions() const {
            for (const auto &owners : _owners) {
                if (!owners.empty()) {
                    return true;
                }
            }
            return false;
        }

      private:
        std::array<std::set<std::string>, static_cast<std::size_t>(Menu::Count)> _owners;
    };

    class GameMenus final: public GameMenuPolicy, public KCDC::Core::IFeatureService {
      public:
        static GameMenus &Get();
        static void Install();
        void Reset() override {
            GameMenuPolicy::Reset();
        }

        /** Also permits the custom character creator's use of the player stage. */
        bool AllowsView(SDK::wh::framework::E_UIApseView::Type view) const;
        void Update() override;

      private:
        static bool __fastcall ShowHook(SDK::wh::framework::I_UIApse *apse, SDK::wh::framework::E_UIApseView::Type view, SDK::wh::framework::S_UIApseShowParams *params);
        static void __fastcall MoveToViewHook(SDK::wh::guimodule::C_UIApseCamera *camera, SDK::wh::framework::E_UIApseView::Type view, bool animate);

        bool(__fastcall *_showOriginal)(SDK::wh::framework::I_UIApse *, SDK::wh::framework::E_UIApseView::Type, SDK::wh::framework::S_UIApseShowParams *) = nullptr;
        void(__fastcall *_moveToViewOriginal)(SDK::wh::guimodule::C_UIApseCamera *, SDK::wh::framework::E_UIApseView::Type, bool)                         = nullptr;
    };
} // namespace KCDC::Features::UI

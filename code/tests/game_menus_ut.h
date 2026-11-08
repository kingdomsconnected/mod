#pragma once

#include <new>

#include "features/ui/ui_game_menus.h"

MODULE(game_menus, {
    IT("defaults to enabled and groups each menu with its detail pages", {
        KCDC::Features::UI::GameMenuPolicy policy;
        EQUALS(policy.HasRestrictions(), false);
        for (int value = 0; value <= 10; ++value) {
            EQUALS(policy.IsViewEnabled(static_cast<SDK::wh::framework::E_UIApseView::Type>(value)), true);
        }
        const int groups[] = {-1, 5, 0, 0, 1, 1, 2, 3, 2, 4, 4};
        for (int menu = 0; menu < 6; ++menu) {
            policy.SetEnabled("first", static_cast<KCDC::Features::UI::GameMenuPolicy::Menu>(menu), false);
            for (int view = 0; view <= 10; ++view) {
                EQUALS(policy.IsViewEnabled(static_cast<SDK::wh::framework::E_UIApseView::Type>(view)), (groups[view] != menu));
            }
            policy.Release("first");
        }
    });

    IT("repeated disable is idempotent and resources cannot release each other", {
        KCDC::Features::UI::GameMenuPolicy policy;
        policy.SetEnabled("first", KCDC::Features::UI::GameMenuPolicy::Menu::Map, false);
        policy.SetEnabled("first", KCDC::Features::UI::GameMenuPolicy::Menu::Map, false);
        policy.SetEnabled("second", KCDC::Features::UI::GameMenuPolicy::Menu::Map, false);
        policy.SetEnabled("second", KCDC::Features::UI::GameMenuPolicy::Menu::Inventory, false);
        policy.SetEnabled("first", KCDC::Features::UI::GameMenuPolicy::Menu::Map, true);
        EQUALS(policy.IsEnabled(KCDC::Features::UI::GameMenuPolicy::Menu::Map), false);
        policy.Release("first");
        EQUALS(policy.IsEnabled(KCDC::Features::UI::GameMenuPolicy::Menu::Inventory), false);
        policy.Release("second");
        EQUALS(policy.HasRestrictions(), false);
        policy.SetEnabled("first", KCDC::Features::UI::GameMenuPolicy::Menu::Journal, false);
        policy.Reset();
        EQUALS(policy.HasRestrictions(), false);
    });

    IT("rejects unknown menu names", {
        KCDC::Features::UI::GameMenuPolicy::Menu menu;
        for (const std::string_view name : KCDC::Features::UI::GameMenuPolicy::Names) {
            EQUALS(KCDC::Features::UI::GameMenuPolicy::Parse(name, menu), true);
        }
        EQUALS(KCDC::Features::UI::GameMenuPolicy::Parse("", menu), false);
        EQUALS(KCDC::Features::UI::GameMenuPolicy::Parse("inventroy", menu), false);
    });

    IT("refusing Show releases native callbacks once through their own allocator", {
        struct Callback final: SDK::wh::framework::S_UIApseShowParams::CallbackImplementation {
            int deleted = 0;
            bool deallocated = false;
            void Opaque0() override {}
            void Opaque1() override {}
            void Opaque2() override {}
            void Opaque3() override {}
            void DeleteThis(bool deallocate) override {
                ++deleted;
                deallocated = deallocate;
            }
        };
        auto params = SDK::wh::framework::S_UIApseShowParams::Make();
        params.ReleaseCallback();
        Callback heapCallback;
        params.m_callback = &heapCallback;
        params.ReleaseCallback();
        params.ReleaseCallback();
        EQUALS(heapCallback.deleted, 1);
        EQUALS(heapCallback.deallocated, true);
        auto *inlineCallback = new (params.m_callbackStorage) Callback;
        params.m_callback = inlineCallback;
        params.ReleaseCallback();
        EQUALS(inlineCallback->deleted, 1);
        EQUALS(inlineCallback->deallocated, false);
        inlineCallback->~Callback();
    });
});

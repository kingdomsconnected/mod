#pragma once

#include <v8.h>

#include <string>

namespace KCDC::Scripting {
    // The global `Hud`: the game's own notification, info-text and tutorial
    // slots, and the switch it keeps for each HUD element, driven from a
    // client resource.
    //
    // Client-side and unreplicated: a message shown here is shown to the
    // player at this machine and nobody else, which is also why there is no
    // server-side twin. Every message verb is one call into
    // KCDC::Features::UI::Hud, which the debug panel's notifications tab
    // drives as well, and returns whether the HUD was there to take it. The
    // element and monologue verbs go to KCDC::Features::UI::HudElements,
    // which needs no HUD.
    // The screen-effect verbs go to KCDC::Features::UI::HudScreenEffects,
    // which draws through the renderer rather than the HUD movie.
    // worldToScreen goes to the renderer's own projection, through
    // KCDC::Features::Camera, so a resource can pin its own overlay to the
    // world.
    class Hud {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_IsInDialogue(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ShowNotification(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ShowInfoText(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_HideInfoText(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ShowPerkUsed(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ShowPerkGained(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ShowComboLearned(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ShowReputationChanged(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ShowRandomEventResult(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ShowXpGain(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ShowTutorial(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_HideTutorial(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_HideCurrentTutorial(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_HideCodexActionHint(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ClearNotifications(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetElementVisible(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsElementVisible(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetMonologueEnabled(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsMonologueEnabled(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetStoryCutscenesEnabled(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsStoryCutscenesEnabled(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ScreenEffects(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_AddScreenEffect(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_UpdateScreenEffect(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_RemoveScreenEffect(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_RemoveAllScreenEffects(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsScreenEffectActive(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_WorldToScreen(const v8::FunctionCallbackInfo<v8::Value> &info);
    };

    namespace Events {
        /** A monologue line of the local character was kept silent and unsubtitled. */
        void MonologueSuppressed(const std::string &text, const std::string &textKey);

        /** A story cutscene a quest graph asked for was not played. */
        void StoryCutsceneSuppressed(const std::string &cutscene);
    } // namespace Events
} // namespace KCDC::Scripting

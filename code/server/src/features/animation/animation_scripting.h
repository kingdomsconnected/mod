#pragma once

#include "shared/features/animation/animation_playback.h"
#include "shared/features/animation/face_state.h"

#include <v8.h>

#include <string>

namespace KCDC::Scripting {
    // The global `Animations`: every animation the game plays on a human
    // body, and every prop it gives one to hold.
    class Animations {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        /**
         * Reads `(fragment, options?)` starting at argument `first` into a
         * request, the one spelling `player.playAnimation` and
         * `npc.playAnimation` share. Throws a JS error naming `caller` and
         * returns false for anything it cannot use.
         */
        static bool ParseRequest(const v8::FunctionCallbackInfo<v8::Value> &info, int first, const char *caller, Shared::Game::AnimationPlayback &out);

        /** Reads `stopAnimation`'s `options?` at argument `first`: whether to `standUp`. */
        static bool ParseStandUp(const v8::FunctionCallbackInfo<v8::Value> &info, int first);

        /**
         * Reads `setFacialExpression`'s `(fragment, options?)` starting at
         * argument `first`. Throws a JS error naming `caller` and returns
         * false for anything it cannot use.
         */
        static bool ParseExpression(const v8::FunctionCallbackInfo<v8::Value> &info, int first, const char *caller, std::string &fragment, std::string &tags);

        /** Reads `setTalking`'s `(talking, style?)` starting at argument `first`, throwing as `ParseExpression` does. */
        static bool ParseTalking(const v8::FunctionCallbackInfo<v8::Value> &info, int first, const char *caller, Shared::Game::TalkStyle &style);

      private:
        static void JS_List(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Props(const v8::FunctionCallbackInfo<v8::Value> &info);
    };
} // namespace KCDC::Scripting

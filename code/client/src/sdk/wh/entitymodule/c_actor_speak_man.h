#pragma once

#include "s_speak_data.h"

#include "../../patterns.h"

#include <utils/hooking/hooking.h>

namespace SDK::wh::entitymodule {
    /**
     * One actor's speech: the line it is saying and the bubble above it.
     *
     * Opaque. The mod never allocates one -- C_Actor builds it on first use --
     * and only ever starts and ends a line through it.
     */
    class C_ActorSpeakMan {
      public:
        /** Says `data`, replacing the current line. Declines unless forced. */
        void Speak(const S_SpeakData &data) {
            hook::this_call<void>(gPatterns.C_ActorSpeakMan__Speak, this, &data);
        }

        /**
         * Releases the bubble and clears its cached id. Null-safe, and needed
         * before every line: the update fills a bubble only on the frame it
         * creates one, so a replacement keeps the old text.
         */
        void HideBubble() {
            hook::this_call<void>(gPatterns.C_ActorSpeakMan__HideBubble, this);
        }
    };
} // namespace SDK::wh::entitymodule

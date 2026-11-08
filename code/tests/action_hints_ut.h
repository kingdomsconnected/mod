#pragma once

#include "features/ui/ui_hint_input_queue.h"

MODULE(action_hint_input, {
    IT("keeps queued input attached to the displayed hint when its slot changes", {
        KCDC::Features::UI::HintInputQueue queue;
        queue.SetTarget("primary", 10, 0);
        queue.Capture("primary", 0);
        queue.SetTarget("primary", 11, 0);
        queue.Capture("primary", 0);
        const auto pending = queue.Drain();
        EQUALS(pending.size(), std::size_t(2));
        EQUALS(pending[0], 10U);
        EQUALS(pending[1], 11U);
        EQUALS(queue.Drain().empty(), true);
    });

    IT("does not redirect a dispatch batch after a handler removes its hint", {
        KCDC::Features::UI::HintInputQueue queue;
        queue.SetTarget("primary", 10, 0);
        queue.Capture("primary", 0);
        queue.Capture("primary", 0);
        const auto pending = queue.Drain();
        queue.SetTarget("primary", 11, 0);
        EQUALS(pending.size(), std::size_t(2));
        EQUALS(pending[1], 10U);
        queue.Capture("primary", 0);
        const auto next = queue.Drain();
        EQUALS(next.size(), std::size_t(1));
        EQUALS(next[0], 11U);
    });

    IT("ignores hidden hints and wrong input types and clears input on reset", {
        KCDC::Features::UI::HintInputQueue queue;
        queue.SetTarget("hold", 10, 2);
        queue.Capture("hold", 0);
        queue.Capture("unknown", 2);
        EQUALS(queue.Drain().empty(), true);
        queue.SetTarget("hold", 0, 2);
        queue.Capture("hold", 2);
        EQUALS(queue.Drain().empty(), true);
        queue.SetTarget("hold", 11, 2);
        queue.Capture("hold", 2);
        queue.Reset();
        queue.Capture("hold", 2);
        EQUALS(queue.Drain().empty(), true);
    });
});

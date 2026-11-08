#pragma once

#include <cstdint>

namespace KCDC::Features::Items {
    /** Replay the entry point that owns the caller's feedback after approval. */
    struct NativeFoodUse {
        enum class Source {
            Actor,
            Player,
            Inventory
        };
        std::int32_t amount   = 1;
        std::int32_t context  = -1;
        bool checkFood        = true;
        Source source         = Source::Actor;
        bool saveConfirmed    = false;
        std::int32_t saveSlot = -1;
    };
} // namespace KCDC::Features::Items

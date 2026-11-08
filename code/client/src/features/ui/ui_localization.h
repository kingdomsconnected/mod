#pragma once

#include <string>

namespace KCDC::Features::UI::Localization {
    /**
     * Registers `text` for display and answers the key that resolves to it.
     *
     * The native UI takes localization keys, not text, and '@'-prefixes every
     * whitespace-separated word of what it is given before resolving it -- which
     * is why handing one a sentence renders as "@Kingdoms @Connected". The key
     * returned here is a single word, so it survives that pass intact.
     *
     * The keys come from a small ring, so a published string stays valid only
     * until the ring wraps: long enough for the engine to resolve it on the
     * same frame, and not a place to keep anything.
     */
    std::string PublishText(std::string text);
} // namespace KCDC::Features::UI::Localization

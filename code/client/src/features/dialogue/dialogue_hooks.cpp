#include "features/dialogue/dialogue_module.h"

#include <utils/hooking/hook_function.h>

/**
 * The interactive-dialogue HUD has no accessor.
 *
 * The GUI module builds it through RTTR reflection and keeps the resulting
 * `shared_ptr` in a type-keyed registry, so there is no `GetInstance` to walk
 * to and no module offset to read. Being told when the game constructs it is
 * the cheap, stable way to the real object -- and it is the real object that
 * matters, because everything the conversation system draws goes through the
 * game's own virtuals on it rather than through the movie.
 *
 * The same install takes the game's own conversation twin setup, which
 * places a participant's twin without checking that one was made. See
 * `DialogueModule::BeginTwinSetup`.
 */
static InitFunction init(
    []() {
        KCDC::Features::Dialogue::DialogueModule::Install();
    },
    "Dialogue");

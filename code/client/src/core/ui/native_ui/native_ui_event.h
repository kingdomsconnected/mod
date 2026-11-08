#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace KCDC::Core {
    /** One value out of ActionScript. Every number is a double there. */
    using NativeUIValue = std::variant<std::monostate, bool, double, std::string>;

    enum class NativeUIEventKind : std::uint8_t {
        Ready,
        Unload,
        /** A declared `<events>` entry; `name` is its XML display name. */
        Element,
        /** An ActionScript function the mod installed was called. */
        Callback,
        /** The focus action map delivered a navigation or accept/back action. */
        Action,
    };

    /**
     * A movie-side event on its way to whatever asked for it.
     *
     * Queued rather than dispatched where it arrives: ActionScript calls into
     * the mod from inside whatever native call the mod itself was making, so
     * running script there would re-enter a page the outer call is still
     * working on.
     */
    struct NativeUIEvent {
        NativeUIEventKind kind = NativeUIEventKind::Element;
        std::string host;
        std::string name;
        /** Empty when the host rather than one of its pages owns the event. */
        std::string page;
        /** The handle inside that page the event is about, when there is one. */
        std::string target;
        int activationMode = 0;
        std::vector<NativeUIValue> arguments;
    };
} // namespace KCDC::Core

#pragma once

namespace SDK {
    /** Listener attached to a Scaleform movie by GUI controllers. */
    class IMovieListener {
      public:
        virtual ~IMovieListener() = default;
    };

    static_assert(sizeof(IMovieListener) == 0x8, "IMovieListener size mismatch");
} // namespace SDK

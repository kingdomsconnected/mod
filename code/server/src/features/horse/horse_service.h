#pragma once

namespace KCDC::Features::Horse {
    /** Server-side authority over who is in which saddle. */
    class HorseService final {
      public:
        /** Registers the mount/dismount RPC. Call once at startup. */
        static void Install();
    };
} // namespace KCDC::Features::Horse

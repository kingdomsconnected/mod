#pragma once

#include <vector>

namespace KCDC::Core {
    class Application;

    /**
     * What the shell knows a session participant by.
     *
     * The shell sweeps one ordered list rather than naming every service at
     * every call site, so joining the session tick is one line in
     * feature_registry.cpp instead of a call that has to be remembered in two
     * different functions. Every method carries a do-nothing default: a
     * service overrides only the ones it actually has, and each sweep stays a
     * single loop over the whole list.
     *
     * Reset() means "the session ended: drop everything it left". A service
     * clears only what it owns, because the sweep reaches every other one
     * beside it -- which is what retired the reset chain that used to run out
     * of LocalEvents. What a service deliberately keeps across a session is
     * named in a comment where it is kept, not left to be inferred from its
     * absence.
     */
    class IFeatureService {
      public:
        virtual ~IFeatureService() = default;
        virtual void Update() {}
        virtual void Reset() {}
        virtual void NativeContextChanged() {}
    };

    /**
     * Fills `services` with every participant of a connected session, in the
     * order the shell sweeps them. This is the only place the shell knows
     * which features exist.
     *
     * The entries are borrowed: they point at members of `application` and at
     * process-lifetime singletons, so the list must not outlive either.
     */
    void BuildSessionFeatureRegistry(Application &application, std::vector<IFeatureService *> &services);
} // namespace KCDC::Core

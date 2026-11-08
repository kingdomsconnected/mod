#pragma once

#include <external/sentry/wrapper.h>

#include <string>

namespace KCDC::Core {

    std::string BuildSentryRelease();

    // Shared by the entry point and Application::Init so the two cannot drift.
    Framework::External::Sentry::InitOptions BuildCrashReporterOptions(const std::string &modulePath);

    // Who a report counts as in Sentry: this machine, pseudonymised. Empty when
    // the machine has no hardware id, which leaves the user unset rather than
    // folding every such player into one.
    std::string BuildCrashReporterUserId();

} // namespace KCDC::Core

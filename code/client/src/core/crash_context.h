#pragma once

#include "application.h"

#include <external/sentry/wrapper.h>

#include <string>

namespace KCDC::Core::CrashContext {
    inline Framework::External::Sentry::Wrapper *Reporter() {
        if (!gApplication) {
            return nullptr;
        }
        Framework::External::Sentry::Wrapper *reporter = gApplication->GetCrashReporter();
        if (!reporter || !reporter->IsValid()) {
            return nullptr;
        }
        return reporter;
    }

    inline void SetTag(const std::string &key, const std::string &value) {
        if (Framework::External::Sentry::Wrapper *reporter = Reporter()) {
            reporter->SetTag(key, value);
        }
    }

    inline void SetContext(const std::string &name, const Framework::External::Sentry::ContextFields &fields) {
        if (Framework::External::Sentry::Wrapper *reporter = Reporter()) {
            reporter->SetContext(name, fields);
        }
    }

    inline void Breadcrumb(const std::string &category, const std::string &message, Framework::External::Sentry::Level level = Framework::External::Sentry::Level::Info, const Framework::External::Sentry::ContextFields &data = {}) {
        if (Framework::External::Sentry::Wrapper *reporter = Reporter()) {
            reporter->AddBreadcrumb(category, message, level, data);
        }
    }
} // namespace KCDC::Core::CrashContext

#include "crash_reporting.h"

#include "shared/version.h"

#include <utils/hardware_id.h>
#include <utils/hashing.h>

namespace KCDC::Core {
    namespace {
        constexpr const char *kSentryDSN = "https://8339b4120163fbb5c43b199ccaffbf31@o982453.ingest.us.sentry.io/4512090251460608";
    } // namespace

    std::string BuildSentryRelease() {
        // Not modVersion: that feeds NetworkPeer::BuildToken, where a sha breaks every handshake.
        std::string release = std::string("kcdc@") + KCDC::Shared::Version::rel;
        if (KCDC::Shared::Version::git && KCDC::Shared::Version::git[0]) {
            release += std::string("+") + KCDC::Shared::Version::git;
        }
        return release;
    }

    Framework::External::Sentry::InitOptions BuildCrashReporterOptions(const std::string &modulePath) {
        Framework::External::Sentry::InitOptions options;
        options.dsn         = kSentryDSN;
        options.handlerPath = modulePath.empty() ? "." : modulePath;
        options.release     = BuildSentryRelease();
        options.attachments.push_back(options.handlerPath + "/logs/KCDC.log");
#ifdef NDEBUG
        options.environment = "retail";
#else
        options.environment = "dev";
#endif
        return options;
    }

    std::string BuildCrashReporterUserId() {
        // The raw id is the system volume's serial, which the game servers see in
        // the connection identity; Sentry only needs to tell machines apart.
        const std::string hardwareId = Framework::Utils::GetHardwareId();
        if (hardwareId.empty()) {
            return {};
        }
        return std::to_string(Framework::Utils::Hashing::CalculateCRC32("kcdc:" + hardwareId));
    }
} // namespace KCDC::Core

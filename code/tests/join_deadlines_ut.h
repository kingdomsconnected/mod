#pragma once

#include "core/states/join_deadlines.h"

#include <cstdint>
#include <string>

MODULE(join_deadlines, {
    IT("starts only the session-held load completion without a timeout", {
        constexpr std::uint32_t load = 0x3F93;
        EQUALS(KCDC::Core::States::LoadReadinessTimeout(load, load, true, 50.0f), -1.0f);
        EQUALS(KCDC::Core::States::LoadReadinessTimeout(load, load, false, 50.0f), 50.0f);
        EQUALS(KCDC::Core::States::LoadReadinessTimeout(0x4022, load, true, 50.0f), 50.0f);
        EQUALS(KCDC::Core::States::LoadReadinessTimeout(0x2A, load, false, 12.0f), 12.0f);
    });

    IT("words the loading hint from the download's figures", {
        EQUALS(KCDC::Core::States::DownloadHintText(0, 848 * 1024, 0, false, -1), std::string("Downloading the server's resources: 0 KB of 848 KB (0%)\nThis can take a while on a slow connection."));
        EQUALS(KCDC::Core::States::DownloadHintText(424 * 1024, 848 * 1024, 0, false, -1), std::string("Downloading the server's resources: 424 KB of 848 KB (50%)\nThis can take a while on a slow connection."));
        EQUALS(KCDC::Core::States::DownloadHintText(900 * 1024, 848 * 1024, 0, false, -1), std::string("Downloading the server's resources: 848 KB of 848 KB (100%)\nThis can take a while on a slow connection."));
    });

    IT("shows the rate while the download moves", {
        EQUALS(KCDC::Core::States::DownloadHintText(424 * 1024, 848 * 1024, 1536 * 1024, false, -1), std::string("Downloading the server's resources: 424 KB of 848 KB (50%)\n1536 KB/s. This can take a while on a slow connection."));
    });

    IT("says the download waits for a slot, and how many are ahead when known", {
        EQUALS(KCDC::Core::States::DownloadHintText(0, 848 * 1024, 0, true, 3), std::string("Downloading the server's resources: 0 KB of 848 KB (0%)\nWaiting for the server to free a download slot (3 ahead)."));
        EQUALS(KCDC::Core::States::DownloadHintText(0, 848 * 1024, 0, true, 0), std::string("Downloading the server's resources: 0 KB of 848 KB (0%)\nWaiting for the server to free a download slot."));
        EQUALS(KCDC::Core::States::DownloadHintText(0, 848 * 1024, 0, true, -1), std::string("Downloading the server's resources: 0 KB of 848 KB (0%)\nWaiting for the server to free a download slot."));
        // A stale rate from before the wait does not show beside it.
        EQUALS(KCDC::Core::States::DownloadHintText(0, 848 * 1024, 4096, true, 1), std::string("Downloading the server's resources: 0 KB of 848 KB (0%)\nWaiting for the server to free a download slot (1 ahead)."));
    });
});

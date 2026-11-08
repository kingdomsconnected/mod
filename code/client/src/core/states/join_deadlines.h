#pragma once

#include <cstdint>
#include <string>

namespace KCDC::Core::States {
    /**
     * The timeout a readiness observer starts with. The game captures it once,
     * at `C_ReadinessObserver::StartEx`, as an absolute deadline; a negative
     * one is the engine's own "never expires". The load completion the
     * session's hold parks must not fire on the game's 50 s clock while a long
     * download is still moving, so it waits without one; the join's own
     * deadline (kServerAnswerTimeoutMs) and the Framework's download stall
     * watch (InstanceOptions::downloadStallTimeoutMs) refuse a join that is
     * genuinely stuck. Every other observer keeps the game's.
     */
    constexpr float LoadReadinessTimeout(std::uint32_t mask, std::uint32_t loadMask, bool sessionHeld, float nativeTimeout) {
        return sessionHeld && mask == loadMask ? -1.0f : nativeTimeout;
    }

    /** Whole KB with a thousands separator would be the game's own style; the hint keeps plain KB. */
    inline std::string KilobytesText(std::uint64_t bytes) {
        return std::to_string(bytes / 1024) + " KB";
    }

    /**
     * What the loading screen's hint line says while the join is downloading.
     * Resource packages arrive behind the game's own loading screen, where the
     * mod's page cannot draw, and on a slow link that screen otherwise sits
     * unchanged for minutes. The screen re-asks for its hint on a timer, so the
     * figures follow the download without anything pushing them. A download
     * waiting for one of the server's slots says so, with how many are ahead
     * of it when the server told; a moving one shows its rate.
     */
    inline std::string DownloadHintText(std::uint64_t bytesDownloaded, std::uint64_t bytesTotal, std::uint64_t bytesPerSecond, bool waitingForSlot, std::int32_t queuePosition) {
        const std::uint64_t done    = bytesDownloaded < bytesTotal ? bytesDownloaded : bytesTotal;
        const std::uint64_t percent = bytesTotal > 0 ? done * 100 / bytesTotal : 0;
        std::string text            = "Downloading the server's resources: " + KilobytesText(done) + " of " + KilobytesText(bytesTotal) + " (" + std::to_string(percent) + "%)";
        if (waitingForSlot) {
            text += "\nWaiting for the server to free a download slot";
            if (queuePosition > 0) {
                text += " (" + std::to_string(queuePosition) + " ahead)";
            }
            text += ".";
        }
        else if (bytesPerSecond > 0) {
            text += "\n" + KilobytesText(bytesPerSecond) + "/s. This can take a while on a slow connection.";
        }
        else {
            text += "\nThis can take a while on a slow connection.";
        }
        return text;
    }
} // namespace KCDC::Core::States

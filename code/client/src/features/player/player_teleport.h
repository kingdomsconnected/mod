#pragma once

#include "sdk/types.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace KCDC::Features::Player {
    /** Stage the last teleport reached, surfaced by the debug panel. */
    enum class TeleportResult : std::uint8_t {
        None,
        Succeeded,
        NoLocalPlayer,
        NoUnlockedDoor,
        InvalidPosition,
        GroundUnavailable,
        ContextChanged,
    };

    const char *TeleportResultToString(TeleportResult result);

    enum class TeleportTarget : std::uint8_t {
        Position,
        NearestUnlockedDoor,
    };

    /** Where to put the player, in the coordinates of the loaded level. */
    struct TeleportRequest {
        SDK::Vec3 position {};
        SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};

        /** False leaves the body facing however it already was. */
        bool hasRotation = false;

        /** What a spawn wants: the world around a join has not streamed in, and a body released into it falls. */
        bool waitForGround = false;

        /** Nonzero confines an editor request to this unmounted offline player. */
        SDK::EntityId offlineEditorPlayer = 0;

        std::string label; // display only, echoed back as the last result
        TeleportTarget target = TeleportTarget::Position;
    };

    /**
     * Moves the local player somewhere else in the loaded level.
     *
     * The overlay issues requests from the render thread, so they are queued
     * and drained by Update() on the game thread, the same way the spawners
     * do it. Nothing is kept across frames but the last result and the hold
     * in progress; the player entity is re-resolved on every use.
     */
    class PlayerTeleporter final {
      public:
        /** Any thread. The returned handle is never zero. */
        std::uint64_t QueueTeleport(const TeleportRequest &request);

        // Game thread only; driven from CCryAction::PostUpdate.
        void Update();

        /** Drops the queue and any hold in progress. */
        void Reset();

        TeleportResult GetLastResult() const;
        std::string GetLastLabel() const;
        SDK::Vec3 GetLastPosition() const;
        std::uint64_t GetLastPlacementId() const;

      private:
        struct QueuedRequest {
            TeleportRequest request;
            std::uint64_t placementId = 0;
        };

        // The pose is re-asserted every frame, not set once: a living
        // physics entity falls through a floor that has not streamed in.
        struct GroundHold {
            SDK::Vec3 position {};
            SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
            bool hasRotation          = false;
            std::uint64_t placementId = 0;
            std::uint32_t frames      = 0;
            std::uint64_t deadlineMs  = 0;
        };

        // Returns whether a ground hold is now running for it.
        bool Execute(const TeleportRequest &request, std::uint64_t placementId);

        // True once the hold has released.
        bool ServiceGroundHold();

        static bool ApplyPlacement(const SDK::Vec3 &position, const SDK::Quat &rotation, bool hasRotation);

        // Tells the application the placement is done: whether the body is
        // in the level, which is all a join waits on.
        static void SettlePlacement(std::uint64_t placementId, bool success);

        mutable std::mutex _mutex;
        std::vector<QueuedRequest> _pending;
        TeleportResult _lastResult = TeleportResult::None;
        std::string _lastLabel;
        SDK::Vec3 _lastPosition {};
        std::uint64_t _lastPlacementId = 0;

        std::uint64_t _nextPlacementId = 1;

        // Game thread only; nothing outside Update() touches it.
        GroundHold _hold;
        bool _holding = false;
    };
} // namespace KCDC::Features::Player

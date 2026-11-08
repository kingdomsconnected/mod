#pragma once

#include "shared/features/door/door_rpc.h"
#include "shared/game/door_catalog.h"

#include <mafianet/virtual_world.h>

#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace KCDC::Shared::Game {
    /** Whether navigation can cross now, must wait for the swing, or cannot use this door. */
    enum class DoorPassage : std::uint8_t {
        Ready,
        Opening,
        Blocked
    };

    /** One change a door report asks for, in the words a script sees. */
    enum class DoorAction : std::uint8_t {
        Open,
        Close,
        Unlock,
        Lockpick
    };

    inline const char *DoorActionName(DoorAction action) {
        switch (action) {
        case DoorAction::Open: return "open";
        case DoorAction::Close: return "close";
        case DoorAction::Unlock: return "unlock";
        case DoorAction::Lockpick: return "lockpick";
        }
        return "invalid";
    }

    /**
     * The changes a local player's press of a door prompt makes, in the order
     * a script sees them, as retail AnimDoor:OnUsed makes them: the lock
     * first when the door is locked -- the key, or the latch on the manual
     * side -- then the push the door's direction leaves; a pick on its own,
     * which only starts the minigame. None for a door with no direction yet,
     * which OnUsed ignores.
     */
    struct DoorPress {
        std::array<DoorAction, 2> actions {};
        std::size_t actionCount = 0;
    };

    /** `direction` is AnimDoor.lua's `nDirection`: -1 shut, 1 open, 0 not yet set. */
    inline DoorPress EvaluateDoorPress(bool lockpick, float direction, bool locked) {
        DoorPress press;
        if (lockpick) {
            press.actions[press.actionCount++] = DoorAction::Lockpick;
            return press;
        }
        if (direction == 0.0F) {
            return press;
        }
        if (locked) {
            press.actions[press.actionCount++] = DoorAction::Unlock;
        }
        press.actions[press.actionCount++] = direction < 0.0F ? DoorAction::Open : DoorAction::Close;
        return press;
    }

    /**
     * The horizontal part of a hung door's forward axis, as
     * `IEntity::GetForwardDir()` reads it: column 1 of the rotation, the axis
     * AnimDoor:IsAccessedFromFront measures a body against. The server keys
     * its rules to it, and a client to the same value.
     */
    inline glm::vec2 HungDoorForward(const glm::quat &rotation) {
        const glm::vec3 forward = rotation * glm::vec3(0.0F, 1.0F, 0.0F);
        const glm::vec2 flat(forward.x, forward.y);
        const float length = glm::length(flat);
        return length > 1e-4F ? flat / length : glm::vec2(0.0F, 1.0F);
    }

    /**
     * How far from a door's pivot a body may be and still have worked it.
     * AnimDoor.lua allows 2 m from the leaf's centre for use and for the
     * lockpick; the rest covers the leaf's own width and a replicated
     * position that trails the body by a few ticks.
     */
    constexpr float kMaxDoorReach = 6.0F;

    /**
     * How close to the door's plane a body may stand before the server stops
     * trusting its own reading of the side. The replicated position lags, and
     * a player leaning through a doorway is legitimately on either side.
     */
    constexpr float kDoorSideTolerance = 0.5F;

    /** What the server holds about a door, and what a client says it did to it. */
    struct DoorReport {
        bool open           = false;
        bool locked         = false;
        bool lockedByServer = false; // a script or operator locked it, not the level
        bool usable         = true;  // a script has not made it unusable to players

        float actorX = 0.0F;
        float actorY = 0.0F;
        float actorZ = 0.0F;

        KCDC::Shared::RPC::DoorEvent event = KCDC::Shared::RPC::DoorEvent::Manipulation;
        bool reportedOpen                  = false;
        bool reportedLocked                = false;
    };

    /**
     * What a report amounts to under the game's own rules.
     *
     * `refusal` is null when the engine's rules allow every action; a script
     * may still refuse it after that. `open` and `locked` are the state the
     * door is left in if it is accepted.
     */
    struct DoorVerdict {
        std::array<DoorAction, 2> actions {};
        std::size_t actionCount = 0;
        const char *refusal     = nullptr;
        bool open               = false;
        bool locked             = false;

        /** Whether the body stood on the side with the keyhole. */
        bool keySide = false;
    };

    /**
     * Folds one report into the door the level placed at `door`.
     *
     * A client only ever reports what its own player did, so a lock edge it
     * carries on a manipulation is only honoured when it unlocks -- the key
     * turned in the same use as the push. Any other difference in the lock
     * bit on a manipulation is that client lagging the server, not a change.
     */
    inline DoorVerdict EvaluateDoorReport(const DoorPlacement &door, const DoorReport &report) {
        DoorVerdict verdict;
        verdict.open   = report.open;
        verdict.locked = report.locked;

        const auto push = [&verdict](DoorAction action) {
            verdict.actions[verdict.actionCount++] = action;
        };
        const auto refuse = [&verdict](const char *reason) {
            if (verdict.refusal == nullptr) {
                verdict.refusal = reason;
            }
        };

        if (!report.usable) {
            refuse("a script made the door unusable");
        }

        const float dx = report.actorX - door.positionX;
        const float dy = report.actorY - door.positionY;
        const float dz = report.actorZ - door.positionZ;
        if (!(dx * dx + dy * dy + dz * dz <= kMaxDoorReach * kMaxDoorReach)) {
            refuse("the body is out of reach of the door");
        }

        // Which side the body is on, and whether the server can tell.
        const float frontDistance = DoorFrontDistance(door, report.actorX, report.actorY);
        const bool sideKnown      = std::fabs(frontDistance) > kDoorSideTolerance;
        verdict.keySide           = IsDoorKeySide(door, frontDistance >= 0.0F);

        switch (report.event) {
        case KCDC::Shared::RPC::DoorEvent::Lockpicked:
            push(DoorAction::Lockpick);
            verdict.locked = false;
            if (!report.locked) {
                refuse("the door is not locked");
            }
            if (!door.lockpickable) {
                refuse("the door cannot be lockpicked");
            }
            // The pick is only offered from the keyhole's side, and a lock
            // with no keyhole offers it nowhere.
            if (!IsDoorKeySide(door, true) && !IsDoorKeySide(door, false)) {
                refuse("the door has no keyhole to pick");
            }
            else if (sideKnown && !verdict.keySide) {
                refuse("the body is on the side without the keyhole");
            }
            break;

        case KCDC::Shared::RPC::DoorEvent::Manipulation:
            if (report.locked && !report.reportedLocked) {
                push(DoorAction::Unlock);
                verdict.locked = false;
            }
            if (report.reportedOpen != report.open) {
                push(report.reportedOpen ? DoorAction::Open : DoorAction::Close);
                verdict.open = report.reportedOpen;
            }
            break;
        }

        for (std::size_t index = 0; index < verdict.actionCount; ++index) {
            if (verdict.actions[index] == DoorAction::Unlock && report.lockedByServer) {
                refuse("the server locked the door; only a lockpick opens it");
            }
            if (verdict.actions[index] == DoorAction::Open && verdict.locked) {
                refuse("the door is locked");
            }
        }
        return verdict;
    }

    /**
     * Which of a door's or gate's copies a player in `playerWorld` shows: 2
     * for its own world's, 1 for the global copy, 0 for one it must not. A
     * player in another world sees its world's copy and the global one, and
     * its own stands over the global; one in the global world sees every
     * world's copies, and only the global one is its own.
     */
    constexpr int WorldCopyRank(std::uint32_t copyWorld, std::uint32_t playerWorld) {
        if (copyWorld == playerWorld) {
            return 2;
        }
        return copyWorld == MafiaNet::VIRTUAL_WORLD_GLOBAL ? 1 : 0;
    }
} // namespace KCDC::Shared::Game

#pragma once

#include "shared/features/level_edit/level_edit_definition.h"
#include "shared/features/level_edit/level_edit_entity.h"

#include <mafianet/virtual_world.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Features::LevelEdit {
    // Everything the server does to the level's own objects, in one place, so every caller changes them the same way.
    //
    // An edit is a replicated entity every player in its virtual world has, whenever they join; each client finds the object by the edit's key and takes it out, moves it, or puts it back when the edit goes. Nothing is reported back, and nothing is kept past a restart: a script applies its map again on boot.
    class LevelEdits final {
      public:
        // Records one edit, or restates the one already keyed the same way in that world, so a map applied twice changes nothing. Null, with `error` saying why, for a definition no client could find the object by, or one with a transform that is not finite.
        static Shared::Entities::LevelEditEntity *Apply(const Shared::Game::LevelEditDefinition &definition, std::uint32_t virtualWorld, std::string &error);

        // Takes the object out of the world, or puts it back in, keeping any move.
        static void SetHidden(Shared::Entities::LevelEditEntity *edit, bool hidden);

        // Swaps the object for another mesh, or with an empty `model` gives it back. False, with `error` saying why, for a mesh that is neither in the prop catalog nor streamed by this server.
        static bool SetModel(Shared::Entities::LevelEditEntity *edit, const std::string &model, std::string &error);

        // Whether `model` is a mesh an edit can swap an object for: a prop catalog path, or one this server streams. `error` says why not.
        static bool IsSwapModel(const std::string &model, std::string &error);

        // Puts the object at a pose of the server's choosing; the scale is clamped, as the editor clamps its own. False for a grass edit, whose box is its key.
        static bool Move(Shared::Entities::LevelEditEntity *edit, const glm::vec3 &position, const glm::quat &rotation, const glm::vec3 &scale);

        // Takes a move back: the object stands where the level put it again.
        static void ClearMove(Shared::Entities::LevelEditEntity *edit);

        // Drops the edit: every client puts the object back as the level had it.
        static void Restore(Shared::Entities::LevelEditEntity *edit);

        // Drops every edit in one virtual world, or in all of them. Returns how many went.
        static std::size_t RestoreAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        // Every live edit in one virtual world, or in all of them, in no particular order.
        static std::vector<Shared::Entities::LevelEditEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        // The live edit keyed like `definition` in that world, or null.
        static Shared::Entities::LevelEditEntity *Find(const Shared::Game::LevelEditDefinition &definition, std::uint32_t virtualWorld);
    };
} // namespace KCDC::Features::LevelEdit

#pragma once

#include "shared/features/prop/prop_entity.h"
#include "shared/features/prop/prop_rpc.h"
#include "shared/game/prop_models.h"

#include <mafianet/virtual_world.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace KCDC::Features::Prop {
    // Everything the server does to a replicated prop. One place per verb, so the `/prop` command and the scripting binding are two callers of the same code -- and so the event a verb raises cannot be raised by one caller and forgotten by the next.
    //
    // The server owns the model and the physics outright, and a static prop's pose. A rigid prop's body is the one thing a client runs: the nearest simulates it and streams its pose, and `Update` turns its reports of the body stopping into `propRest`.
    //
    // Every query takes the virtual world it is about, because a prop belongs to one and the interest grid already refuses to stream it anywhere else. An empty optional means all of them, which is what a script gets when it names none.
    //
    // No V8. What happened goes through this feature's own KCDC::Scripting::Events, declared in prop_scripting.h, so a server with no resources loaded runs all of this unchanged.
    class Props final {
      public:
        // Spawns and replicates one mesh: a row of the shared catalog, or -- with `modelIndex` at `kNoPropModel` -- a path the server streams. Raises `propSpawn`.
        //
        // Null for a model index off the end of the catalog, a streamed path no stream-lane pak carries, a position, rotation or scale that is not finite, or a `propSpawn` handler that destroyed the new prop.
        //
        // The scale is clamped rather than refused: it sizes a mesh, and a number outside the range describes nothing the engine can build. `axisScale` sizes each axis on top of `scale`, for World Builder exports.
        static Shared::Entities::PropEntity *Spawn(std::size_t modelIndex, const std::string &streamedModel, const glm::vec3 &position, const glm::quat &rotation, float scale, Shared::Entities::PropPhysics physics, std::uint32_t virtualWorld,
            const glm::vec3 &axisScale = glm::vec3(1.0F));

        // Whether `path` is a mesh the server streams right now, so `Spawn` can take it.
        static bool IsStreamedModel(const std::string &path);

        // Despawns it everywhere, raising `propDestroy` while the entity can still be read.
        static void Destroy(Shared::Entities::PropEntity *prop);

        // Removes every prop in one virtual world, raising `propDestroy` for each. Returns how many went.
        static std::size_t DestroyAll(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        static std::size_t Count(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        // Every live prop in one virtual world, in no particular order.
        static std::vector<Shared::Entities::PropEntity *> Collect(std::optional<MafiaNet::VirtualWorldId> virtualWorld);

        // Rebuilds the prop on every client. A rigid one is simulated by the nearest client within 60 m, which streams its pose to everyone else; one that stops being rigid is taken back from it.
        static bool SetPhysics(Shared::Entities::PropEntity *prop, Shared::Entities::PropPhysics physics);

        // Resizes the prop, which also rebuilds it: the proxy is sized at spawn and never re-read. False for a scale that is not finite or that it already has.
        static bool SetScale(Shared::Entities::PropEntity *prop, float scale);

        // Sets a rigid prop's mass in kilograms, which rebuilds it: the body is given its mass as it is physicalized. 0 leaves it to the mesh's collision. False for no prop, a mass that is not finite or is outside 0..`PropEntity::kMaxMass`, or the mass it already has.
        static bool SetMass(Shared::Entities::PropEntity *prop, float mass);

        // Sends `motion` to the client simulating a rigid prop, electing one first if nobody is. False for no prop, one that is not rigid, a motion that fails its own check, or no player near enough to simulate it.
        static bool Move(Shared::Entities::PropEntity *prop, Shared::RPC::PropMotion motion);

        // Stops a rigid prop and puts it back where it was spawned, for its simulator and everyone else. False for no prop, or one that is not rigid.
        static bool Reset(Shared::Entities::PropEntity *prop);

        // Raises `propRest` for each rigid prop its simulator has just reported resting. Every server tick.
        static void Update();

        // How far a reported rest may lie from where the prop last rested or was spawned. One farther is a simulator's mistake, or a lie, and the prop is put back.
        static constexpr float kMaxRestTravel = 100.0F;

        // The mesh a prop was built from: its catalog row, or for a streamed one its path, its file stem and the group `kcdc/<resource>`. Views into the catalog or into the prop, so read them before the prop can go.
        // Nullopt for no prop, or a row this build's catalog does not have.
        static std::optional<Shared::Game::PropModelEntry> ModelOf(const Shared::Entities::PropEntity *prop);

        // One line per prop in one virtual world, newest first, for `/prop list`.
        static std::vector<std::string> Describe(std::optional<MafiaNet::VirtualWorldId> virtualWorld, std::size_t limit);
    };
} // namespace KCDC::Features::Prop

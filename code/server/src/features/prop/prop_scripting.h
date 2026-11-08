#pragma once

#include <unordered_map>

#include "shared/features/prop/prop_entity.h"

#include <scripting/builtins/entity.h>

#include <v8.h>
#include <v8pp/class.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // Server-side handle for a replicated prop. Wraps the prop's NetworkID and resolves the live entity through replication, so a handle to a despawned prop reads as gone rather than dangling. Transform and virtual world come from the framework's Entity base.
    //
    // Bindings only: every verb is one call into KCDC::Features::Prop::Props, which the `/prop` command also uses, so a prop spawned from a script and one spawned from chat are seeded the same way and raise the same event.
    class Prop final: public Framework::Scripting::Builtins::Entity {
      public:
        Prop(std::uint64_t networkId): Framework::Scripting::Builtins::Entity(networkId) {}

        // The live prop, or nullptr once it has been despawned.
        Shared::Entities::PropEntity *ResolveProp() const;

        // Empty when this build's catalog does not have the prop's model.
        std::string GetModel() const;
        std::string GetModelName() const;
        std::string GetModelGroup() const;

        // `rigid`, `static` or `none`. Assignment rebuilds the prop on every client, as does the scale: both are read at spawn and never again.
        std::string GetPhysics() const;
        void SetPhysics(const std::string &physics);

        float GetScale() const;
        void SetScale(float scale);

        // A rigid prop's mass in kilograms, 0 for the one its mesh's collision gives it. Assignment rebuilds the prop on every client.
        float GetMass() const;
        void SetMass(float mass);

        std::string ToString() const override;

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Prop> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Prop>>> _classes;
    };

    // What this feature tells loaded resources happened to a prop. Event arguments are network IDs; the handles a resource sees are created inside the scripting isolate.
    namespace Events {
        void PropSpawned(std::uint64_t propNetworkId);

        // Raised while the prop is still resolvable, so a handler can read its model and its pose.
        void PropDestroyed(std::uint64_t propNetworkId);

        // Raised once the prop has been moved to where a client says its body came to rest.
        void PropRested(std::uint64_t propNetworkId, const glm::vec3 &position, const glm::quat &rotation);
    } // namespace Events
} // namespace KCDC::Scripting

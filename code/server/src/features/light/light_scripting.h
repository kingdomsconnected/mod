#pragma once

#include "shared/features/light/light_entity.h"
#include <memory>
#include <scripting/builtins/entity.h>
#include <unordered_map>
#include <v8.h>
#include <v8pp/class.hpp>

namespace KCDC::Scripting {
    class Light final: public Framework::Scripting::Builtins::Entity {
      public:
        explicit Light(std::uint64_t id): Framework::Scripting::Builtins::Entity(id) {}
        Shared::Entities::LightEntity *ResolveLight() const;
        glm::vec3 GetColor() const;
        void SetColor(v8::Isolate *isolate, v8::Local<v8::Value> value);
        float GetIntensity() const;
        void SetIntensity(float intensity);
        float GetRadius() const;
        void SetRadius(float radius);
        bool GetCastShadows() const;
        void SetCastShadows(v8::Isolate *isolate, v8::Local<v8::Value> value);
        void SetPosition(const Framework::Scripting::Builtins::Vector3 &position);
        void Destroy();
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static void UnregisterIsolate(v8::Isolate *isolate);
        static v8pp::class_<Light> &GetClass(v8::Isolate *isolate);

      private:
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Light>>> _classes;
    };
} // namespace KCDC::Scripting

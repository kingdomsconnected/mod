#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <v8.h>
#include <v8pp/class.hpp>

namespace KCDC::Scripting {
    /** A client-local native light. The service owns its carrier; JS only keeps an id. */
    class Light final {
      public:
        explicit Light(std::uint32_t id): _id(id) {}
        double GetId() const {
            return _id;
        }
        bool IsValid() const;
        glm::vec3 GetPosition() const;
        void SetPosition(v8::Isolate *isolate, v8::Local<v8::Value> value);
        glm::vec3 GetColor() const;
        void SetColor(v8::Isolate *isolate, v8::Local<v8::Value> value);
        float GetIntensity() const;
        void SetIntensity(v8::Isolate *isolate, v8::Local<v8::Value> value);
        float GetRadius() const;
        void SetRadius(v8::Isolate *isolate, v8::Local<v8::Value> value);
        bool GetCastShadows() const;
        void SetCastShadows(v8::Isolate *isolate, v8::Local<v8::Value> value);
        void Destroy();
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8pp::class_<Light> &GetClass(v8::Isolate *isolate);

      private:
        std::uint32_t _id = 0;
        inline static std::unique_ptr<v8pp::class_<Light>> _class;
    };
} // namespace KCDC::Scripting

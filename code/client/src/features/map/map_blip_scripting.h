#pragma once

#include "features/map/map_blip_service.h"

#include <glm/glm.hpp>

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    // A mark this client owns on the game's compass and map screen. The handle is an id resolved against the registry on every read, so a blip that was removed reads as invalid rather than dangling.
    //
    // Client-side and unreplicated. The server has a Blip of its own, a replicated entity that writes the same registry; those rows are the server's, so no handle here reads or changes them.
    class Blip final {
      public:
        explicit Blip(double id): _id(static_cast<std::uint32_t>(id)) {}

        std::uint32_t GetId() const {
            return _id;
        }

        double GetIdNumber() const {
            return static_cast<double>(_id);
        }
        bool IsValid() const;
        bool IsLive() const;

        glm::vec3 GetPosition() const;
        void SetPosition(glm::vec3 position);

        std::string GetType() const;
        void SetType(std::string type);

        double GetState() const;
        void SetState(double state);

        std::string GetLabel() const;
        void SetLabel(std::string label);

        bool GetCompass() const;
        void SetCompass(bool compass);

        bool GetMap() const;
        void SetMap(bool map);

        bool GetLegend() const;
        void SetLegend(bool legend);

        std::string ToString() const;

        static v8pp::class_<Blip> &GetClass(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        std::uint32_t _id = 0;

        static std::unique_ptr<v8pp::class_<Blip>> _class;
    };
} // namespace KCDC::Scripting

#pragma once

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    /**
     * A preview this client is drawing: geometry that is in the world without
     * being part of it.
     *
     * A handle, resolved against the registry on every read, so a ghost that
     * was destroyed reads as invalid rather than dangling. Client-side and
     * unreplicated -- a preview everyone should see is a prop the server
     * spawned, not a ghost.
     */
    class PropGhost final {
      public:
        explicit PropGhost(double id): _id(static_cast<std::uint32_t>(id)) {}

        std::uint32_t GetId() const {
            return _id;
        }

        double GetIdNumber() const {
            return static_cast<double>(_id);
        }

        bool IsValid() const;
        std::string ToString() const;

        static v8pp::class_<PropGhost> &GetClass(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        std::uint32_t _id = 0;

        static std::unique_ptr<v8pp::class_<PropGhost>> _class;
    };

    /** Publishes the `PropPlacer` global: the placement mode. */
    void RegisterPropPlacer(v8::Isolate *isolate, v8::Local<v8::Object> global);
} // namespace KCDC::Scripting

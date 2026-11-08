#pragma once

#include <v8.h>
#include <v8pp/class.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace KCDC::Scripting {
    /**
     * A screen this client draws with the game's own Scaleform renderer, and
     * the second way to put a UI up beside `Web`.
     *
     * The handle is a number resolved against the service on every call, so a
     * closed screen reads as invalid rather than through a dangling page.
     * Client-side and unreplicated, which is why there is no server twin.
     */
    class NativeScreen final {
      public:
        explicit NativeScreen(double id): _id(static_cast<std::uint32_t>(id)) {}

        std::uint32_t GetId() const {
            return _id;
        }
        double GetIdNumber() const {
            return static_cast<double>(_id);
        }
        bool IsValid() const;
        bool IsReady() const;
        /** The screen's own root clip, or null before its movie composed it. */
        v8::Local<v8::Value> GetRoot() const;
        std::string ToString() const;

        static v8pp::class_<NativeScreen> &GetClass(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        std::uint32_t _id = 0;

        static std::unique_ptr<v8pp::class_<NativeScreen>> _class;
    };

    /** One display object inside a screen: a clip, a sprite or a text field. */
    class NativeClip final {
      public:
        NativeClip(double screen, std::string key): _screen(static_cast<std::uint32_t>(screen)), _key(std::move(key)) {}

        std::uint32_t GetScreen() const {
            return _screen;
        }
        const std::string &GetKey() const {
            return _key;
        }
        double GetScreenNumber() const {
            return static_cast<double>(_screen);
        }
        std::string GetKeyString() const {
            return _key;
        }
        bool IsValid() const;
        std::string ToString() const;

        static v8pp::class_<NativeClip> &GetClass(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        std::uint32_t _screen = 0;
        std::string _key;

        static std::unique_ptr<v8pp::class_<NativeClip>> _class;
    };

    /** A handle on one of the game's own UI elements, driven in place. */
    class NativeElement final {
      public:
        NativeElement(std::string name, double instanceId): _name(std::move(name)), _instanceId(static_cast<int>(instanceId)) {}

        const std::string &GetName() const {
            return _name;
        }
        std::string GetNameString() const {
            return _name;
        }
        int GetInstanceId() const {
            return _instanceId;
        }
        double GetInstanceIdNumber() const {
            return static_cast<double>(_instanceId);
        }
        bool IsValid() const;
        bool IsVisible() const;
        std::string ToString() const;

        static v8pp::class_<NativeElement> &GetClass(v8::Isolate *isolate);
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        std::string _name;
        int _instanceId = 0;

        static std::unique_ptr<v8pp::class_<NativeElement>> _class;
    };

    /** Publishes the global `NativeUI` and the three handle classes. */
    void RegisterNativeUI(v8::Isolate *isolate, v8::Local<v8::Object> global);

    /** Releases every retained handler while the session's isolate is still alive. */
    void DropNativeUIHandlers();
} // namespace KCDC::Scripting

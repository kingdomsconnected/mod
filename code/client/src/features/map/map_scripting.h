#pragma once

#include <glm/glm.hpp>

#include <v8.h>

#include <cstdint>

namespace KCDC::Scripting {
    // The globals `WorldMap`, `Compass` and `Travel`: the map screen, the compass strip, and fast travel as something to read.
    //
    // Client-side and unreplicated, which is why there are no server-side twins. Every verb returns whether the game was there to take it; before the GUI module has built its pages it is not.
    class WorldMap {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_GetWaypoint(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_MoveWaypoint(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ClearWaypoint(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_IsCategoryVisible(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetCategoryVisible(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_CanStartTravel(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_Discover(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_RevealFog(const v8::FunctionCallbackInfo<v8::Value> &info);
    };

    // What this feature tells loaded resources happened on this machine. Nothing here mentions V8, so MapService can report what the map screen did without depending on the scripting layer's classes.
    //
    // Local and unreplicated throughout: what the server decides arrives as an RPC instead.
    namespace Events {
        /** The map screen came up. */
        void MapOpened();

        /** The map screen went away. */
        void MapClosed();

        /** Dropped or moved, which the game does through one action. */
        void MapWaypointSet(const glm::vec3 &position, std::int32_t mapId, bool moved);

        void MapWaypointCleared(const glm::vec3 &position, std::int32_t mapId);
    } // namespace Events
} // namespace KCDC::Scripting

#pragma once

#include "features/world/world_query_service.h"

#include <v8.h>

namespace KCDC::Scripting {
    /**
     * The global `World`: what this machine's engine can be asked about the
     * level around it. Synchronous, because this is the machine that has the
     * world; the server's `World` carries the same questions as promises.
     */
    class World {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_Raycast(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_RaycastAll(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_GetGroundZ(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_ResolveGround(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_EntitiesInRadius(const v8::FunctionCallbackInfo<v8::Value> &info);
    };

    namespace Events {
        /** The local player's swing met something solid that is not a body. */
        void WeaponHit(const KCDC::Features::World::WorldQueryService::RayHit &hit);
    } // namespace Events
} // namespace KCDC::Scripting

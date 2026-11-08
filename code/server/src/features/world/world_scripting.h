#pragma once

#include <v8.h>

#include <cstdint>
#include <string>

namespace KCDC::Scripting {
    // The global `World`: the server's clock and weather.
    //
    // Reads are properties, writes are verbs returning whether they were taken
    // -- the clock only moves forward and a blend cannot be interrupted, so a
    // refusal is ordinary and a script has to see it. Every verb is one call
    // into KCDC::Features::World::World, which `/world` parses into as well.
    class World {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);

      private:
        static void JS_SetTime(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetHour(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetTimeScale(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetWeather(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetRain(const v8::FunctionCallbackInfo<v8::Value> &info);
        static void JS_SetWind(const v8::FunctionCallbackInfo<v8::Value> &info);
    };

    // What this feature tells loaded resources the clock and the sky did. Neither event carries an entity, so nothing here is a handle.
    namespace Events {
        // The clock crossed midnight, whether it walked over or a verb jumped past.
        void WorldDayChanged(std::int64_t day);

        // The sky started blending to another time-of-day preset.
        void WorldWeatherChanged(const std::string &preset, const std::string &previous, double seconds);
    } // namespace Events
} // namespace KCDC::Scripting

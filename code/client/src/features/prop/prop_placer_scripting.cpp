#include "prop_placer_scripting.h"

#include "core/application.h"
#include "core/scripting/script_context.h"

#include "features/prop/prop_ghost.h"
#include "features/prop/prop_placer_service.h"

#include "common/module_support.h"
#include "common/vector_math.h"

#include "shared/streamed_assets/streamed_asset_policy.h"
#include "shared/game/prop_models.h"
#include "shared/scripting/catalog.h"
#include "shared/scripting/option_field.h"

#include <logging/logger.h>
#include <scripting/resource/resource_manager.h>
#include <scripting/v8pp_types.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace KCDC::Scripting {
    std::unique_ptr<v8pp::class_<PropGhost>> PropGhost::_class;

    namespace {
        constexpr const char *kLogger = "PropPlacer";

        /** Every event a placement session raises, which is the whole vocabulary `on` takes. */
        constexpr std::array<std::string_view, 5> kPlacementEvents {"move", "confirm", "rejected", "cancel", "end"};

        KCDC::Features::Prop::PropGhostService *Ghosts() {
            return Core::gApplication != nullptr ? &Core::gApplication->GetPropGhostService() : nullptr;
        }

        KCDC::Features::Prop::PropPlacerService *Placer() {
            return Core::gApplication != nullptr ? &Core::gApplication->GetPropPlacerService() : nullptr;
        }

        void ThrowError(v8::Isolate *isolate, const std::string &message) {
            isolate->ThrowException(v8::Exception::Error(v8pp::to_v8(isolate, message)));
        }

        struct Handler {
            v8::Global<v8::Function> callback;
            std::string resource;
        };

        // Handlers live here rather than on a handle, which a script may copy.
        std::map<std::string, std::vector<Handler>> gPlacementHandlers;

        /** Which resource owns each live ghost, so a stopped one takes its previews with it. */
        std::map<std::uint32_t, std::string> gGhostOwners;

        /** Which resource started the running session, or empty when none is. */
        std::string gSessionOwner;

        bool gCleanupInstalled = false;

        // --- Reading options -------------------------------------------------

        bool ReadBool(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *name, bool &out) {
            v8::Local<v8::Value> value = OptionField(isolate, options, name);
            if (value.IsEmpty() || !value->IsBoolean()) {
                return false;
            }
            out = value->BooleanValue(isolate);
            return true;
        }

        bool ReadVec3(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *name, SDK::Vec3 &out) {
            v8::Local<v8::Value> value = OptionField(isolate, options, name);
            if (value.IsEmpty() || !value->IsObject()) {
                return false;
            }
            const glm::vec3 asked = v8pp::convert<glm::vec3>::from_v8(isolate, value);
            if (!KCDC::Common::IsFinite(asked)) {
                return false;
            }
            out = KCDC::Common::ToNative(asked);
            return true;
        }

        bool ReadQuat(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *name, SDK::Quat &out) {
            v8::Local<v8::Value> value = OptionField(isolate, options, name);
            if (value.IsEmpty() || !value->IsObject()) {
                return false;
            }
            const glm::quat asked = v8pp::convert<glm::quat>::from_v8(isolate, value);
            if (!KCDC::Common::IsFinite(asked)) {
                return false;
            }
            out = KCDC::Common::ToNative(glm::normalize(asked));
            return true;
        }

        std::uint8_t Channel(double value) {
            return static_cast<std::uint8_t>(std::clamp(value, 0.0, 1.0) * 255.0 + 0.5);
        }

        /**
         * A tint off a `Color`, or off anything else carrying r/g/b/a. Read
         * property by property so a plain literal works as well.
         */
        bool ReadTint(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *name, KCDC::Features::Prop::GhostTint &out) {
            v8::Local<v8::Value> value = OptionField(isolate, options, name);
            if (value.IsEmpty() || !value->IsObject()) {
                return false;
            }

            v8::Local<v8::Object> color    = value.As<v8::Object>();
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            double channels[4] = {1.0, 1.0, 1.0, 1.0};
            const char *names[4] {"r", "g", "b", "a"};
            for (int index = 0; index < 4; ++index) {
                v8::Local<v8::Value> component;
                if (color->Get(context, v8pp::to_v8(isolate, names[index])).ToLocal(&component) && component->IsNumber()) {
                    const double asked = v8pp::from_v8<double>(isolate, component);
                    if (std::isfinite(asked)) {
                        channels[index] = asked;
                    }
                }
            }

            out = KCDC::Features::Prop::PackGhostTint(Channel(channels[0]), Channel(channels[1]), Channel(channels[2]), Channel(channels[3]));

            // Zero means "keep the mesh's own materials", so nudge off it.
            if (out == KCDC::Features::Prop::kGhostUntinted) {
                out = KCDC::Features::Prop::PackGhostTint(0, 0, 0, 1);
            }
            return true;
        }

        std::vector<std::string> ReadStrings(v8::Isolate *isolate, v8::Local<v8::Object> options, const char *name) {
            std::vector<std::string> out;
            v8::Local<v8::Value> value = OptionField(isolate, options, name);
            if (value.IsEmpty() || !value->IsArray()) {
                return out;
            }

            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> array     = value.As<v8::Array>();
            for (std::uint32_t index = 0; index < array->Length(); ++index) {
                v8::Local<v8::Value> element;
                if (array->Get(context, index).ToLocal(&element) && element->IsString()) {
                    out.push_back(v8pp::from_v8<std::string>(isolate, element));
                }
            }
            return out;
        }

        /**
         * The mesh list, and the per-mesh offsets and rotations that line up
         * with it by index. Parallel arrays rather than an array of part
         * objects: the contract generator takes an options bag as an inline
         * TS literal, and nothing else in it nests one inside an array.
         */
        bool ReadParts(v8::Isolate *isolate, v8::Local<v8::Object> options, std::vector<KCDC::Features::Prop::GhostPart> &parts, std::string &error) {
            std::vector<std::string> models = ReadStrings(isolate, options, "models");

            if (models.empty()) {
                v8::Local<v8::Value> single = OptionField(isolate, options, "model");
                if (!single.IsEmpty() && single->IsString()) {
                    models.push_back(v8pp::from_v8<std::string>(isolate, single));
                }
            }

            if (models.empty()) {
                error = "model or models is required";
                return false;
            }

            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Value> offsets   = OptionField(isolate, options, "offsets");
            v8::Local<v8::Value> rotations = OptionField(isolate, options, "rotations");

            for (std::size_t index = 0; index < models.size(); ++index) {
                // By index rather than by path: that is the lookup
                // `Prop.spawn` uses, and it takes a file stem as well, so the
                // two cannot disagree about what a resource may ask for.
                const std::size_t row = KCDC::Shared::Game::FindPropModelIndex(models[index]);
                const KCDC::Shared::Game::PropModelEntry *entry = KCDC::Shared::Game::PropModelAt(row);

                KCDC::Features::Prop::GhostPart part;
                if (entry != nullptr) {
                    // The canonical path: a stem means nothing to the mesh loader.
                    part.model = std::string {entry->path};
                }
                else if (KCDC::Shared::StreamedAssets::Policy::IsStreamedModelPath(models[index]) && KCDC::Core::gApplication->GetStreamedAssetService().HasEntry(models[index])) {
                    // A mesh the server streams, already mounted here: the same paths `Prop.spawn` takes.
                    part.model = models[index];
                }
                else {
                    error = models[index] + " is neither in the prop catalog nor a mesh this server streams";
                    return false;
                }

                if (!offsets.IsEmpty() && offsets->IsArray()) {
                    v8::Local<v8::Value> element;
                    if (offsets.As<v8::Array>()->Get(context, static_cast<std::uint32_t>(index)).ToLocal(&element) && element->IsObject()) {
                        const glm::vec3 offset = v8pp::convert<glm::vec3>::from_v8(isolate, element);
                        if (KCDC::Common::IsFinite(offset)) {
                            part.offset = KCDC::Common::ToNative(offset);
                        }
                    }
                }

                if (!rotations.IsEmpty() && rotations->IsArray()) {
                    v8::Local<v8::Value> element;
                    if (rotations.As<v8::Array>()->Get(context, static_cast<std::uint32_t>(index)).ToLocal(&element) && element->IsObject()) {
                        const glm::quat rotation = v8pp::convert<glm::quat>::from_v8(isolate, element);
                        if (KCDC::Common::IsFinite(rotation)) {
                            part.rotation = KCDC::Common::ToNative(glm::normalize(rotation));
                        }
                    }
                }

                parts.push_back(std::move(part));
            }
            return true;
        }

        // --- Answering -------------------------------------------------------

        v8::Local<v8::Value> PoseToObject(v8::Isolate *isolate, const KCDC::Features::Prop::PlacementPose &pose) {
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "position"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(pose.position))).Check();
            result->Set(context, v8pp::to_v8(isolate, "rotation"), v8pp::convert<glm::quat>::to_v8(isolate, KCDC::Common::ToGlm(pose.rotation))).Check();
            result->Set(context, v8pp::to_v8(isolate, "normal"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(pose.normal))).Check();
            result->Set(context, v8pp::to_v8(isolate, "surface"), v8pp::to_v8(isolate, pose.surface)).Check();
            result->Set(context, v8pp::to_v8(isolate, "distance"), v8pp::to_v8(isolate, pose.distance)).Check();
            result->Set(context, v8pp::to_v8(isolate, "onSurface"), v8pp::to_v8(isolate, pose.onSurface)).Check();
            return result;
        }

        /** Raises one placement event into every resource listening for it. */
        void Raise(const char *event, const KCDC::Features::Prop::PlacementPose *pose, bool valid) {
            const auto it = gPlacementHandlers.find(event);
            if (it == gPlacementHandlers.end() || it->second.empty()) {
                return;
            }

            WithScriptContext([&](v8::Isolate *isolate, v8::Local<v8::Context> context, Framework::Scripting::ResourceManager *) {
                v8::Local<v8::Value> args[2];
                args[0] = pose != nullptr ? PoseToObject(isolate, *pose) : v8::Local<v8::Value>(v8::Null(isolate));
                args[1] = v8pp::to_v8(isolate, valid);

                // Copied first: a handler may unsubscribe from inside itself.
                std::vector<v8::Global<v8::Function>> callbacks;
                callbacks.reserve(it->second.size());
                for (const Handler &handler : it->second) {
                    callbacks.emplace_back(isolate, handler.callback.Get(isolate));
                }

                for (v8::Global<v8::Function> &callback : callbacks) {
                    v8::TryCatch guard(isolate);
                    v8::Local<v8::Function> function = callback.Get(isolate);
                    std::ignore                      = function->Call(context, v8::Undefined(isolate), 2, args);
                    if (guard.HasCaught()) {
                        Framework::Logging::GetLogger(kLogger)->warn("A placement \"{}\" handler threw", event);
                    }
                }
            });
        }

        /** The one sink the service reports to, which fans out to the handlers. */
        class ScriptSink final: public KCDC::Features::Prop::IPropPlacerSink {
          public:
            void OnPlacementMove(const KCDC::Features::Prop::PlacementPose &pose, bool valid) override {
                Raise("move", &pose, valid);
            }

            void OnPlacementConfirm(const KCDC::Features::Prop::PlacementPose &pose) override {
                Raise("confirm", &pose, true);
            }

            void OnPlacementRejected(const KCDC::Features::Prop::PlacementPose &pose) override {
                Raise("rejected", &pose, false);
            }

            void OnPlacementCancel() override {
                Raise("cancel", nullptr, false);
            }

            void OnPlacementEnd() override {
                gSessionOwner.clear();
                Raise("end", nullptr, false);
            }
        };

        ScriptSink gSink;

        void CleanupResource(const std::string &resource) {
            if (gSessionOwner == resource) {
                if (KCDC::Features::Prop::PropPlacerService *placer = Placer()) {
                    placer->End();
                }
                gSessionOwner.clear();
            }

            if (KCDC::Features::Prop::PropGhostService *ghosts = Ghosts()) {
                for (auto it = gGhostOwners.begin(); it != gGhostOwners.end();) {
                    if (it->second == resource) {
                        ghosts->Destroy(it->first);
                        it = gGhostOwners.erase(it);
                    }
                    else {
                        ++it;
                    }
                }
            }

            for (auto entry = gPlacementHandlers.begin(); entry != gPlacementHandlers.end();) {
                auto &handlers = entry->second;
                handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                   [&resource](const Handler &handler) {
                                       return handler.resource == resource;
                                   }),
                    handlers.end());
                entry = handlers.empty() ? gPlacementHandlers.erase(entry) : std::next(entry);
            }
        }

        void EnsureCleanupInstalled() {
            Framework::Scripting::ResourceManager *manager = KCDC::ScriptResources();
            if (manager == nullptr || gCleanupInstalled) {
                return;
            }
            manager->AddOnResourceStopped([](const std::string &resource) {
                CleanupResource(resource);
            });
            gCleanupInstalled = true;
        }

        // --- PropGhost -------------------------------------------------------

        PropGhost *Self(const v8::FunctionCallbackInfo<v8::Value> &info) {
            return v8pp::class_<PropGhost>::unwrap_object(info.GetIsolate(), info.This());
        }

        // JS: PropGhost.create({ model | models, position, ... }) -> PropGhost | null
        void JS_GhostCreate(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsObject()) {
                ThrowError(isolate, "PropGhost.create: expected ({ model, position, tint?, rotation?, scale? })");
                return;
            }

            KCDC::Features::Prop::PropGhostService *ghosts = Ghosts();
            if (ghosts == nullptr) {
                info.GetReturnValue().SetNull();
                return;
            }

            v8::Local<v8::Object> options = info[0].As<v8::Object>();

            std::vector<KCDC::Features::Prop::GhostPart> parts;
            std::string error;
            if (!ReadParts(isolate, options, parts, error)) {
                ThrowError(isolate, "PropGhost.create: " + error);
                return;
            }

            SDK::Vec3 position {};
            if (!ReadVec3(isolate, options, "position", position)) {
                ThrowError(isolate, "PropGhost.create: position is required and must be a Vector3");
                return;
            }

            SDK::Quat rotation {0.0F, 0.0F, 0.0F, 1.0F};
            ReadQuat(isolate, options, "rotation", rotation);

            float scale = 1.0F;
            FloatOption(isolate, options, "scale", scale);

            KCDC::Features::Prop::GhostTint tint = KCDC::Features::Prop::kGhostUntinted;
            ReadTint(isolate, options, "tint", tint);

            const std::uint32_t handle = ghosts->Create(parts, tint, position, rotation, scale);
            if (handle == KCDC::Features::Prop::PropGhostService::kNoHandle) {
                info.GetReturnValue().SetNull();
                return;
            }

            EnsureCleanupInstalled();
            gGhostOwners[handle] = KCDC::ResolveCallingResource(isolate);

            info.GetReturnValue().Set(v8pp::class_<PropGhost>::create_object(isolate, static_cast<double>(handle)));
        }

        // JS: ghost.setPose(position, rotation?, scale?)
        void JS_GhostSetPose(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                           = info.GetIsolate();
            PropGhost *self                                = Self(info);
            KCDC::Features::Prop::PropGhostService *ghosts = Ghosts();
            if (self == nullptr || ghosts == nullptr || info.Length() < 1 || !info[0]->IsObject()) {
                info.GetReturnValue().Set(false);
                return;
            }

            const glm::vec3 position = v8pp::convert<glm::vec3>::from_v8(isolate, info[0]);
            if (!KCDC::Common::IsFinite(position)) {
                ThrowError(isolate, "PropGhost.setPose: position must be a finite Vector3");
                return;
            }

            glm::quat rotation {1.0F, 0.0F, 0.0F, 0.0F};
            if (info.Length() > 1 && info[1]->IsObject()) {
                const glm::quat asked = v8pp::convert<glm::quat>::from_v8(isolate, info[1]);
                if (KCDC::Common::IsFinite(asked)) {
                    rotation = glm::normalize(asked);
                }
            }

            float scale = 1.0F;
            if (info.Length() > 2 && info[2]->IsNumber()) {
                const double asked = v8pp::from_v8<double>(isolate, info[2]);
                if (std::isfinite(asked)) {
                    scale = static_cast<float>(asked);
                }
            }

            info.GetReturnValue().Set(ghosts->SetPose(self->GetId(), KCDC::Common::ToNative(position), KCDC::Common::ToNative(rotation), scale));
        }

        // JS: ghost.setTint(color | null)
        void JS_GhostSetTint(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                           = info.GetIsolate();
            PropGhost *self                                = Self(info);
            KCDC::Features::Prop::PropGhostService *ghosts = Ghosts();
            if (self == nullptr || ghosts == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }

            KCDC::Features::Prop::GhostTint tint = KCDC::Features::Prop::kGhostUntinted;
            if (info.Length() > 0 && info[0]->IsObject()) {
                v8::Local<v8::Object> wrapper  = v8::Object::New(isolate);
                v8::Local<v8::Context> context = isolate->GetCurrentContext();
                wrapper->Set(context, v8pp::to_v8(isolate, "tint"), info[0]).Check();
                ReadTint(isolate, wrapper, "tint", tint);
            }

            info.GetReturnValue().Set(ghosts->SetTint(self->GetId(), tint));
        }

        // JS: ghost.setHidden(hidden)
        void JS_GhostSetHidden(const v8::FunctionCallbackInfo<v8::Value> &info) {
            PropGhost *self                                = Self(info);
            KCDC::Features::Prop::PropGhostService *ghosts = Ghosts();
            const bool hidden                              = info.Length() > 0 && info[0]->BooleanValue(info.GetIsolate());
            info.GetReturnValue().Set(self != nullptr && ghosts != nullptr && ghosts->SetHidden(self->GetId(), hidden));
        }

        // JS: ghost.destroy()
        void JS_GhostDestroy(const v8::FunctionCallbackInfo<v8::Value> &info) {
            PropGhost *self                                = Self(info);
            KCDC::Features::Prop::PropGhostService *ghosts = Ghosts();
            if (self == nullptr || ghosts == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }
            gGhostOwners.erase(self->GetId());
            info.GetReturnValue().Set(ghosts->Destroy(self->GetId()));
        }

        // JS: ghost.getBounds() -> PropGhostBounds | null
        void JS_GhostBounds(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                           = info.GetIsolate();
            PropGhost *self                                = Self(info);
            KCDC::Features::Prop::PropGhostService *ghosts = Ghosts();

            SDK::AABB bounds {};
            if (self == nullptr || ghosts == nullptr || !ghosts->Bounds(self->GetId(), bounds)) {
                info.GetReturnValue().SetNull();
                return;
            }

            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Object> result   = v8::Object::New(isolate);
            result->Set(context, v8pp::to_v8(isolate, "min"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(bounds.min))).Check();
            result->Set(context, v8pp::to_v8(isolate, "max"), v8pp::convert<glm::vec3>::to_v8(isolate, KCDC::Common::ToGlm(bounds.max))).Check();
            result->Set(context, v8pp::to_v8(isolate, "centre"),
                       v8pp::convert<glm::vec3>::to_v8(isolate, glm::vec3 {(bounds.min.x + bounds.max.x) * 0.5F, (bounds.min.y + bounds.max.y) * 0.5F, (bounds.min.z + bounds.max.z) * 0.5F}))
                .Check();
            result->Set(context, v8pp::to_v8(isolate, "size"), v8pp::convert<glm::vec3>::to_v8(isolate, glm::vec3 {bounds.max.x - bounds.min.x, bounds.max.y - bounds.min.y, bounds.max.z - bounds.min.z})).Check();
            info.GetReturnValue().Set(result);
        }

        // JS: PropGhost.destroyAll()
        void JS_GhostDestroyAll(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (KCDC::Features::Prop::PropGhostService *ghosts = Ghosts()) {
                ghosts->DestroyAll();
            }
            gGhostOwners.clear();
            info.GetReturnValue().SetUndefined();
        }

        // --- PropPlacer ------------------------------------------------------

        void ReadRules(v8::Isolate *isolate, v8::Local<v8::Object> options, KCDC::Features::Prop::PlacementRules &rules) {
            v8::Local<v8::Value> value = OptionField(isolate, options, "rules");
            if (value.IsEmpty() || !value->IsObject()) {
                return;
            }

            v8::Local<v8::Object> bag = value.As<v8::Object>();
            FloatOption(isolate, bag, "maxSlope", rules.maxSlopeDegrees);
            FloatOption(isolate, bag, "minDistance", rules.minDistanceFromPlayer);
            FloatOption(isolate, bag, "maxDistance", rules.maxDistanceFromPlayer);
            FloatOption(isolate, bag, "clearance", rules.clearance);
            ReadBool(isolate, bag, "requireSurface", rules.requireSurface);
            rules.surfaces         = ReadStrings(isolate, bag, "surfaces");
            rules.clearanceClasses = ReadStrings(isolate, bag, "clearanceClasses");

            rules.maxSlopeDegrees        = std::clamp(rules.maxSlopeDegrees, 0.0F, 90.0F);
            rules.minDistanceFromPlayer  = std::max(rules.minDistanceFromPlayer, 0.0F);
            rules.maxDistanceFromPlayer  = std::max(rules.maxDistanceFromPlayer, 0.0F);
            rules.clearance              = std::clamp(rules.clearance, 0.0F, 64.0F);
        }

        // JS: PropPlacer.begin({ model, ... }) -> boolean
        void JS_Begin(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 1 || !info[0]->IsObject()) {
                ThrowError(isolate, "PropPlacer.begin: expected ({ model, ... })");
                return;
            }

            KCDC::Features::Prop::PropPlacerService *placer = Placer();
            if (placer == nullptr) {
                info.GetReturnValue().Set(false);
                return;
            }

            v8::Local<v8::Object> options = info[0].As<v8::Object>();

            KCDC::Features::Prop::PlacementSpec spec;
            std::string error;
            if (!ReadParts(isolate, options, spec.parts, error)) {
                ThrowError(isolate, "PropPlacer.begin: " + error);
                return;
            }

            ReadTint(isolate, options, "validTint", spec.validTint);
            ReadTint(isolate, options, "invalidTint", spec.invalidTint);
            FloatOption(isolate, options, "range", spec.range);
            FloatOption(isolate, options, "fallbackDistance", spec.fallbackDistance);
            FloatOption(isolate, options, "scale", spec.scale);
            FloatOption(isolate, options, "verticalOffset", spec.verticalOffset);
            FloatOption(isolate, options, "yaw", spec.yawDegrees);
            ReadBool(isolate, options, "alignToSurface", spec.alignToSurface);
            ReadBool(isolate, options, "faceAwayFromPlayer", spec.faceAwayFromPlayer);
            ReadBool(isolate, options, "keepOpen", spec.keepOpen);
            ReadRules(isolate, options, spec.rules);

            EnsureCleanupInstalled();

            if (!placer->Begin(spec, &gSink)) {
                info.GetReturnValue().Set(false);
                return;
            }

            gSessionOwner = KCDC::ResolveCallingResource(isolate);
            info.GetReturnValue().Set(true);
        }

        // JS: PropPlacer.end()
        void JS_End(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (KCDC::Features::Prop::PropPlacerService *placer = Placer()) {
                placer->End();
            }
            info.GetReturnValue().SetUndefined();
        }

        // JS: PropPlacer.isActive() -> boolean
        void JS_IsActive(const v8::FunctionCallbackInfo<v8::Value> &info) {
            KCDC::Features::Prop::PropPlacerService *placer = Placer();
            info.GetReturnValue().Set(placer != nullptr && placer->IsActive());
        }

        // JS: PropPlacer.setVeto(vetoed) -> boolean
        void JS_SetVeto(const v8::FunctionCallbackInfo<v8::Value> &info) {
            KCDC::Features::Prop::PropPlacerService *placer = Placer();
            const bool vetoed                               = info.Length() > 0 && info[0]->BooleanValue(info.GetIsolate());
            info.GetReturnValue().Set(placer != nullptr && placer->SetVeto(vetoed));
        }

        // JS: PropPlacer.rotate(degrees) -> boolean
        void JS_Rotate(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                           = info.GetIsolate();
            KCDC::Features::Prop::PropPlacerService *placer = Placer();
            if (placer == nullptr || info.Length() < 1 || !info[0]->IsNumber()) {
                info.GetReturnValue().Set(false);
                return;
            }
            info.GetReturnValue().Set(placer->Rotate(static_cast<float>(v8pp::from_v8<double>(isolate, info[0]))));
        }

        // JS: PropPlacer.getPose() -> PlacementPose | null
        void JS_GetPose(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate                           = info.GetIsolate();
            KCDC::Features::Prop::PropPlacerService *placer = Placer();

            KCDC::Features::Prop::PlacementPose pose;
            bool valid = false;
            if (placer == nullptr || !placer->ReadPose(pose, valid)) {
                info.GetReturnValue().SetNull();
                return;
            }
            info.GetReturnValue().Set(PoseToObject(isolate, pose));
        }

        // JS: PropPlacer.on(event, handler) -> Unsubscribe
        void JS_On(const v8::FunctionCallbackInfo<v8::Value> &info) {
            v8::Isolate *isolate = info.GetIsolate();
            v8::HandleScope handleScope(isolate);

            if (info.Length() < 2 || !info[0]->IsString() || !info[1]->IsFunction()) {
                ThrowError(isolate, "PropPlacer.on: expected (event, handler)");
                return;
            }

            const std::string event = v8pp::from_v8<std::string>(isolate, info[0]);
            if (std::find(kPlacementEvents.begin(), kPlacementEvents.end(), event) == kPlacementEvents.end()) {
                ThrowError(isolate, "PropPlacer.on: " + event + " is not a placement event");
                return;
            }

            v8::Local<v8::Function> callback = info[1].As<v8::Function>();

            EnsureCleanupInstalled();

            Handler handler;
            handler.callback.Reset(isolate, callback);
            handler.resource = KCDC::ResolveCallingResource(isolate, callback);
            gPlacementHandlers[event].push_back(std::move(handler));

            // Closes over the event and the function and matches by identity,
            // so registering the same function twice and dropping one works.
            v8::Local<v8::Context> context = isolate->GetCurrentContext();
            v8::Local<v8::Array> captured  = v8::Array::New(isolate, 2);
            captured->Set(context, 0, info[0]).Check();
            captured->Set(context, 1, callback).Check();

            v8::Local<v8::Function> unsubscribe =
                v8::Function::New(
                    context,
                    [](const v8::FunctionCallbackInfo<v8::Value> &call) {
                        v8::Isolate *inner             = call.GetIsolate();
                        v8::Local<v8::Context> ctx     = inner->GetCurrentContext();
                        v8::Local<v8::Array> data      = call.Data().As<v8::Array>();
                        v8::Local<v8::Value> eventName;
                        v8::Local<v8::Value> function;
                        if (!data->Get(ctx, 0).ToLocal(&eventName) || !data->Get(ctx, 1).ToLocal(&function)) {
                            return;
                        }

                        const auto entry = gPlacementHandlers.find(v8pp::from_v8<std::string>(inner, eventName));
                        if (entry == gPlacementHandlers.end()) {
                            return;
                        }

                        auto &handlers = entry->second;
                        handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                           [&](const Handler &held) {
                                               return held.callback.Get(inner) == function;
                                           }),
                            handlers.end());
                        if (handlers.empty()) {
                            gPlacementHandlers.erase(entry);
                        }
                    },
                    captured)
                    .ToLocalChecked();

            info.GetReturnValue().Set(unsubscribe);
        }
    } // namespace

    bool PropGhost::IsValid() const {
        KCDC::Features::Prop::PropGhostService *ghosts = Ghosts();
        return ghosts != nullptr && ghosts->IsAlive(_id);
    }

    std::string PropGhost::ToString() const {
        std::ostringstream ss;
        ss << "PropGhost{ id: " << _id << (IsValid() ? "" : ", destroyed") << " }";
        return ss.str();
    }

    v8pp::class_<PropGhost> &PropGhost::GetClass(v8::Isolate *isolate) {
        if (!_class) {
            _class = std::make_unique<v8pp::class_<PropGhost>>(isolate, ClientCatalog(), "PropGhost", "A preview this client is drawing: one or more of the game's meshes standing in the world with no body and no collision.");
            _class->auto_wrap_objects(true);

            _class->ctor<double>(v8pp::metadata::docs("void", {v8pp::metadata::param("id", "number", false, "Identifier of a ghost that already exists.")}, "Creates a handle for a ghost that already exists; use PropGhost.create() to make one."));
            _class->function("toString", &PropGhost::ToString, v8pp::metadata::docs("string", {}, "Formats this ghost handle for logging and debugging.", "The ghost's id, and whether it is still standing."));

            _class->property("id", &PropGhost::GetIdNumber, v8pp::metadata::property_docs("number", "Identifier of this ghost, unique for the lifetime of the session. Ids are not reused."));
            _class->property("valid", &PropGhost::IsValid,
                v8pp::metadata::property_docs("boolean", "Whether the ghost is still standing. A handle to a destroyed one keeps reading; every call on it answers false. A level load destroys every ghost, because the nodes they are made of belong to the level that went."));

            _class->prototype_function("setPose", &JS_GhostSetPose,
                v8pp::metadata::docs("boolean",
                    {
                        v8pp::metadata::param("position", "Vector3", false, "Where the ghost stands, in world-space metres."),
                        v8pp::metadata::param("rotation", "Quaternion", true, "Which way it faces. The identity by default, which faces +Y."),
                        v8pp::metadata::param("scale", "number", true, "Uniform scale, 1 by default and clamped to 0.01 - 100."),
                    },
                    "Moves the whole ghost, parts and all. Cheap enough to call every frame: a part is moved with one engine call that re-sorts it into the world by itself.", "True while the ghost is still standing."));

            _class->prototype_function("setTint", &JS_GhostSetTint,
                v8pp::metadata::docs("boolean",
                    {v8pp::metadata::param("tint", "Color", true,
                        "The colour to wash the ghost in, with `a` below 1 making it translucent. Omit it, or pass null, to put every mesh back to the materials it shipped with.")},
                    "Recolours the ghost.\n\nThis is a real tint rather than a swapped material: the ghost's materials are private copies, and writing an opacity under 1 on one is what moves it into the renderer's transparent pass. A tint it already carries costs nothing, so this is safe to call every frame.",
                    "True while the ghost is still standing."));

            _class->prototype_function("setHidden", &JS_GhostSetHidden,
                v8pp::metadata::docs("boolean", {v8pp::metadata::param("hidden", "boolean", false, "Whether to take it out of the draw.")}, "Hides or shows the ghost without dropping it, which is cheaper than destroying and rebuilding one.",
                    "True while the ghost is still standing."));

            _class->prototype_function("getBounds", &JS_GhostBounds,
                v8pp::metadata::docs("PropGhostBounds | null", {}, "The box the ghost occupies right now, in world space, as the engine reports it for the meshes themselves.", "Its bounds, or null once it is gone and for a ghost whose meshes carry none."));

            _class->prototype_function("destroy", &JS_GhostDestroy, v8pp::metadata::docs("boolean", {}, "Takes the ghost out of the world.", "True when it was still there to take."));

            _class->static_function("create", &JS_GhostCreate,
                v8pp::metadata::docs("PropGhost | null",
                    {v8pp::metadata::param("options", "{ model?: string; models?: string[]; offsets?: Vector3[]; rotations?: Quaternion[]; position: Vector3; rotation?: Quaternion; scale?: number; tint?: Color }", false,
                        "`model` is one mesh from the shared prop catalog; `models` is several, which is how a blueprint of more than one piece is previewed. `offsets` and `rotations` line up with `models` by index and place each piece inside the ghost's own frame, so the whole thing turns and scales as one. A path the catalog does not carry is refused, because it is a path the server could never be asked to spawn afterwards.\n\n"
                        "`position`, `rotation` and `scale` are where the ghost stands. `tint` washes it in a colour, with `a` below 1 making it translucent; omitted, every mesh keeps the materials it shipped with.")},
                    "Puts a preview into the world: the game's own geometry, with no body, no collision and nothing replicated.\n\nThis is what a build preview, a placement outline or a menu turntable is made of. It is not a prop -- nobody else can see it, and it does not survive a level load.",
                    "The ghost, or null when this client has no world, at the ghost limit, and when not one of the meshes would load."));

            _class->static_function("destroyAll", &JS_GhostDestroyAll, v8pp::metadata::docs("void", {}, "Takes every ghost this client is drawing out of the world, including other resources'."));
        }
        return *_class;
    }

    void PropGhost::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &bounds = ClientCatalog().data_type("PropGhostBounds", "The box a ghost occupies, in world space.");
        bounds.add_property("min", "Vector3", "The low corner, in world-space metres.");
        bounds.add_property("max", "Vector3", "The high corner.");
        bounds.add_property("centre", "Vector3", "The middle of the box.");
        bounds.add_property("size", "Vector3", "How big it is on each axis, in metres.");

        GetClass(isolate).publish(global);
    }

    void RegisterPropPlacer(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (isolate == nullptr || global.IsEmpty()) {
            return;
        }

        auto &pose = ClientCatalog().data_type("PlacementPose", "Where a placement is aimed, and what it is aimed at.");
        pose.add_property("position", "Vector3", "Where the preview stands, in world-space metres.");
        pose.add_property("rotation", "Quaternion", "Which way it faces, including whatever the surface and the rotate keys added.");
        pose.add_property("normal", "Vector3", "The surface normal under the aim, as a unit vector. Straight up when nothing was hit.");
        pose.add_property("surface", "string", "The game's own name for what is underfoot -- `mat_dirt`, `mat_stone`. Empty when nothing was hit.");
        pose.add_property("distance", "number", "How far the aim point is from the camera, in metres.");
        pose.add_property("onSurface", "boolean", "Whether the aim actually met something. False when the ray left the level, and the preview is standing at the fallback distance in mid-air.");

        v8pp::module placer(isolate, ClientCatalog(), "PropPlacer",
            "Placement mode: a translucent preview under the crosshair, tinted for whether the spot is accepted, put down with a click.\n\n"
            "The loop is native and runs with the game's own frame, so the preview tracks the camera exactly and no script runs per frame. While a session is up this client's input belongs to it, the way one of the game's own screens takes it -- which is what stops a confirm click also swinging the player's sword, and what stops the player walking away mid-placement.\n\n"
            "One session at a time, and it belongs to the resource that started it: a resource that stops takes its session with it.\n\n"
            "**Nothing here is authority.** The rules below colour a preview at one client. What actually gets built is the server's decision, made again from the pose the client confirmed -- two players aiming at the same patch of ground both see green.");

        placer.function("begin", &JS_Begin,
            v8pp::metadata::docs("boolean",
                {v8pp::metadata::param("options",
                    "{ model?: string; models?: string[]; offsets?: Vector3[]; rotations?: Quaternion[]; scale?: number; validTint?: Color; invalidTint?: Color; range?: number; fallbackDistance?: number; verticalOffset?: number; yaw?: number; alignToSurface?: boolean; faceAwayFromPlayer?: boolean; keepOpen?: boolean; rules?: { maxSlope?: number; surfaces?: string[]; minDistance?: number; maxDistance?: number; clearance?: number; clearanceClasses?: string[]; requireSurface?: boolean } }",
                    false,
                    "`model`, `models`, `offsets` and `rotations` describe the preview exactly as `PropGhost.create` takes them.\n\n"
                    "`validTint` and `invalidTint` are the two colours the preview flips between, translucent green and translucent red by default. `range` is how far the aim ray reaches in metres (40 by default, up to 512) and `fallbackDistance` is where the preview sits when the ray reaches nothing. `verticalOffset` lifts or sinks it, `yaw` is a fixed turn added to whatever the aim settles on, and the mouse wheel moves that while a session is up.\n\n"
                    "`alignToSurface` stands the preview on the slope it is aimed at instead of upright; `faceAwayFromPlayer` turns it to face away from where you stand, which is what a wall or a tower wants and is on by default. `keepOpen` leaves the session running after a confirm so several can be placed in one go, and is on by default.\n\n"
                    "`rules` is what the preview is tinted by, and every one of them is off unless named. `maxSlope` is degrees the ground may lean; `surfaces` is which surface names are allowed; `minDistance` and `maxDistance` bound how far from the player it may stand; `clearance` is metres that must be free of other entities, narrowed to `clearanceClasses` when given; `requireSurface` refuses a spot the ray never reached, and is on by default.")},
                "Starts placement. The preview appears under the crosshair on the same frame.", "True when the session started; false when one is already running, when this client has no world or camera, when another part of the mod holds the controls, and when the preview could not be built."));

        placer.function("end", &JS_End, v8pp::metadata::docs("void", {}, "Ends the session and gives the player their input back. `end` is raised. Doing nothing when no session is running is not an error."));

        placer.function("isActive", &JS_IsActive, v8pp::metadata::docs("boolean", {}, "Whether a placement session is running at this client right now.", "True while one is up."));

        placer.function("setVeto", &JS_SetVeto,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("vetoed", "boolean", false, "Whether to refuse the spot whatever the rules say.")},
                "Refuses every spot until cleared, which is where a rule the engine cannot answer belongs -- what the player can afford, what the server has already said no to. It latches, so it costs the placement loop nothing.",
                "True while a session is running."));

        placer.function("rotate", &JS_Rotate,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("degrees", "number", false, "How far to turn it, in degrees. Negative turns the other way.")}, "Turns the preview, the same way the mouse wheel does while a session is up.",
                "True while a session is running."));

        placer.function("getPose", &JS_GetPose, v8pp::metadata::docs("PlacementPose | null", {}, "Where the placement is aimed right now, without waiting for a `move`.", "The pose, or null when no session is running and before the first aim settles."));

        placer.function("on", &JS_On,
            v8pp::metadata::docs("Unsubscribe",
                {
                    v8pp::metadata::param("event", "\"move\" | \"confirm\" | \"rejected\" | \"cancel\" | \"end\"", false,
                        "`move` fires when the aim has travelled far enough to be worth reporting, and whenever the spot flips between accepted and refused -- it is throttled on purpose, because it is the one event that could otherwise fire every frame. `confirm` is a click the rules accepted, and is the one to send to the server. `rejected` is a click they refused, so the resource can say why; the session stays up. `cancel` is the player backing out. `end` is the session stopping, however it stopped, and is always the last one."),
                    v8pp::metadata::param("handler", "PlacementHandler<PlacementPose>", false, "Called with where the placement is aimed and whether that spot is accepted. The pose is null for `cancel` and `end`."),
                },
                "Listens for one of the session's events. Handlers belong to the resource that registered them and go when it stops.", "A function that removes this subscription."));

        placer.publish(global);
    }
} // namespace KCDC::Scripting

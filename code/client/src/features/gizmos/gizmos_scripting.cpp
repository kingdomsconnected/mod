#include "gizmos_scripting.h"

#include "core/scripting/script_context.h"
#include "gizmos_service.h"
#include "shared/scripting/catalog.h"

#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <stdexcept>

namespace KCDC::Scripting {
    namespace {
        std::string Owner(v8::Isolate *isolate) {
            std::string resource = KCDC::ResolveCallingResource(isolate);
            if (resource.empty()) {
                throw std::invalid_argument("the calling resource could not be identified");
            }
            return resource;
        }

        v8::Local<v8::Object> Object(v8::Local<v8::Value> value) {
            if (!value->IsObject() || value->IsArray()) {
                throw std::invalid_argument("expected an object");
            }
            return value.As<v8::Object>();
        }

        v8::Local<v8::Value> Field(v8::Isolate *isolate, v8::Local<v8::Object> object, const char *key) {
            v8::Local<v8::Value> value;
            if (!object->Get(isolate->GetCurrentContext(), v8pp::to_v8(isolate, key)).ToLocal(&value)) {
                throw std::invalid_argument(std::string("could not read ") + key);
            }
            return value;
        }

        double Number(v8::Isolate *isolate, v8::Local<v8::Value> value, double low, double high) {
            const double number = value->IsNumber() ? v8pp::from_v8<double>(isolate, value) : NAN;
            if (!std::isfinite(number) || number < low || number > high) {
                throw std::invalid_argument("number is missing, non-finite or outside its allowed range");
            }
            return number;
        }

        glm::vec3 Vector(v8::Isolate *isolate, v8::Local<v8::Value> value) {
            const auto object = Object(value);
            return {Number(isolate, Field(isolate, object, "x"), -16384, 16384), Number(isolate, Field(isolate, object, "y"), -16384, 16384), Number(isolate, Field(isolate, object, "z"), -16384, 16384)};
        }

        glm::quat Rotation(v8::Isolate *isolate, v8::Local<v8::Value> value) {
            if (value->IsUndefined()) {
                return glm::quat(1, 0, 0, 0);
            }
            const glm::vec3 xyz = Vector(isolate, value);
            const float w       = static_cast<float>(Number(isolate, Field(isolate, Object(value), "w"), -16384, 16384));
            const glm::quat rotation(w, xyz.x, xyz.y, xyz.z);
            if (glm::dot(rotation, rotation) < 1e-12F) {
                throw std::invalid_argument("rotation must be a non-zero quaternion");
            }
            return glm::normalize(rotation);
        }

        std::string Id(const v8::FunctionCallbackInfo<v8::Value> &info) {
            if (info.Length() < 1 || !info[0]->IsString()) {
                throw std::invalid_argument("id must be a string");
            }
            const std::string id = v8pp::from_v8<std::string>(info.GetIsolate(), info[0]);
            if (id.empty() || id.size() > 128) {
                throw std::invalid_argument("id must contain 1 to 128 UTF-8 bytes");
            }
            return id;
        }

        bool Boolean(v8::Isolate *isolate, v8::Local<v8::Object> object, const char *name, bool fallback) {
            const auto value = Field(isolate, object, name);
            if (value->IsUndefined()) {
                return fallback;
            }
            if (!value->IsBoolean()) {
                throw std::invalid_argument(std::string(name) + " must be a boolean");
            }
            return v8pp::from_v8<bool>(isolate, value);
        }

        double Option(v8::Isolate *isolate, v8::Local<v8::Object> object, const char *name, double fallback, double low, double high, bool integer = false) {
            const auto value    = Field(isolate, object, name);
            const double number = value->IsUndefined() ? fallback : Number(isolate, value, low, high);
            if (integer && std::floor(number) != number) {
                throw std::invalid_argument(std::string(name) + " must be an integer");
            }
            return number;
        }

        void Style(const v8::FunctionCallbackInfo<v8::Value> &info, int index, Features::Gizmos::Drawing &drawing) {
            if (info.Length() <= index || info[index]->IsUndefined()) {
                return;
            }
            auto *isolate      = info.GetIsolate();
            const auto options = Object(info[index]);
            drawing.color      = static_cast<std::uint32_t>(Option(isolate, options, "color", drawing.color, 0, 4294967295.0, true));
            drawing.fillColor  = static_cast<std::uint32_t>(Option(isolate, options, "fillColor", 0, 0, 4294967295.0, true));
            drawing.width      = static_cast<float>(Option(isolate, options, "width", 1.5, 0.5, 8));
            drawing.durationMs = static_cast<std::int64_t>(Option(isolate, options, "durationMs", 0, 0, 3600000, true));
            drawing.depthTest  = Boolean(isolate, options, "depthTest", true);
        }

        std::vector<glm::vec3> Points(v8::Isolate *isolate, v8::Local<v8::Object> object, std::uint32_t minimum, std::uint32_t maximum) {
            const auto value = Field(isolate, object, "points");
            if (!value->IsArray() || value.As<v8::Array>()->Length() < minimum || value.As<v8::Array>()->Length() > maximum) {
                throw std::invalid_argument("points count is outside its allowed range");
            }
            const auto array = value.As<v8::Array>();
            std::vector<glm::vec3> points;
            points.reserve(array->Length());
            for (std::uint32_t i = 0; i < array->Length(); ++i) {
                v8::Local<v8::Value> point;
                if (!array->Get(isolate->GetCurrentContext(), i).ToLocal(&point)) {
                    throw std::invalid_argument("could not read point");
                }
                points.push_back(Vector(isolate, point));
            }
            return points;
        }

        Shared::Game::AreaShape Shape(v8::Isolate *isolate, v8::Local<v8::Value> value) {
            const auto object = Object(value);
            Shared::Game::AreaShape shape;
            const auto type = Field(isolate, object, "type");
            if (!type->IsString() || !Shared::Game::ParseAreaShapeType(v8pp::from_v8<std::string>(isolate, type), shape.type)) {
                throw std::invalid_argument("type must be 'box', 'sphere' or 'shape'");
            }
            const auto position = Field(isolate, object, "position");
            if (!position->IsUndefined()) {
                shape.position = Vector(isolate, position);
            }
            shape.rotation = Rotation(isolate, Field(isolate, object, "rotation"));
            if (shape.type == Shared::Game::AreaShapeType::Box) {
                const auto min = Field(isolate, object, "min");
                const auto max = Field(isolate, object, "max");
                if (!min->IsUndefined()) {
                    shape.boxMin = Vector(isolate, min);
                }
                if (!max->IsUndefined()) {
                    shape.boxMax = Vector(isolate, max);
                }
            }
            else if (shape.type == Shared::Game::AreaShapeType::Sphere) {
                shape.radius = static_cast<float>(Number(isolate, Field(isolate, object, "radius"), 0, 16384));
            }
            else {
                shape.points      = Points(isolate, object, 3, Shared::Game::AreaShape::kMaxPoints);
                const auto height = Field(isolate, object, "height");
                if (!height->IsUndefined()) {
                    shape.height = static_cast<float>(Number(isolate, height, 0, 16384));
                }
            }
            if (!shape.IsValid()) {
                throw std::invalid_argument("invalid area extents, radius or points");
            }
            return shape;
        }

        void JS_Set(const v8::FunctionCallbackInfo<v8::Value> &info) {
            try {
                auto *isolate              = info.GetIsolate();
                const std::string resource = Owner(isolate);
                const std::string id       = Id(info);
                if (info.Length() < 2) {
                    throw std::invalid_argument("set needs id and a primitive definition");
                }
                const auto object    = Object(info[1]);
                const auto typeValue = Field(isolate, object, "type");
                if (!typeValue->IsString()) {
                    throw std::invalid_argument("type must be a primitive name");
                }
                const std::string type = v8pp::from_v8<std::string>(isolate, typeValue);
                Features::Gizmos::Drawing drawing;
                Style(info, 2, drawing);
                const auto vector = [&](const char *name) {
                    return Vector(isolate, Field(isolate, object, name));
                };
                const auto number = [&](const char *name, double fallback, double low, double high, bool integer = false) {
                    return Option(isolate, object, name, fallback, low, high, integer);
                };
                if (type == "box" || type == "sphere" || type == "shape") {
                    Features::Gizmos::Area(drawing, Shape(isolate, info[1]));
                }
                else if (type == "line" || type == "arrow" || type == "capsule" || type == "cone" || type == "cylinder") {
                    const auto from = vector("from"), to = vector("to");
                    if (type == "line") {
                        if (drawing.Filled()) {
                            throw std::invalid_argument("a line has no faces to fill");
                        }
                        drawing.segments.push_back({from, to});
                    }
                    else if (type == "arrow") {
                        Features::Gizmos::Arrow(drawing, from, to, static_cast<float>(number("headLength", std::min(0.5F, glm::length(to - from) * 0.25F), 0.001, 16384)));
                    }
                    else {
                        const float radius = static_cast<float>(Number(isolate, Field(isolate, object, "radius"), 0.001, 16384));
                        const int segments = static_cast<int>(number("segments", 24, 8, 64, true));
                        if (type == "capsule") {
                            Features::Gizmos::Capsule(drawing, from, to, radius, segments);
                        }
                        else {
                            Features::Gizmos::ConeOrCylinder(drawing, from, to, radius, segments, type == "cone");
                        }
                    }
                }
                else if (type == "circle" || type == "grid") {
                    if (type == "grid" && drawing.Filled()) {
                        throw std::invalid_argument("grid has no faces to fill");
                    }
                    const auto position    = vector("position");
                    const auto normalValue = Field(isolate, object, "normal");
                    const auto normal      = normalValue->IsUndefined() ? glm::vec3(0, 0, 1) : Vector(isolate, normalValue);
                    if (type == "circle") {
                        const float radius = static_cast<float>(Number(isolate, Field(isolate, object, "radius"), 0.001, 16384));
                        Features::Gizmos::Circle(drawing, position, normal, radius, static_cast<int>(number("segments", 24, 8, 64, true)));
                    }
                    else {
                        const auto [right, up] = Features::Gizmos::Basis(normal);
                        const float size       = static_cast<float>(number("size", 10, 0.001, 16384));
                        const int divisions    = static_cast<int>(number("divisions", 10, 1, 64, true));
                        for (int i = 0; i <= divisions; ++i) {
                            const float offset = size * (static_cast<float>(i) / divisions - 0.5F);
                            drawing.segments.push_back({position + right * offset - up * size * 0.5F, position + right * offset + up * size * 0.5F});
                            drawing.segments.push_back({position + up * offset - right * size * 0.5F, position + up * offset + right * size * 0.5F});
                        }
                    }
                }
                else if (type == "axes") {
                    if (drawing.Filled()) {
                        throw std::invalid_argument("axes have no faces to fill");
                    }
                    const auto position      = vector("position");
                    const float size         = static_cast<float>(number("size", 1, 0.001, 16384));
                    const glm::quat rotation = Rotation(isolate, Field(isolate, object, "rotation"));
                    constexpr std::uint32_t colors[] {0xFFFF5050, 0xFF50FF50, 0xFF5090FF};
                    for (int axis = 0; axis < 3; ++axis) {
                        glm::vec3 end(0);
                        end[axis] = size;
                        drawing.segments.push_back({position, position + rotation * end, 1, colors[axis]});
                    }
                }
                else if (type == "polyline") {
                    if (drawing.Filled()) {
                        throw std::invalid_argument("a polyline has no faces to fill; use type 'mesh'");
                    }
                    const auto points = Points(isolate, object, 2, 256);
                    for (std::size_t i = 1; i < points.size(); ++i) {
                        drawing.segments.push_back({points[i - 1], points[i]});
                    }
                    if (Boolean(isolate, object, "closed", false)) {
                        drawing.segments.push_back({points.back(), points.front()});
                    }
                }
                else if (type == "mesh") {
                    const auto points = Points(isolate, object, 3, 1024);
                    const auto value  = Field(isolate, object, "indices");
                    if (!value->IsArray() || value.As<v8::Array>()->Length() == 0 || value.As<v8::Array>()->Length() > 3072 || value.As<v8::Array>()->Length() % 3 != 0) {
                        throw std::invalid_argument("indices must contain 3 to 3072 indices in triangle triples");
                    }
                    const auto indices   = value.As<v8::Array>();
                    const bool wireframe = Boolean(isolate, object, "wireframe", true);
                    for (std::uint32_t i = 0; i < indices->Length(); i += 3) {
                        glm::vec3 corners[3];
                        for (int j = 0; j < 3; ++j) {
                            v8::Local<v8::Value> index;
                            if (!indices->Get(isolate->GetCurrentContext(), i + j).ToLocal(&index)) {
                                throw std::invalid_argument("could not read triangle index");
                            }
                            const double n = Number(isolate, index, 0, points.size() - 1);
                            if (n != std::floor(n)) {
                                throw std::invalid_argument("mesh indices must be integers");
                            }
                            corners[j] = points[static_cast<std::size_t>(n)];
                        }
                        if (drawing.Filled()) {
                            drawing.triangles.push_back({corners[0], corners[1], corners[2]});
                        }
                        if (wireframe) {
                            for (int j = 0; j < 3; ++j) {
                                drawing.segments.push_back({corners[j], corners[(j + 1) % 3]});
                            }
                        }
                    }
                }
                else {
                    throw std::invalid_argument("unknown primitive type");
                }
                if (!Features::Gizmos::GizmosService::Get().Set(resource, id, std::move(drawing))) {
                    throw std::invalid_argument("drawing budget exceeded");
                }
            }
            catch (const std::exception &error) {
                info.GetIsolate()->ThrowException(v8::Exception::Error(v8pp::to_v8(info.GetIsolate(), std::string("Gizmos.set: ") + error.what())));
            }
        }

        void JS_SetTransform(const v8::FunctionCallbackInfo<v8::Value> &info) {
            try {
                auto *isolate              = info.GetIsolate();
                const std::string resource = Owner(isolate);
                const std::string id       = Id(info);
                if (info.Length() < 2) {
                    throw std::invalid_argument("setTransform needs id and a position");
                }
                const glm::vec3 position = Vector(isolate, info[1]);
                const glm::quat rotation = Rotation(isolate, info.Length() > 2 ? info[2] : v8::Undefined(isolate).As<v8::Value>());
                info.GetReturnValue().Set(Features::Gizmos::GizmosService::Get().SetTransform(resource, id, position, rotation));
            }
            catch (const std::exception &error) {
                info.GetIsolate()->ThrowException(v8::Exception::Error(v8pp::to_v8(info.GetIsolate(), std::string("Gizmos.setTransform: ") + error.what())));
            }
        }

        void JS_Remove(const v8::FunctionCallbackInfo<v8::Value> &info) {
            try {
                info.GetReturnValue().Set(Features::Gizmos::GizmosService::Get().Remove(Owner(info.GetIsolate()), Id(info)));
            }
            catch (const std::exception &error) {
                info.GetIsolate()->ThrowException(v8::Exception::Error(v8pp::to_v8(info.GetIsolate(), std::string("Gizmos.remove: ") + error.what())));
            }
        }

        void JS_Clear(const v8::FunctionCallbackInfo<v8::Value> &info) {
            try {
                Features::Gizmos::GizmosService::Get().Clear(Owner(info.GetIsolate()));
            }
            catch (const std::exception &error) {
                info.GetIsolate()->ThrowException(v8::Exception::Error(v8pp::to_v8(info.GetIsolate(), std::string("Gizmos.clear: ") + error.what())));
            }
        }
    } // namespace

    void RegisterGizmos(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (auto *resources = KCDC::ScriptResources(); resources != nullptr && Features::Gizmos::GizmosService::Get().InstallResourceCleanup()) {
            resources->AddOnResourceStopped([](const std::string &resource) {
                Features::Gizmos::GizmosService::Get().Clear(resource);
            });
            resources->AddOnResourceStateChanged([](const std::string &resource, Framework::Scripting::ResourceState, Framework::Scripting::ResourceState state) {
                if (state == Framework::Scripting::ResourceState::Stopping || state == Framework::Scripting::ResourceState::Error) {
                    Features::Gizmos::GizmosService::Get().Clear(resource);
                }
            });
        }

        auto &point = ClientCatalog().data_type("GizmoPoint", "A position or offset in metres. Accepts plain objects and Vector3 instances.");
        point.add_property("x", "number", "X coordinate.");
        point.add_property("y", "number", "Y coordinate.");
        point.add_property("z", "number", "Z coordinate.");

        auto &style = ClientCatalog().data_type("GizmoStyle", "Appearance and lifetime of a client debug drawing.");
        style.add_property("color", v8pp::metadata::type {"number | undefined", {}, true}, "Unsigned 0xAARRGGBB color. Defaults to 0xFF6EE678 (opaque green).");
        style.add_property("width", v8pp::metadata::type {"number | undefined", {}, true}, "Line width in pixels, 0.5 to 8. Defaults to 1.5; polygon vertical edges use 60% of this width.");

        style.add_property("depthTest", v8pp::metadata::type {"boolean | undefined", {}, true}, "Defaults to true: scene geometry occludes the drawing. Set false to show through walls. Drawings never write scene depth.");
        style.add_property("fillColor", v8pp::metadata::type {"number | undefined", {}, true},
            "Unsigned 0xAARRGGBB fill color. Defaults to transparent (outline only). Supports boxes, spheres, circles, cones, cylinders, capsules, arrow heads and meshes. Throws for lines, polylines, grids, axes and polygon areas, which have no faces; fill a polygon with explicit mesh triangles.");
        style.add_property("durationMs", v8pp::metadata::type {"number | undefined", {}, true}, "Integer lifetime from 0 to 3600000 milliseconds. Zero (default) persists until removed. Replacing the drawing restarts its lifetime.");

        auto &area = ClientCatalog().data_type("GizmoArea", "Geometry from an Area.toJSON() or World Builder area export. Extra fields are ignored; this draws debug geometry and creates no trigger.");
        area.add_property("type", "'box' | 'sphere' | 'shape'", "Which area geometry to draw.");
        area.add_property("position", v8pp::metadata::type {"GizmoPoint | undefined", {}, true}, "World origin in metres; defaults to zero.");
        area.add_property("rotation", v8pp::metadata::type {"{ x: number; y: number; z: number; w: number } | undefined", {}, true}, "Local-to-world quaternion, normalized on input; defaults to identity. Plain {x,y,z,w} objects work.");
        area.add_property("min", v8pp::metadata::type {"GizmoPoint | undefined", {}, true}, "Box minimum in local space; defaults to {-1,-1,-1}. Each component must be less than max.");
        area.add_property("max", v8pp::metadata::type {"GizmoPoint | undefined", {}, true}, "Box maximum in local space; defaults to {1,1,1}.");
        area.add_property("radius", v8pp::metadata::type {"number | undefined", {}, true}, "Required for spheres: radius in metres, greater than zero and less than 16384.");
        area.add_property("points", v8pp::metadata::type {"GizmoPoint[] | undefined", {}, true}, "Required for shapes: 3 to 256 local points, forming a closed polygon.");
        area.add_property("height", v8pp::metadata::type {"number | undefined", {}, true}, "Shape height above its lowest transformed point, 0 to less than 16384 metres. Zero (default) draws one ring at the points' own heights.");

        auto &line = ClientCatalog().data_type("GizmoLine", "Client debug line geometry.");
        line.add_property("type", "'line'", "Primitive kind.");
        line.add_property("from", "GizmoPoint", "World start.");
        line.add_property("to", "GizmoPoint", "World end.");

        auto &arrow = ClientCatalog().data_type("GizmoArrow", "Client debug arrow geometry.");
        arrow.add_property("type", "'arrow'", "Primitive kind.");
        arrow.add_property("from", "GizmoPoint", "World start.");
        arrow.add_property("to", "GizmoPoint", "World tip.");
        arrow.add_property("headLength", v8pp::metadata::type {"number | undefined", {}, true}, "Head length in metres, greater than zero and no longer than the arrow. Defaults to the smaller of 0.5 and a quarter of the arrow length.");

        auto &roundvolume = ClientCatalog().data_type("GizmoRoundVolume", "Client debug capsule/cone/cylinder geometry.");
        roundvolume.add_property("type", "'capsule' | 'cone' | 'cylinder'", "Primitive kind.");
        roundvolume.add_property("from", "GizmoPoint", "Capsule lower hemisphere center, or cone/cylinder base center.");
        roundvolume.add_property("to", "GizmoPoint", "Capsule upper hemisphere center, cone tip or cylinder top center. Capsule endpoints may coincide for a sphere.");
        roundvolume.add_property("radius", "number", "Radius in metres, 0.001 to 16384.");
        roundvolume.add_property("segments", v8pp::metadata::type {"number | undefined", {}, true}, "Integer radial segments, 8 to 64; default 24.");

        auto &circle = ClientCatalog().data_type("GizmoCircle", "Client debug circle geometry.");
        circle.add_property("type", "'circle'", "Primitive kind.");
        circle.add_property("position", "GizmoPoint", "World center.");
        circle.add_property("normal", v8pp::metadata::type {"GizmoPoint | undefined", {}, true}, "Non-zero plane normal; default {x:0,y:0,z:1}.");
        circle.add_property("radius", "number", "Radius in metres, 0.001 to 16384.");
        circle.add_property("segments", v8pp::metadata::type {"number | undefined", {}, true}, "Integer radial segments, 8 to 64; default 24.");

        auto &grid = ClientCatalog().data_type("GizmoGrid", "Client debug grid geometry.");
        grid.add_property("type", "'grid'", "Primitive kind.");
        grid.add_property("position", "GizmoPoint", "World center.");
        grid.add_property("normal", v8pp::metadata::type {"GizmoPoint | undefined", {}, true}, "Non-zero plane normal; default {x:0,y:0,z:1}.");
        grid.add_property("size", v8pp::metadata::type {"number | undefined", {}, true}, "Square side length in metres, 0.001 to 16384; default 10.");
        grid.add_property("divisions", v8pp::metadata::type {"number | undefined", {}, true}, "Integer subdivisions on each side, 1 to 64; default 10.");

        auto &axes = ClientCatalog().data_type("GizmoAxes", "Client debug axes geometry.");
        axes.add_property("type", "'axes'", "Primitive kind.");
        axes.add_property("position", "GizmoPoint", "World origin.");
        axes.add_property("rotation", v8pp::metadata::type {"{ x: number; y: number; z: number; w: number } | undefined", {}, true}, "Local-to-world quaternion, normalized on input; default identity.");
        axes.add_property("size", v8pp::metadata::type {"number | undefined", {}, true}, "Axis length in metres, 0.001 to 16384; default 1. X is red, Y green and Z blue; these fixed colors override style.color.");

        auto &polyline = ClientCatalog().data_type("GizmoPolyline", "Client debug polyline geometry.");
        polyline.add_property("type", "'polyline'", "Primitive kind.");
        polyline.add_property("points", "GizmoPoint[]", "2 to 256 world positions.");
        polyline.add_property("closed", v8pp::metadata::type {"boolean | undefined", {}, true}, "Join last to first; default false.");

        auto &mesh = ClientCatalog().data_type("GizmoMesh", "Client debug mesh geometry.");
        mesh.add_property("type", "'mesh'", "Primitive kind.");
        mesh.add_property("points", "GizmoPoint[]", "3 to 1024 world positions.");
        mesh.add_property("indices", "number[]", "3 to 3072 integer vertex indices in triangle triples, each from 0 to points.length - 1. Both triangle sides render.");
        mesh.add_property("wireframe", v8pp::metadata::type {"boolean | undefined", {}, true}, "Draw triangle edges with style.color and width; default true. Set fillColor to draw faces.");

        v8pp::module gizmos(isolate, ClientCatalog(), "Gizmos",
            "Resource-owned native debug geometry visible only on this client, without opening World Builder. Scene depth testing is on by default; depthTest: false opts out. Stop/error, disconnect and moving to another level clear drawings; reloading a save of the same level keeps them. No replication or input capture.");
        gizmos.function("set", &JS_Set,
            v8pp::metadata::docs("void",
                {v8pp::metadata::param("id", "string"), v8pp::metadata::param("primitive", "GizmoArea | GizmoLine | GizmoArrow | GizmoRoundVolume | GizmoCircle | GizmoGrid | GizmoAxes | GizmoPolyline | GizmoMesh"), v8pp::metadata::param("style", "GizmoStyle", true)},
                "Creates or replaces a named primitive owned by this resource. Finite input coordinates within +/-16384 metres. IDs are 1 to 128 UTF-8 bytes. Limit: 128 IDs per resource, 16384 geometry units across resources (one per line, three per triangle). Throws on invalid input or budget overflow and preserves the previous drawing. Copies geometry and reapplies style defaults; use setTransform to move it."));
        gizmos.function("setTransform", &JS_SetTransform,
            v8pp::metadata::docs("boolean", {v8pp::metadata::param("id", "string"), v8pp::metadata::param("position", "GizmoPoint"), v8pp::metadata::param("rotation", "{ x: number; y: number; z: number; w: number }", true)},
                "Moves this resource's named drawing without rebuilding it: every authored point p is drawn at position + rotation * p. Author the geometry around the origin to move it as a whole. The rotation is normalized and defaults to identity. set() resets the transform to identity.",
                "True if the drawing exists; false if this resource has no drawing with that ID."));
        gizmos.function("remove", &JS_Remove, v8pp::metadata::docs("boolean", {v8pp::metadata::param("id", "string")}, "Removes this resource's named drawing. Throws for an invalid ID or unknown resource.", "True if removed; false if this resource has no drawing with that ID."));
        gizmos.function("clear", &JS_Clear, v8pp::metadata::docs("void", {}, "Removes all drawings owned by this resource. Other resources are unaffected. Throws if the caller's resource cannot be identified."));
        gizmos.publish(global);
    }
} // namespace KCDC::Scripting

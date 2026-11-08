#pragma once

#include "common/spawn/prop_spawner.h"
#include "features/camera/anchor_camera.h"
#include "prop_editor_support.h"
#include "shared/features/area/area_geometry.h"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace KCDC::Features::Prop {
    /** Which list the current selection lives in. */
    enum class SelectionKind : std::uint8_t {
        None,
        Placed, // a mesh the editor spawned
        World,  // something the level shipped with
        Effect, // a particle effect the editor placed
        Light,  // an authored point light
    };

    /** One member of the selection. */
    struct SelectionRef {
        SelectionKind kind = SelectionKind::None;
        std::uint64_t id   = 0;

        bool operator==(const SelectionRef &other) const = default;
    };

    struct SelectionRefHash {
        std::size_t operator()(const SelectionRef &ref) const noexcept {
            return std::hash<std::uint64_t> {}(ref.id * 8U + static_cast<std::uint64_t>(ref.kind));
        }
    };

    /**
     * Members of the selection in the order they joined, which is the order
     * the gizmo and the panel show them, with a hash beside it: a region
     * selection can hold thousands of objects, and a scan per lookup would
     * make every panel row and every toggle cost the whole selection.
     */
    class SelectionSet final {
      public:
        bool Contains(const SelectionRef &ref) const {
            return _members.contains(ref);
        }

        /** False when it was already a member. */
        bool Insert(const SelectionRef &ref) {
            if (!_members.insert(ref).second) {
                return false;
            }
            _order.push_back(ref);
            return true;
        }

        /** False when it was not a member. */
        bool Erase(const SelectionRef &ref) {
            if (_members.erase(ref) == 0) {
                return false;
            }
            _order.erase(std::find(_order.begin(), _order.end(), ref));
            return true;
        }

        /** Takes out every member `predicate` accepts, asking once per member; how many went. */
        template <typename Predicate>
        std::size_t EraseIf(Predicate predicate) {
            return std::erase_if(_order, [this, &predicate](const SelectionRef &ref) {
                if (!predicate(ref)) {
                    return false;
                }
                _members.erase(ref);
                return true;
            });
        }

        /** Takes out the oldest member and returns it. Only on a non-empty set. */
        SelectionRef PopFront() {
            const SelectionRef front = _order.front();
            _members.erase(front);
            _order.erase(_order.begin());
            return front;
        }

        template <typename Iterator>
        void Assign(Iterator first, Iterator last) {
            Clear();
            for (; first != last; ++first) {
                Insert(*first);
            }
        }

        void Clear() {
            _members.clear();
            _order.clear();
        }

        bool Empty() const {
            return _order.empty();
        }
        std::size_t Size() const {
            return _order.size();
        }
        const SelectionRef &Front() const {
            return _order.front();
        }
        const std::vector<SelectionRef> &Items() const {
            return _order;
        }
        std::vector<SelectionRef>::const_iterator begin() const {
            return _order.begin();
        }
        std::vector<SelectionRef>::const_iterator end() const {
            return _order.end();
        }

      private:
        std::vector<SelectionRef> _order;
        std::unordered_set<SelectionRef, SelectionRefHash> _members;
    };

    /** What a selection tool may take: one bit per kind of object. */
    namespace SelectableKinds {
        inline constexpr std::uint8_t Prop       = 1U << 0;
        inline constexpr std::uint8_t Effect     = 1U << 1;
        inline constexpr std::uint8_t Light      = 1U << 2;
        inline constexpr std::uint8_t Brush      = 1U << 3; // a level brush
        inline constexpr std::uint8_t Vegetation = 1U << 4; // a level tree, bush or painted rock
        inline constexpr std::uint8_t Entity     = 1U << 5; // a level entity
        inline constexpr std::uint8_t Level      = Brush | Vegetation | Entity;
        inline constexpr std::uint8_t All        = Prop | Effect | Light | Level;

        /** Each bit's name in filters and MCP, in bit order. */
        inline constexpr std::array<std::string_view, 6> kNames {"prop", "effect", "light", "brush", "vegetation", "entity"};
    } // namespace SelectableKinds

    /** One object a selection tool weighs, as the game thread reads it off its lists or the level. */
    struct SelectionCandidate {
        std::uint8_t kind = 0; // one SelectableKinds bit
        std::string name;
        std::string mesh;      // a prop's model, a brush's or vegetation's mesh, a level entity's model; may be empty
        std::string className; // a level entity's class, or an effect's name
        std::string group;
        std::string layer = "default";                           // editor layer id
        std::optional<KCDC::Common::Spawn::PropPhysics> physics; // placed props only
        glm::vec3 position {0.0F};
        bool hidden = false;
    };

    /**
     * What every selection tool and Select similar keep or skip. An empty
     * field matches anything. `text` is the asset library's search: every
     * word must appear in the name, mesh, class or group, and a word with a
     * leading `-` must not.
     */
    struct SelectionFilter {
        std::uint8_t kinds = SelectableKinds::All;
        std::string text;
        std::string mesh;      // exact, ignoring case and slash direction
        std::string className; // exact, ignoring case
        std::string layer;     // editor layer id
        std::string group;
        std::optional<KCDC::Common::Spawn::PropPhysics> physics;
        bool includeHidden = false;

        /** `text` split into the words `Matches` takes, so a query splits it once rather than per object. */
        std::vector<std::string> Tokens() const {
            return SearchTokens(text);
        }

        bool Matches(const SelectionCandidate &candidate, const std::vector<std::string> &tokens) const {
            if ((kinds & candidate.kind) == 0 || (candidate.hidden && !includeHidden)) {
                return false;
            }
            if (!mesh.empty() && !SameMeshPath(candidate.mesh, mesh)) {
                return false;
            }
            if (!className.empty() && !Framework::Utils::StringUtils::EqualsIgnoreCase(candidate.className, className)) {
                return false;
            }
            if ((!layer.empty() && candidate.layer != layer) || (!group.empty() && candidate.group != group)) {
                return false;
            }
            if (physics.has_value() && candidate.physics != physics) {
                return false;
            }
            return PassesSearch(tokens, {candidate.name, candidate.mesh, candidate.className, candidate.group});
        }
    };

    inline nlohmann::json SelectionFilterToJson(const SelectionFilter &filter) {
        nlohmann::json kinds = nlohmann::json::array();
        for (std::size_t bit = 0; bit < SelectableKinds::kNames.size(); ++bit) {
            if ((filter.kinds & (1U << bit)) != 0) {
                kinds.push_back(SelectableKinds::kNames[bit]);
            }
        }
        nlohmann::json node {{"kinds", kinds}, {"text", filter.text}, {"mesh", filter.mesh}, {"className", filter.className}, {"layer", filter.layer}, {"group", filter.group}, {"includeHidden", filter.includeHidden}};
        node["physics"] = filter.physics.has_value() ? nlohmann::json(KCDC::Common::Spawn::PropPhysicsToString(*filter.physics)) : nlohmann::json();
        return node;
    }

    /** Reads a filter; every field is optional. False, with `error`, for a field of the wrong type or an unknown kind. */
    inline bool SelectionFilterFromJson(const nlohmann::json &node, SelectionFilter &filter, std::string &error) {
        filter = SelectionFilter {};
        if (node.is_null()) {
            return true;
        }
        if (!node.is_object()) {
            error = "filter must be an object";
            return false;
        }
        if (const auto kinds = node.find("kinds"); kinds != node.end()) {
            if (!kinds->is_array() || kinds->empty()) {
                error = "filter.kinds must be a non-empty array";
                return false;
            }
            filter.kinds = 0;
            for (const auto &entry : *kinds) {
                const auto found = entry.is_string() ? std::find(SelectableKinds::kNames.begin(), SelectableKinds::kNames.end(), entry.get<std::string>()) : SelectableKinds::kNames.end();
                if (found == SelectableKinds::kNames.end()) {
                    error = "filter.kinds may name prop, effect, light, brush, vegetation and entity";
                    return false;
                }
                filter.kinds |= static_cast<std::uint8_t>(1U << (found - SelectableKinds::kNames.begin()));
            }
        }
        for (const auto &[key, field] : {std::pair {"text", &filter.text}, std::pair {"mesh", &filter.mesh}, std::pair {"className", &filter.className}, std::pair {"layer", &filter.layer}, std::pair {"group", &filter.group}}) {
            if (const auto found = node.find(key); found != node.end()) {
                if (!found->is_string()) {
                    error = std::string("filter.") + key + " must be a string";
                    return false;
                }
                *field = found->get<std::string>();
            }
        }
        if (const auto physics = node.find("physics"); physics != node.end() && !physics->is_null()) {
            filter.physics = physics->is_string() ? KCDC::Common::Spawn::PropPhysicsFromString(physics->get<std::string>()) : std::nullopt;
            if (!filter.physics.has_value()) {
                error = "filter.physics must be rigid, static or none";
                return false;
            }
        }
        if (const auto hidden = node.find("includeHidden"); hidden != node.end()) {
            if (!hidden->is_boolean()) {
                error = "filter.includeHidden must be a boolean";
                return false;
            }
            filter.includeHidden = hidden->get<bool>();
        }
        return true;
    }

    /** Even-odd test of a point against a closed outline, as the lasso draws it. */
    inline bool PointInOutline(const std::vector<glm::vec2> &outline, const glm::vec2 &point) {
        bool inside = false;
        for (std::size_t i = 0, j = outline.size() - 1; i < outline.size(); j = i++) {
            const glm::vec2 &a = outline[i];
            const glm::vec2 &b = outline[j];
            if ((a.y > point.y) != (b.y > point.y) && point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
                inside = !inside;
            }
        }
        return inside;
    }

    enum class SelectionRegionKind : std::uint8_t {
        Sphere, // around a point: the brush and Select similar
        Screen, // inside an outline on screen: the rectangle and the lasso
        Area,   // inside an authored area
    };

    /** Where a selection tool looks. Screen outlines are in normalised device coordinates, -1 to 1. */
    struct SelectionRegion {
        static constexpr float kDefaultReach           = 150.0F;
        static constexpr float kMaxReach               = 250.0F;
        static constexpr float kMaxRadius              = 250.0F;
        static constexpr std::size_t kMaxOutlinePoints = 256;

        SelectionRegionKind kind = SelectionRegionKind::Sphere;
        glm::vec3 centre {0.0F};
        float radius = 0.0F;
        std::vector<glm::vec2> outline; // Screen: closed back to the first point; a rectangle is its four corners
        float reach = kDefaultReach;    // Screen: how far from the camera an object may stand
        Shared::Game::AreaShape area;   // Area

        /** Precomputes what `Contains` reads. Call once after filling the region. */
        void Prepare() {
            _areaPoints = kind == SelectionRegionKind::Area ? area.WorldPoints() : std::vector<glm::vec3> {};
        }

        bool IsValid() const {
            switch (kind) {
            case SelectionRegionKind::Sphere: return radius > 0.0F && radius <= kMaxRadius && std::isfinite(centre.x) && std::isfinite(centre.y) && std::isfinite(centre.z);
            case SelectionRegionKind::Screen:
                return outline.size() >= 3 && outline.size() <= kMaxOutlinePoints && reach > 0.0F && reach <= kMaxReach && std::all_of(outline.begin(), outline.end(), [](const glm::vec2 &point) {
                    return std::isfinite(point.x) && std::isfinite(point.y);
                });
            case SelectionRegionKind::Area: return area.IsValid();
            }
            return false;
        }

        bool Contains(const glm::vec3 &point, const Camera::CameraPose &camera) const {
            switch (kind) {
            case SelectionRegionKind::Sphere: {
                const glm::vec3 offset = point - centre;
                return glm::dot(offset, offset) <= radius * radius;
            }
            case SelectionRegionKind::Screen: {
                const glm::vec3 offset = point - glm::vec3(camera.position.x, camera.position.y, camera.position.z);
                float x                = 0.0F;
                float y                = 0.0F;
                return glm::dot(offset, offset) <= reach * reach && Camera::ProjectToNdc(camera, SDK::Vec3 {point.x, point.y, point.z}, x, y) && PointInOutline(outline, glm::vec2(x, y));
            }
            case SelectionRegionKind::Area: return area.Contains(point, _areaPoints);
            }
            return false;
        }

        /** The world box a level query must cover to find everything `Contains` can accept. */
        void QueryBox(const Camera::CameraPose &camera, glm::vec3 &min, glm::vec3 &max) const {
            switch (kind) {
            case SelectionRegionKind::Sphere:
                min = centre - glm::vec3(radius);
                max = centre + glm::vec3(radius);
                return;
            case SelectionRegionKind::Screen: {
                // The slice of the view the outline covers. An accepted point is
                // within `reach` of the eye, so no deeper than `reach` along the
                // view; each corner ray is stretched to that depth, not to that
                // length, or the box would stop short off the view's axis.
                const glm::vec3 eye(camera.position.x, camera.position.y, camera.position.z);
                const SDK::Vec3 axis = Camera::PoseForward(camera);
                const glm::vec3 forward(axis.x, axis.y, axis.z);
                min = eye - glm::vec3(reach * 0.01F);
                max = eye + glm::vec3(reach * 0.01F);
                for (const glm::vec2 &corner : outline) {
                    const SDK::Vec3 ray     = Camera::RayThroughNdc(camera, corner.x, corner.y);
                    const glm::vec3 heading = glm::vec3(ray.x, ray.y, ray.z);
                    const float depth       = std::max(glm::dot(heading, forward), 0.05F);
                    const glm::vec3 point   = eye + heading * (reach / depth);
                    min                     = glm::min(min, point);
                    max                     = glm::max(max, point);
                }
                // Anything within `reach` of the eye fits in this cube too, so the slice never needs more.
                min = glm::max(min, eye - glm::vec3(reach));
                max = glm::min(max, eye + glm::vec3(reach));
                return;
            }
            case SelectionRegionKind::Area:
                area.WorldBounds(_areaPoints, min, max);
                // An open-topped polygon reaches the whole height of the world; its contents do not.
                min.z = std::max(min.z, area.position.z - kMaxRadius);
                max.z = std::min(max.z, area.position.z + kMaxRadius);
                return;
            }
        }

      private:
        std::vector<glm::vec3> _areaPoints;
    };

    /** How a selection tool's result joins the selection. */
    enum class SelectionMode : std::uint8_t {
        Replace,
        Add,
        Remove,
    };

    /** One selection tool's request, handed to the game thread whole. */
    struct SelectionQuery {
        static constexpr std::size_t kMaxSelection = 5000;

        SelectionRegion region;
        SelectionFilter filter;
        SelectionMode mode = SelectionMode::Replace;
        /** Only what the camera can see: a ray from it must reach the object before anything else. */
        bool visibleOnly = false;
        /** Select similar: the filter is derived from this object, and the region is a sphere around it. */
        std::optional<SelectionRef> similarTo;
        /** The brush: the sphere's centre is the first surface under this screen point, found on the game thread. */
        std::optional<glm::vec2> aimNdc;
    };

    /** What a selection tool did, for the status line and MCP. */
    struct SelectionOutcome {
        std::size_t matched = 0; // objects the region and filter accepted
        std::size_t added   = 0;
        std::size_t removed = 0;
        std::size_t total   = 0;     // the selection's size afterwards
        bool truncated      = false; // the level query or the selection limit cut the result short
        std::string error;
    };
} // namespace KCDC::Features::Prop

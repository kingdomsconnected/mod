#pragma once

#include "features/gizmos/gizmos_geometry.h"
#include "sdk/types.h"
#include "shared/game/prop_models.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <utils/string_utils.h>

namespace KCDC::Features::Prop {
    /** Every editor scale is clamped to this, drafts included, so a field never snaps back. */
    inline constexpr float kMinObjectScale = 0.01F;
    inline constexpr float kMaxObjectScale = 100.0F;

    inline SDK::Vec3 ClampObjectScale(const SDK::Vec3 &scale) {
        return SDK::Vec3 {std::clamp(scale.x, kMinObjectScale, kMaxObjectScale), std::clamp(scale.y, kMinObjectScale, kMaxObjectScale), std::clamp(scale.z, kMinObjectScale, kMaxObjectScale)};
    }

    /** `objects/a/b/wall_stone_01.cgf` reads as `wall_stone_01`. */
    inline std::string StemOf(std::string_view path) {
        const std::size_t slash = path.find_last_of("/\\");
        const std::size_t start = slash == std::string_view::npos ? 0 : slash + 1;
        const std::size_t dot   = path.find_last_of('.');
        const std::size_t end   = dot != std::string_view::npos && dot > start ? dot : path.size();
        return std::string {path.substr(start, end - start)};
    }

    /** A meshless object answers a bounds query with a reset, inverted box. */
    inline bool IsUsableBounds(const SDK::AABB &bounds) {
        return std::isfinite(bounds.min.x) && std::isfinite(bounds.max.x) && bounds.min.x <= bounds.max.x && bounds.min.y <= bounds.max.y && bounds.min.z <= bounds.max.z;
    }

    inline SDK::Vec3 BoxCentre(const SDK::AABB &bounds) {
        return SDK::Vec3 {(bounds.min.x + bounds.max.x) * 0.5F, (bounds.min.y + bounds.max.y) * 0.5F, (bounds.min.z + bounds.max.z) * 0.5F};
    }

    /** Where an NDC point lands inside an ImGui viewport, in pixels. */
    /**
     * One editor outline for the native renderer: ImGui's ABGR colour as its
     * ARGB, seen through the scene as the editor's lines always were.
     */
    inline Gizmos::Drawing EditorOutline(ImU32 colour, float thickness) {
        Gizmos::Drawing drawing;
        drawing.depthTest = false;
        drawing.color     = (colour & 0xFF00FF00U) | ((colour & 0xFFU) << 16) | ((colour >> 16) & 0xFFU);
        drawing.width     = thickness;
        return drawing;
    }

    /** An axis-aligned box's twelve edges. */
    inline void BoxEdges(Gizmos::Drawing &drawing, const SDK::AABB &box) {
        Shared::Game::AreaShape shape;
        shape.boxMin = glm::vec3(box.min.x, box.min.y, box.min.z);
        shape.boxMax = glm::vec3(box.max.x, box.max.y, box.max.z);
        Shared::Game::AreaWireframe(shape, [&](const glm::vec3 &from, const glm::vec3 &to, float weight) {
            drawing.segments.push_back({from, to, weight});
        });
    }

    inline ImVec2 NdcToScreen(const ImGuiViewport &viewport, float ndcX, float ndcY) {
        return ImVec2 {viewport.Pos.x + (ndcX * 0.5F + 0.5F) * viewport.Size.x, viewport.Pos.y + (0.5F - ndcY * 0.5F) * viewport.Size.y};
    }

    /** NdcToScreen's inverse. */
    inline glm::vec2 ScreenToNdc(const ImGuiViewport &viewport, const ImVec2 &pixel) {
        return glm::vec2((pixel.x - viewport.Pos.x) / std::max(viewport.Size.x, 1.0F) * 2.0F - 1.0F, 1.0F - (pixel.y - viewport.Pos.y) / std::max(viewport.Size.y, 1.0F) * 2.0F);
    }

    // --- Matrices: ImGuizmo is column-major float[16], the engine row-major Matrix34.

    inline void ComposeMatrix(const SDK::Vec3 &position, const SDK::Quat &rotation, const SDK::Vec3 &scale, float matrix[16]) {
        const SDK::Matrix34 affine = SDK::Matrix34FromTransform(position, rotation, scale);

        for (int column = 0; column < 3; ++column) {
            for (int row = 0; row < 3; ++row) {
                matrix[column * 4 + row] = affine.m[row][column];
            }
            matrix[column * 4 + 3] = 0.0F;
        }

        matrix[12] = affine.m[0][3];
        matrix[13] = affine.m[1][3];
        matrix[14] = affine.m[2][3];
        matrix[15] = 1.0F;
    }

    inline void DecomposeMatrix(const float matrix[16], SDK::Vec3 &position, SDK::Quat &rotation, SDK::Vec3 &scale) {
        SDK::Matrix34 affine {};
        for (int column = 0; column < 3; ++column) {
            for (int row = 0; row < 3; ++row) {
                affine.m[row][column] = matrix[column * 4 + row];
            }
        }
        affine.m[0][3] = matrix[12];
        affine.m[1][3] = matrix[13];
        affine.m[2][3] = matrix[14];

        SDK::Matrix34ToTransform(affine, position, rotation, scale);
    }

    /** A mesh path as the prop catalog spells paths: lowercase, forward slashes. A level keeps its own spelling. */
    inline std::string NormalisedMeshPath(std::string_view path) {
        std::string normalised = Framework::Utils::StringUtils::ToLower(std::string {path});
        std::replace(normalised.begin(), normalised.end(), '\\', '/');
        return normalised;
    }

    /** Whether two mesh paths name the same file, ignoring case and slash direction, without building either normalised path. */
    inline bool SameMeshPath(std::string_view left, std::string_view right) {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            const char a = left[index] == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(left[index])));
            const char b = right[index] == '\\' ? '/' : static_cast<char>(std::tolower(static_cast<unsigned char>(right[index])));
            if (a != b) {
                return false;
            }
        }
        return true;
    }

    /** A level mesh path as the shipped prop catalog spells it, or empty when the catalog does not ship that mesh. */
    inline std::string CatalogModelForLevelMesh(std::string_view meshPath) {
        std::string path = NormalisedMeshPath(meshPath);
        return !path.empty() && Shared::Game::FindPropModel(path) != nullptr ? path : std::string {};
    }

    /** A search box's words, split on spaces. A word with a leading `-` excludes. */
    inline std::vector<std::string> SearchTokens(std::string_view text) {
        std::vector<std::string> tokens;
        std::istringstream stream {std::string {text}};
        std::string word;
        while (stream >> word) {
            tokens.push_back(word);
        }
        return tokens;
    }

    /** Whether every word appears in one of `fields`, ignoring case, and no `-word` does. */
    inline bool PassesSearch(const std::vector<std::string> &tokens, std::initializer_list<std::string_view> fields) {
        for (const std::string &token : tokens) {
            const bool exclude          = token.size() > 1 && token[0] == '-';
            const std::string_view word = exclude ? std::string_view {token}.substr(1) : std::string_view {token};
            const bool hit              = std::any_of(fields.begin(), fields.end(), [word](std::string_view field) {
                return Framework::Utils::StringUtils::ContainsIgnoreCase(field, word);
            });
            if (hit == exclude) {
                return false;
            }
        }
        return true;
    }

    /**
     * How far a duplicated level object's copy steps: along its own X, as a
     * duplicated prop steps. Only the world box is known, so the width is that
     * box measured along the axis: exact when the object is square to the
     * world, a little wide when it is turned.
     */
    inline SDK::Vec3 LevelCopyStep(const SDK::Quat &rotation, const SDK::AABB *worldBounds) {
        const SDK::Matrix34 basis = SDK::Matrix34FromTransform(SDK::Vec3 {}, rotation, SDK::Vec3 {1.0F, 1.0F, 1.0F});
        const SDK::Vec3 axis {basis.m[0][0], basis.m[1][0], basis.m[2][0]};
        float width = 0.25F;
        if (worldBounds != nullptr) {
            const SDK::Vec3 size {worldBounds->max.x - worldBounds->min.x, worldBounds->max.y - worldBounds->min.y, worldBounds->max.z - worldBounds->min.z};
            width = std::max(std::fabs(axis.x) * size.x + std::fabs(axis.y) * size.y + std::fabs(axis.z) * size.z, 0.25F);
        }
        return SDK::Vec3 {axis.x * width, axis.y * width, axis.z * width};
    }
} // namespace KCDC::Features::Prop

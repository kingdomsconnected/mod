#pragma once

#include "core/feature_registry.h"
#include "gizmos_geometry.h"

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace KCDC::Features::Gizmos {
    /** Named drawings are private to their resource, with bounded storage. */
    class Drawings final {
      public:
        static constexpr std::size_t kMaxPerResource   = 128;
        static constexpr std::size_t kMaxGeometryUnits = 16384;

        bool Set(const std::string &resource, const std::string &id, Drawing drawing, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
            Expire(now);
            auto found                = _entries.find(std::make_pair(resource, id));
            const std::size_t oldSize = found == _entries.end() ? 0 : found->second.Cost();
            if ((found == _entries.end() && Owned(resource) >= kMaxPerResource) || _units - oldSize + drawing.Cost() > kMaxGeometryUnits) {
                return false;
            }
            _units            = _units - oldSize + drawing.Cost();
            drawing.expiresAt = drawing.durationMs > 0 ? std::optional(now + std::chrono::milliseconds(drawing.durationMs)) : std::nullopt;
            drawing.ComputeBounds();
            if (found == _entries.end()) {
                ++_owned[resource];
                _entries.emplace(std::make_pair(resource, id), std::move(drawing));
            }
            else {
                found->second = std::move(drawing);
            }
            return true;
        }

        /** Re-places a drawing without rebuilding it; false if there is none. */
        bool SetTransform(const std::string &resource, const std::string &id, const glm::vec3 &position, const glm::quat &rotation) {
            const auto found = _entries.find(std::make_pair(resource, id));
            if (found == _entries.end()) {
                return false;
            }
            found->second.position = position;
            found->second.rotation = rotation;
            return true;
        }

        bool Remove(const std::string &resource, const std::string &id) {
            const auto found = _entries.find(std::make_pair(resource, id));
            if (found == _entries.end()) {
                return false;
            }
            Erase(found);
            return true;
        }

        void Clear(const std::string &resource) {
            for (auto entry = _entries.begin(); entry != _entries.end();) {
                entry = entry->first.first == resource ? Erase(entry) : std::next(entry);
            }
        }

        void Expire(std::chrono::steady_clock::time_point now) {
            for (auto entry = _entries.begin(); entry != _entries.end();) {
                entry = entry->second.expiresAt && now >= *entry->second.expiresAt ? Erase(entry) : std::next(entry);
            }
        }

        void Reset() {
            _entries.clear();
            _owned.clear();
            _units = 0;
        }

        const auto &Entries() const {
            return _entries;
        }

      private:
        std::size_t Owned(const std::string &resource) const {
            const auto found = _owned.find(resource);
            return found == _owned.end() ? 0 : found->second;
        }

        std::map<std::pair<std::string, std::string>, Drawing>::iterator Erase(std::map<std::pair<std::string, std::string>, Drawing>::iterator entry) {
            _units -= entry->second.Cost();
            const auto owned = _owned.find(entry->first.first);
            if (--owned->second == 0) {
                _owned.erase(owned);
            }
            return _entries.erase(entry);
        }

        std::map<std::pair<std::string, std::string>, Drawing> _entries;
        std::map<std::string, std::size_t> _owned;
        std::size_t _units = 0;
    };

    class GizmosService final: public Core::IFeatureService {
      public:
        static GizmosService &Get();
        bool Set(const std::string &resource, const std::string &id, Drawing drawing);
        bool SetTransform(const std::string &resource, const std::string &id, const glm::vec3 &position, const glm::quat &rotation);
        bool Remove(const std::string &resource, const std::string &id);
        void Clear(const std::string &resource);
        void Reset() override;

        /**
         * World Builder's world-space lines for the next frame, replacing the
         * last ones. Kept apart from script drawings: no budget, no names.
         */
        void SetEditorOverlay(std::vector<Drawing> drawings);

        /** Game thread: submits world geometry to the native renderer. */
        void Update() override;

        /** The resource manager survives reconnects, as do its callbacks. */
        bool InstallResourceCleanup();

      private:
        std::mutex _mutex;
        Drawings _drawings;
        std::vector<Drawing> _editorOverlay;

        // Drawings outlive a save load of the same level, which fires the
        // context-change edge too; only arriving in another level drops them.
        std::string _level;
        bool _cleanupInstalled = false;
    };
} // namespace KCDC::Features::Gizmos

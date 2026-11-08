#pragma once

#include <cstdint>
#include <function2/function2.hpp>
#include <glm/vec3.hpp>
#include <string>
#include <string_view>

namespace KCDC::Features::Prop {
    struct EditorGroundResult {
        std::uint64_t id = 0;
        glm::vec3 position {0};
        bool hit = false;
    };

    using GroundSnapFunction = fu2::function_view<bool(std::string_view, glm::vec3 &, bool, float, bool) const>;

    /** Render-thread drag state. Physics probes travel through the game-thread command queue. */
    class EditorGroundSnap {
      public:
        void BeginFrame() {
            _used = false;
        }
        void EndFrame() {
            if (!_used) {
                Reset();
            }
        }
        bool Pending() const {
            return _pending != 0 || _dirty;
        }
        void Reset() {
            _pending = 0;
            _dirty   = false;
            _key.clear();
            _dragging = false;
        }

        template <typename Probe>
        bool Update(std::string_view key, glm::vec3 &position, bool moved, bool dragging, bool shift, float footOffset, const EditorGroundResult &result, Probe probe, bool request = false) {
            _used = true;
            // A different selection, numeric edit, or releasing Shift during the
            // drag must not receive a late answer from the previous gesture.
            if (_key != key || (dragging && (!_dragging || !shift)) || (!dragging && Pending() && position != _latest)) {
                Reset();
                _key = key;
            }
            _dragging = dragging;
            if (request || (moved && dragging && shift)) {
                _dirty = true;
            }
            bool changed = false;
            if (_pending && result.id == _pending) {
                _pending = 0;
                if (result.hit) {
                    const float height = result.position.z + footOffset;
                    changed            = position.z != height;
                    position.z         = height;
                }
            }
            // One outstanding probe, with the latest drag position remembered.
            // The final probe is still applied after mouse-up, under the same undo gesture.
            if (!_pending && _dirty) {
                _dirty   = false;
                _pending = ++_next;
                probe(_pending, position);
            }
            _latest = position;
            return changed;
        }

      private:
        std::string _key;
        glm::vec3 _latest {0};
        std::uint64_t _next    = 0;
        std::uint64_t _pending = 0;
        bool _dirty            = false;
        bool _used             = false;
        bool _dragging         = false;
    };
} // namespace KCDC::Features::Prop

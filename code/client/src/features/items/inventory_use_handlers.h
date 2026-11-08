#pragma once

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace KCDC::Features::Items {
    /** Game-thread registrations and deferred uses. No native pointers survive a capture. */
    template <typename Callback>
    class InventoryUseHandlers final {
      public:
        struct Selector {
            std::string item;
            std::uint64_t uid = 0;
        };
        struct Handler {
            std::string resource;
            Selector selector;
            Callback callback;
        };
        struct Use {
            std::uint32_t handler  = 0;
            std::uint64_t uid      = 0;
            std::uint64_t revision = 0;
            std::string item;
            std::string id;
            std::string action;
        };

        static bool ParseUid(std::string_view text, std::uint64_t &uid) {
            uid = 0;
            if (text.empty() || text.front() == '0') {
                return false;
            }
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), uid);
            return parsed.ec == std::errc {} && parsed.ptr == text.data() + text.size() && uid != 0;
        }

        std::uint32_t Add(std::string resource, Selector selector, Callback callback) {
            if (resource.empty() || (selector.uid == 0 && selector.item.empty()) || _handlers.size() >= 256 || _next == std::numeric_limits<std::uint32_t>::max()) {
                return 0;
            }
            const auto id = ++_next;
            _handlers.emplace(id, Handler {std::move(resource), std::move(selector), std::move(callback)});
            return id;
        }

        std::uint32_t Match(std::string_view item, std::uint64_t uid) const {
            std::uint32_t byClass = 0;
            for (auto it = _handlers.rbegin(); it != _handlers.rend(); ++it) {
                const auto &selector = it->second.selector;
                if (selector.uid != 0 && selector.uid == uid) {
                    return it->first;
                }
                if (byClass == 0 && selector.uid == 0 && selector.item == item) {
                    byClass = it->first;
                }
            }
            return byClass;
        }

        const Handler *Find(std::uint32_t id) const {
            const auto found = _handlers.find(id);
            return found == _handlers.end() ? nullptr : &found->second;
        }

        bool Empty() const {
            return _handlers.empty();
        }

        bool Remove(std::uint32_t id, const std::string &resource) {
            const auto found = _handlers.find(id);
            if (found == _handlers.end() || found->second.resource != resource) {
                return false;
            }
            _handlers.erase(found);
            std::erase_if(_pending, [id](const Use &use) { return use.handler == id; });
            return true;
        }

        void Release(const std::string &resource) {
            std::erase_if(_handlers, [&](const auto &entry) { return entry.second.resource == resource; });
            std::erase_if(_pending, [&](const Use &use) { return Find(use.handler) == nullptr; });
        }

        void Queue(Use use) {
            if (Find(use.handler) == nullptr || _pending.size() >= 16) {
                return;
            }
            // A native action can reach more than one detour. One UID gets one callback per frame.
            if (std::none_of(_pending.begin(), _pending.end(), [&](const Use &other) { return other.uid == use.uid; })) {
                _pending.push_back(std::move(use));
            }
        }

        std::vector<Use> Drain() {
            return std::exchange(_pending, {});
        }

        void Reset() {
            _pending.clear();
            _handlers.clear();
            // Never reuse ids: a drained batch or a script retaining an old id cannot target a new handler.
        }

      private:
        std::map<std::uint32_t, Handler> _handlers;
        std::vector<Use> _pending;
        std::uint32_t _next = 0;
    };
} // namespace KCDC::Features::Items

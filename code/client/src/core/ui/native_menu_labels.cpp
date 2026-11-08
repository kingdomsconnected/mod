#include "native_menu_labels.h"

#include <cstring>

namespace KCDC::Core {
    namespace {
        // Lowercase already: Localize lowercases a key before Scaleform asks for it.
        constexpr char kKeyPrefix[] = "kcdc_menu_text_";
    } // namespace

    NativeMenuLabels &NativeMenuLabels::Get() {
        static NativeMenuLabels instance;
        return instance;
    }

    const NativeMenuLabels::Key &NativeMenuLabels::Lookup(std::string_view text) {
        const auto found = _keyByText.find(std::string(text));
        if (found != _keyByText.end()) {
            return found->second;
        }
        std::string key = kKeyPrefix + std::to_string(_keyByText.size());
        _textByKey.emplace(key, text);
        // Node-based: the strings a returned pointer reads stay where they are as the map grows.
        return _keyByText.emplace(std::string(text), Key {key, "@" + key}).first->second;
    }

    const char *NativeMenuLabels::KeyFor(std::string_view text) {
        std::scoped_lock lock(_mutex);
        return Lookup(text).key.c_str();
    }

    const char *NativeMenuLabels::MarkedKeyFor(std::string_view text) {
        std::scoped_lock lock(_mutex);
        return Lookup(text).marked.c_str();
    }

    bool NativeMenuLabels::Resolve(const char *label, SDK::CryStringT<char> &out) const {
        if (label == nullptr) {
            return false;
        }
        const char *key = label[0] == '@' ? label + 1 : label;
        if (std::strncmp(key, kKeyPrefix, sizeof(kKeyPrefix) - 1) != 0) {
            return false;
        }
        std::scoped_lock lock(_mutex);
        const auto found = _textByKey.find(key);
        if (found == _textByKey.end()) {
            return false;
        }
        out.Assign(found->second.c_str());
        return true;
    }
} // namespace KCDC::Core

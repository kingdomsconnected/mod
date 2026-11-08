#pragma once

#include "i_flash_variable_object.h"

#include <utility>

namespace SDK {
    /**
     * Owns one native wrapper returned by Get/Create/Clone. Does not own a
     * display-list entry: removing a clip and releasing its wrapper differ.
     * Never adopt borrowed element-cache or ActionScript callback arguments.
     * Release all owned wrappers before the native UI subsystem shuts down.
     */
    class FlashVariableObjectRef final {
      public:
        FlashVariableObjectRef() = default;
        explicit FlashVariableObjectRef(IFlashVariableObject *owned): _object(owned) {}
        ~FlashVariableObjectRef() { Reset(); }
        FlashVariableObjectRef(const FlashVariableObjectRef &)            = delete;
        FlashVariableObjectRef &operator=(const FlashVariableObjectRef &) = delete;
        FlashVariableObjectRef(FlashVariableObjectRef &&other) noexcept: _object(std::exchange(other._object, nullptr)) {}
        FlashVariableObjectRef &operator=(FlashVariableObjectRef &&other) noexcept {
            if (this != &other) {
                Reset(std::exchange(other._object, nullptr));
            }
            return *this;
        }

        IFlashVariableObject *Get() const { return _object; }
        IFlashVariableObject *operator->() const { return _object; }
        explicit operator bool() const { return _object != nullptr; }
        /** Clear the previous owned result before passing to a native out arg. */
        IFlashVariableObject *&Receive() {
            Reset();
            return _object;
        }
        void Reset(IFlashVariableObject *owned = nullptr) {
            if (_object == owned) {
                return;
            }
            IFlashVariableObject *previous = std::exchange(_object, owned);
            if (previous != nullptr) {
                previous->Release();
            }
        }
        FlashVariableObjectRef Clone() const { return FlashVariableObjectRef(_object != nullptr ? _object->Clone() : nullptr); }

      private:
        IFlashVariableObject *_object = nullptr;
    };
} // namespace SDK

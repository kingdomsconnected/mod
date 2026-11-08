#pragma once

#include "i_xml_node.h"

#include <utility>

namespace SDK {
    /** Native intrusive handle; allocation and deletion remain in the game. */
    class XmlNodeRef {
      public:
        XmlNodeRef() = default;
        XmlNodeRef(const XmlNodeRef &other): _node(other._node) {
            if (_node != nullptr) {
                _node->AddRef();
            }
        }
        XmlNodeRef(XmlNodeRef &&other) noexcept: _node(std::exchange(other._node, nullptr)) {}
        ~XmlNodeRef() {
            if (_node != nullptr) {
                _node->Release();
            }
        }

        XmlNodeRef &operator=(const XmlNodeRef &other) {
            if (this != &other) {
                if (other._node != nullptr) {
                    other._node->AddRef();
                }
                if (_node != nullptr) {
                    _node->Release();
                }
                _node = other._node;
            }
            return *this;
        }

        XmlNodeRef &operator=(XmlNodeRef &&other) noexcept {
            if (this != &other) {
                if (_node != nullptr) {
                    _node->Release();
                }
                _node = std::exchange(other._node, nullptr);
            }
            return *this;
        }

        bool IsValid() const { return _node != nullptr; }

        /**
         * The node itself, for the interface's own methods.
         *
         * Null on an invalid handle, which is what every engine method that
         * answers with a node returns when it has none -- `getChild` past the
         * end, `findChild` for a tag that is not there -- so a handle taken
         * from one of those is tested before it is followed.
         */
        IXmlNode *Get() const { return _node; }
        IXmlNode *operator->() const { return _node; }

      private:
        IXmlNode *_node = nullptr;
        static void AssertLayout() { static_assert(offsetof(XmlNodeRef, _node) == 0, "XmlNodeRef pointer offset mismatch"); }
    };

    static_assert(sizeof(XmlNodeRef) == 0x8, "XmlNodeRef size mismatch");
} // namespace SDK

#pragma once

#include <cstddef>

namespace SDK {
    /**
     * Declared rather than included, because `XmlNodeRef` is a handle *to*
     * this interface and its own header includes this one. The definition
     * follows in `xml_node_ref.h`: a caller that wants to use one of the
     * returned handles includes that header, which is where both halves of
     * the pair become complete.
     */
    class XmlNodeRef;

    /**
     * CryEngine's XML node, vtable `CXmlNode` at 0x183A38690.
     *
     * Two implementations answer it and they are not interchangeable.
     * `CXmlNode` is the mutable one a parse or a `clone` produces.
     * `CXMLReadOnlyNode` (0x183A3CF28) is what comes back out of a pak,
     * where the document is binary XML sharing one immutable string pool:
     * every mutating slot on it is `nullsub` -- `addChild` included -- so a
     * write against one is silently dropped rather than refused. Clone it
     * first. `CXMLReadOnlyNode::clone` (0x180A6E960) ignores its `bool` and
     * always deep-copies into a fresh `CXmlNode` with a pool of its own.
     *
     * KCD2's table is stock CryEngine's declaration order with insertions, so
     * only the slots proven against surviving `CXmlNode` symbols and against
     * `C_Keybinds::ParseSuperactions` (0x1808FB4CC), which reads a document
     * through +0x30, +0x108, +0x110, +0x160 and +0x168, are named here.
     */
    class IXmlNode {
      protected:
        virtual void DeleteThis() = 0;
        virtual ~IXmlNode()       = default;

      public:
        /** A new node of this node's own document, parented to nothing. */
        virtual XmlNodeRef createNode(const char *tag) = 0; // +0x10

        virtual void AddRef()  = 0; // +0x18
        virtual void Release() = 0; // +0x20

        virtual void unk_fn0005() = 0;

        virtual const char *getTag() const   = 0; // +0x30
        virtual void setTag(const char *tag) = 0;
        virtual bool isTag(const char *tag) const = 0;

        virtual void unk_fn0009() = 0; // getNumAttributes
        virtual void unk_fn0010() = 0; // getAttributeByIndex
        virtual void unk_fn0011() = 0; // copyAttributes

        // +0x60 through +0x100: the twenty-one typed `getAttr` overloads,
        // which stay opaque because naming one of them would be declaring an
        // overload set whose emission order MSVC reverses.
        virtual void unk_fn0012() = 0;
        virtual void unk_fn0013() = 0;
        virtual void unk_fn0014() = 0;
        virtual void unk_fn0015() = 0;
        virtual void unk_fn0016() = 0;
        virtual void unk_fn0017() = 0;
        virtual void unk_fn0018() = 0;
        virtual void unk_fn0019() = 0;
        virtual void unk_fn0020() = 0;
        virtual void unk_fn0021() = 0;
        virtual void unk_fn0022() = 0;
        virtual void unk_fn0023() = 0;
        virtual void unk_fn0024() = 0;
        virtual void unk_fn0025() = 0;
        virtual void unk_fn0026() = 0;
        virtual void unk_fn0027() = 0;
        virtual void unk_fn0028() = 0;
        virtual void unk_fn0029() = 0;
        virtual void unk_fn0030() = 0;
        virtual void unk_fn0031() = 0;
        virtual void unk_fn0032() = 0;

        /** The empty string for an attribute this node does not carry, never null. */
        virtual const char *getAttr(const char *key) const = 0; // +0x108

        virtual bool haveAttr(const char *key) const = 0; // +0x110

        virtual void unk_fn0035() = 0;
        virtual void unk_fn0036() = 0;
        virtual void unk_fn0037() = 0;

        /** Adopts an existing node. A no-op on `CXMLReadOnlyNode`. */
        virtual void addChild(const XmlNodeRef &node) = 0; // +0x130

        /** Creates the child and parents it in one step. */
        virtual XmlNodeRef newChild(const char *tag) = 0; // +0x138

        virtual void unk_fn0040() = 0; // removeChild
        virtual void unk_fn0041() = 0; // insertChild
        virtual void unk_fn0042() = 0; // replaceChild
        virtual void unk_fn0043() = 0; // removeAllChilds

        virtual int getChildCount() const = 0; // +0x160

        /** An invalid handle for an index out of range. */
        virtual XmlNodeRef getChild(int index) const = 0; // +0x168

        /** The first child carrying `tag`, or an invalid handle. */
        virtual XmlNodeRef findChild(const char *tag) const = 0; // +0x170

        virtual void unk_fn0047() = 0; // getParent
        virtual void unk_fn0048() = 0; // setParent
        virtual void unk_fn0049() = 0;
        virtual void unk_fn0050() = 0; // setContent
        virtual void unk_fn0051() = 0; // clone(IXmlStringPool *)

        /**
         * A deep, mutable copy. The flag is stock CryEngine's; both
         * implementations in this build ignore it and always recurse.
         */
        virtual XmlNodeRef clone(bool recursive) = 0; // +0x1A0

        virtual void unk_fn0053() = 0;
        virtual void unk_fn0054() = 0;
        virtual void unk_fn0055() = 0; // getLine
        virtual void unk_fn0056() = 0; // setLine
        virtual void unk_fn0057() = 0; // getXMLData
        virtual void unk_fn0058() = 0; // getXML
        virtual void unk_fn0059() = 0;
        virtual void unk_fn0060() = 0;

        // +0x1E8 through +0x268: the typed `setAttr` overloads, opaque for
        // the same reason the `getAttr` run above is.
        virtual void unk_fn0061() = 0;
        virtual void unk_fn0062() = 0;
        virtual void unk_fn0063() = 0;
        virtual void unk_fn0064() = 0;
        virtual void unk_fn0065() = 0;
        virtual void unk_fn0066() = 0;
        virtual void unk_fn0067() = 0;
        virtual void unk_fn0068() = 0;
        virtual void unk_fn0069() = 0;
        virtual void unk_fn0070() = 0;
        virtual void unk_fn0071() = 0;
        virtual void unk_fn0072() = 0;
        virtual void unk_fn0073() = 0;
        virtual void unk_fn0074() = 0;
        virtual void unk_fn0075() = 0;
        virtual void unk_fn0076() = 0;
        virtual void unk_fn0077() = 0;

        /** Interns both strings in the node's own pool; replaces an existing key. */
        virtual void setAttr(const char *key, const char *value) = 0; // +0x270

      private:
        int m_refCount;
        std::byte m_opaque000C[4];

        static void AssertLayout() { static_assert(offsetof(IXmlNode, m_refCount) == 0x8, "IXmlNode reference count mismatch"); }
    };

    static_assert(sizeof(IXmlNode) == 0x10, "IXmlNode size mismatch");
} // namespace SDK

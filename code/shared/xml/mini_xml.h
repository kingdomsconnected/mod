#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace KCDC::Shared::Xml {
    /** One element of a parsed document: its tag, attributes and child elements. Text content is dropped. */
    struct Element {
        std::string tag;
        std::map<std::string, std::string, std::less<>> attributes;
        std::vector<Element> children;

        /** The attribute's value, or empty when the element has none of that name. */
        std::string_view Attribute(std::string_view name) const;
        bool HasAttribute(std::string_view name) const;
        /** The first child carrying `tag`, or null. */
        const Element *Child(std::string_view tag) const;
    };

    /**
     * Reads the element tree of a well-formed XML document: declarations, comments, processing
     * instructions and CDATA are skipped, the five standard entities and numeric references are
     * decoded in attribute values. Namespaced attributes are kept under their full name. Enough for
     * the game's tables, which are elements and attributes only; not a validating parser. Elements
     * nest at most 256 deep.
     *
     * @return False, with `error` set, on a document this reader cannot follow.
     */
    bool Parse(std::string_view text, Element &root, std::string &error);
} // namespace KCDC::Shared::Xml

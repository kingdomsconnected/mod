#include "mini_xml.h"

#include <cctype>
#include <cstdint>

namespace KCDC::Shared::Xml {
    namespace {
        // Every element is a call deeper; the game's tables nest a dozen levels at most, and a
        // document from a server is not trusted to stop before the stack does.
        constexpr std::size_t kMaximumDepth = 256;

        bool IsNameChar(char c) {
            const auto u = static_cast<unsigned char>(c);
            return std::isalnum(u) != 0 || c == '_' || c == '-' || c == '.' || c == ':' || u >= 0x80;
        }

        void AppendUtf8(std::string &out, std::uint32_t code) {
            if (code < 0x80) {
                out.push_back(static_cast<char>(code));
            }
            else if (code < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            else if (code < 0x10000) {
                out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
            else {
                out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            }
        }

        std::string Decode(std::string_view raw) {
            std::string out;
            out.reserve(raw.size());
            for (std::size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] != '&') {
                    out.push_back(raw[i]);
                    continue;
                }
                const std::size_t end = raw.find(';', i);
                if (end == std::string_view::npos) {
                    out.push_back('&');
                    continue;
                }
                const std::string_view entity = raw.substr(i + 1, end - i - 1);
                if (entity == "amp") {
                    out.push_back('&');
                }
                else if (entity == "lt") {
                    out.push_back('<');
                }
                else if (entity == "gt") {
                    out.push_back('>');
                }
                else if (entity == "quot") {
                    out.push_back('"');
                }
                else if (entity == "apos") {
                    out.push_back('\'');
                }
                else if (!entity.empty() && entity[0] == '#') {
                    const bool hex = entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X');
                    std::uint32_t code = 0;
                    bool valid         = entity.size() > (hex ? 2U : 1U);
                    for (std::size_t k = hex ? 2 : 1; k < entity.size() && valid; ++k) {
                        const char c = entity[k];
                        if (hex && std::isxdigit(static_cast<unsigned char>(c)) != 0) {
                            code = code * 16 + static_cast<std::uint32_t>(std::isdigit(static_cast<unsigned char>(c)) != 0 ? c - '0' : (std::tolower(static_cast<unsigned char>(c)) - 'a' + 10));
                        }
                        else if (!hex && std::isdigit(static_cast<unsigned char>(c)) != 0) {
                            code = code * 10 + static_cast<std::uint32_t>(c - '0');
                        }
                        else {
                            valid = false;
                        }
                        valid = valid && code <= 0x10FFFF;
                    }
                    if (valid) {
                        AppendUtf8(out, code);
                    }
                    else {
                        out.append(raw.substr(i, end - i + 1));
                    }
                }
                else {
                    out.append(raw.substr(i, end - i + 1));
                }
                i = end;
            }
            return out;
        }

        class Reader {
          public:
            Reader(std::string_view text, std::string &error): _text(text), _error(error) {}

            bool Document(Element &root) {
                bool haveRoot = false;
                while (SkipMisc()) {
                    if (_pos >= _text.size()) {
                        break;
                    }
                    if (haveRoot) {
                        return Fail("content after the root element");
                    }
                    if (!ReadElement(root)) {
                        return false;
                    }
                    haveRoot = true;
                }
                if (!_error.empty()) {
                    return false;
                }
                return haveRoot || Fail("no root element");
            }

          private:
            bool Fail(const char *why) {
                _error = std::string(why) + " at offset " + std::to_string(_pos);
                return false;
            }

            void SkipSpace() {
                while (_pos < _text.size() && std::isspace(static_cast<unsigned char>(_text[_pos])) != 0) {
                    ++_pos;
                }
            }

            bool SkipTo(std::string_view marker) {
                const std::size_t end = _text.find(marker, _pos);
                if (end == std::string_view::npos) {
                    return Fail("unterminated construct");
                }
                _pos = end + marker.size();
                return true;
            }

            /** Skips whitespace, text, comments, declarations and CDATA up to the next tag or the end; false on error. */
            bool SkipMisc() {
                while (_pos < _text.size()) {
                    if (_text[_pos] != '<') {
                        ++_pos;
                        continue;
                    }
                    if (_text.compare(_pos, 4, "<!--") == 0) {
                        if (!SkipTo("-->")) {
                            return false;
                        }
                    }
                    else if (_text.compare(_pos, 9, "<![CDATA[") == 0) {
                        if (!SkipTo("]]>")) {
                            return false;
                        }
                    }
                    else if (_text.compare(_pos, 2, "<?") == 0) {
                        if (!SkipTo("?>")) {
                            return false;
                        }
                    }
                    else if (_text.compare(_pos, 2, "<!") == 0) {
                        if (!SkipTo(">")) {
                            return false;
                        }
                    }
                    else {
                        return true;
                    }
                }
                return true;
            }

            bool ReadName(std::string &out) {
                const std::size_t start = _pos;
                while (_pos < _text.size() && IsNameChar(_text[_pos])) {
                    ++_pos;
                }
                if (_pos == start) {
                    return Fail("expected a name");
                }
                out.assign(_text.substr(start, _pos - start));
                return true;
            }

            bool ReadElement(Element &element, std::size_t depth = 1) {
                if (depth > kMaximumDepth) {
                    return Fail("elements nested too deeply");
                }
                if (_pos >= _text.size() || _text[_pos] != '<') {
                    return Fail("expected '<'");
                }
                ++_pos;
                if (!ReadName(element.tag)) {
                    return false;
                }
                for (;;) {
                    SkipSpace();
                    if (_pos >= _text.size()) {
                        return Fail("unterminated start tag");
                    }
                    if (_text[_pos] == '/') {
                        ++_pos;
                        if (_pos >= _text.size() || _text[_pos] != '>') {
                            return Fail("expected '>' after '/'");
                        }
                        ++_pos;
                        return true;
                    }
                    if (_text[_pos] == '>') {
                        ++_pos;
                        break;
                    }
                    std::string name;
                    if (!ReadName(name)) {
                        return false;
                    }
                    SkipSpace();
                    if (_pos >= _text.size() || _text[_pos] != '=') {
                        return Fail("expected '=' after an attribute name");
                    }
                    ++_pos;
                    SkipSpace();
                    if (_pos >= _text.size() || (_text[_pos] != '"' && _text[_pos] != '\'')) {
                        return Fail("expected a quoted attribute value");
                    }
                    const char quote      = _text[_pos++];
                    const std::size_t end = _text.find(quote, _pos);
                    if (end == std::string_view::npos) {
                        return Fail("unterminated attribute value");
                    }
                    element.attributes.insert_or_assign(std::move(name), Decode(_text.substr(_pos, end - _pos)));
                    _pos = end + 1;
                }
                // Children until the matching end tag.
                for (;;) {
                    if (!SkipMisc()) {
                        return false;
                    }
                    if (_pos >= _text.size()) {
                        return Fail("unterminated element");
                    }
                    if (_text.compare(_pos, 2, "</") == 0) {
                        _pos += 2;
                        std::string closing;
                        if (!ReadName(closing)) {
                            return false;
                        }
                        if (closing != element.tag) {
                            return Fail("mismatched end tag");
                        }
                        SkipSpace();
                        if (_pos >= _text.size() || _text[_pos] != '>') {
                            return Fail("expected '>' in an end tag");
                        }
                        ++_pos;
                        return true;
                    }
                    element.children.emplace_back();
                    if (!ReadElement(element.children.back(), depth + 1)) {
                        return false;
                    }
                }
            }

            std::string_view _text;
            std::string &_error;
            std::size_t _pos = 0;
        };
    } // namespace

    std::string_view Element::Attribute(std::string_view name) const {
        const auto found = attributes.find(name);
        return found == attributes.end() ? std::string_view {} : std::string_view(found->second);
    }

    bool Element::HasAttribute(std::string_view name) const {
        return attributes.find(name) != attributes.end();
    }

    const Element *Element::Child(std::string_view tag) const {
        for (const Element &child : children) {
            if (child.tag == tag) {
                return &child;
            }
        }
        return nullptr;
    }

    bool Parse(std::string_view text, Element &root, std::string &error) {
        error.clear();
        root = Element {};
        // A UTF-8 byte order mark is not content.
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
            text.remove_prefix(3);
        }
        return Reader(text, error).Document(root);
    }
} // namespace KCDC::Shared::Xml

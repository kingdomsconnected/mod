#include "lua_script_scan.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <optional>

namespace KCDC::Shared::StreamedAssets {
    namespace {
        constexpr std::array<std::string_view, 5> kRiskyNames  = {"io", "loadstring", "load", "dofile", "loadfile"};
        constexpr std::array<std::string_view, 4> kSafeOsNames = {"time", "date", "clock", "difftime"};

        enum class TokenKind {
            Name,
            Dot,
            Colon,
            Assign,
            Open,
            Separator,
            Other,
        };

        struct Token {
            TokenKind kind = TokenKind::Other;
            std::string_view text;
        };

        bool IsNameStart(char c) {
            return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
        }

        bool IsNameBody(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
        }

        template <std::size_t N>
        bool Contains(const std::array<std::string_view, N> &set, std::string_view value) {
            for (const std::string_view entry : set) {
                if (entry == value) {
                    return true;
                }
            }
            return false;
        }

        // The level of a long bracket opening at `at` (`[[` is 0, `[=[` is 1), if one does.
        std::optional<std::size_t> LongBracketLevel(std::string_view source, std::size_t at) {
            if (at >= source.size() || source[at] != '[') {
                return std::nullopt;
            }
            std::size_t cursor = at + 1;
            while (cursor < source.size() && source[cursor] == '=') {
                ++cursor;
            }
            if (cursor < source.size() && source[cursor] == '[') {
                return cursor - at - 1;
            }
            return std::nullopt;
        }

        // Just past the long bracket opening at `at`, or the end of the source when it never closes.
        std::size_t SkipLongBracket(std::string_view source, std::size_t at, std::size_t level) {
            const std::string closing = "]" + std::string(level, '=') + "]";
            const std::size_t found   = source.find(closing, at + level + 2);
            return found == std::string_view::npos ? source.size() : found + closing.size();
        }

        std::vector<Token> Tokenize(std::string_view source) {
            std::vector<Token> tokens;
            std::size_t at = 0;
            while (at < source.size()) {
                const char c = source[at];
                if (std::isspace(static_cast<unsigned char>(c)) != 0) {
                    ++at;
                }
                else if (source.compare(at, 2, "--") == 0) {
                    if (const std::optional<std::size_t> level = LongBracketLevel(source, at + 2)) {
                        at = SkipLongBracket(source, at + 2, *level);
                    }
                    else {
                        const std::size_t newline = source.find('\n', at);
                        at                        = newline == std::string_view::npos ? source.size() : newline + 1;
                    }
                }
                else if (c == '"' || c == '\'') {
                    std::size_t cursor = at + 1;
                    while (cursor < source.size() && source[cursor] != c && source[cursor] != '\n') {
                        cursor += source[cursor] == '\\' ? 2 : 1;
                    }
                    at = cursor + 1;
                    tokens.push_back({TokenKind::Other, {}});
                }
                else if (const std::optional<std::size_t> level = LongBracketLevel(source, at)) {
                    at = SkipLongBracket(source, at, *level);
                    tokens.push_back({TokenKind::Other, {}});
                }
                else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
                    while (at < source.size() && (IsNameBody(source[at]) || source[at] == '.')) {
                        ++at;
                    }
                    tokens.push_back({TokenKind::Other, {}});
                }
                else if (IsNameStart(c)) {
                    const std::size_t start = at;
                    while (at < source.size() && IsNameBody(source[at])) {
                        ++at;
                    }
                    tokens.push_back({TokenKind::Name, source.substr(start, at - start)});
                }
                else if (source.compare(at, 2, "..") == 0) {
                    at += source.compare(at, 3, "...") == 0 ? 3 : 2;
                    tokens.push_back({TokenKind::Other, {}});
                }
                else if (source.compare(at, 2, "==") == 0 || source.compare(at, 2, "~=") == 0 || source.compare(at, 2, "<=") == 0 || source.compare(at, 2, ">=") == 0) {
                    at += 2;
                    tokens.push_back({TokenKind::Other, {}});
                }
                else {
                    ++at;
                    switch (c) {
                    case '.': tokens.push_back({TokenKind::Dot, {}}); break;
                    case ':': tokens.push_back({TokenKind::Colon, {}}); break;
                    case '=': tokens.push_back({TokenKind::Assign, {}}); break;
                    case '{': tokens.push_back({TokenKind::Open, {}}); break;
                    case ',':
                    case ';': tokens.push_back({TokenKind::Separator, {}}); break;
                    default: tokens.push_back({TokenKind::Other, {}}); break;
                    }
                }
            }
            return tokens;
        }
    } // namespace

    LuaScriptScan ScanLuaScript(std::string_view source) {
        LuaScriptScan scan;
        // Bytecode is refused rather than scanned: it does not have to mean what its text says.
        if ((!source.empty() && source.front() == '\x1b') || source.find('\0') != std::string_view::npos) {
            scan.unreadable = true;
            return scan;
        }

        std::set<std::string> risky;
        const std::vector<Token> tokens = Tokenize(source);
        for (std::size_t index = 0; index < tokens.size(); ++index) {
            if (tokens[index].kind != TokenKind::Name) {
                continue;
            }
            const TokenKind before = index == 0 ? TokenKind::Other : tokens[index - 1].kind;
            if (before == TokenKind::Dot || before == TokenKind::Colon) {
                continue;
            }
            const TokenKind after = index + 1 < tokens.size() ? tokens[index + 1].kind : TokenKind::Other;
            // `{ load = 1 }` names a field; nothing is read.
            if (after == TokenKind::Assign && (before == TokenKind::Open || before == TokenKind::Separator)) {
                continue;
            }

            const std::string_view name = tokens[index].text;
            if (Contains(kRiskyNames, name)) {
                risky.emplace(name);
            }
            else if (name == "os") {
                const bool field = after == TokenKind::Dot && index + 2 < tokens.size() && tokens[index + 2].kind == TokenKind::Name;
                if (!field) {
                    risky.emplace("os");
                }
                else if (!Contains(kSafeOsNames, tokens[index + 2].text)) {
                    risky.insert("os." + std::string(tokens[index + 2].text));
                }
            }
        }
        scan.risky.assign(risky.begin(), risky.end());
        return scan;
    }

    LuaScriptApprovals LuaScriptApprovals::Parse(std::string_view text) {
        LuaScriptApprovals approvals;
        std::size_t start = 0;
        while (start < text.size()) {
            const std::size_t newline = text.find('\n', start);
            std::string_view line     = text.substr(start, newline == std::string_view::npos ? std::string_view::npos : newline - start);
            if (!line.empty() && line.back() == '\r') {
                line.remove_suffix(1);
            }
            const std::size_t space = line.find(' ');
            if (space != std::string_view::npos && space > 0 && space + 1 < line.size() && line.find(' ', space + 1) == std::string_view::npos) {
                approvals._lines.emplace(line);
            }
            if (newline == std::string_view::npos) {
                break;
            }
            start = newline + 1;
        }
        return approvals;
    }

    std::string LuaScriptApprovals::ToText() const {
        std::string text;
        for (const std::string &line : _lines) {
            text += line + "\n";
        }
        return text;
    }

    bool LuaScriptApprovals::Contains(std::string_view resource, std::string_view digest) const {
        return _lines.count(std::string(resource) + " " + std::string(digest)) != 0;
    }

    void LuaScriptApprovals::Add(std::string_view resource, std::string_view digest) {
        _lines.insert(std::string(resource) + " " + std::string(digest));
    }
} // namespace KCDC::Shared::StreamedAssets

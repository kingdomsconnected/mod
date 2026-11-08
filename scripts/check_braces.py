"""Fails when an `if`, `else`, `for` or `while` body is not a braced block.

usage: python scripts/check_braces.py [--fix] [path ...]

Every control statement takes braces, even a one-line `return`. clang-format
inserts them on format (`InsertBraces` in `.clang-format`); this is the gate
for code that was never run through it. Comments, string and character
literals and preprocessor lines are blanked before scanning, so a keyword in
any of them is not a statement.
"""
import os
import re
import sys

ROOTS = ['code/client/src', 'code/server/src', 'code/shared', 'code/launcher/src', 'code/tests']
SKIP_DIRS = {'generated'}
EXTENSIONS = ('.h', '.hpp', '.cpp', '.cc')
KEYWORD = re.compile(r'\b(if|else|for|while)\b')


def blank(text):
    """Replaces comments, literals and preprocessor lines with spaces, keeping offsets."""
    out = list(text)
    i, n = 0, len(text)

    def wipe(a, b):
        for k in range(a, b):
            if out[k] != '\n':
                out[k] = ' '

    line_start = True
    while i < n:
        c = text[i]
        if line_start and c in ' \t':
            i += 1
            continue
        if line_start and c == '#':
            j = i
            while j < n and (text[j] != '\n' or text[j - 1] == '\\'):
                j += 1
            wipe(i, j)
            i = j
            continue
        line_start = c == '\n'
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            wipe(i, j)
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            wipe(i, j)
            i = j
        elif c == 'R' and text.startswith('R"', i):
            m = re.match(r'R"([^(\s]*)\(', text[i:])
            if m:
                end = text.find(')' + m.group(1) + '"', i)
                end = n if end < 0 else end + len(m.group(1)) + 2
                wipe(i, end)
                i = end
            else:
                i += 1
        elif c in '"\'':
            # A digit separator (1'000'000) is not a character literal.
            if c == "'" and i > 0 and text[i - 1].isalnum() and i + 1 < n and text[i + 1].isalnum():
                i += 1
                continue
            j = i + 1
            while j < n and text[j] != c and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            wipe(i, min(j + 1, n))
            i = j + 1
        else:
            i += 1
    return ''.join(out)


def skip_space(code, i):
    while i < len(code) and code[i].isspace():
        i += 1
    return i


def close_paren(code, i):
    depth = 0
    while i < len(code):
        if code[i] == '(':
            depth += 1
        elif code[i] == ')':
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def heads(code):
    """Yields (keyword start, end of the head, body start) for every brace-less body."""
    for m in KEYWORD.finditer(code):
        word, i = m.group(1), m.end()
        if m.start() > 0 and (code[m.start() - 1].isalnum() or code[m.start() - 1] == '_'):
            continue
        if word == 'else':
            j = skip_space(code, i)
            if code.startswith('if', j) and not (j + 2 < len(code) and (code[j + 2].isalnum() or code[j + 2] == '_')):
                continue
            head_end, body = i, j
        else:
            j = skip_space(code, i)
            if word == 'if' and code.startswith('constexpr', j):
                j = skip_space(code, j + len('constexpr'))
            if j >= len(code) or code[j] != '(':
                continue
            end = close_paren(code, j)
            if end < 0:
                continue
            head_end, body = end + 1, skip_space(code, end + 1)
            # `do { ... } while (x);` -- the loop condition, not a statement.
            if word == 'while' and body < len(code) and code[body] == ';':
                continue
        if body < len(code) and code[body] != '{':
            yield m.start(), head_end, body


def violations(text):
    code = blank(text)
    for start, _, _ in heads(code):
        yield text.count('\n', 0, start) + 1, KEYWORD.match(code, start).group(1)


def matching(code, i, open_ch, close_ch):
    depth = 0
    while i < len(code):
        if code[i] == open_ch:
            depth += 1
        elif code[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def word_at(code, i):
    m = re.match(r'[A-Za-z_]\w*', code[i:])
    return m.group(0) if m else ''


def statement_end(code, i):
    """Offset just past the statement starting at i, or -1 when it cannot be bounded."""
    i = skip_space(code, i)
    word = word_at(code, i)
    if code.startswith('{', i):
        end = matching(code, i, '{', '}')
        return -1 if end < 0 else end + 1
    if word in ('if', 'for', 'while', 'switch'):
        j = skip_space(code, i + len(word))
        if word == 'if' and code.startswith('constexpr', j):
            j = skip_space(code, j + len('constexpr'))
        end = close_paren(code, j) if code.startswith('(', j) else -1
        if end < 0:
            return -1
        end = statement_end(code, end + 1)
        if word == 'if' and end > 0:
            k = skip_space(code, end)
            if word_at(code, k) == 'else':
                return statement_end(code, k + len('else'))
        return end
    if word in ('do', 'try'):
        return -1
    depth = 0
    while i < len(code):
        c = code[i]
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth < 0:
                return -1
        elif c == ';' and depth == 0:
            return i + 1
        i += 1
    return -1


def line_indent(text, offset):
    start = text.rfind('\n', 0, offset) + 1
    return text[start:offset][:len(text[start:offset]) - len(text[start:offset].lstrip(' \t'))]


def fix(text):
    """Braces every brace-less body, innermost-last-first; returns (text, fixed, skipped lines)."""
    fixed, skipped = 0, set()
    while True:
        code = blank(text)
        pending = [h for h in heads(code) if h[0] not in skipped]
        if not pending:
            break
        start, head_end, body = pending[-1]
        end = statement_end(code, body)
        if end < 0:
            skipped.add(start)
            continue
        indent = line_indent(text, start)
        nl = '\r\n' if '\r\n' in text else '\n'
        # Close: after the statement, and after a comment trailing it on its line.
        line_end = text.find('\n', end)
        line_end = len(text) if line_end < 0 else line_end
        rest = code[end:line_end]
        head_line_start = text.rfind('\n', 0, start) + 1
        before = code[head_line_start:start]
        if rest.strip() and '\n' not in text[start:end] and before.count('{') > before.count('}'):
            # Inside a block written on one line (a one-line lambda): brace it in place.
            text = text[:end] + ' }' + text[end:]
            text = text[:head_end] + ' {' + text[head_end:]
            fixed += 1
            continue
        if rest.strip():
            # Code follows on the same line (`if (a) b; else c;`): break it off.
            tail_start = skip_space(text, end) if text[end:line_end].strip() else end
            closing = nl + indent + '}' + nl + indent
            text = text[:end] + closing + text[tail_start:]
        else:
            close_at = line_end - 1 if text[line_end - 1:line_end] == '\r' else line_end
            text = text[:close_at] + nl + indent + '}' + text[close_at:]
        # Open: right after the head; a body on the same line moves to its own.
        same_line = '\n' not in text[head_end:body]
        if same_line:
            text = text[:head_end] + ' {' + nl + indent + '    ' + text[body:]
        else:
            text = text[:head_end] + ' {' + text[head_end:]
        fixed += 1
    return text, fixed, sorted(text.count('\n', 0, s) + 1 for s in skipped)


def files_under(roots):
    for root in roots:
        if os.path.isfile(root):
            yield root
            continue
        for d, dirs, names in os.walk(root):
            dirs[:] = [x for x in dirs if x not in SKIP_DIRS]
            for name in names:
                if name.endswith(EXTENSIONS):
                    yield os.path.join(d, name)


def main(args):
    apply = '--fix' in args
    paths = [a for a in args if a != '--fix'] or ROOTS
    failures = 0
    for path in files_under(paths):
        with open(path, encoding='utf-8', newline='') as f:
            text = f.read()
        if apply:
            new, fixed, skipped = fix(text)
            if fixed:
                with open(path, 'w', encoding='utf-8', newline='') as f:
                    f.write(new)
            text = new
        for line, word in violations(text):
            print(f'{path}:{line}: `{word}` body is not a braced block')
            failures += 1
    if failures:
        print(f'{failures} control statement(s) without braces')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))

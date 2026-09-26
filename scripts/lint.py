#!/usr/bin/env python3
"""House rules for OmaPhoto: files under 500 lines, comments of one line and at most ten words.

Reads C++ sources and tests, everything under scripts/, CMakeLists.txt and Dockerfile.dev, each with a
scanner for its own language. Run from the repository root; exits 1 and lists each violation.
`--self-test` checks the checker.
"""
import glob, io, os, re, sys, tokenize


def comments(text):
    """Yields (first line, last line, text) for each comment outside literals and numbers."""
    index, line, size = 0, 1, len(text)
    while index < size:
        char, two = text[index], text[index:index + 2]
        if two == '//':
            end = text.find('\n', index)
            end = size if end < 0 else end
            yield line, line, text[index + 2:end]
            index = end
        elif two == '/*':
            end = text.find('*/', index + 2)
            end = size if end < 0 else end
            body = text[index + 2:end]
            yield line, line + body.count('\n'), body
            line += body.count('\n')
            index = end + 2
        elif char.isdigit() or (char == '.' and text[index + 1:index + 2].isdigit()):
            # A number keeps its digit separators: 1'000, 0xFF'FF.
            index += 1
            while index < size and (text[index].isalnum() or text[index] in '._'
                                    or (text[index] == "'" and text[index + 1:index + 2].isalnum())):
                index += 1
        elif char.isalpha() or char == '_':
            start = index
            while index < size and (text[index].isalnum() or text[index] == '_'):
                index += 1
            if text[start:index].endswith('R') and text[index:index + 1] == '"':
                opened = text.index('(', index)
                closing = ')' + text[index + 1:opened] + '"'
                end = text.index(closing, opened) + len(closing)
                line += text.count('\n', index, end)
                index = end
        elif char in '"\'':
            start, index = index, index + 1
            while index < size and text[index] != char:
                index += 2 if text[index] == '\\' else 1
            index += 1
            line += text.count('\n', start, index)
        else:
            line += char == '\n'
            index += 1


def python_comments(text):
    """Yields (line, line, text) for each comment of a Python file; its tokenizer knows strings."""
    for token in tokenize.generate_tokens(io.StringIO(text).readline):
        if token.type == tokenize.COMMENT and not token.string.startswith('#!'):
            yield token.start[0], token.start[0], token.string[1:]


def docker_comments(text):
    """Yields (line, line, text): Docker drops every line that begins with `#`, wherever it stands."""
    for number, row in enumerate(text.splitlines(), 1):
        if row.strip().startswith('#'):
            yield number, number, row.strip()[1:]


class Unreadable(Exception):
    """Shell the scanner refuses to read; the file fails at that line."""


def shell_comments(text):
    """Returns (line, line, text) for each comment of a shell script.

    Reads the shell our scripts keep to: quotes, escapes, `$( )` and `${ }` to any depth, here-strings,
    and `#` where a word begins. What it cannot read exactly it refuses by raising Unreadable, so nothing
    passes unread: `<<` (a heredoc or a shift), backticks, `$'…'`, `((`, `case` inside `$( )`, and a
    continued line anywhere but between words (it could split `$(`, `<<` or a keyword).
    """
    index, line, size, found = 0, 1, len(text), []

    def refuse(what):
        raise Unreadable('%d: lint does not read %s' % (line, what))

    def single():
        nonlocal index, line
        end = text.find("'", index + 1)
        if end < 0:
            refuse('a quote left open')
        line += text.count('\n', index, end)
        index = end + 1

    def expansion():
        """One step inside `"…"` or `${…}`: an escape, a substitution, or a character."""
        nonlocal index, line
        if text[index] == '$' and text.startswith('\\\n', index + 1):
            refuse('a continued line that splits `$` from what it opens')
        elif text[index] == '\\':
            line += text[index + 1:index + 2] == '\n'
            index += 2
        elif text[index] == '`':
            refuse('backticks')
        elif text.startswith('$((', index):
            refuse('arithmetic')
        elif text.startswith('$(', index):
            index += 2
            code(')')
        elif text.startswith('${', index):
            index += 2
            enclosed('}', 'a brace left open')
        else:
            line += text[index] == '\n'
            index += 1

    def enclosed(closer, otherwise):
        """To the `closer` of `"…"` or `${…}`; strings nest inside braces."""
        nonlocal index
        if closer == '"':
            index += 1
        while index < size and text[index] != closer:
            if closer == '}' and text[index] == "'":
                single()
            elif closer == '}' and text[index] == '"':
                enclosed('"', 'a quote left open')
            else:
                expansion()
        if index >= size:
            refuse(otherwise)
        index += 1

    def code(closer):
        """Shell code to `closer`, or to the end of the text."""
        nonlocal index, line
        word, depth = False, 0
        while index < size:
            char = text[index]
            if char == '\\':
                # A continued line is nothing; other escapes are words.
                continued = text[index + 1:index + 2] == '\n'
                if continued and index > 0 and text[index - 1] not in ' \t\n':
                    refuse('a continued line that splits a word or an operator')
                line, word, index = line + continued, word or not continued, index + 2
            elif char in '\'"':
                single() if char == "'" else enclosed('"', 'a quote left open')
                word = True
            elif char == '`' or text.startswith("$'", index):
                refuse('backticks' if char == '`' else "$'…' quoting")
            elif text.startswith('${', index) or text.startswith('$(', index):
                expansion()
                word = True
            elif text.startswith('<<<', index):
                index, word = index + 3, False
            elif text.startswith('<<', index) or text.startswith('((', index):
                refuse('`<<`: a heredoc or a shift' if char == '<' else '`((`')
            elif char == ')' and closer and depth == 0:
                index += 1
                return
            elif char in ' \t\n;&|<>()':
                depth += (char == '(') - (char == ')')
                line, word, index = line + (char == '\n'), False, index + 1
            elif char == '#' and not word:
                end = text.find('\n', index)
                end = size if end < 0 else end
                found.append((line, line, text[index + 1:end]))
                index = end
            else:
                if closer and not word and re.compile(r'case\b').match(text, index):
                    refuse('`case` inside `$( )`')
                index, word = index + 1, True
        if closer:
            refuse('`$(` left open')

    if text.startswith('#!'):
        index = text.find('\n') if '\n' in text else size
    code(None)
    return found


def cmake_comments(text):
    """Yields (first line, last line, text) for each comment of a CMake file.

    `"` strings and `\\` escapes, bracket arguments `[=[ ]=]` where an argument begins (inside an unquoted
    argument, escapes included, `[[` is text), bracket comments `#[=[ ]=]`, and `#` anywhere else.
    """
    index, line, size, argument = 0, 1, len(text), False
    while index < size:
        char = text[index]
        if char == '\\':
            line += text[index + 1:index + 2] == '\n'
            index, argument = index + 2, True
        elif char == '"':
            # A quote inside an unquoted argument is part of it.
            start, index = index, index + 1
            while index < size and text[index] != '"':
                index += 2 if text[index] == '\\' else 1
            index += 1
            line += text.count('\n', start, index)
        elif (bracket := re.compile(r'#?\[(=*)\[').match(text, index)) and (char == '#' or not argument):
            closer = ']' + bracket.group(1) + ']'
            close = text.find(closer, bracket.end())
            close = size if close < 0 else close
            lines = text.count('\n', index, close)
            if bracket.group(0).startswith('#'):
                yield line, line + lines, text[bracket.end():close]
            line, index, argument = line + lines, close + len(closer), False
        elif char == '#':
            end = text.find('\n', index)
            end = size if end < 0 else end
            yield line, line, text[index + 1:end]
            index = end
        else:
            # An unquoted argument runs to a blank or a parenthesis.
            line, index, argument = line + (char == '\n'), index + 1, char not in ' \t\n()'


def violations(path, text):
    found = []
    if len(text.splitlines()) >= 500:
        found.append('%s: %d lines' % (path, len(text.splitlines())))
    rows = text.splitlines()
    previous = None
    name = path.rsplit('/', 1)[-1]
    scanners = [(('.cpp', '.h'), comments), (('.py',), python_comments), (('.sh',), shell_comments),
                (('CMakeLists.txt', '.cmake'), cmake_comments)]
    scanner = docker_comments if name.startswith('Dockerfile') else next((s for ends, s in scanners if name.endswith(ends)), None)
    if scanner is None:
        return found + ['%s: lint has no scanner for this kind of file' % path]
    try:
        found_comments = list(scanner(text))
    except Unreadable as refusal:
        return found + ['%s:%s' % (path, refusal)]
    for first, last, body in found_comments:
        if last > first:
            found.append('%s:%d: comment runs over one line' % (path, first))
        alone = rows[first - 1].strip().startswith(('//', '/*', '#'))
        if alone and previous == first - 1:
            found.append('%s:%d: comment runs over one line' % (path, first))
        previous = last if alone else None
        if len(body.split()) > 10:
            found.append('%s:%d: comment of %d words' % (path, first, len(body.split())))
    return found


def self_test():
    eleven = 'one two three four five six seven eight nine ten eleven'
    cases = [
        ('int a; // fine\n', 0),
        ('// ' + eleven + '\n', 1),
        ('/* ' + eleven + ' */\n', 1),
        ('/* two\n   lines */\n', 1),
        ('// one\n// two\n', 1),
        ('int a; // one\nint b; // two\n', 0),
        ('const char *url = "http://example.com/a b c d e f g h i j k l";\n', 0),
        ("char slash = '/'; char quote = '\"'; // fine\n", 0),
        ('auto raw = R"x(a quote " then // ' + eleven + ')x"; // fine\n', 0),
        ('const char *escaped = "a \\" // b c d e f g h i j k l m";\n', 0),
        ("int value = 1'000; // " + eleven + '\n', 1),
        ("int mask = 0xFF'FF; int next = 2'0; // " + eleven + '\n', 1),
        ("double big = 1e+5; char c = 'x'; // fine\n", 0),
        ("auto wide = u8R\"(a quote \" then // " + eleven + ")\"; // fine\n", 0),
        ('const char *value = "first\\\nsecond";\n// first\n// second\n', 1),
        ('auto raw = R"(one\ntwo)";\n// first\n// second\n', 1),
        ('int a;\n' * 499, 0),
        ('int a;\n' * 500, 1),
        ('int a;\n' * 498 + 'int a;', 0),
        ('int a;\n' * 499 + 'int a;', 1),
    ]
    for text, expected in cases:
        got = violations('fixture.cpp', text)
        assert len(got) == expected, (text[:60], expected, got)
    ten = eleven[:-7]
    scripts = [
        ('fixture.sh', '#!/usr/bin/env bash ' + eleven + '\necho hello\n', 0, ''),
        ('fixture.sh', '# ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', '# ' + ten + '\n', 0, ''),
        ('fixture.sh', '#' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', '#\t' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'echo hello # ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'echo hello\t# ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'echo hello;# ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', '(# ' + eleven + '\necho)\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'sleep 1 &# ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'true ||# ' + eleven + '\ntrue\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'echo >#' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'name() { echo; } # ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'case x in\n a) echo;; # ' + eleven + '\nesac\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'echo "# ' + eleven + '"\n', 0, ''),
        ('fixture.sh', 'echo "first\n# ' + eleven + '\nlast"\n', 0, ''),
        ('fixture.sh', "sh -c '\n# " + eleven + "\n'\n", 0, ''),
        ('fixture.sh', 'echo "a \\" # ' + eleven + '"\n', 0, ''),
        ('fixture.sh', "echo 'a \\' # " + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'echo \\" # ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'echo a#b ' + eleven + '\n', 0, ''),
        ('fixture.sh', 'echo a\\ #' + eleven + '\n', 0, ''),
        ('fixture.sh', 'echo a\\\n#b ' + eleven + '\n', 1, 'lint does not read a continued line that splits a word'),
        ('fixture.sh', 'echo a \\\n# ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'echo a \\\n b\n# one\n# two\n', 1, ':4: comment runs over one line'),
        ('fixture.sh', 'echo $# ${#name} ${name#a b} ${name:-# c} ' + eleven + '\n', 0, ''),
        ('fixture.sh', 'echo \\# ' + eleven + '\n', 0, ''),
        ('fixture.sh', 'echo $# # ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'value="$(echo hi # ' + eleven + '\n)"\n', 1, ':1: comment of 11 words'),
        ('fixture.sh', 'value="$(echo "in $(echo deep # ' + eleven + '\n)")"\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'value="${name:-$(echo # ' + eleven + '\n)}"\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'value=$( (cd /tmp; pwd) ) # ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'value="$(echo \\) # ' + eleven + '\n)"\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'value="$(echo \\))" # ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'cat <<< "text" # ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'cat <<< text\n# ' + eleven + '\n', 1, ':2: comment of 11 words'),
        ('fixture.sh', 'echo\ncat <<EOF\nbody\nEOF\n', 1, ':2: lint does not read `<<`'),
        ('fixture.sh', 'cat <<-END-TEXT\nbody\nEND-TEXT\n', 1, 'lint does not read `<<`'),
        ('fixture.sh', 'value=$((1 << shift))\n', 1, 'lint does not read arithmetic'),
        ('fixture.sh', 'value="$((1 + 2))"\n', 1, 'lint does not read arithmetic'),
        ('fixture.sh', '((count += 1))\n', 1, 'lint does not read `((`'),
        ('fixture.sh', 'echo `date`\n', 1, 'lint does not read backticks'),
        ('fixture.sh', 'echo "`date`"\n', 1, 'lint does not read backticks'),
        ('fixture.sh', "echo $'it\\'s'\n", 1, "lint does not read $'"),
        ('fixture.sh', 'value=$(case x in a) echo;; esac)\n', 1, 'lint does not read `case` inside'),
        ('fixture.sh', 'value=$(echo showcase)\n', 0, ''),
        ('fixture.sh', 'echo "open\n', 1, 'a quote left open'),
        ('fixture.sh', "echo 'open\n", 1, 'a quote left open'),
        ('fixture.sh', 'echo ${open\n', 1, 'a brace left open'),
        ('fixture.sh', 'value=$(echo open\n', 1, '`$(` left open'),
        ('fixture.sh', "echo 'a\nb'\n# one\n# two\n", 1, ':4: comment runs over one line'),
        ('fixture.sh', 'echo "a\nb"\n# one\n# two\n', 1, ':4: comment runs over one line'),
        ('fixture.sh', "echo ${name:-'}'} # " + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'echo ${name:-"}"} # ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'echo "it\'s" # ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'echo \\ #' + eleven + '\n', 0, ''),
        ('fixture.sh', "echo 'a'#b " + eleven + '\n', 0, ''),
        ('fixture.sh', 'echo "a"#b ' + eleven + '\n', 0, ''),
        ('fixture.sh', 'echo $(date)#b ${name}#c ' + eleven + '\n', 0, ''),
        ('fixture.sh', 'cat <<<#' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'value="$( (echo a) # ' + eleven + '\n)"\n', 1, 'comment of 11 words'),
        ('fixture.sh', 'value=$(caseless)\n', 0, ''),
        ('fixture.sh', 'value="$\\\n(printf x # ' + eleven + '\n)"\n', 1, ':1: lint does not read a continued line that splits `$`'),
        ('fixture.sh', 'value=$\\\n(printf x # ' + eleven + '\n)\n', 1, 'lint does not read a continued line'),
        ('fixture.sh', 'value=$(\\\n(1 + 2))\n', 1, 'lint does not read a continued line'),
        ('fixture.sh', 'cat <\\\n<EOF\n# ' + eleven + '\nEOF\n', 1, 'lint does not read a continued line'),
        ('fixture.sh', 'value=$(ca\\\nse x in a) echo;; esac)\n', 1, 'lint does not read a continued line'),
        ('fixture.sh', 'echo one \\\n\ttwo \\\n\\\n  three # ' + eleven + '\n', 1, ':4: comment of 11 words'),
        ('fixture.sh', 'echo "one\\\ntwo" # ' + eleven + '\n', 1, ':2: comment of 11 words'),
        ('fixture.sh', 'echo one\t\\\n two # ' + eleven + '\n', 1, ':2: comment of 11 words'),
        ('fixture.sh', '# one\n# two\n', 1, 'comment runs over one line'),
        ('fixture.sh', '# one\n#\n', 1, 'comment runs over one line'),
        ('fixture.sh', '# one\necho\n# two\n', 0, ''),
        ('fixture.sh', 'echo one # first\necho two # second\n', 0, ''),
        ('CMakeLists.txt', '    # ' + eleven + '\n', 1, 'comment of 11 words'),
        ('CMakeLists.txt', 'set(NAME value)#' + eleven + '\n', 1, 'comment of 11 words'),
        ('CMakeLists.txt', 'set(NAME "# ' + eleven + '")\n', 0, ''),
        ('CMakeLists.txt', 'set(NAME "a \\" # ' + eleven + '")\n', 0, ''),
        ('CMakeLists.txt', 'set(NAME a\\# ' + eleven + ')\n', 0, ''),
        ('CMakeLists.txt', "message(it's) # " + eleven + '\n', 1, 'comment of 11 words'),
        ('CMakeLists.txt', 'set(NAME [[\n# ' + eleven + '\n]])\n', 0, ''),
        ('CMakeLists.txt', 'set(NAME [=[ ]] # ' + eleven + ' ]=])\n', 0, ''),
        ('CMakeLists.txt', 'set(NAME a[[b # ' + eleven + ' ]])\n', 1, 'comment of 12 words'),
        ('CMakeLists.txt', 'set(NAME\t[[b # ' + eleven + ' ]])\n', 0, ''),
        ('CMakeLists.txt', '[[ # ' + eleven + ' ]]', 0, ''),
        ('CMakeLists.txt', 'set(NAME a\\ [[b # ' + eleven + '\n]])\n', 1, 'comment of 11 words'),
        ('CMakeLists.txt', 'set(NAME "a"[[b # ' + eleven + ' ]])\n', 0, ''),
        ('CMakeLists.txt', 'set(NAME a"b"[[c # ' + eleven + '\n]])\n', 1, 'comment of 11 words'),
        ('CMakeLists.txt', 'set(NAME a"b # c"[[d # ' + eleven + '\n]])\n', 1, 'comment of 11 words'),
        ('CMakeLists.txt', 'set(NAME a)[[b # ' + eleven + ' ]]\n', 0, ''),
        ('CMakeLists.txt', 'set([[b # ' + eleven + ' ]])\n', 0, ''),
        ('CMakeLists.txt', 'set(NAME\n[[b # ' + eleven + ' ]])\n', 0, ''),
        ('CMakeLists.txt', 'set(NAME [[a]][[b # ' + eleven + ' ]])\n', 0, ''),
        ('CMakeLists.txt', '# first\nset(NAME\n# ' + ten + '\n[[b # ' + eleven + ' ]])\n', 0, ''),
        ('CMakeLists.txt', 'set(NAME a [==[b # ' + eleven + ' ]==] c[[d # ' + eleven + '\n)\n', 1, 'comment of 11 words'),
        ('CMakeLists.txt', 'set(NAME a#[[ ' + eleven + ' ]]\n)\n', 1, 'comment of 11 words'),
        ('CMakeLists.txt', '#[[ ' + ten + ' ]]\nset(NAME value)\n', 0, ''),
        ('CMakeLists.txt', '#[==[ ' + ten + ' ]==]\n', 0, ''),
        ('CMakeLists.txt', '#[[ two\nlines ]]\n', 1, 'comment runs over one line'),
        ('CMakeLists.txt', '#[=[ ' + eleven + ' ]=]\n', 1, 'comment of 11 words'),
        ('helpers.cmake', '# ' + eleven + '\n', 1, 'comment of 11 words'),
        ('Dockerfile.dev', '# fine\nFROM ubuntu:24.04\n', 0, ''),
        ('Dockerfile.dev', '    # ' + eleven + '\n', 1, 'comment of 11 words'),
        ('Dockerfile.dev', 'RUN echo "a # ' + eleven + '"\n', 0, ''),
        ('Dockerfile.dev', 'RUN echo "\\\n# ' + eleven + '\\\n"\n', 1, 'comment of 11 words'),
        ('Dockerfile', '# ' + ten + '\n', 0, ''),
        ('fixture.py', 'value = "# ' + eleven + '"  # fine\n', 0, ''),
        ('fixture.py', 'value = 1  # ' + eleven + '\n', 1, 'comment of 11 words'),
        ('fixture.py', 'value = 1  # ' + ten + '\n', 0, ''),
        ('fixture.py', '#!/usr/bin/env python3 ' + eleven + '\nvalue = 1\n', 0, ''),
        ('fixture.py', '"""' + eleven + '\n' + eleven + '"""\n', 0, ''),
        ('fixture.py', '# one\n# two\n', 1, 'comment runs over one line'),
        ('fixture.py', "value = '''it's # " + eleven + "'''\n", 0, ''),
        ('notes.txt', 'plain text\n', 1, 'no scanner for this kind of file'),
        ('fixture.sh', 'echo\n' * 500, 1, '500 lines'),
    ]
    for path, text, expected, message in scripts:
        got = violations(path, text)
        assert len(got) == expected and all(message in line for line in got), (path, text[:60], expected, message, got)
    print('lint self-test: %d cases pass' % (len(cases) + len(scripts)))


if __name__ == '__main__':
    if sys.argv[1:] == ['--self-test']:
        self_test()
        sys.exit(0)
    paths = sorted(p for p in glob.glob('src/**/*', recursive=True) + glob.glob('tests/*') if p.endswith(('.cpp', '.h')))
    paths += sorted(p for p in glob.glob('scripts/**/*', recursive=True) if os.path.isfile(p))
    paths += ['CMakeLists.txt', 'Dockerfile.dev']
    failures = [failure for path in paths for failure in violations(path, open(path).read())]
    print('\n'.join(failures) if failures else 'lint: %d files clean' % len(paths))
    sys.exit(1 if failures else 0)

#!/usr/bin/env python3
#
# cppcheck addon enforcing a selected subset of docs/modules-c++20.mdy's
# MUST rules: throw/try/catch, new template declarations, manual new/delete,
# std::function, enum (must be enum class), any preprocessor directive
# other than #include, and assert()/__FILE__/__LINE__/__func__. This is
# not full enforcement of the specification - see docs/modules-c++20.mdy's
# own "Enforcement" section for what is and is not covered, and
# tools/check's own doc comment for how this addon's result should be
# described.
#
# Invoked automatically by tools/check as:
#   cppcheck --addon=tools/check/cppcheck/cpp20_rules.py ...
#
# This addon walks CppcheckData.rawTokens rather than the
# configured token list (cfg.tokenlist): rawTokens preserves the source
# exactly as written (a "template <>" specialization header, for example,
# is simplified away in cfg.tokenlist by cppcheck's own analysis passes),
# and it comes from the lexer, so string and comment contents never appear
# as separate tokens - a real advantage over plain text or regex matching,
# which would have to be taught to skip them by hand.
#
# The one thing rawTokens does not give back is `.link`: '(', '{' and '<'
# are not paired with their matching close token the way they are in
# cfg.tokenlist. Every check below is written as a short forward walk over
# `.next` instead.
#
# Pawel Wodnicki (C) 2026
# 32bitmicro LLC (C) 2026

import sys

import cppcheckdata

ADDON = 'cpp20'

# docs/modules-c++20.mdy, "Known non-conformance": the one place a MUST
# violation is permitted, and only for the specific files listed here.
# Matched against the project-relative path's suffix, not the bare
# basename: a basename-only match would let any future file anywhere in
# the tree named exactly "test.cpp" inherit mm.test's exception, regardless
# of directory.
EXCEPT_EXCEPTIONS = {'modules/mm/test/src/test.cpp'}  # mm.test's expect/failure: throw, try, catch
EXCEPT_TEMPLATES = {
    'modules/mm/test/test.cppm',      # mm.test's registrar constructor template
    'modules/mm/test/src/test.cpp',
}


def matches_known_path(tok, known_paths):
    # Dump file paths are whatever form cppcheck was invoked with (relative
    # to the project root in normal use, but this tolerates an absolute
    # path too), so a suffix match on "/" + the known relative path is what
    # actually pins it to that one file rather than any file sharing its
    # name.
    path = tok.file.replace('\\', '/')
    return any(path == known or path.endswith('/' + known) for known in known_paths)


def report(tok, message, error_id):
    cppcheckdata.reportError(tok, 'error', message, ADDON, error_id)


# Raw tokens carry none of cfg.tokenlist's semantic attributes: a real
# cppcheck dump's <rawtokens> elements have only fileIndex/linenr/column/str
# (verified directly), never a 'type' attribute, so Token.isName - which
# cppcheckdata derives from that attribute - is always False here. A prior
# version of skip_type_name relied on tok.isName and so never advanced past
# an ordinary type name at all, which silently made check_new stop matching
# any new $TYPE(...) or new $TYPE[...] with a real, single-token type name
# (verified live: new int(); new Foo(); new int[5]; produced no report).
#
# A type name (possibly qualified, possibly a template instantiation, such
# as std::vector<int>) cannot itself contain any of these terminators, so
# walking until one is reached is sufficient without needing to classify
# each token along the way.
TYPE_NAME_TERMINATORS = {'(', '[', ';', ',', ')', ']', '{', '}'}


def skip_type_name(tok):
    while tok is not None and tok.str not in TYPE_NAME_TERMINATORS:
        tok = tok.next
    return tok


def looks_like_identifier(tok):
    # Same root cause as skip_type_name above: tok.isName cannot be used on
    # a raw token, so this treats anything starting with a letter or
    # underscore as a name instead, which is what follows delete/delete[]
    # in valid C++.
    return tok is not None and len(tok.str) > 0 and (tok.str[0].isalpha() or tok.str[0] == '_')


def check_throw(tok):
    if tok.str != 'throw':
        return
    if matches_known_path(tok, EXCEPT_EXCEPTIONS):
        return
    report(
        tok,
        'throw is disallowed by docs/modules-c++20.mdy, "Disallowed: exceptions". '
        'Report failure through a status enum, bool, or std::error_code out-parameter '
        'instead. mm.test is the one documented exception; see "Known non-conformance" '
        'in the same document.',
        'noThrow',
    )


def check_try(tok):
    if not cppcheckdata.simpleMatch(tok, 'try {'):
        return
    if matches_known_path(tok, EXCEPT_EXCEPTIONS):
        return
    report(
        tok,
        'try is disallowed by docs/modules-c++20.mdy, "Disallowed: exceptions". '
        'mm.test is the one documented exception; see "Known non-conformance" in the '
        'same document.',
        'noTry',
    )


def check_catch(tok):
    if not cppcheckdata.simpleMatch(tok, 'catch ('):
        return
    if matches_known_path(tok, EXCEPT_EXCEPTIONS):
        return
    report(
        tok,
        'catch is disallowed by docs/modules-c++20.mdy, "Disallowed: exceptions". '
        'mm.test is the one documented exception; see "Known non-conformance" in the '
        'same document.',
        'noCatch',
    )


def check_template(tok):
    if tok.str != 'template':
        return
    if matches_known_path(tok, EXCEPT_TEMPLATES):
        return
    report(
        tok,
        'New template declarations (function, class, struct, or specialization) are '
        'disallowed by docs/modules-c++20.mdy, "Disallowed: templates and '
        'metaprogramming". Using a standard-library template (std::vector, '
        'std::optional, ...) is fine; authoring a new one is not. mm.test\'s registrar '
        'constructor template is the one documented exception; see "Known '
        'non-conformance" in the same document.',
        'noNewTemplate',
    )


def check_new(tok):
    # new $TYPE(...), new $TYPE[...], or new $TYPE; - not new $TYPE() or a
    # bare `new $TYPE` with no terminator, which this addon cannot tell apart
    # from other uses of those tokens without deeper expression parsing.
    if tok.str != 'new':
        return
    after_type = skip_type_name(tok.next)
    if after_type is None:
        return
    if after_type.str not in ('(', '[', ';'):
        return
    if after_type.str == '(' and cppcheckdata.simpleMatch(after_type, '( )'):
        return
    report(
        tok,
        'Manual new is disallowed by docs/modules-c++20.mdy, "Permitted language '
        'features" (RAII is required for owned resources).',
        'noManualNew',
    )


def check_delete(tok):
    # Anchored to a statement boundary so this does not flag `= delete;` on a
    # deleted special member function.
    #
    # delete[] arr; is not matched: the token after delete is '[', not an
    # identifier, so looks_like_identifier(tok.next) is false and this
    # returns before reporting. Manual array delete is exactly as disallowed
    # as the scalar form; this is a real gap in this check, not a
    # deliberate exemption the way empty-paren new is in check_new.
    if tok.str != 'delete':
        return
    if tok.previous is not None and tok.previous.str not in (';', '{', '}', None):
        return
    if not looks_like_identifier(tok.next):
        return
    report(
        tok,
        'Manual delete is disallowed by docs/modules-c++20.mdy, "Permitted language '
        'features" (RAII is required for owned resources).',
        'noManualDelete',
    )


def check_std_function(tok):
    if not cppcheckdata.simpleMatch(tok, 'std :: function <'):
        return
    report(
        tok,
        'std::function is disallowed by docs/modules-c++20.mdy, "Standard library '
        'usage". Use a plain function-pointer type alias, following mm.test\'s '
        '`using fn = void (*)();` pattern.',
        'noStdFunction',
    )


def check_enum(tok):
    if tok.str != 'enum':
        return
    if tok.next is not None and tok.next.str == 'class':
        return
    report(
        tok,
        'New enums must be `enum class`, per docs/modules-c++20.mdy, "Permitted '
        'language features".',
        'enumMustBeScoped',
    )


def check_preprocessor(tok):
    # rawTokens gives back '#' and the directive name as two separate
    # tokens (verified against a real cppcheck dump; raw tokens carry no
    # 'type' attribute at all, unlike cfg.tokenlist, so this checks .str
    # directly rather than relying on .isName, which is always False here).
    if tok.str != '#':
        return
    directive = tok.next
    if directive is not None and directive.str == 'include':
        return
    name = directive.str if directive is not None else '?'
    report(
        tok,
        '#%s is disallowed by docs/modules-c++20.mdy, "Disallowed: macros and '
        'the preprocessor". #include is the only preprocessor directive project '
        'code may use.' % name,
        'noPreprocessorDirective',
    )


# docs/modules-c++20.mdy, "Disallowed: macros and the preprocessor":
# std::source_location::current() replaces __FILE__/__LINE__/__func__, and
# failure MUST be reported through a function's return type rather than
# asserted away.
DISALLOWED_STANDARD_MACROS = {'assert', '__FILE__', '__LINE__', '__func__'}


def check_standard_macro(tok):
    if tok.str not in DISALLOWED_STANDARD_MACROS:
        return
    report(
        tok,
        '%s is disallowed by docs/modules-c++20.mdy, "Disallowed: macros and the '
        'preprocessor". std::source_location::current() is the normative '
        'replacement for call-site information, and failure MUST be reported '
        'through a function\'s return type, not asserted away.' % tok.str,
        'noStandardMacro',
    )


CHECKS = (
    check_throw,
    check_try,
    check_catch,
    check_template,
    check_new,
    check_delete,
    check_std_function,
    check_enum,
    check_preprocessor,
    check_standard_macro,
)


def main():
    for arg in sys.argv[1:]:
        if not arg.endswith('.dump'):
            continue
        data = cppcheckdata.CppcheckData(arg)
        for tok in data.rawTokens:
            for check in CHECKS:
                check(tok)


if __name__ == '__main__':
    main()

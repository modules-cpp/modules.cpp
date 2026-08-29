#!/usr/bin/env python3
#
# cppcheck addon enforcing docs/modules-c++20.mdy's MUST rules.
#
# Invoked automatically by tools/check as:
#   cppcheck --addon=tools/check/cppcheck/cpp20_rules.py ...
#
# Each check below mirrors a rule in semgrep/cpp20-spec.yml and, through it,
# a section of docs/modules-c++20.mdy; keep the two in step when either
# changes. This addon walks CppcheckData.rawTokens rather than the
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
# violation is permitted, and only for the files listed here. Matched
# against the basename cppcheck reports, since dump file paths are
# otherwise whatever form they were invoked with (relative, absolute, ...).
EXCEPT_EXCEPTIONS = {'test.cpp'}          # mm.test's expect/failure: throw, try, catch
EXCEPT_TEMPLATES = {'test.cppm', 'test.cpp'}  # mm.test's registrar constructor template


def basename(tok):
    return tok.file.replace('\\', '/').rsplit('/', 1)[-1]


def report(tok, message, error_id):
    cppcheckdata.reportError(tok, 'error', message, ADDON, error_id)


def is_type_name(tok):
    # A (possibly qualified) type name: NAME ("::" NAME)*
    return tok is not None and (tok.isName or tok.str == '::')


def skip_type_name(tok):
    while is_type_name(tok):
        tok = tok.next
    return tok


def check_throw(tok):
    if tok.str != 'throw':
        return
    if basename(tok) in EXCEPT_EXCEPTIONS:
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
    if basename(tok) in EXCEPT_EXCEPTIONS:
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
    if basename(tok) in EXCEPT_EXCEPTIONS:
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
    if basename(tok) in EXCEPT_TEMPLATES:
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
    if tok.str != 'delete':
        return
    if tok.previous is not None and tok.previous.str not in (';', '{', '}', None):
        return
    if tok.next is None or not tok.next.isName:
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


CHECKS = (
    check_throw,
    check_try,
    check_catch,
    check_template,
    check_new,
    check_delete,
    check_std_function,
    check_enum,
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

import lib.cdemo;
import mm.test;

namespace {

void calls_the_foreign_c_source() {
    mm::test::expect(lib::cdemo::scale(2) == 7,
                     "expected the compiled foreign C file, with its c-option and forced include");
}

void calls_the_strict_glue() {
    mm::test::expect(lib::cdemo::twice(2) == 14,
                     "expected the compiled glue over the foreign file");
}

const mm::test::case_ cases[] = {
    {"calls the foreign C source", &calls_the_foreign_c_source},
    {"calls the strict glue", &calls_the_strict_glue},
};

const mm::test::registrar reg{"lib.cdemo", cases};

}

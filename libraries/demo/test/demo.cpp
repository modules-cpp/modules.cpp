import ext.demo;
import mm.test;

namespace {

void returns_foreign_header_value() {
    mm::test::expect(ext::demo::answer() == 42,
                     "expected the wrapper to call the header-only library");
}

const mm::test::case_ cases[] = {
    {"returns the foreign header value", &returns_foreign_header_value},
};

const mm::test::registrar reg{"ext.demo", cases};

}

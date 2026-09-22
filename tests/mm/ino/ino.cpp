// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

import mm.mdy;
import mm.ino;
import mm.test;

namespace {

using mm::test::expect;

void function_used_before_definition() {
    const std::vector<mm::ino::SourceFile> sources = {
        {"test.ino", "void loop() {\n    helper();\n}\n\nvoid helper() {\n}\n"}
    };
    const auto result = mm::ino::transform(sources);
    expect(result.ok, "transformation should succeed");
    expect(result.output.find("void helper();") != std::string::npos,
           "expected prototype for helper() to be generated");
}

void prototype_already_written() {
    const std::vector<mm::ino::SourceFile> sources = {
        {"test.ino", "void helper();\n\nvoid loop() {\n    helper();\n}\n\nvoid helper() {\n}\n"}
    };
    const auto result = mm::ino::transform(sources);
    expect(result.ok, "transformation should succeed");
    // Count occurrences of "void helper();" - should be exactly 1 (the one in the sketch body, not duplicated in prelude)
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = result.output.find("void helper();", pos)) != std::string::npos) {
        ++count;
        pos += 14;
    }
    expect(count == 1, "expected no duplicate prototype generated when sketch declares it");
}

void standard_include_in_middle() {
    const std::vector<mm::ino::SourceFile> sources = {
        {"test.ino", "void setup() {}\n#include <vector>\nvoid loop() {}\n"}
    };
    const auto result = mm::ino::transform(sources);
    expect(result.ok, "transformation should succeed");
    expect(result.output.find("#include <vector>\n#include \"Arduino.h\"\nimport mm.sketch;") != std::string::npos,
           "expected #include <vector> hoisted above the compatibility header and import mm.sketch;");
    // Check that #include <vector> is not duplicated in the sketch body
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = result.output.find("#include <vector>", pos)) != std::string::npos) {
        ++count;
        pos += 17;
    }
    expect(count == 1, "expected hoisted include not duplicated in sketch body");

    // Verify displacement accurately maps line after include
    const auto disp_pos = result.output.find("(displaced by ");
    expect(disp_pos != std::string::npos, "expected displacement comment");
    const auto disp_num_start = disp_pos + 14;
    const auto disp_num_end = result.output.find(" lines)", disp_num_start);
    const int disp = std::stoi(result.output.substr(disp_num_start, disp_num_end - disp_num_start));

    const auto loop_pos = result.output.find("void loop() {}");
    int loop_line = 1;
    for (std::size_t i = 0; i < loop_pos; ++i) {
        if (result.output[i] == '\n') ++loop_line;
    }
    expect(loop_line - disp == 3, "line after hoisted include must retain exact displacement");
}

void sketch_with_main_is_rejected() {
    const std::vector<mm::ino::SourceFile> sources = {
        {"test.ino", "int main() {\n    return 0;\n}\n"}
    };
    const auto result = mm::ino::transform(sources);
    expect(!result.ok, "sketch defining main must fail");
    expect(!result.diagnostics.empty(), "expected error diagnostic");
    expect(result.diagnostics.front().line == 1, "expected error at line 1");
    expect(result.diagnostics.front().message.find("main") != std::string::npos,
           "diagnostic should mention main");
}

void quoted_include_is_hoisted() {
    const std::vector<mm::ino::SourceFile> sources = {
        {"test.ino",
         "#include \"custom.h\"\n#include <vector>\n"
         "#include \"custom.h\"\nvoid setup() {}\nvoid loop() {}\n"}
    };
    const auto result = mm::ino::transform(sources);
    expect(result.ok, "quoted include must be accepted");
    expect(result.diagnostics.empty(), "expected no diagnostics");
    expect(result.output.find("#include \"custom.h\"\n#include <vector>\n"
                              "#include \"Arduino.h\"\nimport mm.sketch;") !=
               std::string::npos,
           "quoted and angled includes hoist in source order above the"
           " compatibility header and the import");

    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = result.output.find("#include \"custom.h\"", pos)) !=
           std::string::npos) {
        ++count;
        pos += 19;
    }
    expect(count == 1, "a repeated quoted include is hoisted once");

    const auto disp_pos = result.output.find("(displaced by ");
    expect(disp_pos != std::string::npos, "expected displacement comment");
    const auto disp_num_start = disp_pos + 14;
    const auto disp_num_end = result.output.find(" lines)", disp_num_start);
    const int disp = std::stoi(
        result.output.substr(disp_num_start, disp_num_end - disp_num_start));

    const auto loop_pos = result.output.find("void loop() {}");
    int loop_line = 1;
    for (std::size_t i = 0; i < loop_pos; ++i) {
        if (result.output[i] == '\n') ++loop_line;
    }
    expect(loop_line - disp == 5,
           "lines after hoisted quoted includes keep their displacement");
}

void two_files_transformation() {
    const std::vector<mm::ino::SourceFile> sources = {
        {"main.ino", "void setup() {}\nvoid loop() { helper(); }\n"},
        {"helper.ino", "void helper() {}\n"}
    };
    const auto result = mm::ino::transform(sources);
    expect(result.ok, "two-file transformation should succeed");
    expect(result.output.find("main.ino (displaced by") != std::string::npos,
           "expected displacement comment for main.ino");
    expect(result.output.find("helper.ino (displaced by") != std::string::npos,
           "expected displacement comment for helper.ino");
    expect(result.output.find("void helper();") != std::string::npos,
           "expected prototype for helper across files");
}

void definition_heuristic_cannot_see() {
    const std::vector<mm::ino::SourceFile> sources = {
        {"test.ino", "template <typename T>\nT identity(T val) {\n    return val;\n}\n"}
    };
    const auto result = mm::ino::transform(sources);
    expect(result.ok, "template should not error in transformation");
    expect(result.output.find("identity(") == result.output.rfind("identity("),
           "template should not have prototype generated");
}

void indented_helper_produces_warning() {
    const std::vector<mm::ino::SourceFile> sources = {
        {"test.ino", "void setup() {}\n  void indented() {\n  }\nvoid loop() {}\n"}
    };
    const auto result = mm::ino::transform(sources);
    expect(result.ok, "indented helper is warning, transformation still succeeds");
    bool found_warning = false;
    for (const auto& d : result.diagnostics) {
        if (d.is_warning && d.line == 2 &&
            d.message == "definition not at column zero; no prototype generated; indent it to column zero or declare it") {
            found_warning = true;
            break;
        }
    }
    expect(found_warning, "expected warning for indented helper");
}

void later_declaration_keeps_generated_prototype() {
    const std::vector<mm::ino::SourceFile> sources = {
        {"test.ino",
         "void loop() { helper(); }\n"
         "void helper() {}\n"
         "void helper();\n"
         "void setup() {}\n"}
    };
    const auto result = mm::ino::transform(sources);
    expect(result.ok, "transform should succeed");
    const auto proto_pos = result.output.find("void helper();");
    const auto loop_pos = result.output.find("void loop() { helper(); }");
    expect(proto_pos != std::string::npos, "helper prototype should be generated");
    expect(proto_pos < loop_pos, "generated prototype must precede call in loop()");
}

void write_guarded_lifecycle() {
    const mm::test::scoped_tree tree{"ino_guard"};
    const auto dir = tree.root();
    std::string error;

    // 1. Target does not exist: creates atomic file
    expect(mm::ino::write_guarded(dir, "main.cpp", "int a = 1;\n", error),
           "write_guarded should succeed creating new file");
    expect(error.empty(), "error should be empty on success");
    {
        std::ifstream in(dir / "main.cpp");
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        expect(content == "int a = 1;\n", "content must match written text");
    }

    // 2. Target exists: overwrites atomically
    expect(mm::ino::write_guarded(dir, "main.cpp", "int a = 2;\n", error),
           "write_guarded should succeed overwriting existing file");
    {
        std::ifstream in(dir / "main.cpp");
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        expect(content == "int a = 2;\n", "content must match updated text");
    }

    // 3. Nonexistent directory: fails gracefully
    expect(!mm::ino::write_guarded(dir / "nonexistent", "main.cpp", "data",
                                   error),
           "write_guarded must fail when directory does not exist");
    expect(!error.empty(), "error message must be provided on failure");

    // 4. Temporary collision: fails when temporary exists
    {
        std::ofstream tmp(dir / "custom.tmp");
        tmp << "collision";
    }
    expect(!mm::ino::write_guarded(dir, "main.cpp", "data", error,
                                   "custom.tmp"),
           "write_guarded must fail when temporary already exists");
    expect(error.find("already exists") != std::string::npos,
           "error message must mention temporary file exists");
}

// A complete application carries the generated headers as well as the
// generated main.cpp, which is what check_application asks for.
void write_generated_headers(const std::filesystem::path& dir) {
    std::ofstream(dir / "Arduino.h") << mm::ino::sketch_header();
    for (const auto& alias : mm::ino::sketch_alias_headers())
        std::ofstream(dir / std::string(alias))
            << mm::ino::sketch_alias_header(alias);
}

void check_application_rules() {
    const mm::test::scoped_tree tree{"ino_check"};
    const auto dir = tree.root();
    std::string error;

    // Manifest missing sketch:
    mm::mdy::MDYDocument doc_no_sketch;
    doc_no_sketch.metadata["kind"] = {"app"};
    expect(!mm::ino::check_application(dir, doc_no_sketch, error),
           "check_application must fail when sketch: is missing");

    // Create sketch file and valid generated main.cpp
    const std::string sketch_src = "void setup() {}\nvoid loop() {}\n";
    {
        std::ofstream f(dir / "app.ino");
        f << sketch_src;
    }
    const std::vector<mm::ino::SourceFile> sources = {{"app.ino", sketch_src}};
    const auto tr = mm::ino::transform(sources);
    expect(tr.ok, "transform should succeed");
    {
        std::ofstream f(dir / "main.cpp");
        f << tr.output;
    }
    // main.cpp includes it, so a complete application has one.
    write_generated_headers(dir);

    mm::mdy::MDYDocument doc_valid;
    doc_valid.metadata["kind"] = {"app"};
    doc_valid.metadata["sketch"] = {"app.ino"};

    expect(mm::ino::check_application(dir, doc_valid, error),
           "check_application must succeed with matching main.cpp");

    // Unmanifested .ino
    {
        std::ofstream extra(dir / "extra.ino");
        extra << "void extra() {}\n";
    }
    expect(!mm::ino::check_application(dir, doc_valid, error),
           "check_application must fail with unmanifested .ino");
    expect(error.find("unmanifested .ino") != std::string::npos,
           "error must report unmanifested .ino");
    std::error_code ec;
    std::filesystem::remove(dir / "extra.ino", ec);

    // Mismatched main.cpp
    {
        std::ofstream f(dir / "main.cpp");
        f << "int main() { return 1; }\n";
    }
    expect(!mm::ino::check_application(dir, doc_valid, error),
           "check_application must fail when main.cpp differs");
    expect(error.find("does not match") != std::string::npos,
           "error must report mismatch");
}

void sketch_header_synthesis() {
    const auto header = mm::ino::sketch_header();
    expect(header.find("import mm.sketch;") != std::string::npos,
           "the header is built on mm.sketch");
    expect(header.find("using namespace mm::sketch;") !=
               std::string::npos,
           "the header introduces the sketch vocabulary"
           " unqualified");
    expect(header.find("using std::uint8_t;") !=
               std::string::npos,
           "uint8_t is brought into the global namespace");
    expect(header.find("using std::int32_t;") !=
               std::string::npos,
           "int32_t is brought into the global namespace");
    expect(header.find("using std::size_t;") !=
               std::string::npos,
           "size_t is brought into the global namespace");
    expect(header.find("using std::ptrdiff_t;") !=
               std::string::npos,
           "ptrdiff_t is brought into the global namespace");
    expect(header.find(
               "#define F(string_literal) (string_literal)") !=
                std::string::npos,
           "F is accepted and inert");
    expect(header.find("#define PROGMEM\n") !=
               std::string::npos,
           "PROGMEM is accepted and inert");
    expect(header.find("A0 = 0;") != std::string::npos &&
               header.find("A7 = 7;") != std::string::npos,
           "the analog channel names are present");
    expect(header.find("inline constexpr double PI = 3.14159") !=
               std::string::npos &&
               header.find("TWO_PI = 6.28318") != std::string::npos &&
               header.find("RAD_TO_DEG = 57.2957") != std::string::npos,
           "the mathematical constants a sketch names are present");
    expect(header.find("inline void yield() { dispatch(); }") !=
               std::string::npos,
           "yield is the foreign spelling of dispatch");
    expect(header.find("B0 = 0, B1 = 1;") != std::string::npos &&
               header.find("B11111111 = 255;") != std::string::npos,
           "the binary constants span one to eight digits");
    expect(header.find("B01 = 1") != std::string::npos &&
               header.find("B00000001 = 1") != std::string::npos,
           "a leading zero is part of the name, not of the value");
    for (const auto& alias : mm::ino::sketch_alias_headers()) {
        const auto forwarder = mm::ino::sketch_alias_header(alias);
        expect(forwarder.find("#include \"Arduino.h\"") != std::string::npos,
               "a forwarding header forwards to Arduino.h");
        expect(forwarder.find(std::string(alias)) != std::string::npos,
               "a forwarding header names the spelling it answers to");
    }
    expect(header.find("do not edit by hand") !=
               std::string::npos,
           "the header says it is generated");
    expect(mm::ino::sketch_header() == header,
           "synthesis is deterministic so --check can"
           " compare it");
}

void sketch_header_is_checked() {
    const mm::test::scoped_tree tree{"ino_header_check"};
    const auto dir = tree.root();
    std::string error;

    const std::string sketch_src = "void setup() {}\nvoid loop() {}\n";
    std::ofstream(dir / "app.ino") << sketch_src;
    const std::vector<mm::ino::SourceFile> sources = {{"app.ino", sketch_src}};
    const auto tr = mm::ino::transform(sources);
    expect(tr.ok, "transform should succeed");
    std::ofstream(dir / "main.cpp") << tr.output;

    mm::mdy::MDYDocument doc;
    doc.metadata["kind"] = {"app"};
    doc.metadata["sketch"] = {"app.ino"};
    expect(!mm::ino::check_application(dir, doc, error),
           "an application without sketch-library still requires the header");
    expect(error.find("Arduino.h") != std::string::npos,
           "the error names the missing header");

    doc.metadata["sketch-library"] = {".."};
    expect(!mm::ino::check_application(dir, doc, error),
           "a declared sketch-library requires the generated header");

    std::ofstream(dir / "Arduino.h") << "#pragma once\n";
    expect(!mm::ino::check_application(dir, doc, error),
           "a hand-edited header is refused");

    write_generated_headers(dir);
    expect(mm::ino::check_application(dir, doc, error),
           "the generated headers pass");

    doc.metadata.erase("sketch-library");
    expect(mm::ino::check_application(dir, doc, error),
           "the generated headers pass without a library too");

    // Each name a library may include by is checked on the same terms.
    std::filesystem::remove(dir / "Wire.h");
    expect(!mm::ino::check_application(dir, doc, error),
           "a missing forwarding header is refused");
    expect(error.find("Wire.h") != std::string::npos,
           "the error names the missing forwarding header");

    std::ofstream(dir / "Wire.h") << "#pragma once\n";
    expect(!mm::ino::check_application(dir, doc, error),
           "a hand-edited forwarding header is refused");

    write_generated_headers(dir);
    expect(mm::ino::check_application(dir, doc, error),
           "the regenerated forwarding header passes");
}

void library_discovery_flat() {
    const mm::test::scoped_tree tree{"ino_lib_flat"};
    const auto root = tree.root();
    std::ofstream(root / "library.properties")
        << "name=AnalogPin\nversion=1.0\n";
    const auto ex = root / "examples";
    std::filesystem::create_directories(ex / "AnalogPin");
    std::filesystem::create_directories(ex / "AnalogPin_fast");
    std::ofstream(ex / "AnalogPin/AnalogPin.ino")
        << "void setup() {}\nvoid loop() {}\n";
    std::ofstream(ex / "AnalogPin_fast/AnalogPin_fast.ino")
        << "void setup() {}\nvoid loop() {}\n";

    expect(mm::ino::is_library_root(root),
           "directory with library.properties and examples is a library root");

    const auto plan = mm::ino::discover_library(root);
    expect(plan.ok, "discover_library must succeed for flat library");
    expect(plan.root_node.name == root.filename().string(),
           "root node name must match directory name");
    expect(plan.root_node.folders.size() == 1 &&
               plan.root_node.folders.front() == "examples",
           "root node must name only examples");
    expect(plan.dir_nodes.size() == 1,
           "flat library must have only examples dir node");
    expect(plan.dir_nodes.front().name == "examples",
           "dir node name must be examples");
    expect(plan.dir_nodes.front().folders.size() == 2,
           "examples dir node must have 2 child folders");
    expect(plan.app_nodes.size() == 2,
           "flat library must discover 2 app nodes");
    expect(plan.app_nodes[0].name == "AnalogPin",
           "first app name must be AnalogPin");
    expect(plan.app_nodes[0].sketch_library_rel == "../..",
           "flat app sketch-library must climb two segments");
}

void library_discovery_category_nested() {
    const mm::test::scoped_tree tree{"ino_lib_cat"};
    const auto root = tree.root();
    std::ofstream(root / "library.json") << "{\"name\":\"RadioLib\"}\n";
    const auto ex = root / "examples";
    std::filesystem::create_directories(ex / "CC1101/CC1101_Transmit");
    std::filesystem::create_directories(ex / "CC1101/CC1101_Receive");
    std::ofstream(ex / "CC1101/CC1101_Transmit/CC1101_Transmit.ino")
        << "void setup() {}\nvoid loop() {}\n";
    std::ofstream(ex / "CC1101/CC1101_Receive/CC1101_Receive.ino")
        << "void setup() {}\nvoid loop() {}\n";

    expect(mm::ino::is_library_root(root),
           "directory with library.json and examples is a library root");

    const auto plan = mm::ino::discover_library(root);
    expect(plan.ok, "discover_library must succeed for category library");
    expect(plan.app_nodes.size() == 2,
           "category library must discover 2 app nodes");
    expect(plan.app_nodes[0].name == "CC1101-CC1101_Receive",
           "app name must be hyphenated path relative to examples");
    expect(plan.app_nodes[0].sketch_library_rel == "../../..",
           "category app sketch-library must climb three segments");
    expect(plan.dir_nodes.size() == 2,
           "category library must have CC1101 and examples dir nodes");
}

void library_discovery_symlink_and_empty_skipped() {
    const mm::test::scoped_tree tree{"ino_lib_skip"};
    const auto root = tree.root();
    std::ofstream(root / "library.properties") << "name=Lib\n";
    const auto ex = root / "examples";
    std::filesystem::create_directories(ex / "RealApp");
    std::filesystem::create_directories(ex / "EmptyDir");
    std::ofstream(ex / "RealApp/RealApp.ino")
        << "void setup() {}\nvoid loop() {}\n";

    std::error_code ec;
    const auto outside = tree.root().parent_path() / "external_target";
    std::filesystem::create_directories(outside, ec);
    std::filesystem::create_directory_symlink(outside, ex / "SymlinkDir", ec);

    const auto plan = mm::ino::discover_library(root);
    expect(plan.ok, "discover_library must succeed");
    expect(plan.app_nodes.size() == 1,
           "only RealApp must be discovered as an application");
    bool found_empty = false;
    bool found_symlink = false;
    for (const auto& s : plan.skipped) {
        if (s.find("EmptyDir") != std::string::npos) found_empty = true;
        if (s.find("SymlinkDir") != std::string::npos) found_symlink = true;
    }
    expect(found_empty, "directory holding no sketch must be skipped");
    expect(found_symlink, "symlinked directory must be skipped");
}

void library_manifest_compatibility_validation() {
    const mm::test::scoped_tree tree{"ino_lib_compat"};
    const auto root = tree.root();

    mm::ino::LibraryDirNode dir_node;
    dir_node.dir = root / "examples";
    dir_node.name = "examples";
    dir_node.folders = {"app1", "app2"};
    dir_node.is_root = false;

    std::string err;
    mm::mdy::MDYDocument valid_doc;
    valid_doc.metadata["kind"] = {"dir"};
    valid_doc.metadata["name"] = {"examples"};
    valid_doc.metadata["folder"] = {"app1", "app2"};
    expect(mm::ino::validate_manifest_compatibility(
               valid_doc, &dir_node, nullptr, false, root,
               dir_node.dir / "mm.mdy", err),
           "compatible manifest must pass validation");

    mm::mdy::MDYDocument incomplete_doc;
    incomplete_doc.metadata["kind"] = {"dir"};
    incomplete_doc.metadata["name"] = {"examples"};
    incomplete_doc.metadata["folder"] = {"app1"};
    expect(!mm::ino::validate_manifest_compatibility(
               incomplete_doc, &dir_node, nullptr, false, root,
               dir_node.dir / "mm.mdy", err),
           "manifest missing child folder must fail validation");
    expect(err.find("does not name app2; add folder: app2") !=
               std::string::npos,
           "error must name missing folder: entry");

    mm::ino::LibraryAppNode app_node;
    app_node.dir = root / "examples/app1";
    std::filesystem::create_directories(app_node.dir);
    app_node.rel_path = "app1";
    app_node.name = "app1";
    app_node.sketches = {"app1.ino"};
    app_node.sketch_library_rel = "../..";

    mm::mdy::MDYDocument app_doc;
    app_doc.metadata["kind"] = {"app"};
    app_doc.metadata["name"] = {"app1"};
    app_doc.metadata["use"] = {"mm.sketch"};
    app_doc.metadata["file"] = {"main.cpp"};
    app_doc.metadata["sketch"] = {"app1.ino"};
    app_doc.metadata["sketch-library"] = {"../.."};
    expect(mm::ino::validate_manifest_compatibility(
               app_doc, nullptr, &app_node, false, root,
               app_node.dir / "mm.mdy", err),
           "compatible app manifest must pass");

    mm::mdy::MDYDocument bad_app_doc = app_doc;
    bad_app_doc.metadata["sketch"] = {"wrong.ino"};
    expect(!mm::ino::validate_manifest_compatibility(
               bad_app_doc, nullptr, &app_node, false, root,
               app_node.dir / "mm.mdy", err),
           "mismatched sketch must fail validation");
}

void library_empty_or_all_skipped() {
    const mm::test::scoped_tree tree{"ino_empty_lib"};
    const auto root = tree.root();
    std::ofstream(root / "library.properties") << "name=EmptyLib\n";
    std::filesystem::create_directories(root / "examples");

    // Case A: empty examples/ directory
    auto plan = mm::ino::discover_library(root);
    expect(!plan.ok, "empty examples directory must fail discover_library");
    expect(plan.error.find("no valid sketch applications found") !=
               std::string::npos,
           "error must state no valid sketch applications found");
    expect(plan.root_node.folders.empty(),
           "root node folders must be empty on failure");

    // Case B: examples/ contains only empty subdirectories
    std::filesystem::create_directories(root / "examples/sub1");
    std::filesystem::create_directories(root / "examples/sub2");
    plan = mm::ino::discover_library(root);
    expect(!plan.ok, "all-empty examples tree must fail discover_library");
    expect(plan.app_nodes.empty(), "app_nodes must be empty");
}

void library_sketch_directly_under_examples_refused() {
    const mm::test::scoped_tree tree{"ino_root_sketch"};
    const auto root = tree.root();
    std::ofstream(root / "library.properties") << "name=DirectLib\n";
    const auto ex = root / "examples";
    std::filesystem::create_directories(ex);
    std::ofstream(ex / "direct.ino") << "void setup() {}\nvoid loop() {}\n";

    const auto plan = mm::ino::discover_library(root);
    expect(!plan.ok,
           "sketch directly under examples/ must fail discover_library");
    expect(plan.error.find(
               "sketch directly under examples/ is not permitted") !=
               std::string::npos,
           "error must report sketch directly under examples/");
}

void library_ambiguous_primary_sketch_skipped() {
    const mm::test::scoped_tree tree{"ino_ambig_lib"};
    const auto root = tree.root();
    std::ofstream(root / "library.properties") << "name=AmbigLib\n";
    const auto ex = root / "examples";
    // Valid app
    std::filesystem::create_directories(ex / "GoodApp");
    std::ofstream(ex / "GoodApp/GoodApp.ino")
        << "void setup() {}\nvoid loop() {}\n";
    // Ambiguous app: multiple .ino files, neither named AmbigApp.ino
    std::filesystem::create_directories(ex / "AmbigApp");
    std::ofstream(ex / "AmbigApp/first.ino") << "void setup() {}\n";
    std::ofstream(ex / "AmbigApp/second.ino") << "void loop() {}\n";

    const auto plan = mm::ino::discover_library(root);
    expect(plan.ok,
           "discover_library succeeds when at least one valid app exists");
    expect(plan.app_nodes.size() == 1,
           "only GoodApp should be discovered as an application");
    expect(plan.app_nodes[0].name == "GoodApp",
           "GoodApp is the only discovered application");
    bool ambig_skipped = false;
    for (const auto& s : plan.skipped) {
        if (s.find("AmbigApp") != std::string::npos &&
            s.find("ambiguous") != std::string::npos) {
            ambig_skipped = true;
        }
    }
    expect(ambig_skipped, "AmbigApp must be recorded in skipped as ambiguous");
}

void library_application_name_collision_refused() {
    const mm::test::scoped_tree tree{"ino_lib_name_collision"};
    const auto root = tree.root();
    std::ofstream(root / "library.properties") << "name=CollisionLib\n";
    const auto ex = root / "examples";
    std::filesystem::create_directories(ex / "A-B");
    std::filesystem::create_directories(ex / "A/B");
    std::ofstream(ex / "A-B/A-B.ino")
        << "void setup() {}\nvoid loop() {}\n";
    std::ofstream(ex / "A/B/B.ino")
        << "void setup() {}\nvoid loop() {}\n";

    const auto plan = mm::ino::discover_library(root);
    expect(!plan.ok, "colliding derived application names must be refused");
    expect(plan.error.find("derived application name collision: A-B") !=
               std::string::npos,
           "collision error must name the derived application name");
}

void library_scalar_validation() {
    // 1. Invalid names in render_dir_manifest
    mm::ino::LibraryDirNode dir_node;
    dir_node.dir = "examples";
    dir_node.name = "bad\nname";
    dir_node.folders = {"child"};
    expect(mm::ino::render_dir_manifest(dir_node).empty(),
           "render_dir_manifest must return empty string for newline in name");

    dir_node.name = "valid";
    dir_node.folders = {"bad\nchild"};
    expect(mm::ino::render_dir_manifest(dir_node).empty(),
           "render_dir_manifest must return empty string for newline in"
           " folder");

    dir_node.folders = {"child:bad"};
    expect(mm::ino::render_dir_manifest(dir_node).empty(),
           "render_dir_manifest must return empty string for colon in folder");

    // 2. Invalid names in render_app_manifest
    mm::ino::LibraryAppNode app_node;
    app_node.name = "app\nname";
    app_node.sketches = {"app.ino"};
    app_node.sketch_library_rel = "../..";
    expect(mm::ino::render_app_manifest(app_node).empty(),
           "render_app_manifest must return empty string for newline in name");

    app_node.name = "valid-app";
    app_node.sketches = {"bad\nsketch.ino"};
    expect(mm::ino::render_app_manifest(app_node).empty(),
           "render_app_manifest must return empty string for newline in"
           " sketch");

    app_node.sketches = {"app.ino"};
    app_node.sketch_library_rel = "../../\ninvalid";
    expect(mm::ino::render_app_manifest(app_node).empty(),
           "render_app_manifest must return empty string for newline in"
           " library rel");

    dir_node.name = "bad\tname";
    dir_node.folders = {"child"};
    expect(mm::ino::render_dir_manifest(dir_node).empty(),
           "render_dir_manifest must reject control characters");
}

void library_metadata_compatibility_failures() {
    const mm::test::scoped_tree tree{"ino_compat_fail"};
    const auto root = tree.root();
    const auto ex = root / "examples";
    std::filesystem::create_directories(ex / "app1");

    mm::ino::LibraryDirNode root_node;
    root_node.dir = root;
    root_node.name = "root";
    root_node.folders = {"examples"};
    root_node.is_root = true;

    std::string err;

    // 1. Wrong kind in root manifest
    mm::mdy::MDYDocument bad_kind_doc;
    bad_kind_doc.metadata["kind"] = {"lib"};
    bad_kind_doc.metadata["folder"] = {"examples"};
    expect(!mm::ino::validate_manifest_compatibility(
               bad_kind_doc, &root_node, nullptr, false, root,
               root / "mm.mdy", err),
           "manifest with wrong kind must fail validation");
    expect(err.find("expected kind: dir") != std::string::npos,
           "error must report expected kind: dir");

    // 2. Duplicate folder: entry in dir manifest
    mm::ino::LibraryDirNode dir_node;
    dir_node.dir = ex;
    dir_node.name = "examples";
    dir_node.folders = {"app1"};
    dir_node.is_root = false;

    mm::mdy::MDYDocument dup_folder_doc;
    dup_folder_doc.metadata["kind"] = {"dir"};
    dup_folder_doc.metadata["name"] = {"examples"};
    dup_folder_doc.metadata["folder"] = {"app1", "app1"};
    expect(!mm::ino::validate_manifest_compatibility(
               dup_folder_doc, &dir_node, nullptr, false, root,
               ex / "mm.mdy", err),
           "manifest with duplicate folder: must fail validation");
    expect(err.find("duplicate folder: app1") != std::string::npos,
           "error must report duplicate folder: app1");

    // 3. Wrong project: declaration in root manifest
    mm::mdy::MDYDocument wrong_proj_doc;
    wrong_proj_doc.metadata["kind"] = {"dir"};
    wrong_proj_doc.metadata["name"] = {"root"};
    wrong_proj_doc.metadata["folder"] = {"examples"};
    wrong_proj_doc.metadata["project"] = {"../wrong-project"};
    expect(!mm::ino::validate_manifest_compatibility(
               wrong_proj_doc, &root_node, nullptr, true, root,
               root / "mm.mdy", err, "/expected/project/path"),
           "root manifest with mismatched project: must fail validation");
    expect(err.find("project: does not resolve to expected project root") !=
               std::string::npos,
           "error must report project root mismatch");

    // 4. Absolute and duplicate project: declarations are loader-invalid.
    mm::mdy::MDYDocument absolute_proj_doc;
    absolute_proj_doc.metadata["kind"] = {"dir"};
    absolute_proj_doc.metadata["name"] = {"root"};
    absolute_proj_doc.metadata["folder"] = {"examples"};
    absolute_proj_doc.metadata["project"] = {root.string()};
    expect(!mm::ino::validate_manifest_compatibility(
               absolute_proj_doc, &root_node, nullptr, true, root,
               root / "mm.mdy", err, root),
           "absolute project: declaration must fail validation");
    expect(err.find("project: value must be relative") != std::string::npos,
           "error must report absolute project: value");

    mm::mdy::MDYDocument duplicate_proj_doc = absolute_proj_doc;
    duplicate_proj_doc.metadata["project"] = {".", "."};
    expect(!mm::ino::validate_manifest_compatibility(
               duplicate_proj_doc, &root_node, nullptr, true, root,
               root / "mm.mdy", err, root),
           "duplicate project: declaration must fail validation");
    expect(err.find("expected one project: declaration") !=
               std::string::npos,
           "error must report duplicate project: declaration");

    // 5. The application compatibility keys have explicit cardinality and
    // relative-path rules even when their first values look correct.
    mm::ino::LibraryAppNode app_node;
    app_node.dir = ex / "app1";
    app_node.rel_path = "app1";
    app_node.name = "app1";
    app_node.sketches = {"app1.ino"};
    app_node.sketch_library_rel = "../..";

    mm::mdy::MDYDocument duplicate_lib_doc;
    duplicate_lib_doc.metadata["kind"] = {"app"};
    duplicate_lib_doc.metadata["name"] = {"app1"};
    duplicate_lib_doc.metadata["use"] = {"mm.sketch"};
    duplicate_lib_doc.metadata["file"] = {"main.cpp"};
    duplicate_lib_doc.metadata["sketch"] = {"app1.ino"};
    duplicate_lib_doc.metadata["sketch-library"] = {"../..", "../.."};
    expect(!mm::ino::validate_manifest_compatibility(
               duplicate_lib_doc, nullptr, &app_node, false, root,
               app_node.dir / "mm.mdy", err),
           "duplicate sketch-library: declaration must fail validation");

    duplicate_lib_doc.metadata["sketch-library"] = {root.string()};
    expect(!mm::ino::validate_manifest_compatibility(
               duplicate_lib_doc, nullptr, &app_node, false, root,
               app_node.dir / "mm.mdy", err),
           "absolute sketch-library: declaration must fail validation");
    expect(err.find("sketch-library: value must be relative") !=
               std::string::npos,
           "error must report absolute sketch-library: value");
}

const mm::test::case_ cases[] = {
    {"function used before definition", &function_used_before_definition},
    {"prototype already written", &prototype_already_written},
    {"standard include in middle", &standard_include_in_middle},
    {"sketch with main is rejected", &sketch_with_main_is_rejected},
    {"quoted include is hoisted", &quoted_include_is_hoisted},
    {"two files transformation", &two_files_transformation},
    {"definition heuristic cannot see", &definition_heuristic_cannot_see},
    {"indented helper produces warning", &indented_helper_produces_warning},
    {"later declaration keeps generated prototype",
     &later_declaration_keeps_generated_prototype},
    {"write_guarded lifecycle", &write_guarded_lifecycle},
    {"check_application rules", &check_application_rules},
    {"sketch header synthesis", &sketch_header_synthesis},
    {"sketch header is checked", &sketch_header_is_checked},
    {"library discovery flat", &library_discovery_flat},
    {"library discovery category nested", &library_discovery_category_nested},
    {"library discovery symlink and empty skipped",
     &library_discovery_symlink_and_empty_skipped},
    {"library manifest compatibility validation",
     &library_manifest_compatibility_validation},
    {"library empty or all skipped", &library_empty_or_all_skipped},
    {"library sketch directly under examples refused",
     &library_sketch_directly_under_examples_refused},
    {"library ambiguous primary sketch skipped",
     &library_ambiguous_primary_sketch_skipped},
    {"library application name collision refused",
     &library_application_name_collision_refused},
    {"library scalar validation", &library_scalar_validation},
    {"library metadata compatibility failures",
     &library_metadata_compatibility_failures},
};

const mm::test::registrar reg{"mm.ino", cases};

} // namespace

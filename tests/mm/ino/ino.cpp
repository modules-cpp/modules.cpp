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
    expect(result.output.find("#include <vector>\nimport mm.sketch;") != std::string::npos,
           "expected #include <vector> hoisted above import mm.sketch;");
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
                              "import mm.sketch;") != std::string::npos,
           "quoted and angled includes hoist in source order above the import");

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
    expect(mm::ino::check_application(dir, doc, error),
           "an application without sketch-library needs no header");

    doc.metadata["sketch-library"] = {".."};
    expect(!mm::ino::check_application(dir, doc, error),
           "a declared sketch-library requires the generated header");
    expect(error.find("Arduino.h") != std::string::npos,
           "the error names the missing header");

    std::ofstream(dir / "Arduino.h") << "#pragma once\n";
    expect(!mm::ino::check_application(dir, doc, error),
           "a hand-edited header is refused");

    std::ofstream(dir / "Arduino.h") << mm::ino::sketch_header();
    expect(mm::ino::check_application(dir, doc, error),
           "the generated header passes");
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
};

const mm::test::registrar reg{"mm.ino", cases};

} // namespace

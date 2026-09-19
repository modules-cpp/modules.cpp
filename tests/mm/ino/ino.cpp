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
    expect(header.find("using namespace mm::sketch;") != std::string::npos,
           "the header introduces the sketch vocabulary unqualified");
    expect(header.find("#define F(string_literal) (string_literal)") !=
               std::string::npos,
           "F is accepted and inert");
    expect(header.find("#define PROGMEM\n") != std::string::npos,
           "PROGMEM is accepted and inert");
    expect(header.find("A0 = 0;") != std::string::npos &&
               header.find("A7 = 7;") != std::string::npos,
           "the analog channel names are present");
    expect(header.find("do not edit by hand") != std::string::npos,
           "the header says it is generated");
    expect(mm::ino::sketch_header() == header,
           "synthesis is deterministic, so --check can compare it");
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
};

const mm::test::registrar reg{"mm.ino", cases};

} // namespace

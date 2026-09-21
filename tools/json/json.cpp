// modules.cpp json tool
//
// Usage: json [-v|--verbose] [-h|--help] (--check | --indent | --compact | --scan)
//             FILE...
//
// The front end over mm.json. --check reads each file with the value layer
// and prints nothing for a document the module accepts; for one it rejects
// it prints FILE:LINE:COLUMN: DESCRIPTION (STATUS) on stderr. --indent and
// --compact read one file and write it to stdout in that layout. --scan
// reads one file with the scanner alone and prints one token per line,
// stopping at the first fault the scanner finds with the same line --check
// would print for it.
//
// Exit 0 when every document is accepted; 1 when mm.json rejected one -- a
// grammar fault or a policy such as a duplicate key, which the message's
// STATUS distinguishes; exit_usage for an argument fault; exit_manifest for
// a file that cannot be read.
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

import mm.app;
import mm.build;
import mm.json;

namespace {

[[nodiscard]] std::string_view name_of(mm::json::Status status) {
    switch (status) {
        case mm::json::Status::Ok: return "Ok";
        case mm::json::Status::Malformed: return "Malformed";
        case mm::json::Status::Truncated: return "Truncated";
        case mm::json::Status::TooDeep: return "TooDeep";
        case mm::json::Status::BadEscape: return "BadEscape";
        case mm::json::Status::BadUnicode: return "BadUnicode";
        case mm::json::Status::BadNumber: return "BadNumber";
        case mm::json::Status::DuplicateKey: return "DuplicateKey";
        case mm::json::Status::Overflow: return "Overflow";
    }
    return "Unknown";
}

[[nodiscard]] std::string_view name_of(mm::json::Token token) {
    switch (token) {
        case mm::json::Token::ObjectBegin: return "object-begin";
        case mm::json::Token::ObjectEnd: return "object-end";
        case mm::json::Token::ArrayBegin: return "array-begin";
        case mm::json::Token::ArrayEnd: return "array-end";
        case mm::json::Token::Key: return "key";
        case mm::json::Token::String: return "string";
        case mm::json::Token::Integer: return "integer";
        case mm::json::Token::Number: return "number";
        case mm::json::Token::True: return "true";
        case mm::json::Token::False: return "false";
        case mm::json::Token::Null: return "null";
        case mm::json::Token::End: return "end";
    }
    return "unknown";
}

[[nodiscard]] bool read_file(const std::filesystem::path& file, std::string& text) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    text = buffer.str();
    return true;
}

void report(const std::filesystem::path& file, const mm::json::Issue& issue) {
    std::cerr << file.string() << ":" << issue.line << ":" << issue.column << ": "
              << issue.description << " (" << name_of(issue.status) << ")\n";
}

[[nodiscard]] int check(const std::filesystem::path& file, const std::string& text) {
    mm::json::Value value;
    const auto outcome = mm::json::parse(text, value);
    if (outcome.status == mm::json::Status::Ok) return 0;
    report(file, outcome.issue);
    return 1;
}

[[nodiscard]] int lay_out(const std::filesystem::path& file, const std::string& text,
                          mm::json::Layout layout) {
    mm::json::Value value;
    const auto outcome = mm::json::parse(text, value);
    if (outcome.status != mm::json::Status::Ok) {
        report(file, outcome.issue);
        return 1;
    }
    std::string written;
    const auto wrote = mm::json::write(value, written, layout);
    if (wrote.status != mm::json::Status::Ok) {
        report(file, wrote.issue);
        return 1;
    }
    std::cout << written << "\n";
    return 0;
}

[[nodiscard]] int scan(const std::filesystem::path& file, const std::string& text) {
    mm::json::Scanner scanner(text);
    while (true) {
        mm::json::Event event{};
        const auto status = scanner.next(event);
        if (status != mm::json::Status::Ok) {
            report(file, scanner.issue());
            return 1;
        }
        std::cout << event.offset << " " << event.depth << " " << name_of(event.token);
        if (event.length != 0 && event.token != mm::json::Token::End)
            std::cout << " " << std::string_view{text}.substr(event.offset, event.length);
        std::cout << "\n";
        if (event.token == mm::json::Token::End) return 0;
    }
}

}  // namespace

int main(int argc, char** argv) {
    mm::app::Options options("json");
    options.flag("--check");
    options.flag("--indent");
    options.flag("--compact");
    options.flag("--scan");
    options.positional_limit(4096);   // --check takes any number of files
    options.help("json [-v|--verbose] [-h|--help] (--check | --indent | --compact | --scan) "
                 "FILE...");
    const auto cli = options.parse(argc, argv);
    if (cli == mm::app::Cli::help) return mm::build::exit_ok;
    if (cli != mm::app::Cli::ok) return mm::build::exit_usage;

    const bool do_check = options.seen("--check");
    const bool do_indent = options.seen("--indent");
    const bool do_compact = options.seen("--compact");
    const bool do_scan = options.seen("--scan");
    const int commands = (do_check ? 1 : 0) + (do_indent ? 1 : 0) + (do_compact ? 1 : 0) +
                         (do_scan ? 1 : 0);
    if (commands != 1) {
        std::cerr << "json: one of --check, --indent, --compact, or --scan is required\n";
        return mm::build::exit_usage;
    }
    const auto& files = options.positional();
    if (files.empty() || (!do_check && files.size() != 1)) {
        std::cerr << (files.empty() ? "json: a file is required\n"
                                    : "json: --indent, --compact, and --scan take one file\n");
        return mm::build::exit_usage;
    }

    int worst = 0;
    for (const auto& name : files) {
        const std::filesystem::path file{name};
        std::string text;
        if (!read_file(file, text)) {
            std::cerr << "json: cannot read " << file.string() << "\n";
            return mm::build::exit_manifest;
        }
        int result = 0;
        if (do_check) result = check(file, text);
        else if (do_scan) result = scan(file, text);
        else result = lay_out(file, text, do_indent ? mm::json::Layout::Indented
                                                     : mm::json::Layout::Compact);
        if (result > worst) worst = result;
    }
    return worst;
}

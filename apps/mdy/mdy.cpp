#include <iostream>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

import mm.app;
import mm.mdy;
import mm.build;

// Retained for compatibility with the original public mdy app's `-s` mode.
void create_dummy_mdy(const std::filesystem::path& path) {
    std::ofstream out(path);
    out << "---\n";
    out << "mm: 1.0\n";
    out << "kind: file\n";
    out << "name: sample.mdy\n";
    out << "---\n";
    out << "# modules.cpp C++20 Modules\n";
    out << "## Features\n";
    out << "Faster compilation speeds than headers\n";
    out << "True logical separation of interface code\n";
    out << "### Rules\n";
    out << "Modules replace old header-file macro include frameworks entirely.\n";
}

int sample() {
    const std::filesystem::path mdy_file = "./out/sample.mdy";
    create_dummy_mdy(mdy_file);

    const auto doc = mm::mdy::Parser::parse_file(mdy_file);
    std::cout << "=== METADATA EXTRACTED ===\n";
    for (const auto& [key, values] : doc.metadata)
        for (const auto& value : values) std::cout << key << " -> " << value << "\n";

    std::cout << "\n=== BODY CONTENT TRAVERSAL ===\n";
    for (const auto& block : doc.body) {
        switch (block.type) {
            case mm::mdy::BlockType::Heading1: std::cout << "Heading1: "; break;
            case mm::mdy::BlockType::Heading2: std::cout << "Heading2: "; break;
            case mm::mdy::BlockType::Heading3: std::cout << "Heading3: "; break;
            case mm::mdy::BlockType::UnorderedList: std::cout << "UnorderedList: "; break;
            default: std::cout << "Text: "; break;
        }
        std::cout << block.content << "\n";
    }
    return 0;
}

// Document text is data, never markup. Everything placed into the output goes
// through here, so a heading containing <script> renders as text rather than
// running.
std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());

    for (const char c : text) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;        break;
        }
    }

    return out;
}

// Renders the body. List items are grouped: a bare <li> outside a <ul> is not
// valid HTML, so a run of consecutive items opens one list and closes it as
// soon as anything else appears.
std::string to_html(const std::vector<mm::mdy::Block>& blocks) {
    std::string html_output;
    bool in_list = false;

    const auto close_list = [&] {
        if (in_list) {
            html_output += "</ul>\n";
            in_list = false;
        }
    };

    for (const auto& block : blocks) {
        if (block.type == mm::mdy::BlockType::UnorderedList) {
            if (!in_list) {
                html_output += "<ul>\n";
                in_list = true;
            }
            html_output += "  <li>" + escape(block.content) + "</li>\n";
            continue;
        }

        close_list();

        switch (block.type) {
            case mm::mdy::BlockType::Heading1:
                html_output += "<h1>" + escape(block.content) + "</h1>\n";
                break;
            case mm::mdy::BlockType::Heading2:
                html_output += "<h2>" + escape(block.content) + "</h2>\n";
                break;
            case mm::mdy::BlockType::Heading3:
                html_output += "<h3>" + escape(block.content) + "</h3>\n";
                break;
            case mm::mdy::BlockType::Paragraph:
                html_output += "<p>" + escape(block.content) + "</p>\n";
                break;
            case mm::mdy::BlockType::Empty:
                break;
            case mm::mdy::BlockType::UnorderedList:
                break;  // handled above
        }
    }

    close_list();

    return html_output;
}

// The document title comes from the front matter when it says so, and from the
// file name otherwise.
std::string title_of(const mm::mdy::MDYDocument& doc, const std::filesystem::path& file) {
    for (const auto* key : {"title", "name"}) {
        const auto it = doc.metadata.find(key);
        if (it != doc.metadata.end() && !it->second.empty() && !it->second.front().empty())
            return it->second.front();
    }

    return file.stem().string();
}

// Each file: or unit: value a manifest lists, mapped to the href its page
// should point at. Built per manifest by generate_site, which is the only
// caller that knows where the page will be written and so the only one that
// can make the link relative to it.
using FileLinks = std::map<std::string, std::string>;

// Front matter is document content, not decoration: for most manifests it is
// the only content there is. Every file: and unit: value is linked, through
// the map above, back to the file it names: the rendered page for a doc
// manifest's prose, and the source file itself for everything else.
//
// children_in_nav drops folder: entries. On a project or dir page the
// navigation above already lists exactly those children, each with its kind
// and a relative link computed for that page, so repeating the raw values
// here put every child on the page twice. The navigation list is the better
// of the two, so this one goes. A folder: value that is not among those
// children was reached through another parent first, and is listed on that
// parent's page instead.
std::string to_metadata(const mm::mdy::MDYDocument& doc, bool children_in_nav,
                        const FileLinks* links) {
    std::string body;

    for (const auto& [key, values] : doc.metadata) {
        if (children_in_nav && key == "folder") continue;

        body += "  <dt>" + escape(key) + "</dt>\n";

        for (const auto& value : values) {
            body += "  <dd>";

            const std::string* href = nullptr;
            if (links != nullptr && (key == "file" || key == "unit")) {
                const auto it = links->find(value);
                if (it != links->end()) href = &it->second;
            }

            if (href != nullptr)
                body += "<a href=\"" + escape(*href) + "\">" + escape(value) + "</a>";
            else
                body += escape(value);

            body += "</dd>\n";
        }
    }

    if (body.empty()) return {};

    return "<dl>\n" + body + "</dl>\n";
}

// A complete document rather than a fragment, so the output can be written
// straight to a file and opened.
std::string to_document(const mm::mdy::MDYDocument& doc, const std::filesystem::path& file,
                        std::string_view nav = {}, bool children_in_nav = false,
                        const FileLinks* links = nullptr) {
    std::string out;

    out += "<!DOCTYPE html>\n";
    out += "<html lang=\"en\">\n";
    out += "<head>\n";
    out += "<meta charset=\"utf-8\">\n";
    out += "<title>" + escape(title_of(doc, file)) + "</title>\n";
    out += "</head>\n";
    out += "<body>\n";
    out += nav;
    out += to_metadata(doc, children_in_nav, links);
    out += to_html(doc.body);
    out += "</body>\n";
    out += "</html>\n";

    return out;
}

// Where a node's page lives, relative to the output root. Paths are rebased on
// the walk root so that generating a subtree puts its top manifest at
// index.html rather than burying it under its source path.
std::filesystem::path page_of(const mm::build::Node& node, const std::filesystem::path& base) {
    return (node.dir.lexically_normal().lexically_relative(base) / "index.html").lexically_normal();
}

// A link from one page to another, relative, so the site works from a file://
// path or any subdirectory of a server.
std::string link_between(const mm::build::Node& from, const mm::build::Node& to,
                         const std::filesystem::path& base) {
    return page_of(to, base)
        .lexically_relative(page_of(from, base).parent_path())
        .string();
}

// Breadcrumbs up to the root, then the children below.
std::string to_nav(const std::vector<mm::build::Node>& nodes, std::size_t index,
                   const std::filesystem::path& base) {
    const auto& node = nodes[index];

    std::vector<std::size_t> trail;
    for (auto up = node.parent; up != mm::build::no_parent; up = nodes[up].parent)
        trail.push_back(up);

    std::string out;

    if (!trail.empty()) {
        out += "<nav>\n";
        for (auto it = trail.rbegin(); it != trail.rend(); ++it) {
            const auto& ancestor = nodes[*it];
            out += "<a href=\"" + escape(link_between(node, ancestor, base)) + "\">" +
                   escape(ancestor.name.empty() ? ancestor.dir.string() : ancestor.name) +
                   "</a> / \n";
        }
        out += "<span>" + escape(node.name) + "</span>\n";
        out += "</nav>\n";
    }

    out += "<h1>" + escape(node.name) + "</h1>\n";
    out += "<p>" + escape(node.kind) + "</p>\n";

    if (!node.children.empty()) {
        out += "<ul>\n";
        for (const auto child : node.children) {
            const auto& target = nodes[child];
            out += "  <li><a href=\"" + escape(link_between(node, target, base)) + "\">" +
                   escape(target.name.empty() ? target.dir.string() : target.name) +
                   "</a> &mdash; " + escape(target.kind) + "</li>\n";
        }
        out += "</ul>\n";
    }

    return out;
}

// A doc manifest's file: entries are prose files, not manifests: nothing
// walks them into a Node of their own (mm::build::load_nodes only recurses
// through folder: on a project or dir manifest), so nothing else in this
// walk would ever render them. Each is written as a page beside the doc
// node's own page, in the same directory, so the link to_metadata writes
// ("<stem>.html") and the link back here ("index.html") both need no path
// computation.
//
// target.sources comes from mm::build::load_tree, not from re-reading
// doc.metadata.find("file") here: load_tree already rejects an absolute or
// ..-climbing file: value the same way it does for every other kind, so
// this function never joins an unvalidated path the way the doc.metadata
// approach it replaced did.
void render_doc_files(const mm::build::Node& node, const mm::build::Target& target,
                      const std::filesystem::path& out_dir, const std::filesystem::path& base,
                      std::size_t& written, bool& had_error) {
    if (target.sources.empty()) return;

    const auto page_dir = (out_dir / page_of(node, base)).parent_path();

    // Two file: entries with the same stem (from different subdirectories,
    // for instance) would otherwise collide into one <stem>.html, one
    // silently overwriting the other's page.
    std::set<std::filesystem::path> seen_pages;

    for (const auto& unit : target.sources) {
        const auto& source = unit.path;
        if (!std::filesystem::exists(source)) {
            std::cerr << "mdy: " << node.manifest.string() << " lists missing file: " << source << "\n";
            had_error = true;
            continue;
        }

        const auto page = page_dir / (std::filesystem::path(source).stem().string() + ".html");
        if (!seen_pages.insert(page).second) {
            std::cerr << "mdy: " << node.manifest.string() << ": " << source
                      << " collides with another file: entry at " << page.string() << "\n";
            had_error = true;
            continue;
        }

        const auto file_doc = mm::mdy::Parser::parse_file(source);

        std::error_code ec;
        std::filesystem::create_directories(page.parent_path(), ec);
        if (ec) {
            std::cerr << "mdy: cannot create " << page.parent_path().string() << ": "
                      << ec.message() << "\n";
            had_error = true;
            continue;
        }

        std::ofstream file(page);
        if (!file) {
            std::cerr << "mdy: cannot write " << page.string() << "\n";
            had_error = true;
            continue;
        }

        const std::string nav = "<nav>\n<a href=\"index.html\">" + escape(node.name) +
                                "</a> / \n<span>" + escape(source) + "</span>\n</nav>\n";
        file << to_document(file_doc, source, nav);
        ++written;

        std::cout << page.string() << "\n";
    }
}

// Where a manifest's own file: and unit: values should point from its page.
//
// A doc manifest's file: entries name prose this tool renders, so they link
// to the page render_doc_files writes beside this one. Every other kind
// names a source file nothing renders, so those link back to the file
// itself, relative to the directory the page is written into. That works
// wherever the output goes, including -o= outside the project, because both
// sides are made absolute before the relative path is computed.
//
// file: is relative to the manifest's own directory, while unit: is already
// project relative; mm::build joins them that way too, and getting it wrong
// here would produce links that resolve to nothing. A value naming a file
// that does not exist is left unlinked rather than pointed somewhere dead.
FileLinks file_links(const mm::build::Node& node, const mm::mdy::MDYDocument& doc,
                     const std::filesystem::path& page_dir, bool is_doc) {
    FileLinks links;

    for (const auto& [key, values] : doc.metadata) {
        if (key != "file" && key != "unit") continue;

        for (const auto& value : values) {
            if (value.empty()) continue;

            if (is_doc && key == "file") {
                links[value] = std::filesystem::path(value).stem().string() + ".html";
                continue;
            }

            const auto source = key == "file" ? node.dir / value : std::filesystem::path(value);
            if (!std::filesystem::exists(source)) continue;

            std::error_code ec;
            const auto from = std::filesystem::absolute(page_dir, ec).lexically_normal();
            if (ec) continue;
            const auto to = std::filesystem::absolute(source, ec).lexically_normal();
            if (ec) continue;

            const auto rel = std::filesystem::relative(to, from, ec);
            if (ec || rel.empty()) continue;

            links[value] = rel.generic_string();
        }
    }

    return links;
}

// Walks the manifest tree and writes one page per manifest, mirroring the
// source layout so relative links need no rewriting.
int generate_site(const std::filesystem::path& root_manifest, const std::filesystem::path& out_dir,
                  bool verbose) {
    const auto root_dir = root_manifest.parent_path();
    const auto walk_root = root_dir.empty() ? std::filesystem::path(".") : root_dir;

    bool ok = false;
    const auto nodes = mm::build::load_nodes(walk_root, ok);
    if (!ok) return 65;

    if (nodes.empty()) {
        std::cerr << "mdy: no manifests found under " << root_manifest.string() << "\n";
        return 65;
    }

    // load_tree, not load_nodes, is what validates file: entries the same
    // way every other kind's paths are validated (no absolute value, no ..
    // climbing out of the tree): load_nodes only confirms the manifest
    // tree's own folder: shape. A doc node's Target is looked up by
    // directory below, mirroring mm.model's own index_by_dir pattern.
    const auto tree = mm::build::load_tree(walk_root);
    if (!tree.ok) return 65;

    std::map<std::filesystem::path, const mm::build::Target*> docs_by_dir;
    for (const auto& target : tree.docs) docs_by_dir[target.dir] = &target;

    std::error_code ec;
    const auto base = nodes.front().dir.lexically_normal();
    std::size_t written = 0;
    bool had_error = false;

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const auto& node = nodes[i];
        const auto doc = mm::mdy::Parser::parse_file(node.manifest);

        const auto page = out_dir / page_of(node, base);
        std::filesystem::create_directories(page.parent_path(), ec);
        if (ec) {
            std::cerr << "mdy: cannot create " << page.parent_path().string() << ": "
                      << ec.message() << "\n";
            return 1;
        }

        std::ofstream file(page);
        if (!file) {
            std::cerr << "mdy: cannot write " << page.string() << "\n";
            return 1;
        }

        const bool structural = node.kind == "project" || node.kind == "dir";
        const bool is_doc = node.kind == "doc";
        const auto links = file_links(node, doc, page.parent_path(), is_doc);
        file << to_document(doc, node.manifest, to_nav(nodes, i, base), structural, &links);

        std::cout << page.string() << "\n";
        ++written;

        if (verbose) {
            std::cerr << "  " << node.kind << " " << node.name << " from "
                      << node.manifest.string() << "\n";
            for (const auto& [value, href] : links)
                std::cerr << "    " << value << " -> " << href << "\n";
        }

        if (is_doc) {
            const auto it = docs_by_dir.find(node.dir.lexically_normal());
            if (it != docs_by_dir.end())
                render_doc_files(node, *it->second, out_dir, base, written, had_error);
        }
    }

    std::cout << written << " page(s) written to " << out_dir.string() << "\n";
    if (had_error) {
        std::cerr << "mdy: site written with errors; see messages above\n";
        return 1;
    }
    return 0;
}


// The mdy application. App (mm.app) is the project's application boundary:
// it takes argc and argv and exposes a virtual run, but deliberately stores
// no state, so this subclass keeps the arguments it was given and does its
// own parsing in run.
class MdyApp : public App {
public:
    MdyApp(int argc, char** argv) : App(argc, argv), argc_(argc), argv_(argv) {}

    MdyApp(const MdyApp&) = delete;
    MdyApp& operator=(const MdyApp&) = delete;
    MdyApp(MdyApp&&) = delete;
    MdyApp& operator=(MdyApp&&) = delete;

    [[nodiscard]] int run() override;

private:
    int argc_;
    char** argv_;
};

int MdyApp::run() {
    const int argc = argc_;
    char** const argv = argv_;

    std::filesystem::path mdy_file;
    // build mirrors the source layout already, so a manifest's page sits beside
    // the objects built from that same directory: docs/mm.mdy -> out/docs/
    // index.html, and the project manifest -> out/index.html.
    std::filesystem::path out_dir = "out";
    bool verbose = false;
    bool html = false;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-v" || arg == "--verbose")
            verbose = true;
        else if (arg == "-s")
            return sample();
        else if (arg == "-h")
            html = true;
        else if (arg.starts_with("-o="))
            out_dir = arg.substr(3);
        else if (mdy_file.empty())
            mdy_file = arg;
        else {
            std::cerr << "mdy: unexpected argument: " << arg << "\n";
            return 2;
        }
    }

    if (mdy_file.empty()) {
        std::cerr << "usage: mdy [-s] | <file.mdy> [-v|--verbose] [-h] [-o=<dir>]\n";
        return 2;
    }

    if (!std::filesystem::exists(mdy_file)) {
        std::cerr << "file not found " << mdy_file << "\n";
        return 1;
    }

    // In HTML mode stdout carries the document and nothing else, so it can be
    // redirected to a file; diagnostics go to stderr.
    std::ostream& log = html ? std::cerr : std::cout;

    if (verbose) log << "mdy file: " << mdy_file.string() << "\n";

    // Run our modular front matter parser
    mm::mdy::MDYDocument doc = mm::mdy::Parser::parse_file(mdy_file);

    if (verbose) {
        // Print parsed front matter metadata
        log << "--- metadata --\n";
        for (const auto& [key, values] : doc.metadata) {
            for (const auto& val : values) {
                log << key << " " << val << "\n";
            }
        }

        for (const auto& [key, values] : doc.metadata) {
            log << key << " -> ";
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (i != 0) log << ", ";
                log << values[i];
            }
            log << "\n";
        }

        log << "\n=== mdy content ===\n";
        for (const auto& block : doc.body) {
            if (block.type == mm::mdy::BlockType::Heading1) {
                log << "Heading: " << block.content << "\n";
            } else {
                log << "Text: " << block.content << "\n";
            }
        }
    }

    if (html) {
        // A kind:project or kind:dir manifest describes a tree, not a page, so
        // rendering just that one file would produce an empty document. Walk it
        // instead and write the whole site.
        const auto kind = doc.metadata.find("kind");
        const bool structural = kind != doc.metadata.end() && !kind->second.empty() &&
                                (kind->second.front() == "project" || kind->second.front() == "dir");

        if (structural) return generate_site(mdy_file, out_dir, verbose);

        std::cout << to_document(doc, mdy_file);
    }

    return 0;
}

int main(int argc, char** argv) {
    MdyApp app(argc, argv);
    return app.run();
}

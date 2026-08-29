// Abstract data model of an MDY document: front matter plus body blocks, as
// docs/mdy.mdy defines the format. See models.manifest
// (models/manifest/manifest.cppm) for the note on why this is a design
// artifact rather than a build target.
//
// Every MDY file is a Document, per docs/modules.mdy's "MDY manifests": a
// manifest is a Document named mm.mdy that happens to declare a
// project/dir/module/app/test/doc target, and this guide is a Document that
// declares none of those. This module is therefore foundational: every
// other models.* module sits above it, the same way mm.build imports mm.mdy
// because "manifests are MDY documents."
//
// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <filesystem>
#include <string_view>
#include <vector>

export module models.document;

export namespace models {

enum class BlockType { Heading1, Heading2, Heading3, Paragraph, List, Empty };

// One body block: a heading, a paragraph, a list item, or an empty line
// preserved as a block boundary.
class Block {
public:
    virtual ~Block() = default;

    [[nodiscard]] virtual BlockType type() const = 0;
    [[nodiscard]] virtual std::string_view text() const = 0;
};

class Document {
public:
    virtual ~Document() = default;

    // Root relative.
    [[nodiscard]] virtual std::filesystem::path path() const = 0;

    // Front matter values for key, in declaration order; empty if key is
    // absent. A scalar key's value is values(key).front(): docs/modules.mdy
    // says "first value wins for scalar keys," and a repeated key such as
    // folder: or file: keeps every value in the order it was declared.
    [[nodiscard]] virtual std::vector<std::string_view> values(std::string_view key) const = 0;

    // Body blocks below the front matter, in document order.
    [[nodiscard]] virtual std::vector<const Block*> body() const = 0;
};

}  // namespace models

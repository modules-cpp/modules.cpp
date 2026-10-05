// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

#include <cstddef>
#include <span>
#include <string_view>

export module mm.fs:path;

import :status;

export namespace mm::fs {

// The normalised form of an absolute path, written into out; length is its
// size. Repeated '/' collapse, "." drops, ".." removes the previous component
// and never climbs above "/", and a trailing '/' is ignored, so the result is
// "/" or "/" followed by components joined by single '/'. The path is resolved
// left to right in out, so it must fit out, and max_path, at every step.
//
// BadArgument for an empty path, one not starting with '/', or one holding a
// NUL; NameTooLong for a component over max_name bytes or a result over
// max_path or out. length and out change only on Ok.
[[nodiscard]] Status normalize(std::string_view path, std::span<char> out,
                               std::size_t& length);

}

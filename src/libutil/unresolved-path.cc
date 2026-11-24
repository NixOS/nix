#include "nix/util/unresolved-path.hh"
#include "nix/util/canon-path.hh"
#include "nix/util/strings-inline.hh"
#include "nix/util/util.hh"

#include <cstring>
#include <ranges>

namespace nix {

static void ensureNoNullBytes(std::string_view s)
{
    if (std::memchr(s.data(), '\0', s.size())) [[unlikely]] {
        using namespace std::string_view_literals;
        auto str = replaceStrings(std::string(s), "\0"sv, "␀"sv);
        throw BadCanonPath("path segment '%s' must not contain null (\\0) bytes", str);
    }
}

UnresolvedPath::UnresolvedPath(std::string_view raw)
    : path("/")
{
    ensureNoNullBytes(raw);
    for (auto c : std::views::split(raw, '/')) {
        std::string_view comp{c.begin(), c.end()};
        /* No `/`, which it was split on, nor NUL, checked above. */
        if (!comp.empty() && comp != ".")
            pushUnchecked(comp);
    }
}

UnresolvedPath::UnresolvedPath(std::string_view raw, const UnresolvedPath & root)
    : UnresolvedPath(raw.starts_with('/') ? std::string{raw} : concatStrings(root.abs(), "/", raw))
{
}

void UnresolvedPath::push(std::string_view c)
{
    assert(c.find('/') == c.npos);
    assert(c != ".");
    ensureNoNullBytes(c);
    pushUnchecked(c);
}

void UnresolvedPath::pop()
{
    assert(!isRoot());
    path.resize(std::max((size_t) 1, path.rfind('/')));
}

UnresolvedPath UnresolvedPath::operator/(std::string_view c) const
{
    auto res = *this;
    res.push(c);
    return res;
}

std::ostream & operator<<(std::ostream & stream, const UnresolvedPath & path)
{
    stream << path.abs();
    return stream;
}

} // namespace nix

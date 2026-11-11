#include "nix/util/os-canon-path.hh"
#include "nix/util/os-string.hh"

#include <algorithm>
#include <cassert>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>

namespace nix {

/* `OsCanonPath::Iterator::operator*` relies on this to get a reference
   to a real `path` object, rather than to a temporary. */
static_assert(std::is_same_v<
              decltype(std::declval<const std::filesystem::path::const_iterator &>().operator->()),
              const std::filesystem::path *>);

static_assert(std::bidirectional_iterator<OsCanonPath::Iterator>);
static_assert(std::ranges::bidirectional_range<OsCanonPath>);
static_assert(std::ranges::common_range<OsCanonPath>);

OsCanonPath::OsCanonPath(const CanonPath & canonPath)
{
    for (const auto & component : canonPath) {
#ifdef _WIN32
        assert(component.str().find('\\') == component.str().npos && "CanonPath component cannot contain backslash");
#endif
        p /= component.str();
    }
    // No need to further validate — CanonPath guarantees no empty/dot/dotdot components,
    // and operator/ produces preferred separators.
}

void OsCanonPath::validate() const
{
    assert(!p.has_root_path() && "OsCanonPath cannot have a root path");
#ifdef _WIN32
    assert(
        p.native().find(L'/') == std::wstring::npos
        && "OsCanonPath must use preferred (backslash) separators on Windows");
#endif
    for (const auto & component : p) {
        const auto & s = component.native();
        assert(!s.empty() && "OsCanonPath cannot have empty components");
        assert(s != OS_STR(".") && "OsCanonPath cannot have '.' components");
        assert(s != OS_STR("..") && "OsCanonPath cannot have '..' components");
    }
}

OsCanonPath OsCanonPath::operator/(const OsCanonPath & other) const
{
    if (p.empty())
        return other;
    if (other.p.empty())
        return *this;
    OsCanonPath result;
    result.p = p / other.p;
    return result;
}

OsCanonPath OsCanonPath::operator/(const OsFilename & name) const
{
    OsCanonPath result;
    if (p.empty())
        result.p = name.path();
    else
        result.p = p / name.path();
    return result;
}

OsCanonPath operator/(const OsFilename & name, const OsCanonPath & path)
{
    if (path.p.empty()) {
        OsCanonPath result;
        result.p = name.path();
        return result;
    }
    OsCanonPath result;
    result.p = name.path() / path.p;
    return result;
}

OsCanonPath operator/(const OsFilename & a, const OsFilename & b)
{
    OsCanonPath result;
    result.p = a.path() / b.path();
    return result;
}

void OsCanonPath::pop()
{
    assert(!p.empty());
    /* `parent_path` drops the trailing component *and* the separator, which
       is exactly what our no-trailing-separator invariant wants. */
    p = p.parent_path();
}

std::optional<OsCanonPath> OsCanonPath::parent() const
{
    if (p.empty())
        return std::nullopt;
    auto res = *this;
    res.pop();
    return res;
}

bool OsCanonPath::isWithin(const OsCanonPath & prefix) const
{
    return std::ranges::mismatch(prefix.p, p).in1 == prefix.p.end();
}

OsCanonPath OsCanonPath::removePrefix(const OsCanonPath & prefix) const
{
    assert(isWithin(prefix));
    OsCanonPath res;
    auto it = begin();
    std::advance(it, std::ranges::distance(prefix));
    for (; it != end(); ++it)
        res = res / *it;
    return res;
}

CanonPath OsCanonPath::toPortable() const
{
    CanonPath res = CanonPath::root;
    for (const auto & component : *this)
        /* An `OsFilename` is also a valid portable file name. */
        res.push(component.toPortable());
    return res;
}

} // namespace nix

#include "nix/util/os-canon-path.hh"
#include "nix/util/os-string.hh"

#include <cassert>
#include <iterator>
#include <ranges>

namespace nix {

OsCanonPath::OsCanonPath(const CanonPath & canonPath)
{
    for (const auto & component : canonPath) {
        p /= std::filesystem::path{std::string{component}};
    }
    // No need to validate — CanonPath guarantees no empty/dot/dotdot components
}

void OsCanonPath::validate() const
{
    assert(!p.has_root_path() && "OsCanonPath cannot have a root path");
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
    auto it = p.begin();
    const auto itEnd = p.end();
    for (auto pIt = prefix.p.begin(), pEnd = prefix.p.end(); pIt != pEnd; ++it, ++pIt) {
        if (it == itEnd)
            return false;
        /* Copy one side out before comparing. `std::filesystem::path::iterator`
           dereferences to a reference into storage owned by the iterator, and
           holding two such references live across a single comparison is not
           portable — it compared equal for distinct same-length components on
           libc++. */
        const std::filesystem::path component = *it;
        if (component != *pIt)
            return false;
    }
    return true;
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
        res.push(component.path().string());
    return res;
}

} // namespace nix

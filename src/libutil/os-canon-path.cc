#include "nix/util/os-canon-path.hh"

#include <cassert>

namespace nix {

OsCanonPath::OsCanonPath(const CanonPath & canonPath)
{
    for (const auto & component : canonPath) {
#ifdef _WIN32
        assert(component.find('\\') == component.npos && "CanonPath component cannot contain backslash");
#endif
        p /= std::filesystem::path{std::string{component}};
    }
    // No need to further validate — CanonPath guarantees no empty/dot/dotdot components,
    // and operator/ produces preferred separators.
}

void OsCanonPath::validate() const
{
    assert(!p.has_root_path() && "OsCanonPath cannot have a root path");
#ifdef _WIN32
    assert(p.native().find(L'/') == std::wstring::npos && "OsCanonPath must use preferred (backslash) separators on Windows");
#endif
    for (const auto & component : p) {
        auto s = component.string();
        assert(!s.empty() && "OsCanonPath cannot have empty components");
        assert(s != "." && "OsCanonPath cannot have '.' components");
        assert(s != ".." && "OsCanonPath cannot have '..' components");
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

} // namespace nix

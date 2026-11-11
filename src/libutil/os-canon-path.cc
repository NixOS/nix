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
#ifdef _WIN32
    OsString res;
    res.reserve(canonPath.rel().size());
    bool first = true;
    for (const auto & component : canonPath) {
        /* TODO: Maybe don't fail with an assert here? */
        assert(!component.contains('\\'));
        if (!first)
            res += std::filesystem::path::preferred_separator;
        res += string_to_os_string(component);
        first = false;
    }
    p = res;
#else
    /* Everything is already as we expect it. */
    p = canonPath.rel();
#endif
}

void OsCanonPath::validate() const
{
#ifdef _WIN32
    assert(!p.has_root_path());
    assert(p.native().find(L'/') == std::wstring::npos);
#else
    assert(!p.native().starts_with('/'));
#endif
    for (const auto & component : p) {
        const auto & s = component.native();
        assert(!s.empty());
        assert(s != OS_STR("."));
        assert(s != OS_STR(".."));
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
    auto p2 = p.parent_path();
    return OsCanonPath(std::move(p2));
}

bool OsCanonPath::isWithin(const OsCanonPath & prefix) const
{
    return std::ranges::mismatch(prefix.p, p).in1 == prefix.p.end();
}

OsCanonPath OsCanonPath::removePrefix(const OsCanonPath & prefix) const
{
    if (prefix.empty())
        return *this;
    assert(isWithin(prefix));
    OsStringView prefix2 = prefix.p.native();
    OsStringView p2 = p.native();
    assert(prefix2.size() <= p2.size());
    if (prefix2.size() == p2.size())
        return OsCanonPath();
    OsStringView res = OsStringView(p2).substr(prefix2.size() + 1);
    return OsCanonPath(std::filesystem::path(res));
}

CanonPath OsCanonPath::toPortable() const
{
    CanonPath res = CanonPath::root;
    for (const auto & component : *this)
        /* FIXME: Lossy conversion. */
        res.push(component.path().string());
    return res;
}

} // namespace nix

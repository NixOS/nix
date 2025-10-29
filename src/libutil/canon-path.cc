#include "nix/util/canon-path.hh"
#include "nix/util/util.hh"
#include "nix/util/file-path-impl.hh"
#include "nix/util/strings-inline.hh"

#include <cstring>

namespace nix {

const CanonPath CanonPath::root = CanonPath("/");

static std::string absPathPure(std::string_view path)
{
    return canonPathInner<UnixPathTrait>(path, [](auto &, auto &) {});
}

static void ensureNoNullBytes(std::string_view s)
{
    if (std::memchr(s.data(), '\0', s.size())) [[unlikely]] {
        using namespace std::string_view_literals;
        auto str = replaceStrings(std::string(s), "\0"sv, "␀"sv);
        throw BadCanonPath("path segment '%s' must not contain null (\\0) bytes", str);
    }
}

CanonPath::CanonPath(std::string_view raw)
    : path(absPathPure(concatStrings("/", raw)))
{
    ensureNoNullBytes(raw);
}

CanonPath::CanonPath(const char * raw)
    : path(absPathPure(concatStrings("/", raw)))
{
}

CanonPath::CanonPath(std::string_view raw, const CanonPath & root)
    : path(absPathPure(raw.size() > 0 && raw[0] == '/' ? raw : concatStrings(root.abs(), "/", raw)))
{
    ensureNoNullBytes(raw);
}

CanonPath::CanonPath(const std::vector<std::string> & elems)
    : path("/")
{
    for (auto & s : elems)
        push(s);
}

std::optional<CanonPath> CanonPath::parent() const
{
    if (isRoot())
        return std::nullopt;
    return CanonPath(unchecked_t(), path.substr(0, std::max((size_t) 1, path.rfind('/'))));
}

void CanonPath::pop()
{
    assert(!isRoot());
    path.resize(std::max((size_t) 1, path.rfind('/')));
}

bool CanonPath::isWithin(const CanonPath & parent) const
{
    return !(
        path.size() < parent.path.size() || path.substr(0, parent.path.size()) != parent.path
        || (parent.path.size() > 1 && path.size() > parent.path.size() && path[parent.path.size()] != '/'));
}

CanonPath CanonPath::removePrefix(const CanonPath & prefix) const
{
    assert(isWithin(prefix));
    if (prefix.isRoot())
        return *this;
    if (path.size() == prefix.path.size())
        return root;
    return CanonPath(unchecked_t(), path.substr(prefix.path.size()));
}

void CanonPath::extend(const CanonPath & x)
{
    if (x.isRoot())
        return;
    if (isRoot())
        path += x.rel();
    else
        path += x.abs();
}

CanonPath CanonPath::operator/(const CanonPath & x) const
{
    auto res = *this;
    res.extend(x);
    return res;
}

void CanonPath::push(std::string_view c)
{
    assert(c.find('/') == c.npos);
    assert(c != "." && c != "..");
    ensureNoNullBytes(c);
    if (!isRoot())
        path += '/';
    path += c;
}

CanonPath CanonPath::operator/(std::string_view c) const
{
    auto res = *this;
    res.push(c);
    return res;
}

bool CanonPath::isAllowed(const std::set<CanonPath> & allowed) const
{
    /* Check if `this` is an exact match or the parent of an
       allowed path. */
    auto lb = allowed.lower_bound(*this);
    if (lb != allowed.end()) {
        if (lb->isWithin(*this))
            return true;
    }

    /* Check if a parent of `this` is allowed. */
    auto path = *this;
    while (!path.isRoot()) {
        path.pop();
        if (allowed.count(path))
            return true;
    }

    return false;
}

std::ostream & operator<<(std::ostream & stream, const CanonPath & path)
{
    stream << path.abs();
    return stream;
}

std::string CanonPath::makeRelative(const CanonPath & path) const
{
    auto p1 = begin();
    auto p2 = path.begin();

    for (; p1 != end() && p2 != path.end() && *p1 == *p2; ++p1, ++p2)
        ;

    if (p1 == end() && p2 == path.end())
        return ".";
    else if (p1 == end())
        return std::string(p2.remaining);
    else {
        std::string res;
        while (p1 != end()) {
            ++p1;
            if (!res.empty())
                res += '/';
            res += "..";
        }
        if (p2 != path.end()) {
            if (!res.empty())
                res += '/';
            res += p2.remaining;
        }
        return res;
    }
}

} // namespace nix

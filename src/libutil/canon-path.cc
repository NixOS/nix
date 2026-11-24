#include "nix/util/canon-path.hh"
#include "nix/util/path-iterator.hh"

#include <algorithm>
#include <cstring>

namespace nix {

void BadCanonPath::anchor() {}

const CanonPath CanonPath::root = CanonPath::fromUnchecked(UnresolvedPath::fromUnchecked("/"));

CanonPath CanonPath::fromFilename(std::string_view segment)
{
    return FilenameView::fromStringThrowing(segment);
}

namespace canon_path {

/**
 * Whether @p path has any `..` components. Without any, the rest of
 * `CanonPath`'s invariants are `UnresolvedPath`'s.
 */
static bool hasDotDot(const UnresolvedPath & path)
{
    return std::ranges::any_of(path, [](std::string_view c) { return c == ".."; });
}

} // namespace canon_path

std::optional<CanonPath> CanonPath::maybeFromUnresolved(const UnresolvedPath & path)
{
    if (canon_path::hasDotDot(path))
        return std::nullopt;
    return CanonPath(path);
}

CanonPath CanonPath::fromAsserting(UnresolvedPath path)
{
    assert(!canon_path::hasDotDot(path));
    return CanonPath(std::move(path));
}

CanonPath CanonPath::fromThrowing(UnresolvedPath path)
{
    if (canon_path::hasDotDot(path))
        throw BadCanonPath("path '%s' must not contain '..' components", path.abs());
    return CanonPath(std::move(path));
}

CanonPath CanonPath::viaLexical(UnresolvedPath path)
{
    /* Remove each `..` along with the component before it, ignoring
       symlinks. The result is never longer, so it is written over
       `path` as that is read: `out` is the length of the result so far,
       which never gets ahead of `start`. */
    auto & s = path.path;
    size_t out = 0;
    for (size_t start = 1; start < s.size();) {
        auto end = std::min(s.find('/', start), s.size());
        auto len = end - start;
        if (std::string_view{s}.substr(start, len) == "..")
            /* Back to before the last component of the result, if any. */
            out = out == 0 ? 0 : s.rfind('/', out - 1);
        else {
            s[out] = '/';
            std::memmove(&s[out + 1], &s[start], len);
            out += 1 + len;
        }
        start = end + 1;
    }
    s.resize(std::max(out, (size_t) 1));
    /* With no `..`, the rest of `CanonPath`'s invariants are
       `UnresolvedPath`'s. */
    return CanonPath(std::move(path));
}

CanonPath::CanonPath(const std::vector<std::string> & elems)
    : CanonPath(UnresolvedPath::fromUnchecked("/"))
{
    for (auto & s : elems)
        push(FilenameView::fromStringThrowing(s));
}

std::optional<CanonPath> CanonPath::parent() const
{
    if (isRoot())
        return std::nullopt;
    auto & path = abs();
    return fromUnchecked(UnresolvedPath::fromUnchecked(path.substr(0, std::max((size_t) 1, path.rfind('/')))));
}

bool CanonPath::isWithin(const CanonPath & parent) const
{
    auto & path = abs();
    auto & parentPath = parent.abs();
    return !(
        path.size() < parentPath.size() || path.substr(0, parentPath.size()) != parentPath
        || (parentPath.size() > 1 && path.size() > parentPath.size() && path[parentPath.size()] != '/'));
}

CanonPath CanonPath::removePrefix(const CanonPath & prefix) const
{
    assert(isWithin(prefix));
    if (prefix.isRoot())
        return *this;
    if (abs().size() == prefix.abs().size())
        return root;
    return fromUnchecked(UnresolvedPath::fromUnchecked(abs().substr(prefix.abs().size())));
}

void CanonPath::extend(const CanonPath & x)
{
    if (x.isRoot())
        return;
    if (isRoot())
        unresolved.path += x.rel();
    else
        unresolved.path += x.abs();
}

CanonPath CanonPath::operator/(const CanonPath & x) const
{
    auto res = *this;
    res.extend(x);
    return res;
}

void CanonPath::push(FilenameView c)
{
    /* A file name is always a valid component. */
    unresolved.pushUnchecked(c.str());
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

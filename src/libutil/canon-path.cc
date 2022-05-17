#include "canon-path.hh"
#include "util.hh"

namespace nix {

CanonPath CanonPath::root = CanonPath("/");

CanonPath::CanonPath(std::string_view raw)
    : path(absPath((Path) raw, "/"))
{ }

CanonPath::CanonPath(std::string_view raw, const CanonPath & root)
    : path(absPath((Path) raw, root.abs()))
{ }

std::optional<CanonPath> CanonPath::parent() const
{
    if (isRoot()) return std::nullopt;
    return CanonPath(unchecked_t(), path.substr(0, path.rfind('/')));
}

void CanonPath::pop()
{
    assert(!isRoot());
    auto slash = path.rfind('/');
    path.resize(std::max((size_t) 1, slash));
}

CanonPath CanonPath::resolveSymlinks() const
{
    return CanonPath(unchecked_t(), canonPath(abs(), true));
}

bool CanonPath::isWithin(const CanonPath & parent) const
{
    return !(
        path.size() < parent.path.size()
        || path.substr(0, parent.path.size()) != parent.path
        || (parent.path.size() > 1 && path.size() > parent.path.size()
            && path[parent.path.size()] != '/'));
}

CanonPath CanonPath::removePrefix(const CanonPath & prefix) const
{
    assert(isWithin(prefix));
    if (prefix.isRoot()) return *this;
    if (path.size() == prefix.path.size()) return root;
    return CanonPath(unchecked_t(), path.substr(prefix.path.size()));
}

void CanonPath::extend(const CanonPath & x)
{
    if (x.isRoot()) return;
    if (isRoot())
        path += x.rel();
    else
        path += x.abs();
}

CanonPath CanonPath::operator + (const CanonPath & x) const
{
    auto res = *this;
    res.extend(x);
    return res;
}

void CanonPath::push(std::string_view c)
{
    assert(c.find('/') == c.npos);
    if (!isRoot()) path += '/';
    path += c;
}

CanonPath CanonPath::operator + (std::string_view c) const
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

std::ostream & operator << (std::ostream & stream, const CanonPath & path)
{
    stream << path.abs();
    return stream;
}

}

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

std::ostream & operator << (std::ostream & stream, const CanonPath & path)
{
    stream << path.abs();
    return stream;
}

}

#pragma once

#include <string>
#include <optional>
#include <cassert>
#include <iostream>
#include <set>

namespace nix {

/* A canonical representation of a path. It ensures the following:

   - It always starts with a slash.

   - It never ends with a slash, except if the path is "/".

   - A slash is never followed by a slash (i.e. no empty components).

   - There are no components equal to '.' or '..'.

   Note that the path does not need to correspond to an actually
   existing path, and there is no guarantee that symlinks are
   resolved.
*/
class CanonPath
{
    std::string path;

public:

    /* Construct a canon path from a non-canonical path. Any '.', '..'
       or empty components are removed. */
    CanonPath(std::string_view raw);

    explicit CanonPath(const char * raw)
        : CanonPath(std::string_view(raw))
    { }

    struct unchecked_t { };

    CanonPath(unchecked_t _, std::string path)
        : path(std::move(path))
    { }

    static CanonPath root;

    /* If `raw` starts with a slash, return
       `CanonPath(raw)`. Otherwise return a `CanonPath` representing
       `root + "/" + raw`. */
    CanonPath(std::string_view raw, const CanonPath & root);

    bool isRoot() const
    { return path.size() <= 1; }

    explicit operator std::string_view() const
    { return path; }

    const std::string & abs() const
    { return path; }

    /* Like abs(), but return an empty string if this path is
       '/'. Thus the returned string never ends in a slash. */
    const std::string & absOrEmpty() const
    {
        const static std::string epsilon;
        return isRoot() ? epsilon : path;
    }

    const char * c_str() const
    { return path.c_str(); }

    std::string_view rel() const
    { return ((std::string_view) path).substr(1); }

    struct Iterator
    {
        std::string_view remaining;
        size_t slash;

        Iterator(std::string_view remaining)
            : remaining(remaining)
            , slash(remaining.find('/'))
        { }

        bool operator != (const Iterator & x) const
        { return remaining.data() != x.remaining.data(); }

        const std::string_view operator * () const
        { return remaining.substr(0, slash); }

        void operator ++ ()
        {
            if (slash == remaining.npos)
                remaining = remaining.substr(remaining.size());
            else {
                remaining = remaining.substr(slash + 1);
                slash = remaining.find('/');
            }
        }
    };

    Iterator begin() const { return Iterator(rel()); }
    Iterator end() const { return Iterator(rel().substr(path.size() - 1)); }

    std::optional<CanonPath> parent() const;

    /* Remove the last component. Panics if this path is the root.  */
    void pop();

    std::optional<std::string_view> dirOf() const
    {
        if (isRoot()) return std::nullopt;
        return ((std::string_view) path).substr(0, path.rfind('/'));
    }

    std::optional<std::string_view> baseName() const
    {
        if (isRoot()) return std::nullopt;
        return ((std::string_view) path).substr(path.rfind('/') + 1);
    }

    bool operator == (const CanonPath & x) const
    { return path == x.path; }

    bool operator != (const CanonPath & x) const
    { return path != x.path; }

    /* Compare paths lexicographically except that path separators
       are sorted before any other character. That is, in the sorted order
       a directory is always followed directly by its children. For
       instance, 'foo' < 'foo/bar' < 'foo!'. */
    bool operator < (const CanonPath & x) const
    {
        auto i = path.begin();
        auto j = x.path.begin();
        for ( ; i != path.end() && j != x.path.end(); ++i, ++j) {
            auto c_i = *i;
            if (c_i == '/') c_i = 0;
            auto c_j = *j;
            if (c_j == '/') c_j = 0;
            if (c_i < c_j) return true;
            if (c_i > c_j) return false;
        }
        return i == path.end() && j != x.path.end();
    }

    /* Return true if `this` is equal to `parent` or a child of
       `parent`. */
    bool isWithin(const CanonPath & parent) const;

    CanonPath removePrefix(const CanonPath & prefix) const;

    /* Append another path to this one. */
    void extend(const CanonPath & x);

    /* Concatenate two paths. */
    CanonPath operator + (const CanonPath & x) const;

    /* Add a path component to this one. It must not contain any slashes. */
    void push(std::string_view c);

    CanonPath operator + (std::string_view c) const;

    /* Check whether access to this path is allowed, which is the case
       if 1) `this` is within any of the `allowed` paths; or 2) any of
       the `allowed` paths are within `this`. (The latter condition
       ensures access to the parents of allowed paths.) */
    bool isAllowed(const std::set<CanonPath> & allowed) const;
};

std::ostream & operator << (std::ostream & stream, const CanonPath & path);

}

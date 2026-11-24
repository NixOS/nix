#pragma once
///@file

#include <cassert>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace nix {

/**
 * Defined in `path-iterator.hh`, as are `UnresolvedPath::begin` and
 * `UnresolvedPath::end`.
 */
template<typename Component>
class BasicPathIterator;

class CanonPath;

/**
 * Like `CanonPath`, but may also contain `..` components:
 *
 * - It always starts with a slash.
 *
 * - It never ends with a slash, except if the path is "/".
 *
 * - A slash is never followed by a slash (i.e. no empty components).
 *
 * - There are no components equal to '.'.
 *
 * - It does not contain NUL bytes.
 *
 * `..` cannot in general be removed without looking at the file system,
 * because of symlinks: `link/..` is the parent of the link's target, not
 * the directory containing the link. See `SourceAccessor::resolvePath`, which
 * makes a `CanonPath` from this. A `CanonPath` is an `UnresolvedPath`
 * with no `..`.
 */
class UnresolvedPath
{
    std::string path;

    /**
     * Only here to ensure that users of the public constructors below never
     * resolve the private constructor by mistake.
     */
    struct Unchecked
    {};

    UnresolvedPath(Unchecked, std::string path)
        : path(std::move(path))
    {
    }

    /**
     * Like `push`, but without checking @p c, for when it is known to be
     * a valid component: not empty or `.`, and with no `/` or NUL.
     */
    void pushUnchecked(std::string_view c)
    {
        if (!isRoot())
            path += '/';
        path += c;
    }

    /* `CanonPath` holds an `UnresolvedPath`, and its `viaLexical`
       rewrites `path` in place. */
    friend class CanonPath;

public:

    /**
     * Construct from a non-canonical path. Any '.' or empty components
     * are removed, but '..' components are kept.
     */
    UnresolvedPath(std::string_view raw);

    /**
     * Like the constructor from `std::string_view`. Also there so that
     * a literal or a `std::string` converts implicitly, wherever an
     * `UnresolvedPath` is wanted.
     */
    UnresolvedPath(const char * raw)
        : UnresolvedPath(std::string_view{raw})
    {
    }

    /**
     * @copydoc UnresolvedPath(const char *)
     */
    UnresolvedPath(const std::string & raw)
        : UnresolvedPath(std::string_view{raw})
    {
    }

    /**
     * If @p raw starts with a slash, the same as the constructor from
     * `std::string_view`. Otherwise, @p raw relative to @p root, i.e.
     * `root + "/" + raw`. Like that constructor, any `..` components are
     * kept.
     */
    UnresolvedPath(std::string_view raw, const UnresolvedPath & root);

    /**
     * @p path as is, without checking it at all, for when it is known
     * to meet `UnresolvedPath`'s invariants.
     */
    static UnresolvedPath fromUnchecked(std::string path)
    {
        return UnresolvedPath(Unchecked{}, std::move(path));
    }

    /**
     * Whether this is `/`.
     */
    bool isRoot() const
    {
        return path.size() <= 1;
    }

    /**
     * The same as `abs`.
     */
    explicit operator std::string_view() const
    {
        return path;
    }

    /**
     * The path as an (Unix-style) absolute path string, i.e. starting with `/`.
     */
    const std::string & abs() const
    {
        return path;
    }

    /**
     * Like abs(), but return an empty string if this path is
     * '/'. Thus the returned string never ends in a slash.
     */
    const std::string & absOrEmpty() const
    {
        static const std::string epsilon;
        return isRoot() ? epsilon : path;
    }

    /**
     * Like `abs`, as a NUL-terminated C string.
     */
    const char * c_str() const
    {
        return path.c_str();
    }

    /**
     * The path as a (Unix-style) relative path string, i.e. *not* starting with `/`.
     *
     * That means that `CanonPath::root.rel() == ""`. Other non-root paths
     * start with their first path component.
     */
    std::string_view rel() const
    {
        return ((std::string_view) path).substr(1);
    }

    /**
     * Like `rel`, as a NUL-terminated C string.
     */
    const char * rel_c_str() const
    {
        auto cs = path.c_str();
        assert(cs[0]); // for safety if invariant is broken
        return &cs[1];
    }

    using Iterator = BasicPathIterator<std::string_view>;

    /**
     * Defined in `path-iterator.hh`. Make sure to include that header when using this method.
     */
    inline Iterator begin() const;

    /**
     * Defined in `path-iterator.hh`. Make sure to include that header when using this method.
     */
    inline Iterator end() const;

    /**
     * Everything but the last component, or `std::nullopt` for the
     * root. Unlike `abs`, the root itself is empty, not `/`.
     */
    std::optional<std::string_view> dirOf() const
    {
        if (isRoot())
            return std::nullopt;
        return ((std::string_view) path).substr(0, path.rfind('/'));
    }

    /**
     * The last component, or `std::nullopt` for the root.
     */
    std::optional<std::string_view> baseName() const
    {
        if (isRoot())
            return std::nullopt;
        return ((std::string_view) path).substr(path.rfind('/') + 1);
    }

    /**
     * Add a path component to this one. It must not contain any
     * slashes, or be `.`, but may be `..`.
     */
    void push(std::string_view c);

    /**
     * Remove the last component, whatever it is (even `..`). Panics if
     * this path is the root.
     */
    void pop();

    UnresolvedPath operator/(std::string_view c) const;

    bool operator==(const UnresolvedPath & x) const = default;

    /**
     * Print `abs`. A hidden friend, found only for an actual
     * `UnresolvedPath`, so that other `<<`s (e.g. of JSON) do not need
     * to look at whether they convert to one.
     */
    friend std::ostream & operator<<(std::ostream & stream, const UnresolvedPath & path);
};

} // namespace nix

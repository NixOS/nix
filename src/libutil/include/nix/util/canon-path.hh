#pragma once
///@file

#include "nix/util/error.hh"
#include "nix/util/filename.hh"
#include "nix/util/unresolved-path.hh"

#include <iosfwd>
#include <string>
#include <optional>
#include <cassert>
#include <set>
#include <vector>
#include <type_traits>

#include <boost/container_hash/hash.hpp>

namespace nix {

MakeError(BadCanonPath, Error);

/**
 * A canonical representation of a path. It ensures the following:
 *
 * - It always starts with a slash.
 *
 * - It never ends with a slash, except if the path is "/".
 *
 * - A slash is never followed by a slash (i.e. no empty components).
 *
 * - There are no components equal to '.' or '..'.
 *
 * - It does not contain NUL bytes.
 *
 * `CanonPath` are "virtual" Nix paths for abstract file system objects;
 * they are always Unix-style paths, regardless of what OS Nix is
 * running on. The `/` root doesn't denote the ambient host file system
 * root, but some virtual FS root.
 *
 * @note It might be useful to compare `openat(some_fd, "foo/bar")` on
 * Unix. `"foo/bar"` is a relative path because an absolute path would
 * "override" the `some_fd` directory file descriptor and escape to the
 * "system root". Conversely, Nix's abstract file operations *never* escape the
 * designated virtual file system (i.e. `SourceAccessor` or
 * `ParseSink`), so `CanonPath` does not need an absolute/relative
 * distinction.
 *
 * @note The path does not need to correspond to an actually existing
 * path, and the path may or may not have unresolved symlinks.
 *
 * A `CanonPath` holds an `UnresolvedPath`, as which it can be used,
 * but only as a `const` reference, so that `..` cannot be pushed onto
 * it that way.
 */
class CanonPath
{
    UnresolvedPath unresolved;

    /**
     * Does not validate; only for callers that already know the
     * invariants hold. Use `fromUnchecked` from outside.
     */
    explicit CanonPath(UnresolvedPath path)
        : unresolved(std::move(path))
    {
    }

public:

    /**
     * Construct a canon path from a vector of elements.
     */
    CanonPath(const std::vector<std::string> & elems);

    /**
     * `/` and @p name, a path of one component. Implicit, as a file name
     * always is a valid one, so this needs no checking.
     */
    CanonPath(FilenameView name)
        : CanonPath(UnresolvedPath::fromUnchecked(std::string{"/"}.append(name.str())))
    {
    }

    static const CanonPath root;

    /**
     * This path as an `UnresolvedPath`, which it always also is. Only
     * `const`, so that its invariants cannot be broken through it.
     */
    operator const UnresolvedPath &() const & noexcept
    {
        return unresolved;
    }

    /**
     * Like the other conversion, but moving out of a temporary, so that
     * a reference bound to the result cannot outlive it.
     */
    operator UnresolvedPath() && noexcept
    {
        return std::move(unresolved);
    }

    /** @see UnresolvedPath::isRoot */
    bool isRoot() const
    {
        return unresolved.isRoot();
    }

    /** @see UnresolvedPath::operator std::string_view */
    explicit operator std::string_view() const
    {
        return unresolved.abs();
    }

    /** @see UnresolvedPath::abs */
    const std::string & abs() const
    {
        return unresolved.abs();
    }

    /** @see UnresolvedPath::absOrEmpty */
    const std::string & absOrEmpty() const
    {
        return unresolved.absOrEmpty();
    }

    /** @see UnresolvedPath::c_str */
    const char * c_str() const
    {
        return unresolved.c_str();
    }

    /** @see UnresolvedPath::rel */
    std::string_view rel() const
    {
        return unresolved.rel();
    }

    /** @see UnresolvedPath::rel_c_str */
    const char * rel_c_str() const
    {
        return unresolved.rel_c_str();
    }

    /**
     * Unlike `UnresolvedPath`'s, the components are always valid file
     * names.
     */
    using Iterator = BasicPathIterator<FilenameView>;

    /**
     * Defined in `path-iterator.hh`. Make sure to include that header when using this method.
     */
    inline Iterator begin() const;

    /**
     * Defined in `path-iterator.hh`. Make sure to include that header when using this method.
     */
    inline Iterator end() const;

    /** @see UnresolvedPath::dirOf */
    std::optional<std::string_view> dirOf() const
    {
        return unresolved.dirOf();
    }

    /**
     * Like `UnresolvedPath::baseName`, but as a `FilenameView`, which
     * the last component always is.
     */
    std::optional<FilenameView> baseName() const
    {
        if (isRoot())
            return std::nullopt;
        auto & s = abs();
        return FilenameView{std::string_view{s}.substr(s.rfind('/') + 1)};
    }

    /**
     * Construct a CanonPath from a single path segment. The segment
     * must not contain any slashes and must not be `.` or `..`: it must
     * be a valid `Filename`, or a `BadFilename` is thrown.
     */
    static CanonPath fromFilename(std::string_view segment);

    /**
     * @p path as a `CanonPath`, if it has no `..` components, or else
     * `std::nullopt`. Unlike `viaLexical`, this never
     * removes `..` lexically.
     */
    static std::optional<CanonPath> maybeFromUnresolved(const UnresolvedPath & path);

    /**
     * @p path as is, without checking that it has no `..` components,
     * for when that is known. To skip `UnresolvedPath`'s checks too, give
     * it an `UnresolvedPath::fromUnchecked`.
     */
    static CanonPath fromUnchecked(UnresolvedPath path)
    {
        return CanonPath(std::move(path));
    }

    /**
     * @p path as a `CanonPath`, asserting that it has no `..`
     * components, for when that is known.
     */
    static CanonPath fromAsserting(UnresolvedPath path);

    /**
     * @p path as a `CanonPath`, throwing a `BadCanonPath` if it has any
     * `..` components, for paths from outside that should not have
     * them, rather than removing them lexically like `viaLexical`.
     */
    static CanonPath fromThrowing(UnresolvedPath path);

    /**
     * @p path with any `..` removed lexically: each removes the
     * component before it, ignoring symlinks, so `link/..` is the
     * directory containing the link, not the parent of its target.
     *
     * You almost certainly don't want this. If you know there are no `..`, you
     * should assert at compile time or runtime that this is the case. Otherwise,
     * you want to use `UnresolvedPath` and resolve it with
     * `SourceAccessor::resolvePath`.
     *
     * If you know there are `..` but no symlinks this is safe to use, but it is
     * rare to know there are no symlinks.
     *
     * The most common use-case for this is actually in the Nix language
     * evaluator, where unfortunately we did misguided lexical
     * canonicalization in the past, and now are stuck with it for back
     * compat. (Though there can and should be some sort of opt-out.)
     */
    static CanonPath viaLexical(UnresolvedPath path);

    std::optional<CanonPath> parent() const;

    bool operator==(const CanonPath & x) const
    {
        return abs() == x.abs();
    }

    bool operator!=(const CanonPath & x) const
    {
        return abs() != x.abs();
    }

    /**
     * Compare paths lexicographically except that path separators
     * are sorted before any other character. That is, in the sorted order
     * a directory is always followed directly by its children. For
     * instance, 'foo' < 'foo/bar' < 'foo!'.
     */
    auto operator<=>(const CanonPath & x) const
    {
        auto & path = abs();
        auto & xPath = x.abs();
        auto i = path.begin();
        auto j = xPath.begin();
        for (; i != path.end() && j != xPath.end(); ++i, ++j) {
            /* Unsigned, so that `/` (mapped to 0) sorts before every
               other byte, including non-ASCII ones. Otherwise we
               violate the spec written above.

               Null bytes are already disallowed so we don't need to worry
               about '\0' and '/' comparisons. */
            unsigned char c_i = *i;
            if (c_i == '/')
                c_i = 0;
            unsigned char c_j = *j;
            if (c_j == '/')
                c_j = 0;
            if (auto cmp = c_i <=> c_j; cmp != 0)
                return cmp;
        }
        return (i != path.end()) <=> (j != xPath.end());
    }

    /**
     * Return true if `this` is equal to `parent` or a child of
     * `parent`.
     */
    bool isWithin(const CanonPath & parent) const;

    CanonPath removePrefix(const CanonPath & prefix) const;

    /**
     * Append another path to this one.
     */
    void extend(const CanonPath & x);

    /**
     * Concatenate two paths.
     */
    CanonPath operator/(const CanonPath & x) const;

    /**
     * Add a path component to this one.
     */
    void push(FilenameView c);

    /** @see UnresolvedPath::pop */
    void pop()
    {
        unresolved.pop();
    }

    CanonPath operator/(FilenameView c) const
    {
        auto res = *this;
        res.push(c);
        return res;
    }

    /**
     * Check whether access to this path is allowed, which is the case
     * if 1) `this` is within any of the `allowed` paths; or 2) any of
     * the `allowed` paths are within `this`. (The latter condition
     * ensures access to the parents of allowed paths.)
     */
    bool isAllowed(const std::set<CanonPath> & allowed) const;

    /**
     * Return a representation `x` of `path` relative to `this`, i.e.
     * `CanonPath::viaLexical(UnresolvedPath(this.makeRelative(x), *this)) == path`.
     * That has to remove the `..` lexically, as it is relative to `this`
     * as a path, regardless of any symlinks.
     */
    std::string makeRelative(const CanonPath & path) const;

    /**
     * Print `abs`. A hidden friend, like `UnresolvedPath`'s.
     */
    friend std::ostream & operator<<(std::ostream & stream, const CanonPath & path);
};

inline std::size_t hash_value(const CanonPath & path)
{
    boost::hash<std::string_view> hasher;
    return hasher(path.abs());
}

/**
 * A string literal for `operator""_canonPath`.
 */
template<size_t N>
struct CanonPathLiteral
{
    char data[N];

    consteval CanonPathLiteral(const char (&s)[N])
    {
        static_assert(N > 0);
        std::copy_n(s, N, data);
    }

    constexpr std::string_view view() const
    {
        return {data, N - 1};
    }

    /**
     * Whether this is already a valid `CanonPath`: `/`, or `/` followed
     * by valid `Filename`s separated by `/`. Unlike `viaLexical`, nothing
     * is normalised: e.g. `/a/./b`, `/a//b` and `/a/` are just invalid.
     */
    constexpr bool valid() const
    {
        auto s = view();
        if (!s.starts_with('/'))
            return false;
        if (s.size() == 1)
            return true;
        s.remove_prefix(1);
        while (true) {
            auto slash = s.find('/');
            if (BadFilename::whyInvalid(s.substr(0, slash)))
                return false;
            if (slash == s.npos)
                return true;
            s.remove_prefix(slash + 1);
        }
    }
};

/**
 * A `CanonPath` from a string literal that is already canonical,
 * checked at compile time, like `operator""_filename`. For example,
 * `"/foo/bar"_canonPath`.
 *
 * Not `consteval`, since a `CanonPath` owns its string: only the check
 * is at compile time, and at runtime the literal is just copied, with
 * no parsing.
 */
template<CanonPathLiteral L>
    requires(L.valid())
CanonPath operator""_canonPath()
{
    return CanonPath::fromUnchecked(UnresolvedPath::fromUnchecked(std::string{L.view()}));
}

} // namespace nix

template<>
struct std::hash<nix::CanonPath>
{
    using is_avalanching = std::true_type;

    std::size_t operator()(const nix::CanonPath & path) const noexcept
    {
        return nix::hash_value(path);
    }
};

#include <exception> // IWYU pragma: keep (Needed by rapidcheck on Darwin and FreeBSD)

#include "nix/util/canon-path.hh"
#include "nix/util/file-path-impl.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/util/path-iterator.hh"
#include "nix/util/unresolved-path.hh"
#include "nix/util/tests/canon-path.hh"
#include "nix/util/tests/unresolved-path.hh"

#include <rapidcheck/gtest.h>

#include <algorithm>
#include <deque>
#include <ranges>
#include <string>
#include <vector>

namespace nix {

/* Property tests of `UnresolvedPath`, the ways of making a `CanonPath`
   from one, and `SourceAccessor::resolvePath`, each against a simple
   reference implementation. */

namespace canon_path_properties {

using nix::testing::rawPath;

/**
 * The components of @p raw that are not empty or `.`, which is all the
 * normalisation `UnresolvedPath` does.
 */
static std::vector<std::string> safelyNormalised(std::string_view raw)
{
    std::vector<std::string> res;
    for (auto c : std::views::split(raw, '/')) {
        std::string_view s{c.begin(), c.end()};
        if (!s.empty() && s != ".")
            res.emplace_back(s);
    }
    return res;
}

static std::string absOf(const std::vector<std::string> & components)
{
    std::string res = "/";
    for (size_t i = 0; i < components.size(); ++i) {
        if (i)
            res += '/';
        res += components[i];
    }
    return res;
}

static bool hasDotDot(const std::vector<std::string> & components)
{
    return std::ranges::find(components, "..") != components.end();
}

/**
 * Remove each `..` along with the component before it, ignoring
 * symlinks, the obvious way.
 */
static std::vector<std::string> lexically(const std::vector<std::string> & components)
{
    std::vector<std::string> res;
    for (auto & c : components) {
        if (c != "..")
            res.push_back(c);
        else if (!res.empty())
            res.pop_back();
    }
    return res;
}

RC_GTEST_PROP(UnresolvedPathProperty, normalisesOnlySafely, ())
{
    auto raw = *rawPath({"a", "b"});
    auto expected = safelyNormalised(raw);

    UnresolvedPath p{raw};
    RC_ASSERT(p.abs() == absOf(expected));
    RC_ASSERT(std::vector<std::string>(p.begin(), p.end()) == expected);
}

RC_GTEST_PROP(CanonPathProperty, improperlyResolveLexicallyFrom, ())
{
    auto raw = *rawPath({"a", "b"});
    auto expected = absOf(lexically(safelyNormalised(raw)));

    RC_ASSERT(CanonPath::improperlyResolveLexicallyFrom(raw).abs() == expected);

    /* The same whether the `UnresolvedPath` is borrowed, which leaves it
       as it was, or consumed, which rewrites it in place. */
    UnresolvedPath p{raw};
    auto before = p;
    RC_ASSERT(CanonPath::improperlyResolveLexicallyFrom(p).abs() == expected);
    RC_ASSERT(p == before);
    RC_ASSERT(CanonPath::improperlyResolveLexicallyFrom(std::move(p)).abs() == expected);
}

RC_GTEST_PROP(CanonPathProperty, improperlyResolveLexicallyLikeCanonPathInner, ())
{
    /* `canonPathInner`, which `canonPath` uses, also removes `..`
       lexically when not following symlinks, but returns the empty
       string for the root. */
    auto raw = *rawPath({"a", "b"});
    auto expected = canonPathInner<UnixPathTrait>("/" + raw, [](auto &, auto &) {});
    if (expected.empty())
        expected = "/";

    RC_ASSERT(CanonPath::improperlyResolveLexicallyFrom(raw).abs() == expected);
}

RC_GTEST_PROP(CanonPathProperty, fromUnresolvedOnlyWithoutDotDot, (const UnresolvedPath & p))
{
    auto components = std::vector<std::string>(p.begin(), p.end());

    auto maybe = CanonPath::maybeFromUnresolved(p);
    if (hasDotDot(components)) {
        RC_ASSERT(!maybe);
        RC_ASSERT_THROWS_AS(CanonPath::fromThrowing(p), BadCanonPath);
    } else {
        /* Never removed lexically: as is. */
        RC_ASSERT(maybe);
        RC_ASSERT(maybe->abs() == p.abs());
        RC_ASSERT(CanonPath::fromThrowing(p) == *maybe);
        RC_ASSERT(CanonPath::fromAsserting(p) == *maybe);
    }
}

RC_GTEST_PROP(CanonPathProperty, unchangedFromItsOwnUnresolvedPath, (const CanonPath & p))
{
    /* A `CanonPath` is an `UnresolvedPath` with no `..`, so making one
       from it again, by any means, gives it back as it was. */
    const UnresolvedPath & u = p;
    RC_ASSERT(UnresolvedPath(p.abs()) == u);
    RC_ASSERT(CanonPath::maybeFromUnresolved(u) == p);
    RC_ASSERT(CanonPath::fromThrowing(u) == p);
    RC_ASSERT(CanonPath::fromAsserting(u) == p);
    RC_ASSERT(CanonPath::improperlyResolveLexicallyFrom(u) == p);
}

/**
 * Some of every kind of symlink: to a directory, with `..` in its
 * target, absolute, to itself, and dangling.
 */
static ref<MemorySourceAccessor> treeWithSymlinks()
{
    auto accessor = make_ref<MemorySourceAccessor>();
    accessor->addDirectory("a"_filename);
    accessor->addDirectory("/a/b"_canonPath);
    accessor->addFile("/a/b/f"_canonPath, "");
    accessor->addFile("/a/f"_canonPath, "");
    accessor->addSymlink("l1"_filename, "a/b");
    accessor->addSymlink("/a/l2"_canonPath, "../a");
    accessor->addSymlink("/a/b/l3"_canonPath, "/a");
    accessor->addSymlink("l4"_filename, "l4");
    accessor->addSymlink("l5"_filename, "nonexistent/..");
    return accessor;
}

static const std::vector<std::string> treeNames{"a", "b", "f", "l1", "l2", "l3", "l4", "l5", "nonexistent"};

/**
 * Resolve @p components the way the OS would: one at a time, each `..`
 * going to the parent of wherever the path got to so far, following
 * every symlink, wherever it is. `std::nullopt` if that never ends.
 */
static std::optional<CanonPath> oneComponentAtATime(SourceAccessor & accessor, std::vector<std::string> components)
{
    std::deque<std::string> todo(components.begin(), components.end());
    auto res = CanonPath::root;
    size_t linksAllowed = 100;
    while (!todo.empty()) {
        auto c = std::move(todo.front());
        todo.pop_front();
        if (c == "..") {
            if (!res.isRoot())
                res.pop();
            continue;
        }
        res.push(FilenameView::fromStringAsserting(c));
        if (auto st = accessor.maybeLstat(res); st && st->type == SourceAccessor::tSymlink) {
            if (!linksAllowed--)
                return std::nullopt;
            auto target = accessor.readLink(res);
            if (target.starts_with('/'))
                res = CanonPath::root;
            else
                res.pop();
            auto targetComponents = safelyNormalised(target);
            todo.insert(todo.begin(), targetComponents.begin(), targetComponents.end());
        }
    }
    return res;
}

RC_GTEST_PROP(ResolvePathProperty, thenResolveSymlinksIsSound, ())
{
    auto accessor = treeWithSymlinks();
    auto raw = *rawPath(treeNames);
    UnresolvedPath p{raw};

    /* As the documentation of `resolvePath` says, resolving the rest of
       its result is the same as resolving the path one component at a
       time, `..` and all. */
    if (auto expected = oneComponentAtATime(*accessor, safelyNormalised(raw)))
        RC_ASSERT(accessor->resolveSymlinks(accessor->resolvePath(p)) == *expected);
    else
        RC_ASSERT_THROWS_AS(accessor->resolveSymlinks(accessor->resolvePath(p)), Error);
}

RC_GTEST_PROP(ResolvePathProperty, onlyAsMuchAsNeeded, ())
{
    auto accessor = treeWithSymlinks();
    auto raw = *rawPath(treeNames);
    auto components = safelyNormalised(raw);
    UnresolvedPath p{raw};

    auto lastDotDot = std::ranges::find(components.rbegin(), components.rend(), "..");
    if (lastDotDot == components.rend()) {
        /* Without any `..`, nothing is resolved. */
        RC_ASSERT(accessor->resolvePath(p) == CanonPath::fromAsserting(p));
        return;
    }

    /* Everything after the last `..` is kept as given, symlinks or not,
       so the result ends with it. Anything up to it might be a loop, in
       which case there is no result. */
    std::vector<std::string> rest(lastDotDot.base(), components.end());
    std::optional<CanonPath> res;
    try {
        res = accessor->resolvePath(p);
    } catch (Error &) {
        RC_SUCCEED("the components up to the last `..` loop");
    }
    std::vector<std::string> resComponents;
    for (auto c : *res)
        resComponents.emplace_back(c.str());
    RC_ASSERT(resComponents.size() >= rest.size());
    RC_ASSERT(std::equal(rest.begin(), rest.end(), resComponents.end() - rest.size()));
}

RC_GTEST_PROP(ResolvePathProperty, prefixIdempotent, ())
{
    auto accessor = treeWithSymlinks();
    auto rawA = *rawPath(treeNames);
    auto rawB = *rawPath(treeNames);

    /* Resolving fully, `..` and symlinks alike. */
    auto f = [&](const UnresolvedPath & p) -> std::optional<CanonPath> {
        try {
            return accessor->resolveSymlinks(accessor->resolvePath(p));
        } catch (Error &) {
            return std::nullopt;
        }
    };

    auto fa = f(UnresolvedPath(rawA));
    if (!fa)
        RC_DISCARD("the first part loops");

    /* Resolving a prefix first changes nothing: `f(f(a)/b) == f(a/b)`. */
    RC_ASSERT(f(UnresolvedPath(fa->abs() + "/" + rawB)) == f(UnresolvedPath("/" + rawA + "/" + rawB)));
}

} // namespace canon_path_properties

} // namespace nix

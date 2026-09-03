#include "nix/util/canon-path.hh"
#include "nix/util/os-canon-path.hh"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace nix {

/**
 * `CanonPath` and `OsCanonPath` implement the same algebra over sequences
 * of filename components; they differ only in representation (a flat
 * `/`-joined string versus a `std::filesystem::path`) and in how the
 * identity element is spelled (the root `/` versus the empty path).
 *
 * These traits paper over that difference so the laws can be stated once
 * and checked against both. Per-type behaviour that genuinely differs
 * (input normalization, validation, display) is tested in `canon-path.cc`
 * and `os-canon-path.cc` respectively.
 */
template<typename T>
struct CanonicalPathTraits
{};

template<>
struct CanonicalPathTraits<CanonPath>
{
    static CanonPath identity()
    {
        return CanonPath::root;
    }

    static bool isIdentity(const CanonPath & p)
    {
        return p.isRoot();
    }

    static CanonPath parse(std::string_view s)
    {
        return CanonPath(s);
    }

    static std::vector<std::string> components(const CanonPath & p)
    {
        std::vector<std::string> res;
        for (auto & c : p)
            res.emplace_back(c);
        return res;
    }
};

template<>
struct CanonicalPathTraits<OsCanonPath>
{
    static OsCanonPath identity()
    {
        return OsCanonPath{};
    }

    static bool isIdentity(const OsCanonPath & p)
    {
        return p.empty();
    }

    /**
     * Deliberately built up with `operator/` rather than by handing a
     * `"foo/bar"` string to `std::filesystem::path`, so that the tests
     * exercise native separators on every platform.
     */
    static OsCanonPath parse(std::string_view s)
    {
        OsCanonPath res;
        size_t pos = 0;
        while (pos < s.size()) {
            auto end = s.find('/', pos);
            if (end == s.npos)
                end = s.size();
            if (end != pos)
                res = res / OsFilename{std::filesystem::path{std::string{s.substr(pos, end - pos)}}};
            pos = end + 1;
        }
        return res;
    }

    static std::vector<std::string> components(const OsCanonPath & p)
    {
        std::vector<std::string> res;
        for (const auto & c : p)
            res.push_back(c.path().string());
        return res;
    }
};

template<typename T>
class CanonicalPath : public ::testing::Test
{
public:
    using Traits = CanonicalPathTraits<T>;

    static T parse(std::string_view s)
    {
        return Traits::parse(s);
    }

    static T identity()
    {
        return Traits::identity();
    }
};

using CanonicalPathTypes = ::testing::Types<CanonPath, OsCanonPath>;
TYPED_TEST_SUITE(CanonicalPath, CanonicalPathTypes);

TYPED_TEST(CanonicalPath, parseRoundTripsThroughComponents)
{
    using Traits = CanonicalPathTraits<TypeParam>;
    ASSERT_EQ(Traits::components(TestFixture::parse("")), (std::vector<std::string>{}));
    ASSERT_EQ(Traits::components(TestFixture::parse("foo")), (std::vector<std::string>{"foo"}));
    ASSERT_EQ(Traits::components(TestFixture::parse("a/foo/bar")), (std::vector<std::string>{"a", "foo", "bar"}));
}

TYPED_TEST(CanonicalPath, identityIsEmpty)
{
    using Traits = CanonicalPathTraits<TypeParam>;
    ASSERT_TRUE(Traits::isIdentity(TestFixture::identity()));
    ASSERT_TRUE(Traits::isIdentity(TestFixture::parse("")));
    ASSERT_TRUE(Traits::components(TestFixture::identity()).empty());
    ASSERT_FALSE(Traits::isIdentity(TestFixture::parse("foo")));
}

TYPED_TEST(CanonicalPath, concatIdentity)
{
    auto p = TestFixture::parse("foo/bar");
    ASSERT_EQ(TestFixture::identity() / p, p);
    ASSERT_EQ(p / TestFixture::identity(), p);
}

TYPED_TEST(CanonicalPath, concatAppendsComponents)
{
    ASSERT_EQ(TestFixture::parse("a/foo") / TestFixture::parse("xyzzy/bla"), TestFixture::parse("a/foo/xyzzy/bla"));
}

TYPED_TEST(CanonicalPath, concatAssociative)
{
    auto a = TestFixture::parse("a/b");
    auto b = TestFixture::parse("c");
    auto c = TestFixture::parse("d/e");
    ASSERT_EQ((a / b) / c, a / (b / c));
}

TYPED_TEST(CanonicalPath, popUndoesConcat)
{
    auto p = TestFixture::parse("foo/bar");
    auto q = p / TestFixture::parse("x");
    q.pop();
    ASSERT_EQ(q, p);
}

TYPED_TEST(CanonicalPath, popToIdentity)
{
    using Traits = CanonicalPathTraits<TypeParam>;
    auto p = TestFixture::parse("foo/bar/x");
    p.pop();
    ASSERT_EQ(p, TestFixture::parse("foo/bar"));
    p.pop();
    ASSERT_EQ(p, TestFixture::parse("foo"));
    p.pop();
    ASSERT_TRUE(Traits::isIdentity(p));
}

TYPED_TEST(CanonicalPath, parentAgreesWithPop)
{
    auto p = TestFixture::parse("foo/bar/x");
    auto popped = p;
    popped.pop();
    ASSERT_EQ(p.parent(), popped);
    /* `parent` is not in-place. */
    ASSERT_EQ(p, TestFixture::parse("foo/bar/x"));
    ASSERT_EQ(TestFixture::identity().parent(), std::nullopt);
}

TYPED_TEST(CanonicalPath, isWithinReflexiveAndIdentity)
{
    auto p = TestFixture::parse("foo/bar");
    ASSERT_TRUE(p.isWithin(p));
    ASSERT_TRUE(p.isWithin(TestFixture::identity()));
    ASSERT_TRUE(TestFixture::identity().isWithin(TestFixture::identity()));
}

TYPED_TEST(CanonicalPath, isWithinFollowsConcat)
{
    auto p = TestFixture::parse("foo");
    ASSERT_TRUE((p / TestFixture::parse("bar/baz")).isWithin(p));
    ASSERT_FALSE(p.isWithin(p / TestFixture::parse("bar")));
}

TYPED_TEST(CanonicalPath, isWithinIsComponentwiseNotTextual)
{
    /* "fo" is a prefix of the *string* "foo", but not of the *path*. */
    ASSERT_FALSE(TestFixture::parse("foo").isWithin(TestFixture::parse("fo")));
    ASSERT_FALSE(TestFixture::parse("foo").isWithin(TestFixture::parse("bar")));
}

TYPED_TEST(CanonicalPath, removePrefixInvertsConcat)
{
    auto p = TestFixture::parse("foo/bar");
    auto q = TestFixture::parse("a/b/c");
    ASSERT_EQ((p / q).removePrefix(p), q);
    ASSERT_EQ(p.removePrefix(p), TestFixture::identity());
    ASSERT_EQ(p.removePrefix(TestFixture::identity()), p);
}

TYPED_TEST(CanonicalPath, orderingIsComponentwise)
{
    ASSERT_FALSE(TestFixture::parse("foo") < TestFixture::parse("foo"));
    ASSERT_TRUE(TestFixture::parse("foo") < TestFixture::parse("foo/bar"));
    ASSERT_TRUE(TestFixture::identity() < TestFixture::parse("foo"));
}

/* --------------------------------------------------------------------------
 * Laws relating the two types.
 *
 * `OsCanonPath::toPortable` must be a homomorphism: it should not matter
 * whether an operation is performed in OS-native terms and then converted,
 * or converted first and then performed. `PosixDirectorySourceAccessor`
 * relies on this — it walks and caches in `OsCanonPath` but reports errors
 * in `CanonPath`.
 * ----------------------------------------------------------------------- */

static OsCanonPath osParse(std::string_view s)
{
    return CanonicalPathTraits<OsCanonPath>::parse(s);
}

static CanonPath portableParse(std::string_view s)
{
    return CanonicalPathTraits<CanonPath>::parse(s);
}

TEST(CanonicalPathHomomorphism, roundTrip)
{
    for (auto raw : {"", "foo", "foo/bar", "foo/bar/baz"}) {
        auto c = portableParse(raw);
        ASSERT_EQ(OsCanonPath{c}.toPortable(), c);
        ASSERT_EQ(OsCanonPath{osParse(raw).toPortable()}, osParse(raw));
    }
}

TEST(CanonicalPathHomomorphism, identity)
{
    ASSERT_EQ(OsCanonPath{}.toPortable(), CanonPath::root);
    ASSERT_TRUE(OsCanonPath{CanonPath::root}.empty());
}

TEST(CanonicalPathHomomorphism, concat)
{
    auto a = osParse("foo/bar");
    auto b = osParse("a/b");
    ASSERT_EQ((a / b).toPortable(), a.toPortable() / b.toPortable());
}

TEST(CanonicalPathHomomorphism, parent)
{
    auto p = osParse("foo/bar/baz");
    ASSERT_EQ(p.parent()->toPortable(), p.toPortable().parent().value());
}

TEST(CanonicalPathHomomorphism, removePrefix)
{
    auto p = osParse("foo/bar/a/b");
    auto prefix = osParse("foo/bar");
    ASSERT_EQ(p.removePrefix(prefix).toPortable(), p.toPortable().removePrefix(prefix.toPortable()));
}

TEST(CanonicalPathHomomorphism, isWithin)
{
    auto p = osParse("foo/bar/baz");
    for (auto raw : {"", "foo", "foo/bar", "foo/bar/baz", "fo", "bar"}) {
        auto prefix = osParse(raw);
        ASSERT_EQ(p.isWithin(prefix), p.toPortable().isWithin(prefix.toPortable())) << "prefix: " << raw;
    }
}

TEST(CanonicalPathHomomorphism, ordering)
{
    /* Order must be preserved, or the dir-fd cache's `std::map` would
       disagree with the portable namespace it mirrors. */
    for (auto a : {"", "foo", "foo/bar", "foo!", "bar"}) {
        for (auto b : {"", "foo", "foo/bar", "foo!", "bar"}) {
            ASSERT_EQ(osParse(a) < osParse(b), portableParse(a) < portableParse(b)) << "a: " << a << ", b: " << b;
        }
    }
}

} // namespace nix

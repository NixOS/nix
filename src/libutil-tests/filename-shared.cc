#include "nix/util/error.hh"
#include "nix/util/filename.hh"
#include "nix/util/os-filename.hh"

#include <type_traits>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace nix {

/* Tests shared between `Filename` and `OsFilename`, for the cases where
   they agree. Those specific to each are in `filename.cc` and
   `os-filename.cc`. */

template<typename T>
struct CanonicalFilenameTraits;

template<>
struct CanonicalFilenameTraits<Filename>
{
    using Raw = std::string;

    static Filename asserting(const char * s)
    {
        return Filename::fromStringAsserting(s);
    }

    static Filename throwing(const char * s)
    {
        return Filename::fromStringThrowing(s);
    }

    static const Raw & raw(const Filename & f)
    {
        return f.str();
    }

    static constexpr const char * slashMessage = "cannot contain '/'";
};

template<>
struct CanonicalFilenameTraits<OsFilename>
{
    using Raw = std::filesystem::path;

    static OsFilename asserting(const char * s)
    {
        return OsFilename::fromPathAsserting(s);
    }

    static OsFilename throwing(const char * s)
    {
        return OsFilename::fromPathThrowing(s);
    }

    static const Raw & raw(const OsFilename & f)
    {
        return f.path();
    }

    static constexpr const char * slashMessage = "cannot have a parent path";
};

template<typename T>
class CanonicalFilename : public ::testing::Test
{
protected:
    using Traits = CanonicalFilenameTraits<T>;
};

using CanonicalFilenameTypes = ::testing::Types<Filename, OsFilename>;
TYPED_TEST_SUITE(CanonicalFilename, CanonicalFilenameTypes);

TYPED_TEST(CanonicalFilename, valid)
{
    using Traits = typename TestFixture::Traits;
    for (auto s : {"foo", "bar.txt", "file.tar.gz", ".hidden", "..."}) {
        EXPECT_EQ(Traits::raw(Traits::asserting(s)), s);
        EXPECT_EQ(Traits::throwing(s), Traits::asserting(s));
    }
}

TYPED_TEST(CanonicalFilename, comparison)
{
    using Traits = typename TestFixture::Traits;
    EXPECT_EQ(Traits::asserting("foo"), Traits::asserting("foo"));
    EXPECT_NE(Traits::asserting("foo"), Traits::asserting("bar"));
    EXPECT_LT(Traits::asserting("a"), Traits::asserting("b"));
}

TYPED_TEST(CanonicalFilename, noConversionToRaw)
{
    /* The newtype would be pointless if it could decay into the type it
       wraps; use the accessor instead. */
    static_assert(!std::is_convertible_v<TypeParam, const typename TestFixture::Traits::Raw &>);
}

TYPED_TEST(CanonicalFilename, throwingRejects)
{
    using Traits = typename TestFixture::Traits;
    using ::testing::HasSubstr;
    using ::testing::ThrowsMessage;
    EXPECT_THAT([] { Traits::throwing(""); }, ThrowsMessage<Error>(HasSubstr("cannot be empty")));
    EXPECT_THAT([] { Traits::throwing("."); }, ThrowsMessage<Error>(HasSubstr("cannot be '.'")));
    EXPECT_THAT([] { Traits::throwing(".."); }, ThrowsMessage<Error>(HasSubstr("cannot be '..'")));
    EXPECT_THAT([] { Traits::throwing("foo/bar"); }, ThrowsMessage<Error>(HasSubstr(Traits::slashMessage)));
}

TYPED_TEST(CanonicalFilename, assertingDies)
{
    using Traits = typename TestFixture::Traits;
    ASSERT_DEATH(Traits::asserting(""), "cannot be empty");
    ASSERT_DEATH(Traits::asserting("."), "cannot be '\\.'");
    ASSERT_DEATH(Traits::asserting(".."), "cannot be '\\.\\.'");
    ASSERT_DEATH(Traits::asserting("foo/bar"), Traits::slashMessage);
}

} // namespace nix

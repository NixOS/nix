#include "nix/util/error.hh"
#include "nix/util/fmt.hh"
#include "nix/util/system-error.hh"
#include "nix/util/tests/capture-logging.hh"

#include <gtest/gtest.h>

namespace nix {

/* No constructor takes a `std::string` or a `const char *`. `is_convertible`
   does not evaluate a `consteval` constructor, so a run-time
   `std::string_view` is rejected only where it is passed. */
static_assert(!std::is_convertible_v<std::string, FormatString<1>>);
static_assert(!std::is_convertible_v<const char *, FormatString<1>>);

/* Whether `literal` converts to `FormatString<nArgs>`. A conversion that is
   not a constant expression makes the template argument invalid, so the
   requirement is false instead of a compile error. */
template<ArrayNoNullAdaptor literal, size_t nArgs>
concept convertsToFormatString = requires {
    std::integral_constant<int, (FormatString<nArgs>(std::string_view(literal.data.data(), literal.data.size())), 0)>{};
};

static_assert(convertsToFormatString<"100", 0>);
static_assert(!convertsToFormatString<"100%%", 0>);
static_assert(!convertsToFormatString<"%s", 0>);
static_assert(convertsToFormatString<"%s", 1>);
static_assert(!convertsToFormatString<"%s %s", 1>);
static_assert(!convertsToFormatString<"%s", 2>);
static_assert(!convertsToFormatString<"%1% %s", 2>);
static_assert(!convertsToFormatString<"%s %", 1>);
static_assert(!convertsToFormatString<"%k", 1>);
static_assert(!convertsToFormatString<"%qd", 1>);
static_assert(!convertsToFormatString<"%0%", 1>);
static_assert(!convertsToFormatString<"%01%", 1>);
static_assert(!convertsToFormatString<"%5.1.2f", 1>);
static_assert(!convertsToFormatString<"%|1%|", 1>);
static_assert(!convertsToFormatString<"%|abc|", 1>);
static_assert(!convertsToFormatString<"%n", 1>);
static_assert(!convertsToFormatString<"%Tx", 1>);
static_assert(!convertsToFormatString<"%*d", 1>);

static_assert(RuntimeMessage<std::string>);
static_assert(RuntimeMessage<std::string_view>);
static_assert(RuntimeMessage<const char *>);
static_assert(!RuntimeMessage<char[4]>);

static_assert(std::is_same_v<decltype(""_fmt), FormatString<0>>);
static_assert(std::is_same_v<decltype("%6.1f"_fmt), FormatString<1>>);
static_assert(std::is_same_v<decltype("%2% %1%"_fmt), FormatString<2>>);
static_assert(std::is_same_v<decltype(ANSI_GREEN "%s"_fmt ANSI_NORMAL), FormatString<1>>);
static_assert(std::is_same_v<decltype(true ? "%6.1f"_fmt : "%.1f"_fmt), FormatString<1>>);

/* `n` must equal the count that `_fmt` takes from the directives. */
template<size_t n>
static void expectArgumentCount(FormatString<n> f)
{
    EXPECT_EQ(boost::format(f.str()).expected_args(), n) << f.str();
}

/* One case for each form that `formatArgumentCount` accepts. */
TEST(formatArgumentCount, agreesWithBoost)
{
    expectArgumentCount<0>(""_fmt);
    expectArgumentCount<1>("%s"_fmt);
    expectArgumentCount<1>("100%% %s"_fmt);
    expectArgumentCount<3>("%s: %s (%d)"_fmt);
    expectArgumentCount<8>("%b %d %i %u %o %x %X %p"_fmt);
    expectArgumentCount<8>("%a %A %e %E %f %F %g %G"_fmt);
    expectArgumentCount<4>("%c %C %s %S"_fmt);
    expectArgumentCount<8>("%-s %=s %_s % s %+s %0s %#s %'s"_fmt);
    expectArgumentCount<5>("%-10s %5.1f %.3f %.f %010d"_fmt);
    expectArgumentCount<7>("%hd %hhd %ld %lld %jd %zd %Lf"_fmt);
    expectArgumentCount<6>("%|s| %|-10s| %|5| %|02| %|| %|.3|"_fmt);
    expectArgumentCount<2>("%1%: %2%"_fmt);
    expectArgumentCount<2>("%2% %1% %2%"_fmt);
    expectArgumentCount<3>("%3%"_fmt);
    expectArgumentCount<10>("%10%"_fmt);
    expectArgumentCount<3>("%1$s %2$5d %3$-5.1f"_fmt);
    expectArgumentCount<2>("%|2$-10s| %|1$|"_fmt);
}

TEST(FormatString, keepsEmbeddedNul)
{
    ASSERT_EQ(fmt("a %s\0b", "x"), std::string("a x\0b", 5));
}

TEST(fmt, singleStringIsLiteral)
{
    /* A run-time string that is not a valid format string, and would
       substitute or throw if it were interpreted as one. */
    const std::string data = "100% %s %1%";

    ASSERT_EQ(fmt(data), data);
    ASSERT_EQ(fmt(std::string_view(data)), data);
    ASSERT_EQ(fmt(data.c_str()), data);
    ASSERT_EQ(HintFmt(data).str(), data);
    ASSERT_EQ(Error(data).info().msg.str(), data);
    ASSERT_EQ(Error(1u, data).info().msg.str(), data);
    ASSERT_EQ(Error(Suggestions{}, data).info().msg.str(), data);

    Error e("x");
    e.addTrace(nullptr, std::string_view(data));
    ASSERT_EQ(e.info().traces.front().hint.str(), data);

    testing::CaptureLogging capture;
    logger->cout(data);
    logger->coutStyled(data);
    ASSERT_EQ(capture.getStdout(), data + "\n" + data + "\n");

    auto sysErrorMessage = data + ": " ANSI_WARNING + std::string(strerror(ENOENT)) + ANSI_NORMAL;
    ASSERT_EQ(SysError(ENOENT, data).info().msg.str(), sysErrorMessage);
    errno = ENOENT;
    ASSERT_EQ(SysError(data).info().msg.str(), sysErrorMessage);

    /* As an argument, it is printed as is too. */
    ASSERT_EQ(HintFmt("<%s>", Uncolored(data)).str(), "<" + data + ">");
}

TEST(fmt, messageWithoutArguments)
{
    EXPECT_EQ(fmt("disk full"), "disk full");
    EXPECT_EQ(HintFmt("disk full").str(), "disk full");
    EXPECT_EQ(Error("disk full").info().msg.str(), "disk full");
    EXPECT_EQ(Error(1u, "disk full").info().msg.str(), "disk full");
    EXPECT_EQ(Error(Suggestions{}, "disk full").info().msg.str(), "disk full");

    Error e("x");
    e.addTrace(nullptr, "disk full");
    EXPECT_EQ(e.info().traces.front().hint.str(), "disk full");

    errno = ENOSPC;
    EXPECT_EQ(
        SysError("disk full").info().msg.str(),
        "disk full: " ANSI_WARNING + std::string(strerror(ENOSPC)) + ANSI_NORMAL);

    testing::CaptureLogging capture;
    logger->cout("disk full");
    logger->coutStyled("disk full");
    EXPECT_EQ(capture.getStdout(), "disk full\ndisk full\n");
    warn("disk full");
    EXPECT_EQ(capture.get(), ANSI_WARNING "warning:" ANSI_NORMAL " disk full\n");
}

TEST(HintFmt, percentStringWithoutArgs)
{

    const char * teststr = "this is 100%s correct!";

    ASSERT_STREQ(HintFmt(teststr).str().c_str(), teststr);
}

TEST(HintFmt, fmtToHintfmt)
{

    ASSERT_STREQ(
        HintFmt(fmt("the color of this this text is %1%", "not yellow")).str().c_str(),
        "the color of this this text is not yellow");
}

TEST(HintFmt, tooFewArguments)
{

    ASSERT_STREQ(
        (HintFmt::fromFormatString("only one arg %1% %2%") % "fulfilled").str().c_str(),
        "only one arg " ANSI_WARNING "fulfilled" ANSI_NORMAL " ");
}

TEST(HintFmt, tooManyArguments)
{

    ASSERT_STREQ(
        (HintFmt::fromFormatString("what about this %1% %2%") % "%3%" % "one" % "two").str().c_str(),
        "what about this " ANSI_WARNING "%3%" ANSI_NORMAL " " ANSI_WARNING "one" ANSI_NORMAL);
}

TEST(warn, argumentKeepsPercentSigns)
{
    testing::CaptureLogging capture;
    warn("%s", "100% %s %%");
    ASSERT_EQ(capture.get(), ANSI_WARNING "warning:" ANSI_NORMAL " 100% %s %%\n");
}

TEST(SysError, formatsMessage)
{
    ASSERT_EQ(
        SysError(ENOENT, "open %s", Uncolored("a")).info().msg.str(),
        "open a: " ANSI_WARNING + std::string(strerror(ENOENT)) + ANSI_NORMAL);
}

} // namespace nix

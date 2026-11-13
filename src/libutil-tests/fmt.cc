#include "nix/util/error.hh"
#include "nix/util/fmt.hh"
#include "nix/util/tests/capture-logging.hh"

#include <gtest/gtest.h>

namespace nix {

/* A run-time string that is not a valid format string, and would
   substitute or throw if it were interpreted as one. */
static const std::string data = "100% %s %1%";

static_assert(!std::is_convertible_v<std::string, FormatString<1>>);
static_assert(!std::is_convertible_v<const char *, FormatString<1>>);

static_assert(std::is_same_v<decltype(""_fmt), FormatString<0>>);
static_assert(std::is_same_v<decltype("%6.1f"_fmt), FormatString<1>>);
static_assert(std::is_same_v<decltype("%2% %1%"_fmt), FormatString<2>>);
static_assert(std::is_same_v<decltype(ANSI_GREEN "%s"_fmt ANSI_NORMAL), FormatString<1>>);

TEST(FormatString, literalOperator)
{
    ASSERT_EQ(fmt(true ? "%6.1f"_fmt : "%.1f"_fmt, 1.0), "   1.0");
    ASSERT_EQ(fmt(false ? "%6.1f"_fmt : "%.1f"_fmt, 1.0), "1.0");
}

struct ArgumentCountCase
{
    std::string_view formatString;
    size_t count;
};

/* One case for each form that `formatArgumentCount` accepts. */
static constexpr ArgumentCountCase argumentCountCases[] = {
    {"", 0},
    {"100%%", 0},
    {"%s", 1},
    {"%s: %s (%d)", 3},
    {"%b %d %i %u %o %x %X %p", 8},
    {"%a %A %e %E %f %F %g %G", 8},
    {"%c %C %s %S", 4},
    {"%-s %=s %_s % s %+s %0s %#s %'s", 8},
    {"%-10s %5.1f %.3f %.f %010d", 5},
    {"%hd %hhd %ld %lld %jd %zd %Lf", 7},
    {"%|s| %|-10s| %|5| %|02| %|| %|.3|", 6},
    {"%1%: %2%", 2},
    {"%2% %1% %2%", 2},
    {"%3%", 3},
    {"%10%", 10},
    {"%1$s %2$5d %3$-5.1f", 3},
    {"%|2$-10s| %|1$|", 2},
};

static_assert([]() consteval {
    for (auto & c : argumentCountCases)
        if (formatArgumentCount(c.formatString) != c.count)
            return false;
    return true;
}());

TEST(formatArgumentCount, agreesWithBoost)
{
    for (auto & c : argumentCountCases)
        ASSERT_EQ(boost::format(std::string(c.formatString)).expected_args(), c.count) << c.formatString;
}

TEST(FormatString, keepsEmbeddedNul)
{
    ASSERT_EQ(fmt("a %s\0b", "x"), std::string("a x\0b", 5));
}

TEST(fmt, singleStringIsLiteral)
{
    ASSERT_EQ(fmt(data), data);
    ASSERT_EQ(fmt(std::string_view(data)), data);
    ASSERT_EQ(fmt(data.c_str()), data);
}

TEST(fmt, formatStringWithoutArgumentsIsLiteral)
{
    ASSERT_EQ(fmt("100%% %s"), "100%% %s");
}

TEST(HintFmt, singleStringIsLiteral)
{
    ASSERT_EQ(HintFmt(data).str(), data);
}

TEST(HintFmt, formatStringWithoutArgumentsIsLiteral)
{
    ASSERT_EQ(HintFmt(FormatString<0>("100%% %s")).str(), "100%% %s");
}

TEST(HintFmt, argumentKeepsPercentSigns)
{
    ASSERT_EQ(HintFmt("<%s>", Uncolored(data)).str(), "<" + data + ">");
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

TEST(Error, singleStringIsLiteral)
{
    ASSERT_EQ(Error(data).info().msg.str(), data);
    ASSERT_EQ(Error(1u, data).info().msg.str(), data);
    ASSERT_EQ(Error(Suggestions{}, data).info().msg.str(), data);
}

TEST(Error, addTraceSingleStringIsLiteral)
{
    Error e("x");
    e.addTrace(nullptr, std::string_view(data));
    ASSERT_EQ(e.info().traces.front().hint.str(), data);
}

TEST(warn, messageWithoutArgumentsIsLiteral)
{
    testing::CaptureLogging capture;
    warn("100% %s %%");
    ASSERT_EQ(capture.get(), ANSI_WARNING "warning:" ANSI_NORMAL " 100% %s %%\n");
}

TEST(SysError, formatsMessage)
{
    ASSERT_EQ(
        SysError(ENOENT, "open %s", Uncolored("a")).info().msg.str(),
        "open a: " ANSI_WARNING + std::string(strerror(ENOENT)) + ANSI_NORMAL);
}

TEST(SysError, singleStringIsLiteral)
{
    auto expected = data + ": " ANSI_WARNING + std::string(strerror(ENOENT)) + ANSI_NORMAL;
    ASSERT_EQ(SysError(ENOENT, data).info().msg.str(), expected);
    errno = ENOENT;
    ASSERT_EQ(SysError(data).info().msg.str(), expected);
}

} // namespace nix

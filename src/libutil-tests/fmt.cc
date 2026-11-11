#include "nix/util/error.hh"
#include "nix/util/fmt.hh"

#include <gtest/gtest.h>

namespace nix {

/* A run-time string that is not a valid format string, and would
   substitute or throw if it were interpreted as one. */
static const std::string data = "100% %s %1%";

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

TEST(fmt, tooFewArguments)
{
    ASSERT_EQ(fmt("%s %s", "a"), "a ");
}

TEST(HintFmt, singleStringIsLiteral)
{
    ASSERT_EQ(HintFmt(data).str(), data);
}

TEST(HintFmt, argumentKeepsPercentSigns)
{
    ASSERT_EQ(HintFmt("<%s>", Uncolored(data)).str(), "<" + data + ">");
}

#if 0
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
        HintFmt("only one arg %1% %2%", "fulfilled").str().c_str(),
        "only one arg " ANSI_WARNING "fulfilled" ANSI_NORMAL " ");
}

TEST(HintFmt, tooManyArguments)
{

    ASSERT_STREQ(
        HintFmt("what about this %1% %2%", "%3%", "one", "two").str().c_str(),
        "what about this " ANSI_WARNING "%3%" ANSI_NORMAL " " ANSI_YELLOW "one" ANSI_NORMAL);
}
#endif

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

TEST(SysError, formatsMessage)
{
    ASSERT_EQ(
        SysError(ENOENT, "open %s", Uncolored("a")).info().msg.str(),
        "open a: " ANSI_WARNING + std::string(strerror(ENOENT)) + ANSI_NORMAL);
}

} // namespace nix

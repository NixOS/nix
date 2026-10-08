#include "nix/util/error.hh"
#include "nix/util/fmt.hh"
#include "nix/util/system-error.hh"
#include "nix/util/tests/capture-logging.hh"

#include <gtest/gtest.h>

namespace nix {

/* No constructor takes a `std::string` or a `const char *`. `is_convertible`
   does not evaluate a `consteval` constructor, so a run-time
   `std::string_view` is rejected only where it is passed. */
static_assert(!std::is_convertible_v<std::string, FormatString>);
static_assert(!std::is_convertible_v<const char *, FormatString>);

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

TEST(fmt, formatStringWithoutArgumentsIsLiteral)
{
    ASSERT_EQ(fmt("100%% %s"), "100%% %s");
    ASSERT_EQ(fmt(FormatString("100%% %s")), "100%% %s");
}

TEST(fmt, tooFewArguments)
{
    ASSERT_EQ(fmt("%s %s", "a"), "a ");
}

TEST(HintFmt, formatStringWithoutArgumentsIsLiteral)
{
    ASSERT_EQ(HintFmt(FormatString("100%% %s")).str(), "100%% %s");
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
        HintFmt("only one arg %1% %2%", "fulfilled").str().c_str(),
        "only one arg " ANSI_WARNING "fulfilled" ANSI_NORMAL " ");
}

TEST(HintFmt, tooManyArguments)
{

    ASSERT_STREQ(
        HintFmt("what about this %1% %2%", "%3%", "one", "two").str().c_str(),
        "what about this " ANSI_WARNING "%3%" ANSI_NORMAL " " ANSI_WARNING "one" ANSI_NORMAL);
}

TEST(Error, tooFewArguments)
{
    ASSERT_EQ(Error("%s %s", Uncolored("a")).info().msg.str(), "a ");
}

TEST(warn, messageWithoutArgumentsIsLiteral)
{
    testing::CaptureLogging capture;
    warn("100% %s %%");
    ASSERT_EQ(capture.get(), ANSI_WARNING "warning:" ANSI_NORMAL " 100% %s %%\n");
}

TEST(warn, tooFewArguments)
{
    testing::CaptureLogging capture;
    warn("%s %s", "a");
    ASSERT_EQ(capture.get(), ANSI_WARNING "warning:" ANSI_NORMAL " a \n");
}

TEST(SysError, formatsMessage)
{
    ASSERT_EQ(
        SysError(ENOENT, "open %s", Uncolored("a")).info().msg.str(),
        "open a: " ANSI_WARNING + std::string(strerror(ENOENT)) + ANSI_NORMAL);
}

} // namespace nix

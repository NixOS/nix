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

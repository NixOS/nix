#include <gtest/gtest.h>

#include "nix/util/processes.hh"

namespace nix {

#ifdef _WIN32
TEST(SpawnTest, spawnEcho)
{
    auto output = runProgram(RunOptions{.program = "cmd.exe", .args = {"/C", "echo", "hello world"}});
    ASSERT_EQ(output.first, 0);
    ASSERT_EQ(output.second, "\"hello world\"\r\n");
}

std::string windowsEscape(const std::string & str, bool cmd);

TEST(SpawnTest, windowsEscape)
{
    auto empty = windowsEscape("", false);
    ASSERT_EQ(empty, R"("")");
    // There's no quotes in this argument so the input should equal the output
    auto backslashStr = R"(\\\\)";
    auto backslashes = windowsEscape(backslashStr, false);
    ASSERT_EQ(backslashes, backslashStr);

    auto nestedQuotes = windowsEscape(R"(he said: "hello there")", false);
    ASSERT_EQ(nestedQuotes, R"("he said: \"hello there\"")");

    auto middleQuote = windowsEscape(R"( \\\" )", false);
    ASSERT_EQ(middleQuote, R"(" \\\\\\\" ")");

    auto space = windowsEscape("hello world", false);
    ASSERT_EQ(space, R"("hello world")");
}
#endif
} // namespace nix

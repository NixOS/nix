#include <gtest/gtest.h>

#include "processes.hh"

namespace nix {
/*
TEST(SpawnTest, spawnEcho)
{
auto output = runProgram(RunOptions{.program = "cmd", .lookupPath = true, .args = {"/C", "echo \"hello world\""}});
std::cout << output.second << std::endl;
}
*/

#ifdef _WIN32
Path lookupPathForProgram(const Path & program);
TEST(SpawnTest, pathSearch)
{
    ASSERT_NO_THROW(lookupPathForProgram("cmd"));
    ASSERT_NO_THROW(lookupPathForProgram("cmd.exe"));
    ASSERT_THROW(lookupPathForProgram("C:/System32/cmd.exe"), UsageError);
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
}

#include <gtest/gtest.h>

#include "position.hh"

namespace nix {

inline Pos::Origin makeStdin(std::string s)
{
    return Pos::Stdin{make_ref<std::string>(s)};
}

TEST(Position, getSnippetUpTo_0)
{
    Pos::Origin o = makeStdin("");
    Pos p(1, 1, o);
    ASSERT_EQ(p.getSnippetUpTo(p), "");
}
TEST(Position, getSnippetUpTo_1)
{
    Pos::Origin o = makeStdin("x");
    {
        // NOTE: line and column are actually 1-based indexes
        Pos start(0, 0, o);
        Pos end(99, 99, o);
        ASSERT_EQ(start.getSnippetUpTo(start), "");
        ASSERT_EQ(start.getSnippetUpTo(end), "x");
        ASSERT_EQ(end.getSnippetUpTo(end), "");
        ASSERT_EQ(end.getSnippetUpTo(start), "");
    }
    {
        // NOTE: line and column are actually 1-based indexes
        Pos start(0, 99, o);
        Pos end(99, 0, o);
        ASSERT_EQ(start.getSnippetUpTo(start), "");

        // "x" might be preferable, but we only care about not crashing for invalid inputs
        ASSERT_EQ(start.getSnippetUpTo(end), "");

        ASSERT_EQ(end.getSnippetUpTo(end), "");
        ASSERT_EQ(end.getSnippetUpTo(start), "");
    }
    {
        Pos start(1, 1, o);
        Pos end(1, 99, o);
        ASSERT_EQ(start.getSnippetUpTo(start), "");
        ASSERT_EQ(start.getSnippetUpTo(end), "x");
        ASSERT_EQ(end.getSnippetUpTo(end), "");
        ASSERT_EQ(end.getSnippetUpTo(start), "");
    }
    {
        Pos start(1, 1, o);
        Pos end(99, 99, o);
        ASSERT_EQ(start.getSnippetUpTo(start), "");
        ASSERT_EQ(start.getSnippetUpTo(end), "x");
        ASSERT_EQ(end.getSnippetUpTo(end), "");
        ASSERT_EQ(end.getSnippetUpTo(start), "");
    }
}
TEST(Position, getSnippetUpTo_2)
{
    Pos::Origin o = makeStdin("asdf\njkl\nqwer");
    {
        Pos start(1, 1, o);
        Pos end(1, 2, o);
        ASSERT_EQ(start.getSnippetUpTo(start), "");
        ASSERT_EQ(start.getSnippetUpTo(end), "a");
        ASSERT_EQ(end.getSnippetUpTo(end), "");
        ASSERT_EQ(end.getSnippetUpTo(start), "");
    }
    {
        Pos start(1, 2, o);
        Pos end(1, 3, o);
        ASSERT_EQ(start.getSnippetUpTo(end), "s");
    }
    {
        Pos start(1, 2, o);
        Pos end(2, 2, o);
        ASSERT_EQ(start.getSnippetUpTo(end), "sdf\nj");
    }
    {
        Pos start(1, 2, o);
        Pos end(3, 2, o);
        ASSERT_EQ(start.getSnippetUpTo(end), "sdf\njkl\nq");
    }
    {
        Pos start(1, 2, o);
        Pos end(2, 99, o);
        ASSERT_EQ(start.getSnippetUpTo(end), "sdf\njkl");
    }
    {
        Pos start(1, 4, o);
        Pos end(2, 99, o);
        ASSERT_EQ(start.getSnippetUpTo(end), "f\njkl");
    }
    {
        Pos start(1, 5, o);
        Pos end(2, 99, o);
        ASSERT_EQ(start.getSnippetUpTo(end), "\njkl");
    }
    {
        Pos start(1, 6, o); // invalid: starting column past last "line character", ie at the newline
        Pos end(2, 99, o);
        ASSERT_EQ(start.getSnippetUpTo(end), "\njkl"); // jkl might be acceptable for this invalid start position
    }
    {
        Pos start(1, 1, o);
        Pos end(2, 0, o); // invalid
        ASSERT_EQ(start.getSnippetUpTo(end), "asdf\n");
    }
}

TEST(Position, example_1)
{
    Pos::Origin o = makeStdin("  unambiguous = \n    /** Very close */\n    x: x;\n# ok\n");
    Pos start(2, 5, o);
    Pos end(2, 22, o);
    ASSERT_EQ(start.getSnippetUpTo(end), "/** Very close */");
}

} // namespace nix

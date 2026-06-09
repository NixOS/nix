#include <gtest/gtest.h>
#include <type_traits>

#include "nix/util/fun.hh"

namespace nix {

static int add(int a, int b)
{
    return a + b;
}

TEST(fun, constructFromLambda)
{
    fun<int(int)> f = [](int x) { return x * 2; };
    EXPECT_EQ(f(3), 6);
}

TEST(fun, constructFromFunctionPointer)
{
    fun<int(int, int)> f = add;
    EXPECT_EQ(f(2, 3), 5);
}

TEST(fun, constructFromStdFunction)
{
    std::function<int(int)> sf = [](int x) { return x + 1; };
    fun<int(int)> f(sf);
    EXPECT_EQ(f(5), 6);
}

TEST(fun, moveConstructFromStdFunction)
{
    std::function<int(int)> sf = [](int x) { return x + 1; };
    fun<int(int)> f(std::move(sf));
    EXPECT_EQ(f(5), 6);
}

TEST(fun, rejectsEmptyStdFunction)
{
    std::function<int(int)> empty;
    EXPECT_THROW((fun<int(int)>{empty}), std::invalid_argument);
}

TEST(fun, rejectsEmptyStdFunctionMove)
{
    std::function<int(int)> empty;
    EXPECT_THROW((fun<int(int)>{std::move(empty)}), std::invalid_argument);
}

TEST(fun, rejectsNullFunctionPointer)
{
    int (*nullFp)(int) = nullptr;
    EXPECT_THROW((fun<int(int)>{nullFp}), std::invalid_argument);
}

TEST(fun, nullptrDeletedAtCompileTime)
{
    // fun(nullptr) is a deleted constructor
    static_assert(!std::is_constructible_v<fun<void()>, std::nullptr_t>);
}

TEST(fun, notDefaultConstructible)
{
    static_assert(!std::is_default_constructible_v<fun<void()>>);
}

TEST(fun, voidReturn)
{
    int called = 0;
    fun<void()> f = [&]() { called++; };
    f();
    EXPECT_EQ(called, 1);
}

TEST(fun, referenceArgs)
{
    fun<void(int &)> f = [](int & x) { x += 10; };
    int val = 5;
    f(val);
    EXPECT_EQ(val, 15);
}

TEST(fun, convertsToStdFunction)
{
    fun<int(int)> f = [](int x) { return x * 3; };
    std::function<int(int)> sf = f.get_fn();
    EXPECT_EQ(sf(4), 12);
}

TEST(fun, copyable)
{
    fun<int(int)> f = [](int x) { return x + 1; };
    auto g = f;
    EXPECT_EQ(f(1), 2);
    EXPECT_EQ(g(1), 2);
}

TEST(fun, movable)
{
    fun<int(int)> f = [](int x) { return x + 1; };
    auto g = std::move(f);
    EXPECT_EQ(g(1), 2);
}

TEST(fun, capturesState)
{
    int offset = 100;
    fun<int(int)> f = [offset](int x) { return x + offset; };
    EXPECT_EQ(f(5), 105);
}

TEST(fun, getFn)
{
    fun<int(int)> f = [](int x) { return x; };
    const auto & sf = f.get_fn();
    EXPECT_EQ(sf(42), 42);
}

TEST(fun, getFnMove)
{
    fun<int(int)> f = [](int x) { return x; };
    auto sf = std::move(f).get_fn();
    EXPECT_EQ(sf(42), 42);
}

TEST(fun, forwardsMoveOnlyTypes)
{
    fun<int(std::unique_ptr<int>)> f = [](std::unique_ptr<int> p) { return *p; };
    auto p = std::make_unique<int>(42);
    EXPECT_EQ(f(std::move(p)), 42);
}

TEST(fun, perfectForwardingZeroCost)
{
    int copies = 0, moves = 0;

    struct Tracker
    {
        int & copies;
        int & moves;

        Tracker(int & copies, int & moves)
            : copies(copies)
            , moves(moves)
        {
        }

        Tracker(const Tracker & o)
            : copies(o.copies)
            , moves(o.moves)
        {
            copies++;
        }

        Tracker(Tracker && o) noexcept
            : copies(o.copies)
            , moves(o.moves)
        {
            moves++;
        }
    };

    // Baseline: call std::function directly
    std::function<void(Tracker, Tracker)> sf = [](Tracker, Tracker) {};
    Tracker t1{copies, moves}, t2{copies, moves};
    sf(t1, t2);
    int baseline_copies = copies, baseline_moves = moves;

    copies = 0;
    moves = 0;

    // Call through fun<> — should match baseline exactly
    fun<void(Tracker, Tracker)> f = [](Tracker, Tracker) {};
    f(t1, t2);
    EXPECT_EQ(copies, baseline_copies);
    EXPECT_EQ(moves, baseline_moves);
}

} // namespace nix

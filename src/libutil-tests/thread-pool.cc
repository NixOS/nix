#include "nix/util/thread-pool.hh"
#include <gtest/gtest.h>

namespace nix {

using namespace std;

TEST(threadpool, correctValue)
{
    ThreadPool pool(3);
    int sum = 0;
    std::mutex mtx;
    for (int i = 0; i < 20; i++) {
        pool.enqueue([&] {
            std::lock_guard<std::mutex> lock(mtx); 
            sum += 1;
        });
    }
    pool.process();
    ASSERT_EQ(sum, 20);
}

TEST(threadpool, properlyHandlesDirectExceptions)
{
    struct TestExn
    {};

    ThreadPool pool(3);
    pool.enqueue([&] {
        throw TestExn();
    });
    EXPECT_THROW(
        pool.process(),
        TestExn);
}


} // namespace nix
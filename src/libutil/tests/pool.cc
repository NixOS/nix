#include "pool.hh"
#include <gtest/gtest.h>

namespace nix {

    struct TestResource
    {

        TestResource() {
            static int counter = 0;
            num = counter++;
        }

        int dummyValue = 1;
        bool good = true;
        int num;
    };

    /* ----------------------------------------------------------------------------
     * Pool
     * --------------------------------------------------------------------------*/

    TEST(Pool, freshPoolHasZeroCountAndSpecifiedCapacity) {
        auto isGood = [](const ref<TestResource> & r) { return r->good; };
        auto createResource = []() { return make_ref<TestResource>(); };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);

        ASSERT_EQ(pool.count(), 0);
        ASSERT_EQ(pool.capacity(), 1);
    }

    TEST(Pool, freshPoolCanGetAResource) {
        auto isGood = [](const ref<TestResource> & r) { return r->good; };
        auto createResource = []() { return make_ref<TestResource>(); };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);
        ASSERT_EQ(pool.count(), 0);

        TestResource r = *(pool.get());

        ASSERT_EQ(pool.count(), 1);
        ASSERT_EQ(pool.capacity(), 1);
        ASSERT_EQ(r.dummyValue, 1);
        ASSERT_EQ(r.good, true);
    }

    TEST(Pool, capacityCanBeIncremented) {
        auto isGood = [](const ref<TestResource> & r) { return r->good; };
        auto createResource = []() { return make_ref<TestResource>(); };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);
        ASSERT_EQ(pool.capacity(), 1);
        pool.incCapacity();
        ASSERT_EQ(pool.capacity(), 2);
    }

    TEST(Pool, capacityCanBeDecremented) {
        auto isGood = [](const ref<TestResource> & r) { return r->good; };
        auto createResource = []() { return make_ref<TestResource>(); };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);
        ASSERT_EQ(pool.capacity(), 1);
        pool.decCapacity();
        ASSERT_EQ(pool.capacity(), 0);
    }

    TEST(Pool, flushBadDropsOutOfScopeResources) {
        auto isGood = [](const ref<TestResource> & r) { return false; };
        auto createResource = []() { return make_ref<TestResource>(); };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);

        {
            auto _r = pool.get();
            ASSERT_EQ(pool.count(), 1);
        }

        pool.flushBad();
        ASSERT_EQ(pool.count(), 0);
    }

    // Test that the resources we allocate are being reused when they are still good.
    TEST(Pool, reuseResource) {
        auto isGood = [](const ref<TestResource> & r) { return true; };
        auto createResource = []() { return make_ref<TestResource>(); };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);

        // Compare the instance counter between the two handles. We expect them to be equal
        // as the pool should hand out the same (still) good one again.
        int counter = -1;
        {
            Pool<TestResource>::Handle h = pool.get();
            counter = h->num;
        } // the first handle goes out of scope

        { // the second handle should contain the same resource (with the same counter value)
            Pool<TestResource>::Handle h = pool.get();
            ASSERT_EQ(h->num, counter);
        }
    }

    // Test that the resources we allocate are being thrown away when they are no longer good.
    TEST(Pool, badResourceIsNotReused) {
        auto isGood = [](const ref<TestResource> & r) { return false; };
        auto createResource = []() { return make_ref<TestResource>(); };

        Pool<TestResource> pool = Pool<TestResource>((size_t)1, createResource, isGood);

        // Compare the instance counter between the two handles. We expect them
        // to *not* be equal as the pool should hand out a new instance after
        // the first one was returned.
        int counter = -1;
        {
            Pool<TestResource>::Handle h = pool.get();
            counter = h->num;
        } // the first handle goes out of scope

        {
          // the second handle should contain a different resource (with a
          //different counter value)
            Pool<TestResource>::Handle h = pool.get();
            ASSERT_NE(h->num, counter);
        }
    }
}

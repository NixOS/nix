#include <gtest/gtest.h>
#if HAVE_BOEHMGC
#include <gc/gc.h>
#endif

#include "eval.hh"
#include "serialise.hh"


#define guard_gc(x) GC_register_finalizer((void*)x, finalizer, x##_collected, nullptr, nullptr)


namespace nix {
#if HAVE_BOEHMGC
    static bool* uncollectable_bool() {
        bool* res = (bool*)GC_MALLOC_UNCOLLECTABLE(1);
        *res = false;
        return res;
    }

    static void finalizer(void *obj, void *data) {
        //printf("finalizer: obj %p data %p\n", obj, data);
        *((bool*)data) = true;
    }

    // Generate 2 objects, discard one, run gc,
    // see if one got collected and the other didn't
    // GC is disabled inside coroutines on __APPLE__
    static void testFinalizerCalls() {
        bool* do_collect_collected = uncollectable_bool();
        bool* dont_collect_collected = uncollectable_bool();
        {
            volatile void* do_collect = GC_MALLOC_ATOMIC(128);
            guard_gc(do_collect);
        }
        volatile void* dont_collect = GC_MALLOC_ATOMIC(128);
        guard_gc(dont_collect);
        GC_gcollect();
        GC_invoke_finalizers();

#if !__APPLE__
        ASSERT_TRUE(*do_collect_collected);
#endif
        ASSERT_FALSE(*dont_collect_collected);
        ASSERT_NE(nullptr, dont_collect);
    }

    // This test tests that boehm handles coroutine stacks correctly
    TEST(CoroGC, CoroutineStackNotGCd) {
        initGC();
        testFinalizerCalls();

        bool* dont_collect_collected = uncollectable_bool();
        bool* do_collect_collected = uncollectable_bool();

        volatile void* dont_collect = GC_MALLOC_ATOMIC(128);
        guard_gc(dont_collect);
        {
            volatile void* do_collect = GC_MALLOC_ATOMIC(128);
            guard_gc(do_collect);
        }

        auto source = sinkToSource([&](Sink& sink) {

#if __APPLE__
            ASSERT_TRUE(GC_is_disabled());
#endif
            testFinalizerCalls();

            bool* dont_collect_inner_collected = uncollectable_bool();
            bool* do_collect_inner_collected = uncollectable_bool();

            volatile void* dont_collect_inner = GC_MALLOC_ATOMIC(128);
            guard_gc(dont_collect_inner);
            {
                volatile void* do_collect_inner = GC_MALLOC_ATOMIC(128);
                guard_gc(do_collect_inner);
            }
            // pass control to main
            writeString("foo", sink);
#if __APPLE__
            ASSERT_TRUE(GC_is_disabled());
#endif

            ASSERT_TRUE(*do_collect_inner_collected);
            ASSERT_FALSE(*dont_collect_inner_collected);
            ASSERT_NE(nullptr, dont_collect_inner);

            // pass control to main
            writeString("bar", sink);
        });

        // pass control to coroutine
        std::string foo = readString(*source);
        ASSERT_EQ(foo, "foo");

        ASSERT_FALSE(GC_is_disabled());
        GC_gcollect();
        GC_invoke_finalizers();

        // pass control to coroutine
        std::string bar = readString(*source);
        ASSERT_EQ(bar, "bar");

        ASSERT_FALSE(*dont_collect_collected);
        ASSERT_TRUE(*do_collect_collected);
        ASSERT_NE(nullptr, dont_collect);
    }
#endif
}

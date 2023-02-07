#include <gtest/gtest.h>
#if HAVE_BOEHMGC
#include <gc/gc.h>

#include "eval.hh"
#include "serialise.hh"

#endif


namespace nix {
#if HAVE_BOEHMGC

    static void finalizer(void *obj, void *data) {
        *((bool*)data) = true;
    }

    static bool* make_witness(volatile void* obj) {
        /* We can't store the witnesses on the stack,
           since they might be collected long afterwards */
        bool* res = (bool*)GC_MALLOC_UNCOLLECTABLE(1);
        *res = false;
        GC_register_finalizer((void*)obj, finalizer, res, nullptr, nullptr);
        return res;
    }

    // Generate 2 objects, discard one, run gc,
    // see if one got collected and the other didn't
    // GC is disabled inside coroutines on __APPLE__
    static void testFinalizerCalls() {
        volatile void* do_collect = GC_MALLOC_ATOMIC(128);
        volatile void* dont_collect = GC_MALLOC_ATOMIC(128);

        bool* do_collect_witness = make_witness(do_collect);
        bool* dont_collect_witness = make_witness(dont_collect);
        GC_gcollect();
        GC_invoke_finalizers();

        ASSERT_TRUE(GC_is_disabled() || *do_collect_witness);
        ASSERT_FALSE(*dont_collect_witness);
        ASSERT_NE(nullptr, dont_collect);
    }

    TEST(CoroGC, BasicFinalizers) {
        initGC();
        testFinalizerCalls();
    }

    // Run testFinalizerCalls inside a coroutine
    // this tests that GC works as expected inside a coroutine
    TEST(CoroGC, CoroFinalizers) {
        initGC();

        auto source = sinkToSource([&](Sink& sink) {
            testFinalizerCalls();

            // pass control to main
            writeString("foo", sink);
        });

        // pass control to coroutine
        std::string foo = readString(*source);
        ASSERT_EQ(foo, "foo");
    }

#if __APPLE__
    // This test tests that GC is disabled on darwin
    // to work around the patch not being sufficient there,
    // causing crashes whenever gc is invoked inside a coroutine
    TEST(CoroGC, AppleCoroDisablesGC) {
        initGC();
        auto source = sinkToSource([&](Sink& sink) {
            ASSERT_TRUE(GC_is_disabled());
            // pass control to main
            writeString("foo", sink);

            ASSERT_TRUE(GC_is_disabled());

            // pass control to main
            writeString("bar", sink);
        });

        // pass control to coroutine
        std::string foo = readString(*source);
        ASSERT_EQ(foo, "foo");
        ASSERT_FALSE(GC_is_disabled());
        // pass control to coroutine
        std::string bar = readString(*source);
        ASSERT_EQ(bar, "bar");

        ASSERT_FALSE(GC_is_disabled());
    }
#endif

    // This test tests that boehm handles coroutine stacks correctly
    // This test tests that coroutine stacks are registered to the GC,
    // even when the coroutine is not running. It also tests that
    // the main stack is still registered to the GC when the coroutine is running.
    TEST(CoroGC, CoroutineStackNotGCd) {
        initGC();

        volatile void* do_collect = GC_MALLOC_ATOMIC(128);
        volatile void* dont_collect = GC_MALLOC_ATOMIC(128);

        bool* do_collect_witness = make_witness(do_collect);
        bool* dont_collect_witness = make_witness(dont_collect);

        do_collect = nullptr;

        auto source = sinkToSource([&](Sink& sink) {
            volatile void* dont_collect_inner = GC_MALLOC_ATOMIC(128);
            volatile void* do_collect_inner = GC_MALLOC_ATOMIC(128);

            bool* do_collect_inner_witness = make_witness(do_collect_inner);
            bool* dont_collect_inner_witness = make_witness(dont_collect_inner);

            do_collect_inner = nullptr;

            // pass control to main
            writeString("foo", sink);

            ASSERT_FALSE(*dont_collect_inner_witness);
            ASSERT_TRUE(*do_collect_inner_witness);
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

        ASSERT_FALSE(*dont_collect_witness);
        ASSERT_TRUE(*do_collect_witness);
        ASSERT_NE(nullptr, dont_collect);
    }
#endif
}

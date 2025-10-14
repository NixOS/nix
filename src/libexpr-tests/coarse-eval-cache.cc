#include <gtest/gtest.h>
#include <memory>

#include "nix/expr/coarse-eval-cache.hh"
#include "nix/expr/coarse-eval-cache-cursor-object.hh"
#include "nix/expr/eval-cache.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/eval-gc.hh"
#include "nix/expr/search-path.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/store/tests/libstore.hh"

namespace nix::expr {

/**
 * Test fixture for CoarseEvalCache-specific tests
 */
class CoarseEvalCacheTest : public LibStoreTest
{
protected:
    std::shared_ptr<EvalState> state;
    std::shared_ptr<CoarseEvalCache> evaluator;

    // Settings must be member variables to outlive EvalState
    bool readOnlyMode = true;
    fetchers::Settings fetchSettings{};
    EvalSettings evalSettings{readOnlyMode};

    static void SetUpTestSuite()
    {
        LibStoreTest::SetUpTestSuite();
        initGC();
    }

    void SetUp() override
    {
        // Initialize settings
        evalSettings.nixPath = {};

        // Create a fresh EvalState for testing
        auto stateRef = make_ref<EvalState>(
            LookupPath{}, // Empty search path
            store,
            fetchSettings,
            evalSettings,
            nullptr);
        state = stateRef;

        evaluator = std::make_shared<CoarseEvalCache>(stateRef);
    }

    /**
     * Helper to create an EvalCache from an expression
     */
    ref<eval_cache::EvalCache> createEvalCache(const std::string & expr)
    {
        auto e = state->parseExprFromString(expr, state->rootPath(CanonPath::root));
        auto v = state->allocValue();
        state->eval(e, *v);

        return make_ref<eval_cache::EvalCache>(
            std::optional<std::filesystem::path>(std::nullopt), // No persistent cache
            *state,
            [v]() { return v; } // RootLoader that returns our evaluated value
        );
    }
};

// Test wrapping an EvalCache as an Object
TEST_F(CoarseEvalCacheTest, WrapEvalCacheAsObject)
{
    // Create an EvalCache with a simple expression
    auto evalCache = createEvalCache("{ foo = \"bar\"; nested = { x = 42; }; }");

    // Get the root cursor from the EvalCache
    auto cursor = evalCache->getRoot();

    // Wrap it in a CoarseEvalCacheCursorObject
    auto obj = std::make_shared<CoarseEvalCacheCursorObject>(cursor);

    // Verify we can navigate through the Object interface
    auto foo = obj->maybeGetAttr("foo");
    ASSERT_NE(foo, nullptr) << "Should be able to get 'foo' attribute";

    auto str = foo->getStringIgnoreContext();
    EXPECT_EQ(str, "bar");

    // Test nested attribute access
    auto nested = obj->maybeGetAttr("nested");
    ASSERT_NE(nested, nullptr) << "Should be able to get 'nested' attribute";

    auto x = nested->maybeGetAttr("x");
    ASSERT_NE(x, nullptr) << "Should be able to get nested 'x' attribute";

    // Note: We don't have getInt() in Object interface yet,
    // so we can't verify the integer value
}

// Test that CoarseEvalCache can create Objects from EvalCache
TEST_F(CoarseEvalCacheTest, CreateObjectFromEvalCache)
{
    // This test verifies that we can create an Object from an existing EvalCache
    // This is what InstallableFlake needs to do after loading a flake

    auto evalCache = createEvalCache("{ packages.x86_64-linux.default = \"dummy-package\"; }");

    // Use the getRoot method to wrap the EvalCache
    auto root = evaluator->getRoot(evalCache);

    // Navigate to packages.x86_64-linux.default
    auto packages = root->maybeGetAttr("packages");
    ASSERT_NE(packages, nullptr);

    auto x86_64 = packages->maybeGetAttr("x86_64-linux");
    ASSERT_NE(x86_64, nullptr);

    auto defaultPkg = x86_64->maybeGetAttr("default");
    ASSERT_NE(defaultPkg, nullptr);

    auto pkgStr = defaultPkg->getStringIgnoreContext();
    EXPECT_EQ(pkgStr, "dummy-package");
}

} // namespace nix::expr
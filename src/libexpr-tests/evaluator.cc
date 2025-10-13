#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

#include "nix/expr/evaluator.hh"
#include "nix/expr/interpreter.hh"
#include "nix/expr/interpreter-object.hh"
#include "nix/expr/coarse-eval-cache.hh"
#include "nix/expr/coarse-eval-cache-cursor-object.hh"
#include "nix/expr/eval-cache.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/eval-gc.hh"
#include "nix/expr/search-path.hh"
#include "nix/expr/evaluation-helpers.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/util/hash.hh"
#include "nix/store/tests/libstore.hh"
#include "nix/util/tests/gmock-matchers.hh"

namespace nix::expr {

/**
 * Simple external value for testing purposes
 */
class ExternalValueForTesting : public ExternalValueBase
{
protected:
    std::ostream & print(std::ostream & str) const override
    {
        str << "ExternalValueForTesting";
        return str;
    }
public:
    std::string showType() const override
    {
        return "an external value for testing";
    }

    std::string typeOf() const override
    {
        return "external-test";
    }

    ~ExternalValueForTesting() override = default;
};

/**
 * Parameterized test fixture for testing different Evaluator implementations.
 * This ensures all implementations of the Evaluator interface behave consistently.
 */
class EvaluatorTest : public LibStoreTest, public ::testing::WithParamInterface<std::string>
{
protected:
    std::shared_ptr<Evaluator> evaluator;
    std::shared_ptr<EvalState> evalStateForTestSetupOnly; // Only for evalExpression, not for direct use in tests
    int testRunIteration = 0;                             // Track cold vs warm cache runs

    // Settings must be member variables to outlive EvalState
    bool readOnlyMode = false; // Allow writing for derivation tests
    fetchers::Settings fetchSettings{};
    EvalSettings evalSettings{readOnlyMode};

    static void SetUpTestSuite()
    {
        LibStoreTest::SetUpTestSuite();
        initGC();
    }

    EvaluatorTest()
        : LibStoreTest(openStore("dummy://?read-only=false"))
    {
    }

    void SetUp() override
    {
        // Initialize settings
        evalSettings.nixPath = {};
        evalSettings.applyConfig("");

        // Create a fresh EvalState for testing
        auto state = make_ref<EvalState>(
            LookupPath{}, // Empty search path
            store,
            fetchSettings,
            evalSettings,
            nullptr);

        // Save for evalExpression only - tests should use the Evaluator interface
        evalStateForTestSetupOnly = state;

        auto implementation = GetParam();
        if (implementation == "Interpreter") {
            evaluator = std::make_shared<Interpreter>(state);
        } else if (implementation == "CoarseEvalCache" || implementation == "CoarseEvalCacheWithPersistence") {
            evaluator = std::make_shared<CoarseEvalCache>(state);
        } else {
            throw std::runtime_error("Unknown evaluator implementation: " + implementation);
        }
    }

    /**
     * Get path for test cache database - same for all runs in this test session.
     */
    static std::filesystem::path getTestCachePath()
    {
        auto tmpDir = std::filesystem::temp_directory_path() / "nix-eval-cache-tests";
        createDirs(tmpDir);
        // Use a unique file per test process
        return tmpDir / ("test-cache-" + std::to_string(getpid()) + ".sqlite");
    }

    /**
     * Clear the test cache database and associated files.
     * Removes the main .sqlite file and any WAL/SHM/journal files.
     */
    static void removeTestCache()
    {
        auto basePath = getTestCachePath();

        // Remove main database file
        if (std::filesystem::exists(basePath)) {
            std::filesystem::remove(basePath);
        }

        // Remove associated SQLite files
        auto walPath = basePath;
        walPath += "-wal";
        if (std::filesystem::exists(walPath)) {
            std::filesystem::remove(walPath);
        }

        auto shmPath = basePath;
        shmPath += "-shm";
        if (std::filesystem::exists(shmPath)) {
            std::filesystem::remove(shmPath);
        }

        auto journalPath = basePath;
        journalPath += "-journal";
        if (std::filesystem::exists(journalPath)) {
            std::filesystem::remove(journalPath);
        }
    }

    /**
     * Evaluate a Nix expression and return an Object.
     * This tests the full evaluation pipeline for each implementation.
     */
    std::shared_ptr<Object> evalExpression(const std::string & expr)
    {
        auto implementation = GetParam();

        // Parse and evaluate the expression
        auto & state = *evalStateForTestSetupOnly;
        auto e = state.parseExprFromString(expr, state.rootPath(CanonPath::root));
        auto v = state.allocValue();

        debug("evalExpression: evaluating '%s' for implementation %s", expr, implementation);
        state.eval(e, *v);
        debug("evalExpression: eval completed");

        if (implementation == "Interpreter") {
            return std::make_shared<InterpreterObject>(state, allocRootValue(v));
        } else if (implementation == "CoarseEvalCache") {
            // Create an EvalCache without persistent storage
            auto cache = std::make_shared<eval_cache::EvalCache>(
                std::optional<std::filesystem::path>(std::nullopt), // No cache
                state,
                [v]() { return v; } // RootLoader that returns our evaluated value
            );

            // Get the root cursor and wrap it in a CoarseEvalCacheCursorObject
            auto cursor = cache->getRoot();
            return std::make_shared<CoarseEvalCacheCursorObject>(cursor);
        } else if (implementation == "CoarseEvalCacheWithPersistence") {
            // Use the controlled test cache path
            auto cachePath = getTestCachePath();

            // Create an EvalCache with persistent storage at our controlled path
            auto cache = std::make_shared<eval_cache::EvalCache>(
                cachePath, state, [v]() { return v; } // RootLoader that returns our evaluated value
            );

            // Get the root cursor and wrap it in a CoarseEvalCacheCursorObject
            auto cursor = cache->getRoot();
            return std::make_shared<CoarseEvalCacheCursorObject>(cursor);
        }
        throw std::runtime_error("Unknown implementation");
    }

    /**
     * Run test body, handling cache clearing for persistent cache tests.
     * For CoarseEvalCacheWithPersistence, runs the test twice:
     * - First with cold cache
     * - Second with warm cache (reusing existing data)
     */
    template<typename TestBody>
    void runTestWithCaching(TestBody body)
    {
        auto implementation = GetParam();

        if (implementation == "CoarseEvalCacheWithPersistence") {
            // Clear cache before the test case
            removeTestCache();

            // Run twice for persistent cache testing
            for (int run = 1; run <= 2; ++run) {
                testRunIteration = run;

                if (run == 1) {
                    SCOPED_TRACE("Cold cache run");
                } else {
                    SCOPED_TRACE("Warm cache run");
                }

                body();
            }

            // Clear cache after the test case
            removeTestCache();
        } else {
            // Single run for non-persistent implementations
            testRunIteration = 1;
            body();
        }
    }
};

// Macro to simplify writing tests that handle cache runs
#define EVALUATOR_TEST(TestName, TestBody)     \
    TEST_P(EvaluatorTest, TestName)            \
    {                                          \
        runTestWithCaching([this]() TestBody); \
    }

// Prevent accidental use of evalStateForTestSetupOnly in test cases
#define evalStateForTestSetupOnly #error evalStateForTestSetupOnly must not be used in tests

// Test Object::maybeGetAttr
EVALUATOR_TEST(Object_maybeGetAttr_ReturnsAttribute, {
    auto obj = evalExpression("{ foo = \"bar\"; baz = \"qux\"; }");
    auto fooAttr = obj->maybeGetAttr("foo");
    ASSERT_NE(fooAttr, nullptr);
    auto fooStr = fooAttr->getStringIgnoreContext();
    EXPECT_EQ(fooStr, "bar");
})

EVALUATOR_TEST(Object_maybeGetAttr_ReturnsNullForMissingAttribute, {
    auto obj = evalExpression("{ foo = \"bar\"; }");
    auto missingAttr = obj->maybeGetAttr("missing");
    EXPECT_EQ(missingAttr, nullptr);
})

EVALUATOR_TEST(Object_maybeGetAttr_ReturnsNullForNonAttrSet, {
    auto obj = evalExpression("\"not an attrset\"");
    auto attr = obj->maybeGetAttr("anything");
    EXPECT_EQ(attr, nullptr);
})

// Test Object::getAttrNames
EVALUATOR_TEST(Object_getAttrNames_ReturnsAttributeNames, {
    auto obj = evalExpression("{ foo = 1; bar = 2; baz = 3; }");
    auto attrNames = obj->getAttrNames();
    ASSERT_EQ(attrNames.size(), 3);
    // Sort for consistent comparison
    std::sort(attrNames.begin(), attrNames.end());
    EXPECT_EQ(attrNames[0], "bar");
    EXPECT_EQ(attrNames[1], "baz");
    EXPECT_EQ(attrNames[2], "foo");
})

EVALUATOR_TEST(Object_getAttrNames_ReturnsEmptyForEmptyAttrset, {
    auto obj = evalExpression("{ }");
    auto attrNames = obj->getAttrNames();
    EXPECT_EQ(attrNames.size(), 0);
})

EVALUATOR_TEST(Object_getAttrNames_ThrowsForNonAttrset, {
    auto obj = evalExpression("42");
    EXPECT_THROW(obj->getAttrNames(), Error);
})

EVALUATOR_TEST(Object_getAttrNames_WorksWithNestedAttrsets, {
    auto obj = evalExpression("{ a = { b = 1; }; c = 2; }");
    auto attrNames = obj->getAttrNames();
    ASSERT_EQ(attrNames.size(), 2);
    std::sort(attrNames.begin(), attrNames.end());
    EXPECT_EQ(attrNames[0], "a");
    EXPECT_EQ(attrNames[1], "c");
})

// Test Object::getStringIgnoreContext
EVALUATOR_TEST(Object_getStringIgnoreContext_ReturnsStringValue, {
    auto obj = evalExpression("\"hello world\"");
    auto str = obj->getStringIgnoreContext();
    EXPECT_EQ(str, "hello world");
})

EVALUATOR_TEST(Object_getStringIgnoreContext_ThrowsForNonString, {
    auto obj = evalExpression("42");
    EXPECT_THROW(obj->getStringIgnoreContext(), Error);
})

EVALUATOR_TEST(Object_getStringIgnoreContext_ThrowsForAttrSet, {
    auto obj = evalExpression("{ foo = \"bar\"; }");
    EXPECT_THROW(obj->getStringIgnoreContext(), Error);
})

// Test nested attribute access
EVALUATOR_TEST(Object_NestedAttributeAccess, {
    auto obj = evalExpression("{ outer = { inner = \"value\"; }; }");
    auto outer = obj->maybeGetAttr("outer");
    ASSERT_NE(outer, nullptr);
    auto inner = outer->maybeGetAttr("inner");
    ASSERT_NE(inner, nullptr);
    auto value = inner->getStringIgnoreContext();
    EXPECT_EQ(value, "value");
})

// Test forceDerivation helper - returns the store path of a derivation
EVALUATOR_TEST(Helper_forceDerivation, {
    // Create a simple derivation
    auto obj = evalExpression("derivation { name = \"test\"; system = \"x86_64-linux\"; builder = \"/bin/sh\"; }");
    // Force the derivation and get its store path using the helper
    auto drvPath = nix::expr::helpers::forceDerivation(*evaluator, *obj, *store);
    // Check that we got a derivation path
    EXPECT_TRUE(drvPath.isDerivation());
    // The path should end with .drv
    auto pathStr = store->printStorePath(drvPath);
    EXPECT_TRUE(pathStr.ends_with(".drv"));
    // The path should contain the name "test"
    EXPECT_TRUE(pathStr.find("test") != std::string::npos);
})

EVALUATOR_TEST(Helper_forceDerivation_MissingDrvPath, {
    auto obj = evalExpression("{ name = \"test\"; type = \"derivation\"; }");

    try {
        nix::expr::helpers::forceDerivation(*evaluator, *obj, *store);
        FAIL();
    } catch (const Error & e) {
        EXPECT_THAT(
            e.what(), nix::testing::HasSubstrIgnoreANSIMatcher("derivation does not contain a 'drvPath' attribute"));
    }
})

EVALUATOR_TEST(Helper_forceDerivation_InvalidDrvPath, {
    // builtins.toFile returns a store path string that doesn't end in .drv
    auto obj = evalExpression(R"({
        type = "derivation";
        drvPath = builtins.toFile "not-a-drv" "content";
    })");

    try {
        nix::expr::helpers::forceDerivation(*evaluator, *obj, *store);
        FAIL();
    } catch (const Error & e) {
        EXPECT_THAT(
            e.what(),
            nix::testing::HasSubstrIgnoreANSIMatcher("while evaluating the 'drvPath' attribute of a derivation"));
    }
})

EVALUATOR_TEST(Helper_forceDerivation_DrvPathNotString, {
    auto obj = evalExpression("{ type = \"derivation\"; drvPath = 42; }");

    try {
        nix::expr::helpers::forceDerivation(*evaluator, *obj, *store);
        FAIL();
    } catch (const Error & e) {
        // Different implementations have different error messages:
        // Interpreter: "value is an integer while a string was expected"
        // CoarseEvalCache: "'drvPath' is not a string"
        EXPECT_THAT(
            e.what(),
            ::testing::AnyOf(
                nix::testing::HasSubstrIgnoreANSIMatcher("value is an integer while a string was expected"),
                nix::testing::HasSubstrIgnoreANSIMatcher("'drvPath' is not a string")));
    }
})

// Test Object::getBool
EVALUATOR_TEST(Object_getBool_ReturnsTrue, {
    auto obj = evalExpression("true");
    EXPECT_TRUE(obj->getBool(""));
})

EVALUATOR_TEST(Object_getBool_ReturnsFalse, {
    auto obj = evalExpression("false");
    EXPECT_FALSE(obj->getBool(""));
})

EVALUATOR_TEST(Object_getBool_ThrowsWhenNotABool, {
    auto obj = evalExpression("\"not a bool\"");
    try {
        obj->getBool("");
        FAIL();
    } catch (const Error & e) {
        EXPECT_THAT(
            e.what(),
            ::testing::AnyOf(
                nix::testing::HasSubstrIgnoreANSIMatcher("expected a Boolean but found a string"),
                nix::testing::HasSubstrIgnoreANSIMatcher("'' is not a Boolean")));
    }
})

EVALUATOR_TEST(Object_getBool_IncludesErrorContext, {
    auto obj = evalExpression("42");
    try {
        obj->getBool("while checking some_bool_context");
        FAIL();
    } catch (const Error & e) {
        EXPECT_THAT(e.what(), nix::testing::HasSubstrIgnoreANSIMatcher("while checking some_bool_context"));
        EXPECT_THAT(
            e.what(),
            ::testing::AnyOf(
                nix::testing::HasSubstrIgnoreANSIMatcher("expected a Boolean but found an integer"),
                // CoarseEvalCache shows '' (the root attribute path) in the error.
                // This is a contrived test - in practice we use this on specific flake output attributes, so this isn't
                // a problem.
                nix::testing::HasSubstrIgnoreANSIMatcher("'' is not a Boolean")));
    }
})

// Test Object::getInt
EVALUATOR_TEST(Object_getInt_ReturnsInteger, {
    auto obj = evalExpression("42");
    EXPECT_EQ(obj->getInt("").value, 42);
})

EVALUATOR_TEST(Object_getInt_ReturnsNegativeInteger, {
    auto obj = evalExpression("-123");
    EXPECT_EQ(obj->getInt("").value, -123);
})

EVALUATOR_TEST(Object_getInt_ThrowsWhenNotAnInt, {
    auto obj = evalExpression("\"some_string\"");
    try {
        obj->getInt("");
        FAIL();
    } catch (const Error & e) {
        EXPECT_THAT(
            e.what(),
            ::testing::AnyOf(
                nix::testing::HasSubstrIgnoreANSIMatcher("expected an integer but found a string"),
                nix::testing::HasSubstrIgnoreANSIMatcher("'' is not an integer")));
    }
})

EVALUATOR_TEST(Object_getInt_IncludesErrorContext, {
    auto obj = evalExpression("true");
    try {
        obj->getInt("while evaluating some_int_context");
        FAIL();
    } catch (const Error & e) {
        EXPECT_THAT(e.what(), nix::testing::HasSubstrIgnoreANSIMatcher("while evaluating some_int_context"));
        EXPECT_THAT(
            e.what(),
            ::testing::AnyOf(
                nix::testing::HasSubstrIgnoreANSIMatcher("expected an integer but found a Boolean"),
                // CoarseEvalCache shows '' (the root attribute path) in the error.
                // This is a contrived test - in practice we use this on specific flake output attributes, so this isn't
                // a problem.
                nix::testing::HasSubstrIgnoreANSIMatcher("'' is not an integer")));
    }
})

// Test Object::getListOfStringsNoCtx
EVALUATOR_TEST(Object_getListOfStringsNoCtx_ReturnsListOfStrings, {
    auto obj = evalExpression("[\"foo\" \"bar\" \"baz\"]");
    auto result = obj->getListOfStringsNoCtx();
    ASSERT_EQ(result.size(), 3);
    EXPECT_EQ(result[0], "foo");
    EXPECT_EQ(result[1], "bar");
    EXPECT_EQ(result[2], "baz");
})

EVALUATOR_TEST(Object_getListOfStringsNoCtx_ThrowsWhenNotAList, {
    auto obj = evalExpression("\"not a list\"");
    try {
        obj->getListOfStringsNoCtx();
        FAIL();
    } catch (const Error & e) {
        EXPECT_THAT(
            e.what(),
            ::testing::AnyOf(
                nix::testing::HasSubstrIgnoreANSIMatcher("expected a list but found a string"),
                nix::testing::HasSubstrIgnoreANSIMatcher("'' is not a list")));
    }
})

EVALUATOR_TEST(Object_getListOfStringsNoCtx_ThrowsWhenListContainsNonString, {
    auto obj = evalExpression("[\"foo\" 42]");
    try {
        obj->getListOfStringsNoCtx();
        FAIL();
    } catch (const Error & e) {
        EXPECT_THAT(
            e.what(),
            ::testing::AnyOf(
                nix::testing::HasSubstrIgnoreANSIMatcher("value is an integer while a string was expected"),
                nix::testing::HasSubstrIgnoreANSIMatcher("expected a string but found an integer")));
    }
})

EVALUATOR_TEST(Object_getListOfStringsNoCtx_ReturnsEmptyListForEmptyList, {
    auto obj = evalExpression("[]");
    auto result = obj->getListOfStringsNoCtx();
    EXPECT_EQ(result.size(), 0);
})

// Test Object::getType and getTypeLazy for nThunk
EVALUATOR_TEST(Object_getType_nThunk, {
    // Note: This test only works with Interpreter because CoarseEvalCache
    // always forces values, so it never exposes thunks
    if (GetParam() != "Interpreter") {
        GTEST_SKIP() << "Thunk testing only implemented for Interpreter";
    }

    // Create an attrset with a thunk value: the argument to f is a thunk
    auto obj = evalExpression("{ a = (let f = x: x; in f 1); }");
    auto attrA = obj->maybeGetAttr("a");
    ASSERT_NE(attrA, nullptr);

    // For Interpreter, the attribute value should still be a thunk
    // getTypeLazy should return nThunk without forcing
    EXPECT_EQ(attrA->getTypeLazy(), nThunk);

    // getType should force evaluation and return the actual type
    EXPECT_EQ(attrA->getType(), nInt);
})

// Test Object::getType and getTypeLazy for nInt
EVALUATOR_TEST(Object_getType_nInt, {
    auto obj = evalExpression("{ x = (v: v) 42; }")->maybeGetAttr("x");
    ASSERT_NE(obj, nullptr);
    EXPECT_THAT(obj->getTypeLazy(), ::testing::AnyOf(nThunk, nInt));
    EXPECT_EQ(obj->getType(), nInt);
})

// Test Object::getType and getTypeLazy for nFloat
EVALUATOR_TEST(Object_getType_nFloat, {
    auto obj = evalExpression("{ x = (v: v) 3.14; }")->maybeGetAttr("x");
    ASSERT_NE(obj, nullptr);
    EXPECT_THAT(obj->getTypeLazy(), ::testing::AnyOf(nThunk, nFloat));
    EXPECT_EQ(obj->getType(), nFloat);
})

// Test Object::getType and getTypeLazy for nBool
EVALUATOR_TEST(Object_getType_nBool, {
    auto obj = evalExpression("{ x = (v: v) true; }")->maybeGetAttr("x");
    ASSERT_NE(obj, nullptr);
    EXPECT_THAT(obj->getTypeLazy(), ::testing::AnyOf(nThunk, nBool));
    EXPECT_EQ(obj->getType(), nBool);
})

// Test Object::getType and getTypeLazy for nString
EVALUATOR_TEST(Object_getType_nString, {
    auto obj = evalExpression("{ x = (v: v) \"test string\"; }")->maybeGetAttr("x");
    ASSERT_NE(obj, nullptr);
    EXPECT_THAT(obj->getTypeLazy(), ::testing::AnyOf(nThunk, nString));
    EXPECT_EQ(obj->getType(), nString);
})

// Test Object::getType and getTypeLazy for nPath
EVALUATOR_TEST(Object_getType_nPath, {
    auto obj = evalExpression("{ x = (v: v) /some/path; }")->maybeGetAttr("x");
    ASSERT_NE(obj, nullptr);
    // Note: Paths are coerced to strings in the cache, which is undesirable but reflects current behavior
    EXPECT_THAT(obj->getTypeLazy(), ::testing::AnyOf(nThunk, nPath, nString));
    if (GetParam() == "Interpreter") {
        EXPECT_EQ(obj->getType(), nPath);
    } else {
        // CoarseEvalCache coerces paths to strings when caching
        EXPECT_THAT(obj->getType(), ::testing::AnyOf(nPath, nString));
    }
})

// Test Object::getType and getTypeLazy for nNull
EVALUATOR_TEST(Object_getType_nNull, {
    auto obj = evalExpression("{ x = (v: v) null; }")->maybeGetAttr("x");
    ASSERT_NE(obj, nullptr);
    EXPECT_THAT(obj->getTypeLazy(), ::testing::AnyOf(nThunk, nNull));
    EXPECT_EQ(obj->getType(), nNull);
})

// Test Object::getType and getTypeLazy for nAttrs
EVALUATOR_TEST(Object_getType_nAttrs, {
    auto obj = evalExpression("{ foo = \"bar\"; }");
    EXPECT_THAT(obj->getTypeLazy(), ::testing::AnyOf(nThunk, nAttrs));
    EXPECT_EQ(obj->getType(), nAttrs);
})

// Test Object::getType and getTypeLazy for nList
EVALUATOR_TEST(Object_getType_nList, {
    auto obj = evalExpression("[\"foo\"]");
    EXPECT_THAT(obj->getTypeLazy(), ::testing::AnyOf(nThunk, nList));
    EXPECT_EQ(obj->getType(), nList);
})

// Test Object::getType and getTypeLazy for nFunction
EVALUATOR_TEST(Object_getType_nFunction, {
    auto obj = evalExpression("x: x + 1");
    EXPECT_THAT(obj->getTypeLazy(), ::testing::AnyOf(nThunk, nFunction));
    EXPECT_EQ(obj->getType(), nFunction);
})

// Test Object::getType and getTypeLazy for nExternal
EVALUATOR_TEST(Object_getType_nExternal, {
    // External values are plugin-defined values
    // There's no Nix syntax to create them, and we cannot create them
    // through the Object interface without internal state access.
    // Skip this test as external values are not commonly used in practice.
    // TODO: add test for Interpreter only
    (void) this;
    GTEST_SKIP() << "Cannot test external values without internal state access";
})

// Test Object::getStringWithContext
EVALUATOR_TEST(Object_getStringWithContext_PlainString, {
    auto obj = evalExpression("\"hello world\"");
    auto result = obj->getStringWithContext();
    EXPECT_EQ(result.first, "hello world");
    EXPECT_TRUE(result.second.empty());
})

EVALUATOR_TEST(Object_getStringWithContext_WithDerivationContext, {
    auto obj = evalExpression(R"(
        let drv = derivation { name = "test"; system = "x86_64-linux"; builder = "/bin/sh"; };
        in "${drv.drvPath}"
    )");
    auto result = obj->getStringWithContext();
    // String should be the drv path
    EXPECT_TRUE(result.first.ends_with(".drv"));
    // Context should contain the derivation
    EXPECT_FALSE(result.second.empty());
    EXPECT_EQ(result.second.size(), 1);
})

EVALUATOR_TEST(Object_getStringWithContext_WithOutputContext, {
    auto obj = evalExpression(R"(
        let drv = derivation { name = "test"; system = "x86_64-linux"; builder = "/bin/sh"; };
        in "${drv.out}"
    )");
    auto result = obj->getStringWithContext();
    // String should be a store path
    EXPECT_TRUE(result.first.starts_with("/nix/store/"));
    // Context should contain the output path
    EXPECT_FALSE(result.second.empty());
})

EVALUATOR_TEST(Object_getStringWithContext_WithMultipleOutputs, {
    auto obj = evalExpression(R"(
        let drv = derivation {
            name = "multi-output-test";
            system = "x86_64-linux";
            builder = "/bin/sh";
            outputs = [ "out" "dev" "doc" ];
        };
        in "${drv.out} ${drv.dev}"
    )");
    auto result = obj->getStringWithContext();
    // String should contain store paths separated by space
    EXPECT_TRUE(result.first.starts_with("/nix/store/"));
    EXPECT_TRUE(result.first.find(" /nix/store/") != std::string::npos);
    // Context should contain multiple output references
    EXPECT_FALSE(result.second.empty());
    EXPECT_GE(result.second.size(), 2);
})

EVALUATOR_TEST(Object_getStringWithContext_ThrowsForNonString, {
    auto obj = evalExpression("42");
    try {
        obj->getStringWithContext();
        FAIL();
    } catch (const Error & e) {
        EXPECT_THAT(
            e.what(),
            ::testing::AnyOf(
                nix::testing::HasSubstrIgnoreANSIMatcher("value is an integer while a string was expected"),
                nix::testing::HasSubstrIgnoreANSIMatcher("expected a string but found an integer"),
                // CoarseEvalCache shows '' (the root attribute path) in the error
                nix::testing::HasSubstrIgnoreANSIMatcher("'' is not a string")));
    }
})

EVALUATOR_TEST(Object_getStringWithContext_CoercesPath, {
    // Skip for Interpreter - it doesn't coerce paths in getStringWithContext
    // NOTE: Path coercion to string is not actually desirable behavior,
    // but this test documents the current implementation difference.
    if (GetParam() == "Interpreter") {
        GTEST_SKIP() << "Interpreter doesn't coerce paths in getStringWithContext";
    }
    auto obj = evalExpression("/some/path");
    auto result = obj->getStringWithContext();
    EXPECT_EQ(result.first, "/some/path");
    EXPECT_TRUE(result.second.empty());
})

// Test Object::getPath
EVALUATOR_TEST(Object_getPath_ReturnsPath, {
    // CoarseEvalCache coerces paths to strings in the cache, which breaks getPath()
    // This is a limitation of the current database format
    if (GetParam() != "Interpreter") {
        GTEST_SKIP() << "Path caching not supported in current database format";
    }
    auto obj = evalExpression("/some/path");
    auto path = obj->getPath();
    EXPECT_EQ(path.path.abs(), "/some/path");
})

EVALUATOR_TEST(Object_getPath_ThrowsForNonPath, {
    auto obj = evalExpression("\"not a path\"");
    try {
        obj->getPath();
        FAIL();
    } catch (const Error & e) {
        EXPECT_THAT(
            e.what(),
            ::testing::AnyOf(
                nix::testing::HasSubstrIgnoreANSIMatcher("value is a string while a path was expected"),
                nix::testing::HasSubstrIgnoreANSIMatcher("expected a path but found a string")));
    }
})

EVALUATOR_TEST(Object_getPath_ThrowsForInteger, {
    auto obj = evalExpression("42");
    try {
        obj->getPath();
        FAIL();
    } catch (const Error & e) {
        EXPECT_THAT(
            e.what(),
            ::testing::AnyOf(
                nix::testing::HasSubstrIgnoreANSIMatcher("value is an integer while a path was expected"),
                nix::testing::HasSubstrIgnoreANSIMatcher("expected a path but found an integer")));
    }
})

// Test Object::defeatCache() - bypasses lossy cache to get actual Value
EVALUATOR_TEST(Object_defeatCache_ReturnsValue, {
    auto obj = evalExpression("42");
    auto value = obj->defeatCache();
    ASSERT_NE(value, nullptr);
    EXPECT_EQ((*value)->type(), nInt);
    EXPECT_EQ((*value)->integer().value, 42);
})

EVALUATOR_TEST(Object_defeatCache_WorksWithPaths, {
    // This tests the specific case where defeatCache() is needed:
    // paths are cached as strings without context (lossy)
    auto obj = evalExpression("/some/path");
    auto value = obj->defeatCache();
    ASSERT_NE(value, nullptr);
    // For Interpreter, this should be nPath
    // For CoarseEvalCache, it might be nString (cache is lossy)
    // But defeatCache() should give us the actual type
    if (GetParam() == "Interpreter") {
        EXPECT_EQ((*value)->type(), nPath);
    }
    // Note: CoarseEvalCache defeatCache() forces evaluation, so it should also return nPath
    EXPECT_EQ((*value)->type(), nPath);
})

EVALUATOR_TEST(Object_defeatCache_WorksWithStringsWithContext, {
    // Create a string with context (from a derivation)
    auto obj = evalExpression(R"(
        let drv = derivation { name = "test"; system = "x86_64-linux"; builder = "/bin/sh"; };
        in "${drv}"
    )");
    auto value = obj->defeatCache();
    ASSERT_NE(value, nullptr);
    EXPECT_EQ((*value)->type(), nString);
    // The string should have context (derivation path)
    // context() returns const char ** (NULL-terminated array)
    EXPECT_NE((*value)->context(), nullptr);
    EXPECT_NE((*value)->context()[0], nullptr);
})

// Instantiate tests for each implementation
INSTANTIATE_TEST_SUITE_P(
    EvaluatorImplementations,
    EvaluatorTest,
    ::testing::Values("Interpreter", "CoarseEvalCache", "CoarseEvalCacheWithPersistence"),
    [](const ::testing::TestParamInfo<std::string> & info) { return info.param; });

} // namespace nix::expr
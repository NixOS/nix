#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/expr/eval-cache.hh"
#include "nix/expr/tests/libexpr.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/file-system.hh"

namespace nix {

using namespace eval_cache;
using testing::_;
using testing::AnyNumber;

/**
 * Mocks the GC-root and validity operations, which each cost a daemon round-trip with a RemoteStore.
 */
struct CountingStore : DummyStoreImpl
{
    CountingStore(ref<const Config> config)
        : Store{*config}
        , DummyStoreImpl{config}
    {
        ON_CALL(*this, addTempRoot).WillByDefault([this](auto & path) { DummyStoreImpl::addTempRoot(path); });
        ON_CALL(*this, isValidPathUncached).WillByDefault([this](auto & path) {
            return DummyStoreImpl::isValidPathUncached(path);
        });
        ON_CALL(*this, queryValidPaths).WillByDefault([this](auto & paths, auto maybeSubstitute) {
            return DummyStoreImpl::queryValidPaths(paths, maybeSubstitute);
        });
        EXPECT_CALL(*this, addTempRoot).Times(AnyNumber());
        EXPECT_CALL(*this, isValidPathUncached).Times(AnyNumber());
        EXPECT_CALL(*this, queryValidPaths).Times(AnyNumber());
    }

    MOCK_METHOD(void, addTempRoot, (const StorePath & path), (override));
    MOCK_METHOD(bool, isValidPathUncached, (const StorePath & path), (override));
    MOCK_METHOD(
        StorePathSet, queryValidPaths, (const StorePathSet & paths, SubstituteFlag maybeSubstitute), (override));

    StorePath addText(std::string_view name)
    {
        StringSource source{name};
        return addToStoreFromDump(
            source, name, FileSerialisationMethod::Flat, ContentAddressMethod::Raw::Text, HashAlgorithm::SHA256);
    }
};

class EvalCacheTest : public LibExprTest
{
protected:
    ref<CountingStore> counting;
    unsigned rootLoads = 0;

    EvalCacheTest()
        : EvalCacheTest([] {
            auto config = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{});
            config->readOnly = false;
            return make_ref<CountingStore>(config);
        }())
    {
    }

    EvalCacheTest(ref<CountingStore> counting)
        : LibExprTest(
              counting,
              [](bool & readOnlyMode) {
                  EvalSettings settings{readOnlyMode};
                  settings.nixPath = {};
                  return settings;
              })
        , counting(counting)
    {
        setEnv("XDG_CACHE_HOME", createTempDir().c_str());
    }

    ref<EvalCache> makeCache(const Hash & fingerprint, Value & vRoot)
    {
        return make_ref<EvalCache>(std::cref(fingerprint), state, [this, &vRoot]() {
            rootLoads++;
            return &vRoot;
        });
    }

    Value evalStringWithContext(const StorePathSet & paths)
    {
        std::string context;
        for (auto & path : paths)
            context += fmt("\"%s\" = { path = true; };\n", store->printStorePath(path));
        return eval(fmt("{ x = builtins.appendContext \"foo\" { %s }; }", context));
    }
};

TEST_F(EvalCacheTest, cachedStringContextIsValidatedInOneBatch)
{
    auto fp = hashString(HashAlgorithm::SHA256, "eval-cache-test-1");
    Value vRoot = evalStringWithContext({counting->addText("a"), counting->addText("b"), counting->addText("c")});

    makeCache(fp, vRoot)->getRoot()->getAttr("x")->getStringWithContext();

    rootLoads = 0;
    EXPECT_CALL(*counting, addTempRoot).Times(3);
    EXPECT_CALL(*counting, queryValidPaths(testing::SizeIs(3), _)).Times(1);
    EXPECT_CALL(*counting, isValidPathUncached).Times(0);
    auto s = makeCache(fp, vRoot)->getRoot()->getAttr("x")->getStringWithContext();

    EXPECT_EQ(s.first, "foo");
    EXPECT_EQ(s.second.size(), 3);
    EXPECT_EQ(rootLoads, 0);
}

TEST_F(EvalCacheTest, cachedStringWithAnInvalidContextPathIsEvaluatedAgain)
{
    auto fp = hashString(HashAlgorithm::SHA256, "eval-cache-test-2");
    auto collected = counting->addText("b");
    Value vRoot = evalStringWithContext({counting->addText("a"), collected});

    makeCache(fp, vRoot)->getRoot()->getAttr("x")->getStringWithContext();

    counting->contents.erase(collected);
    counting->clearPathInfoCache();
    rootLoads = 0;
    auto s = makeCache(fp, vRoot)->getRoot()->getAttr("x")->getStringWithContext();

    EXPECT_EQ(s.first, "foo");
    EXPECT_EQ(rootLoads, 1);
}

TEST_F(EvalCacheTest, concurrentCachesOfOneFlakeAllWrite)
{
    auto fp = hashString(HashAlgorithm::SHA256, "eval-cache-test-3");
    Value vRoot = eval(R"({ a = "1"; b = "2"; c = "3"; })");

    makeCache(fp, vRoot)->getRoot()->getAttr("a")->getString();

    auto second = makeCache(fp, vRoot);
    {
        auto first = makeCache(fp, vRoot);
        EXPECT_EQ(second->getRoot()->getAttr("a")->getString(), "1");
        EXPECT_EQ(first->getRoot()->getAttr("b")->getString(), "2");
    }
    EXPECT_EQ(second->getRoot()->getAttr("c")->getString(), "3");

    rootLoads = 0;
    auto third = makeCache(fp, vRoot);
    EXPECT_EQ(third->getRoot()->getAttr("b")->getString(), "2");
    EXPECT_EQ(third->getRoot()->getAttr("c")->getString(), "3");
    EXPECT_EQ(rootLoads, 0) << "every evaluator's writes reached the shared cache file";
}

TEST_F(EvalCacheTest, cachedDeterministicEvalErrorIsReportedWithoutLoadingFlakeRootAndRethrownByForce)
{
    auto fp = hashString(HashAlgorithm::SHA256, "eval-cache-test-4");
    Value vRoot = eval(R"({ x = throw "boom"; })");

    EXPECT_THROW(makeCache(fp, vRoot)->getRoot()->getAttr("x")->getString(), ThrownError);

    rootLoads = 0;
    try {
        makeCache(fp, vRoot)->getRoot()->getAttr("x")->getString();
        FAIL() << "expected a cached failure";
    } catch (CachedEvalError & e) {
        EXPECT_EQ(rootLoads, 0);
        EXPECT_THROW(e.force(), ThrownError);
    }
}

TEST_F(EvalCacheTest, failedEvaluationWithInvalidStorePathInStringContextIsNotCachedAndIsRetried)
{
    auto fp = hashString(HashAlgorithm::SHA256, "eval-cache-test-5");
    auto collected = counting->addText("collected");
    auto path = store->printStorePath(collected);
    Value vBroken = eval(fmt(
        R"(let s = builtins.appendContext "%s" { "%s" = { path = true; }; }; in builtins.seq s { x = builtins.readFile s; })",
        path,
        path));
    Value vFixed = eval(R"({ x = "ok"; })");

    counting->contents.erase(collected);
    counting->clearPathInfoCache();
    EXPECT_THROW(makeCache(fp, vBroken)->getRoot()->getAttr("x")->getString(), InvalidPathError);
    EXPECT_EQ(makeCache(fp, vFixed)->getRoot()->getAttr("x")->getString(), "ok");
}

} // namespace nix

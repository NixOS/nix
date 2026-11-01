#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/expr/eval-cache.hh"
#include "nix/expr/tests/libexpr.hh"
#include "nix/store/store-open.hh"
#include "nix/store/tests/forwarding-store.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/file-system.hh"

namespace nix {

using namespace eval_cache;

/**
 * Records the GC-root and validity operations, which are daemon round-trips with a `RemoteStore`.
 */
struct CountingStore : ForwardingStore
{
    std::vector<StorePath> tempRoots;
    std::vector<StorePath> validityChecks;
    std::vector<StorePathSet> validityBatches;

    using ForwardingStore::ForwardingStore;

    void addTempRoot(const StorePath & path) override
    {
        tempRoots.push_back(path);
    }

    bool isValidPathUncached(const StorePath & path) override
    {
        validityChecks.push_back(path);
        return true;
    }

    StorePathSet queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute) override
    {
        validityBatches.push_back(paths);
        return paths;
    }
};

class EvalCacheTest : public LibExprTest
{
protected:
    ref<CountingStore> counting;

    EvalCacheTest()
        : EvalCacheTest(make_ref<CountingStore>(openStore("dummy://?read-only=false")))
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
        return make_ref<EvalCache>(std::cref(fingerprint), state, [&vRoot]() { return &vRoot; });
    }
};

TEST_F(EvalCacheTest, cachedStringContextIsValidatedInOneBatch)
{
    auto fp = hashString(HashAlgorithm::SHA256, "eval-cache-test-1");
    Value vRoot = eval(R"({
      x = builtins.appendContext "foo" {
        "/nix/store/00000000000000000000000000000001-ctx-a" = { path = true; };
        "/nix/store/00000000000000000000000000000002-ctx-b" = { path = true; };
        "/nix/store/00000000000000000000000000000003-ctx-c" = { path = true; };
      };
    })");

    makeCache(fp, vRoot)->getRoot()->getAttr("x")->getStringWithContext();

    counting->tempRoots.clear();
    counting->validityChecks.clear();
    counting->validityBatches.clear();
    auto s = makeCache(fp, vRoot)->getRoot()->getAttr("x")->getStringWithContext();

    EXPECT_EQ(s.first, "foo");
    EXPECT_EQ(s.second.size(), 3);
    EXPECT_EQ(counting->tempRoots.size(), 3);
    EXPECT_EQ(counting->validityBatches.size(), 1);
    EXPECT_EQ(counting->validityChecks.size(), 0);
}

} // namespace nix

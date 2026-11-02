#include <gtest/gtest.h>

#include "nix/expr/tests/libexpr.hh"
#include "nix/store/build.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-open.hh"
#include "nix/store/tests/forwarding-store.hh"
#include "nix/util/finally.hh"

namespace nix {

struct FailingBuilder : Builder
{
    unsigned & attempts;

    FailingBuilder(unsigned & attempts)
        : attempts(attempts)
    {
    }

    void buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode) override
    {
        attempts++;
        throw Error("the build failed");
    }

    std::vector<KeyedBuildResult>
    buildPathsWithResults(const std::vector<DerivedPath> & reqs, BuildMode buildMode) override
    {
        throw Unsupported("buildPathsWithResults");
    }

    BuildResult buildDerivation(const StorePath & drvPath, const BasicDerivation & drv, BuildMode buildMode) override
    {
        throw Unsupported("buildDerivation");
    }

    void ensurePath(const StorePath & path) override
    {
        throw Unsupported("ensurePath");
    }

    void repairPath(const StorePath & path) override
    {
        throw Unsupported("repairPath");
    }
};

struct FailingBuildStore : ForwardingStore
{
    unsigned buildAttempts = 0;

    using ForwardingStore::ForwardingStore;

    ref<Builder> getBuilder(std::shared_ptr<Store> evalStore) override
    {
        return make_ref<FailingBuilder>(buildAttempts);
    }
};

class ImportFromDerivationTest : public LibExprTest
{
protected:
    ref<FailingBuildStore> failing;

    ImportFromDerivationTest()
        : ImportFromDerivationTest(make_ref<FailingBuildStore>(openStore("dummy://?read-only=false")))
    {
    }

    ImportFromDerivationTest(ref<FailingBuildStore> failing)
        : LibExprTest(
              failing,
              [](bool & readOnlyMode) {
                  EvalSettings settings{readOnlyMode};
                  settings.nixPath = {};
                  return settings;
              })
        , failing(failing)
    {
    }
};

TEST_F(ImportFromDerivationTest, failedRealisationIsReplayed)
{
    bool oldReadOnly = settings.readOnlyMode;
    settings.readOnlyMode = false;
    Finally restore([&]() { settings.readOnlyMode = oldReadOnly; });

    Value v = eval(R"(
      let d = derivation { name = "broken"; system = "x86_64-linux"; builder = "/bin/sh"; };
      in { a = import d; b = import d; }
    )");
    auto * a = v.attrs()->get(createSymbol("a"))->value;
    auto * b = v.attrs()->get(createSymbol("b"))->value;

    EXPECT_THROW(state.forceValue(*a, noPos), Error);
    EXPECT_EQ(failing->buildAttempts, 1);

    EXPECT_THROW(state.forceValue(*a, noPos), Error);
    EXPECT_THROW(state.forceValue(*b, noPos), Error);
    EXPECT_EQ(failing->buildAttempts, 1) << "a failed realisation is replayed, not attempted again";
}

} // namespace nix

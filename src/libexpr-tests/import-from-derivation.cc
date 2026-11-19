#include <gtest/gtest.h>

#include "nix/expr/tests/libexpr.hh"
#include "nix/store/build.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/store/globals.hh"

namespace nix {

struct FailingBuilder : Builder
{
    unsigned & attempts;
    const BuildResult::Failure::Status & status;

    FailingBuilder(unsigned & attempts, const BuildResult::Failure::Status & status)
        : attempts(attempts)
        , status(status)
    {
    }

    void buildPaths(const std::vector<DerivedPath> & reqs, BuildMode buildMode) override
    {
        throw Unsupported("buildPaths");
    }

    std::vector<KeyedBuildResult>
    buildPathsWithResults(const std::vector<DerivedPath> & reqs, BuildMode buildMode) override
    {
        attempts++;
        std::vector<KeyedBuildResult> results;
        for (auto & req : reqs)
            results.emplace_back(BuildResult{.inner = BuildResult::Failure{status, "the build failed"}}, req);
        return results;
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

struct FailingBuildStore : DummyStoreImpl
{
    unsigned buildAttempts = 0;
    BuildResult::Failure::Status buildFailureStatus = BuildResult::Failure::PermanentFailure;

    FailingBuildStore(ref<const Config> config)
        : Store{*config}
        , DummyStoreImpl{config}
    {
    }

    ref<Builder> getBuilder(std::shared_ptr<Store> evalStore) override
    {
        return make_ref<FailingBuilder>(buildAttempts, buildFailureStatus);
    }
};

class ImportFromDerivationTest : public LibExprTest
{
protected:
    ref<FailingBuildStore> failing;
    bool previousReadOnlyMode = std::exchange(settings.readOnlyMode, false);

    ImportFromDerivationTest()
        : ImportFromDerivationTest([] {
            auto config = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{});
            config->readOnly = false;
            return make_ref<FailingBuildStore>(config);
        }())
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

    ~ImportFromDerivationTest()
    {
        settings.readOnlyMode = previousReadOnlyMode;
    }

    Value & getAttributeValueByName(Value & v, const char * name)
    {
        return *v.attrs()->get(createSymbol(name))->value;
    }

    size_t traceCount(Value & v)
    {
        try {
            state.forceValue(v, noPos);
        } catch (Error & e) {
            return e.info().traces.size();
        }
        ADD_FAILURE() << "forcing the import did not throw";
        return 0;
    }
};

static constexpr std::string_view failingImportFromDerivationNixExpression = R"(
  let d = derivation { name = "broken"; system = "x86_64-linux"; builder = "/bin/sh"; };
  in { a = import d; b = import d; }
)";

TEST_F(ImportFromDerivationTest, failedRealisationIsReplayed)
{
    Value v = eval(std::string{failingImportFromDerivationNixExpression});

    auto tracesOfA = traceCount(getAttributeValueByName(v, "a"));
    EXPECT_EQ(failing->buildAttempts, 1);

    EXPECT_EQ(traceCount(getAttributeValueByName(v, "b")), tracesOfA)
        << "the replayed failure has none of the traces of its first throw";
    EXPECT_EQ(failing->buildAttempts, 1) << "a failed realisation is replayed, not attempted again";
}

TEST_F(ImportFromDerivationTest, transientFailureIsNotRemembered)
{
    failing->buildFailureStatus = BuildResult::Failure::TransientFailure;
    Value v = eval(std::string{failingImportFromDerivationNixExpression});

    traceCount(getAttributeValueByName(v, "a"));
    traceCount(getAttributeValueByName(v, "b"));
    EXPECT_EQ(failing->buildAttempts, 2);
}

TEST_F(ImportFromDerivationTest, timedOutBuildIsRemembered)
{
    failing->buildFailureStatus = BuildResult::Failure::TimedOut;
    Value v = eval(std::string{failingImportFromDerivationNixExpression});

    traceCount(getAttributeValueByName(v, "a"));
    traceCount(getAttributeValueByName(v, "b"));
    EXPECT_EQ(failing->buildAttempts, 1);
}

TEST_F(ImportFromDerivationTest, resetFileCacheForgetsFailedBuild)
{
    Value v = eval(std::string{failingImportFromDerivationNixExpression});

    traceCount(getAttributeValueByName(v, "a"));
    state.resetFileCache();
    traceCount(getAttributeValueByName(v, "b"));
    EXPECT_EQ(failing->buildAttempts, 2);
}

TEST_F(ImportFromDerivationTest, twoOutputFailureIsReplayedForSingleOutputImport)
{
    Value v = eval(R"(
      let d = derivation { name = "broken"; system = "x86_64-linux"; builder = "/bin/sh"; outputs = [ "out" "dev" ]; };
      in { both = import (d.out + d.dev); dev = import d.dev; }
    )");

    traceCount(getAttributeValueByName(v, "both"));
    traceCount(getAttributeValueByName(v, "dev"));
    EXPECT_EQ(failing->buildAttempts, 1);
}

} // namespace nix

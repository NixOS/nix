#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "nix/store/build/worker.hh"
#include "nix/store/derivations.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/store/globals.hh"
#include "nix/util/memory-source-accessor.hh"

#include "nix/store/tests/libstore.hh"
#include "nix/util/tests/json-characterization.hh"

namespace nix {

class WorkerSubstitutionTest : public LibStoreTest, public JsonCharacterizationTest<ref<DummyStore>>
{
    std::filesystem::path unitTestData = getUnitTestData() / "worker-substitution";

protected:
    ref<DummyStore> dummyStore;
    ref<DummyStore> substituter;

    WorkerSubstitutionTest()
        : LibStoreTest([] {
            auto config = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{});
            config->readOnly = false;
            return config->openDummyStore();
        }())
        , dummyStore(store.dynamic_pointer_cast<DummyStore>())
        , substituter([] {
            auto config = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{});
            config->readOnly = false;
            config->isTrusted = true;
            return config->openDummyStore();
        }())
    {
    }

public:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }

    static void SetUpTestSuite()
    {
        initLibStore(false);
    }
};

TEST_F(WorkerSubstitutionTest, singleStoreObject)
{
    // Add a store path to the substituter
    auto pathInSubstituter = substituter->addToStore(
        "hello",
        SourcePath{
            [] {
                auto sc = make_ref<MemorySourceAccessor>();
                sc->root = MemorySourceAccessor::File{MemorySourceAccessor::File::Regular{
                    .executable = false,
                    .contents = "Hello, world!",
                }};
                return sc;
            }(),
        },
        ContentAddressMethod::Raw::NixArchive,
        HashAlgorithm::SHA256);

    // Snapshot the substituter (has one store object)
    checkpointJson("single/substituter", substituter);

    // Snapshot the destination store before (should be empty)
    checkpointJson("../dummy-store/empty", dummyStore);

    // The path should not exist in the destination store yet
    ASSERT_FALSE(dummyStore->isValidPath(pathInSubstituter));

    // Create a worker with our custom substituter
    Worker worker{*dummyStore, *dummyStore};

    // Override the substituters to use our dummy store substituter
    ref<Store> substituerAsStore = substituter;
    worker.getSubstituters = [substituerAsStore]() -> std::list<ref<Store>> { return {substituerAsStore}; };

    // Create a substitution goal for the path
    auto goal = worker.makePathSubstitutionGoal(pathInSubstituter);

    // Run the worker with -j0 semantics (no local builds, only substitution)
    // The worker.run() takes a set of goals
    Goals goals;
    goals.insert(upcast_goal(goal));
    worker.run(goals);

    // Snapshot the destination store after (should match the substituter)
    checkpointJson("single/substituter", dummyStore);

    // The path should now exist in the destination store
    ASSERT_TRUE(dummyStore->isValidPath(pathInSubstituter));

    // Verify the goal succeeded
    ASSERT_EQ(upcast_goal(goal)->exitCode, Goal::ecSuccess);
}

TEST_F(WorkerSubstitutionTest, singleRootStoreObjectWithSingleDepStoreObject)
{
    // First, add a dependency store path to the substituter
    auto dependencyPath = substituter->addToStore(
        "dependency",
        SourcePath{
            [] {
                auto sc = make_ref<MemorySourceAccessor>();
                sc->root = MemorySourceAccessor::File{MemorySourceAccessor::File::Regular{
                    .executable = false,
                    .contents = "I am a dependency",
                }};
                return sc;
            }(),
        },
        ContentAddressMethod::Raw::NixArchive,
        HashAlgorithm::SHA256);

    // Now add a store path that references the dependency
    auto mainPath = substituter->addToStore(
        "main",
        SourcePath{
            [&] {
                auto sc = make_ref<MemorySourceAccessor>();
                // Include a reference to the dependency path in the contents
                sc->root = MemorySourceAccessor::File{MemorySourceAccessor::File::Regular{
                    .executable = false,
                    .contents = "I depend on " + substituter->printStorePath(dependencyPath),
                }};
                return sc;
            }(),
        },
        ContentAddressMethod::Raw::NixArchive,
        HashAlgorithm::SHA256,
        StorePathSet{dependencyPath});

    // Snapshot the substituter (has two store objects)
    checkpointJson("with-dep/substituter", substituter);

    // Snapshot the destination store before (should be empty)
    checkpointJson("../dummy-store/empty", dummyStore);

    // Neither path should exist in the destination store yet
    ASSERT_FALSE(dummyStore->isValidPath(dependencyPath));
    ASSERT_FALSE(dummyStore->isValidPath(mainPath));

    // Create a worker with our custom substituter
    Worker worker{*dummyStore, *dummyStore};

    // Override the substituters to use our dummy store substituter
    ref<Store> substituterAsStore = substituter;
    worker.getSubstituters = [substituterAsStore]() -> std::list<ref<Store>> { return {substituterAsStore}; };

    // Create a substitution goal for the main path only
    // The worker should automatically substitute the dependency as well
    auto goal = worker.makePathSubstitutionGoal(mainPath);

    // Run the worker
    Goals goals;
    goals.insert(upcast_goal(goal));
    worker.run(goals);

    // Snapshot the destination store after (should match the substituter)
    checkpointJson("with-dep/substituter", dummyStore);

    // Both paths should now exist in the destination store
    ASSERT_TRUE(dummyStore->isValidPath(dependencyPath));
    ASSERT_TRUE(dummyStore->isValidPath(mainPath));

    // Verify the goal succeeded
    ASSERT_EQ(upcast_goal(goal)->exitCode, Goal::ecSuccess);
}

TEST_F(WorkerSubstitutionTest, floatingDerivationOutput)
{
    // Enable CA derivations experimental feature
    experimentalFeatureSettings.set("extra-experimental-features", "ca-derivations");

    // Create a CA floating output derivation
    Derivation drv;
    drv.name = "test-ca-drv";
    drv.outputs = {
        {
            "out",
            DerivationOutput{DerivationOutput::CAFloating{
                .method = ContentAddressMethod::Raw::NixArchive,
                .hashAlgo = HashAlgorithm::SHA256,
            }},
        },
    };

    // Write the derivation to the destination store
    auto drvPath = writeDerivation(*dummyStore, drv);

    // Snapshot the destination store before
    checkpointJson("ca-drv/store-before", dummyStore);

    // Compute the hash modulo of the derivation
    // For CA floating derivations, the kind is Deferred since outputs aren't known until build
    auto hashModulo = hashDerivationModulo(*dummyStore, drv, true);
    ASSERT_EQ(hashModulo.kind, DrvHash::Kind::Deferred);
    auto drvHash = hashModulo.hashes.at("out");

    // Create the output store object
    auto outputPath = substituter->addToStore(
        "test-ca-drv-out",
        SourcePath{
            [] {
                auto sc = make_ref<MemorySourceAccessor>();
                sc->root = MemorySourceAccessor::File{MemorySourceAccessor::File::Regular{
                    .executable = false,
                    .contents = "I am the output of a CA derivation",
                }};
                return sc;
            }(),
        },
        ContentAddressMethod::Raw::NixArchive,
        HashAlgorithm::SHA256);

    // Add the realisation (build trace) to the substituter
    substituter->buildTrace.insert_or_assign(
        drvHash,
        std::map<std::string, UnkeyedRealisation>{
            {
                "out",
                UnkeyedRealisation{
                    .outPath = outputPath,
                },
            },
        });

    // Snapshot the substituter
    checkpointJson("ca-drv/substituter", substituter);

    // The realisation should not exist in the destination store yet
    DrvOutput drvOutput{drvHash, "out"};
    ASSERT_FALSE(dummyStore->queryRealisation(drvOutput));

    // Create a worker with our custom substituter
    Worker worker{*dummyStore, *dummyStore};

    // Override the substituters to use our dummy store substituter
    ref<Store> substituterAsStore = substituter;
    worker.getSubstituters = [substituterAsStore]() -> std::list<ref<Store>> { return {substituterAsStore}; };

    // Create a derivation goal for the CA derivation output
    // The worker should substitute the output rather than building
    auto goal = worker.makeDerivationGoal(drvPath, drv, "out", bmNormal, true);

    // Run the worker
    Goals goals;
    goals.insert(upcast_goal(goal));
    worker.run(goals);

    // Snapshot the destination store after
    checkpointJson("ca-drv/store-after", dummyStore);

    // The output path should now exist in the destination store
    ASSERT_TRUE(dummyStore->isValidPath(outputPath));

    // The realisation should now exist in the destination store
    auto realisation = dummyStore->queryRealisation(drvOutput);
    ASSERT_TRUE(realisation);
    ASSERT_EQ(realisation->outPath, outputPath);

    // Verify the goal succeeded
    ASSERT_EQ(upcast_goal(goal)->exitCode, Goal::ecSuccess);

    // Disable CA derivations experimental feature
    experimentalFeatureSettings.set("extra-experimental-features", "");
}

} // namespace nix

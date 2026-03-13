#include <gtest/gtest.h>

#include "nix/store/local-store.hh"
#include "nix/store/store-open.hh"
#include "nix/store/globals.hh"
#include "nix/store/derivations.hh"
#include "nix/util/file-system.hh"
#include "nix/util/hash.hh"

#ifndef _WIN32

#  include <filesystem>
#  include <fstream>
#  include <sqlite3.h>

using namespace nix;

/**
 * Test fixture that creates a temporary local store for DB round-trip tests.
 */
class LocalStoreDbTest : public ::testing::Test
{
protected:
    std::filesystem::path tmpRoot;
    std::filesystem::path realStoreDir;
    std::shared_ptr<Store> store;
    std::shared_ptr<LocalStore> localStore;

    static void SetUpTestSuite()
    {
        initLibStore(false);
    }

    void SetUp() override
    {
        tmpRoot = createTempDir();
        realStoreDir = tmpRoot / "nix/store";
        std::filesystem::create_directories(realStoreDir);
        store = openStore(fmt("local?root=%s", tmpRoot.string()));
        localStore = std::dynamic_pointer_cast<LocalStore>(store);
        ASSERT_NE(localStore, nullptr) << "expected local store";
    }

    void TearDown() override
    {
        localStore.reset();
        store.reset();
        std::filesystem::remove_all(tmpRoot);
    }

    /**
     * Create a dummy derivation on disk and return its ValidPathInfo.
     * The derivation file must exist for registerValidPaths to succeed.
     */
    ValidPathInfo createDummyDrv(const std::string & name)
    {
        auto drvPath = StorePath::random(name + ".drv");

        Derivation drv;
        drv.name = name;
        drv.outputs.emplace("out", DerivationOutput{DerivationOutput::Deferred{}});
        drv.platform = "x86_64-linux";
        drv.builder = "foo";
        drv.env["out"] = "";
        drv.fillInOutputPaths(*localStore);

        auto drvContents = drv.unparse(*localStore, /*maskOutputs=*/false);

        std::ofstream out(realStoreDir / std::string(drvPath.to_string()), std::ios::binary);
        out.write(drvContents.data(), drvContents.size());
        EXPECT_TRUE(out.good());

        ValidPathInfo info{drvPath, UnkeyedValidPathInfo(*localStore, Hash::dummy)};
        info.narSize = drvContents.size();
        return info;
    }

    /**
     * Create a dummy non-derivation store path and return its ValidPathInfo.
     */
    ValidPathInfo
    createDummyPath(const std::string & name, std::optional<StorePath> deriver = std::nullopt, StorePathSet refs = {})
    {
        auto path = StorePath::random(name);

        ValidPathInfo info{path, UnkeyedValidPathInfo(*localStore, Hash::dummy)};
        info.narSize = 42;
        info.deriver = deriver;
        info.references = refs;
        return info;
    }

    /**
     * Query raw column values from the DB via direct SQLite access.
     */
    std::vector<std::string> queryRawColumn(const std::string & table, const std::string & column)
    {
        auto dbPath = tmpRoot / "nix/var/nix/db/db.sqlite";
        sqlite3 * db;
        EXPECT_EQ(sqlite3_open(dbPath.c_str(), &db), SQLITE_OK);

        auto sql = fmt("SELECT %s FROM %s WHERE %s IS NOT NULL", column, table, column);
        sqlite3_stmt * stmt;
        EXPECT_EQ(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr), SQLITE_OK);

        std::vector<std::string> results;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            auto text = (const char *) sqlite3_column_text(stmt, 0);
            if (text)
                results.push_back(text);
        }

        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return results;
    }
};

/**
 * Basic round-trip: register a path, query it back, verify fields match.
 */
TEST_F(LocalStoreDbTest, registerAndQueryPathInfo)
{
    auto info = createDummyPath("test-roundtrip");
    ValidPathInfos infos;
    infos.emplace(info.path, info);
    localStore->registerValidPaths(infos);

    EXPECT_TRUE(localStore->isValidPath(info.path));

    auto queried = localStore->queryPathInfo(info.path);
    EXPECT_EQ(queried->path, info.path);
    EXPECT_EQ(queried->narSize, info.narSize);
    EXPECT_EQ(queried->narHash, info.narHash);
}

/**
 * Verify dual-column format: pathName has baseName, path has full /nix/store/... path.
 */
TEST_F(LocalStoreDbTest, dualColumnFormat)
{
    auto info = createDummyPath("test-dual-column");
    ValidPathInfos infos;
    infos.emplace(info.path, info);
    localStore->registerValidPaths(infos);

    // pathName column should have baseName (no prefix)
    auto rawPathNames = queryRawColumn("ValidPaths", "pathName");
    ASSERT_FALSE(rawPathNames.empty());
    for (auto & raw : rawPathNames) {
        EXPECT_FALSE(raw.starts_with("/")) << "pathName should be baseName: " << raw;
    }

    // path column should have full /nix/store/... path (backward compat)
    auto rawPaths = queryRawColumn("ValidPaths", "path");
    ASSERT_FALSE(rawPaths.empty());
    for (auto & raw : rawPaths) {
        EXPECT_TRUE(raw.starts_with("/")) << "path should be full store path: " << raw;
        EXPECT_TRUE(raw.find("/nix/store/") != std::string::npos) << "path should contain /nix/store/: " << raw;
    }
}

/**
 * Register a path with a deriver, verify it round-trips correctly
 * and both columns are populated correctly.
 */
TEST_F(LocalStoreDbTest, deriverRoundTrip)
{
    auto drvInfo = createDummyDrv("test-deriver-drv");
    auto outInfo = createDummyPath("test-deriver-out", drvInfo.path);

    ValidPathInfos infos;
    infos.emplace(drvInfo.path, drvInfo);
    infos.emplace(outInfo.path, outInfo);
    localStore->registerValidPaths(infos);

    auto queried = localStore->queryPathInfo(outInfo.path);
    ASSERT_TRUE(queried->deriver.has_value());
    EXPECT_EQ(*queried->deriver, drvInfo.path);

    // deriverName should be baseName
    auto rawDeriverNames = queryRawColumn("ValidPaths", "deriverName");
    for (auto & raw : rawDeriverNames) {
        EXPECT_FALSE(raw.starts_with("/")) << "deriverName should be baseName: " << raw;
    }

    // deriver (legacy) should be full path
    auto rawDerivers = queryRawColumn("ValidPaths", "deriver");
    for (auto & raw : rawDerivers) {
        EXPECT_TRUE(raw.starts_with("/")) << "deriver should be full path: " << raw;
    }
}

/**
 * Register paths with references, query them back via queryReferrers.
 */
TEST_F(LocalStoreDbTest, referencesRoundTrip)
{
    auto depInfo = createDummyPath("test-dep");
    auto mainInfo = createDummyPath("test-main", std::nullopt, {depInfo.path});

    ValidPathInfos infos;
    infos.emplace(depInfo.path, depInfo);
    infos.emplace(mainInfo.path, mainInfo);
    localStore->registerValidPaths(infos);

    // queryPathInfo should return the references
    auto queried = localStore->queryPathInfo(mainInfo.path);
    EXPECT_EQ(queried->references.size(), 1u);
    EXPECT_TRUE(queried->references.count(depInfo.path));

    // queryReferrers should find mainInfo as a referrer of depInfo
    StorePathSet referrers;
    localStore->queryReferrers(depInfo.path, referrers);
    EXPECT_TRUE(referrers.count(mainInfo.path));
}

/**
 * queryAllValidPaths returns all registered paths.
 */
TEST_F(LocalStoreDbTest, queryAllValidPaths)
{
    auto info1 = createDummyPath("test-all-1");
    auto info2 = createDummyPath("test-all-2");

    ValidPathInfos infos;
    infos.emplace(info1.path, info1);
    infos.emplace(info2.path, info2);
    localStore->registerValidPaths(infos);

    auto all = localStore->queryAllValidPaths();
    EXPECT_TRUE(all.count(info1.path));
    EXPECT_TRUE(all.count(info2.path));
}

/**
 * queryPathFromHashPart finds the right path.
 */
TEST_F(LocalStoreDbTest, queryPathFromHashPart)
{
    auto info = createDummyPath("test-hash-lookup");
    ValidPathInfos infos;
    infos.emplace(info.path, info);
    localStore->registerValidPaths(infos);

    auto hashPart = info.path.hashPart();
    auto found = localStore->queryPathFromHashPart(std::string(hashPart));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, info.path);
}

/**
 * queryPathFromHashPart returns nullopt for non-existent hash.
 */
TEST_F(LocalStoreDbTest, queryPathFromHashPart_notFound)
{
    // Use a hash that's extremely unlikely to exist
    auto found = localStore->queryPathFromHashPart("00000000000000000000000000000000");
    EXPECT_FALSE(found.has_value());
}

/**
 * queryValidDerivers returns derivers of a given output path.
 * This works through DerivationOutputs table, populated when a derivation
 * with known outputs is registered.
 */
TEST_F(LocalStoreDbTest, queryValidDerivers)
{
    auto drvInfo = createDummyDrv("test-valid-derivers");

    ValidPathInfos infos;
    infos.emplace(drvInfo.path, drvInfo);

    // The derivation has fillInOutputPaths, so register derivation outputs.
    // First, register the drv itself.
    localStore->registerValidPaths(infos);

    // Read the derivation to find its actual output paths
    auto drv = localStore->readInvalidDerivation(drvInfo.path);
    for (auto & [name, outputAndPath] : drv.outputsAndOptPaths(*localStore)) {
        if (outputAndPath.second) {
            auto outPath = *outputAndPath.second;
            // queryValidDerivers should find the derivation for this output
            auto derivers = localStore->queryValidDerivers(outPath);
            EXPECT_TRUE(derivers.count(drvInfo.path)) << "Expected drv to be a deriver of output " << name;
        }
    }
}

/**
 * Derivation outputs have dual-column format: pathName (baseName) and path (full).
 */
TEST_F(LocalStoreDbTest, derivationOutputsRoundTrip)
{
    auto drvInfo = createDummyDrv("test-drv-outputs");
    ValidPathInfos infos;
    infos.emplace(drvInfo.path, drvInfo);
    localStore->registerValidPaths(infos);

    auto outputs = localStore->queryStaticPartialDerivationOutputMap(drvInfo.path);
    // The dummy derivation has a "deferred" output, so the output map should
    // contain an entry for "out"
    EXPECT_TRUE(outputs.count("out"));

    // pathName column should have baseNames
    auto rawPathNames = queryRawColumn("DerivationOutputs", "pathName");
    for (auto & raw : rawPathNames) {
        EXPECT_FALSE(raw.starts_with("/")) << "DerivationOutputs.pathName should be baseName: " << raw;
    }

    // path column (legacy) should have full paths
    auto rawPaths = queryRawColumn("DerivationOutputs", "path");
    for (auto & raw : rawPaths) {
        EXPECT_TRUE(raw.starts_with("/")) << "DerivationOutputs.path should be full path: " << raw;
    }
}

/**
 * Backfill test: simulate older Nix inserting rows with only legacy columns
 * (path, deriver) and NULL pathName/deriverName. Reopen store — backfill
 * should populate the new columns from the legacy data.
 */
TEST_F(LocalStoreDbTest, backfillFromLegacyColumns)
{
    // Register a path normally first
    auto info = createDummyPath("test-backfill-normal");
    ValidPathInfos infos;
    infos.emplace(info.path, info);
    localStore->registerValidPaths(infos);

    auto dbPath = tmpRoot / "nix/var/nix/db/db.sqlite";

    // Close the store
    localStore.reset();
    store.reset();

    {
        sqlite3 * db;
        ASSERT_EQ(sqlite3_open(dbPath.c_str(), &db), SQLITE_OK);

        // Simulate older Nix: insert a row with only legacy columns, NULL pathName
        std::string sql =
            fmt("INSERT INTO ValidPaths (path, hash, registrationTime, narSize) "
                "VALUES ('%s/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1-legacy-path', "
                "'sha256:0000000000000000000000000000000000000000000000000000000000000000', "
                "%d, 100);",
                "/nix/store",
                time(nullptr));
        char * errMsg = nullptr;
        ASSERT_EQ(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg), SQLITE_OK)
            << (errMsg ? errMsg : "unknown error");

        // Verify pathName is NULL for the inserted row
        sqlite3_stmt * stmt;
        sqlite3_prepare_v2(db, "SELECT pathName FROM ValidPaths WHERE path LIKE '%legacy-path'", -1, &stmt, nullptr);
        ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
        EXPECT_EQ(sqlite3_column_type(stmt, 0), SQLITE_NULL) << "pathName should be NULL before backfill";
        sqlite3_finalize(stmt);

        sqlite3_close(db);
    }

    // Reopen the store — backfill should run
    store = openStore(fmt("local?root=%s", tmpRoot.string()));
    localStore = std::dynamic_pointer_cast<LocalStore>(store);
    ASSERT_NE(localStore, nullptr);

    // pathName should now be populated for all rows
    auto rawPathNames = queryRawColumn("ValidPaths", "pathName");
    ASSERT_GE(rawPathNames.size(), 2u); // at least the normal + legacy row
    for (auto & raw : rawPathNames) {
        EXPECT_FALSE(raw.starts_with("/")) << "pathName should be baseName after backfill: " << raw;
    }

    // The backfilled row should be queryable via pathName
    auto found = localStore->queryPathFromHashPart("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->to_string(), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1-legacy-path");

    // The originally registered path should still be queryable
    EXPECT_TRUE(localStore->isValidPath(info.path));
}

/**
 * Multiple paths with various features: deriver, references, self-references.
 * This is an integration test covering the full write+read cycle.
 */
TEST_F(LocalStoreDbTest, fullIntegration)
{
    // Create several paths with interconnections
    auto dep1 = createDummyPath("integration-dep1");
    auto dep2 = createDummyPath("integration-dep2");
    auto drv = createDummyDrv("integration-drv");

    // Main path references dep1 and dep2, has drv as deriver
    auto mainPath = StorePath::random("integration-main");
    ValidPathInfo mainInfo{mainPath, UnkeyedValidPathInfo(*localStore, Hash::dummy)};
    mainInfo.narSize = 100;
    mainInfo.deriver = drv.path;
    mainInfo.references = {dep1.path, dep2.path};

    ValidPathInfos infos;
    infos.emplace(dep1.path, dep1);
    infos.emplace(dep2.path, dep2);
    infos.emplace(drv.path, drv);
    infos.emplace(mainInfo.path, mainInfo);
    localStore->registerValidPaths(infos);

    // Verify round-trip of all paths
    EXPECT_TRUE(localStore->isValidPath(dep1.path));
    EXPECT_TRUE(localStore->isValidPath(dep2.path));
    EXPECT_TRUE(localStore->isValidPath(drv.path));
    EXPECT_TRUE(localStore->isValidPath(mainInfo.path));

    // Verify references
    auto queried = localStore->queryPathInfo(mainInfo.path);
    EXPECT_EQ(queried->references.size(), 2u);
    EXPECT_TRUE(queried->references.count(dep1.path));
    EXPECT_TRUE(queried->references.count(dep2.path));

    // Verify deriver
    ASSERT_TRUE(queried->deriver.has_value());
    EXPECT_EQ(*queried->deriver, drv.path);

    // Verify referrers
    StorePathSet referrers;
    localStore->queryReferrers(dep1.path, referrers);
    EXPECT_TRUE(referrers.count(mainInfo.path));

    // Verify queryValidDerivers for actual derivation outputs
    // (queryValidDerivers uses DerivationOutputs table, not the deriver field)
    auto parsedDrv = localStore->readInvalidDerivation(drv.path);
    for (auto & [name, outputAndPath] : parsedDrv.outputsAndOptPaths(*localStore)) {
        if (outputAndPath.second) {
            auto derivers = localStore->queryValidDerivers(*outputAndPath.second);
            EXPECT_TRUE(derivers.count(drv.path));
        }
    }

    // Verify queryAllValidPaths
    auto all = localStore->queryAllValidPaths();
    EXPECT_GE(all.size(), 4u);

    // Verify queryPathFromHashPart
    auto hashPart = mainInfo.path.hashPart();
    auto found = localStore->queryPathFromHashPart(std::string(hashPart));
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, mainInfo.path);

    // Verify dual-column format in raw DB
    auto rawPathNames = queryRawColumn("ValidPaths", "pathName");
    for (auto & raw : rawPathNames) {
        EXPECT_FALSE(raw.starts_with("/")) << "pathName should be baseName: " << raw;
    }
    auto rawPaths = queryRawColumn("ValidPaths", "path");
    for (auto & raw : rawPaths) {
        EXPECT_TRUE(raw.starts_with("/")) << "path should be full store path: " << raw;
    }
}

#endif // !_WIN32

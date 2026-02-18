#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/expr/tests/libexpr.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/fetchers/git-utils.hh"
#include "nix/store/globals.hh"
#include "nix/util/file-system.hh"

#include <git2/global.h>
#include <git2/repository.h>
#include <git2/signature.h>
#include <git2/commit.h>
#include <git2/blob.h>
#include <git2/tree.h>
#include <git2/index.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>
#include <mutex>
#include <map>

namespace nix {

// ============================================================================
// Test Fixture for Tectonix Tests
// ============================================================================

class TectonixTest : public ::testing::Test
{
protected:
    std::unique_ptr<AutoDelete> delTmpDir;
    std::filesystem::path repoPath;
    std::string commitSha;

    // Manifest content for test repo
    static constexpr std::string_view TEST_MANIFEST = R"({
        "//areas/tools/dev": { "id": "W-000001" },
        "//areas/tools/tec": { "id": "W-000002" },
        "//areas/platform/core": { "id": "W-000003" }
    })";

public:
    static void SetUpTestSuite()
    {
        initLibStore(false);
        initGC();
    }

    void SetUp() override
    {
        // Create temp directory for git repo
        repoPath = createTempDir();
        delTmpDir = std::make_unique<AutoDelete>(repoPath, true);

        // Initialize git repo and create test structure
        git_libgit2_init();
        createTestRepository();
    }

    void TearDown() override
    {
        delTmpDir.reset();
    }

    void createTestRepository()
    {
        git_repository * repo = nullptr;
        ASSERT_EQ(git_repository_init(&repo, repoPath.string().c_str(), 0), 0);

        // Create directory structure with files
        createDir(".meta");
        writeFile(".meta/manifest.json", std::string(TEST_MANIFEST));

        createDir("areas");
        createDir("areas/tools");
        createDir("areas/tools/dev");
        createDir("areas/tools/tec");
        createDir("areas/platform");
        createDir("areas/platform/core");

        writeFile("areas/tools/dev/zone.nix", "{ }");
        writeFile("areas/tools/dev/README.md", "Dev zone");
        writeFile("areas/tools/tec/zone.nix", "{ }");
        writeFile("areas/platform/core/zone.nix", "{ }");
        writeFile("README.md", "Test World");

        // Create git tree from files
        git_index * index = nullptr;
        ASSERT_EQ(git_repository_index(&index, repo), 0);

        // Add all files to index
        ASSERT_EQ(git_index_add_bypath(index, ".meta/manifest.json"), 0);
        ASSERT_EQ(git_index_add_bypath(index, "areas/tools/dev/zone.nix"), 0);
        ASSERT_EQ(git_index_add_bypath(index, "areas/tools/dev/README.md"), 0);
        ASSERT_EQ(git_index_add_bypath(index, "areas/tools/tec/zone.nix"), 0);
        ASSERT_EQ(git_index_add_bypath(index, "areas/platform/core/zone.nix"), 0);
        ASSERT_EQ(git_index_add_bypath(index, "README.md"), 0);
        ASSERT_EQ(git_index_write(index), 0);

        git_oid treeOid;
        ASSERT_EQ(git_index_write_tree(&treeOid, index), 0);

        git_tree * tree = nullptr;
        ASSERT_EQ(git_tree_lookup(&tree, repo, &treeOid), 0);

        // Create commit
        git_signature * sig = nullptr;
        ASSERT_EQ(git_signature_now(&sig, "test", "test@example.com"), 0);

        git_oid commitOid;
        ASSERT_EQ(git_commit_create_v(
            &commitOid, repo, "HEAD", sig, sig, nullptr,
            "Initial commit", tree, 0), 0);

        // Store commit SHA
        char sha[GIT_OID_SHA1_HEXSIZE + 1];
        git_oid_tostr(sha, sizeof(sha), &commitOid);
        commitSha = sha;

        git_signature_free(sig);
        git_tree_free(tree);
        git_index_free(index);
        git_repository_free(repo);
    }

    void createDir(const std::string & path)
    {
        std::filesystem::create_directories(repoPath / path);
    }

    void writeFile(const std::string & path, const std::string & content)
    {
        auto fullPath = repoPath / path;
        std::ofstream f(fullPath);
        f << content;
    }

    // Create EvalState with tectonix settings configured
    struct TectonixEvalContext {
        bool readOnlyMode = true;
        fetchers::Settings fetchSettings{};
        EvalSettings evalSettings{readOnlyMode};
        ref<Store> store;
        std::unique_ptr<EvalState> state;

        TectonixEvalContext(const std::filesystem::path & repoPath, const std::string & commitSha, bool withCheckout = false)
            : store(openStore("dummy://"))
        {
            evalSettings.nixPath = {};
            evalSettings.tectonixGitDir = (repoPath / ".git").string();
            evalSettings.tectonixGitSha = commitSha;
            if (withCheckout) {
                evalSettings.tectonixCheckoutPath = repoPath.string();
            }

            state = std::make_unique<EvalState>(
                LookupPath{},
                store,
                fetchSettings,
                evalSettings,
                nullptr
            );
        }

        Value eval(const std::string & input)
        {
            Value v;
            Expr * e = state->parseExprFromString(input, state->rootPath(CanonPath::root));
            state->eval(e, v);
            state->forceValue(v, noPos);
            return v;
        }
    };

    std::unique_ptr<TectonixEvalContext> createTectonixContext(bool withCheckout = false)
    {
        return std::make_unique<TectonixEvalContext>(repoPath, commitSha, withCheckout);
    }
};

// ============================================================================
// Phase 4: Builtin Tests - __unsafeTectonixInternalManifest
// ============================================================================

TEST_F(TectonixTest, manifest_returns_path_to_metadata_mapping)
{
    auto ctx = createTectonixContext();
    auto v = ctx->eval("builtins.unsafeTectonixInternalManifest");

    ASSERT_THAT(v, IsAttrs());
    ASSERT_EQ(v.attrs()->size(), 3u);

    // Check //areas/tools/dev maps to W-000001
    auto dev = v.attrs()->get(ctx->state->symbols.create("//areas/tools/dev"));
    ASSERT_NE(dev, nullptr);
    ASSERT_THAT(*dev->value, IsAttrs());
    auto devId = dev->value->attrs()->get(ctx->state->symbols.create("id"));
    ASSERT_NE(devId, nullptr);
    ASSERT_THAT(*devId->value, IsStringEq("W-000001"));

    // Check //areas/tools/tec maps to W-000002
    auto tec = v.attrs()->get(ctx->state->symbols.create("//areas/tools/tec"));
    ASSERT_NE(tec, nullptr);
    ASSERT_THAT(*tec->value, IsAttrs());
    auto tecId = tec->value->attrs()->get(ctx->state->symbols.create("id"));
    ASSERT_NE(tecId, nullptr);
    ASSERT_THAT(*tecId->value, IsStringEq("W-000002"));

    // Check //areas/platform/core maps to W-000003
    auto core = v.attrs()->get(ctx->state->symbols.create("//areas/platform/core"));
    ASSERT_NE(core, nullptr);
    ASSERT_THAT(*core->value, IsAttrs());
    auto coreId = core->value->attrs()->get(ctx->state->symbols.create("id"));
    ASSERT_NE(coreId, nullptr);
    ASSERT_THAT(*coreId->value, IsStringEq("W-000003"));
}

// ============================================================================
// Phase 4: Builtin Tests - __unsafeTectonixInternalManifestInverted
// ============================================================================

TEST_F(TectonixTest, manifest_inverted_returns_id_to_path_mapping)
{
    auto ctx = createTectonixContext();
    auto v = ctx->eval("builtins.unsafeTectonixInternalManifestInverted");

    ASSERT_THAT(v, IsAttrs());
    ASSERT_EQ(v.attrs()->size(), 3u);

    // Check W-000001 maps to //areas/tools/dev
    auto w1 = v.attrs()->get(ctx->state->symbols.create("W-000001"));
    ASSERT_NE(w1, nullptr);
    ASSERT_THAT(*w1->value, IsStringEq("//areas/tools/dev"));

    // Check W-000002 maps to //areas/tools/tec
    auto w2 = v.attrs()->get(ctx->state->symbols.create("W-000002"));
    ASSERT_NE(w2, nullptr);
    ASSERT_THAT(*w2->value, IsStringEq("//areas/tools/tec"));

    // Check W-000003 maps to //areas/platform/core
    auto w3 = v.attrs()->get(ctx->state->symbols.create("W-000003"));
    ASSERT_NE(w3, nullptr);
    ASSERT_THAT(*w3->value, IsStringEq("//areas/platform/core"));
}

// ============================================================================
// Phase 4: Builtin Tests - __unsafeTectonixInternalTreeSha
// ============================================================================

TEST_F(TectonixTest, treeSha_returns_sha_string)
{
    auto ctx = createTectonixContext();
    auto v = ctx->eval(R"(builtins.unsafeTectonixInternalTreeSha "//areas/tools/dev")");

    ASSERT_THAT(v, IsString());
    // SHA should be 40 hex characters
    ASSERT_EQ(v.string_view().size(), 40u);
    for (char c : v.string_view()) {
        ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST_F(TectonixTest, treeSha_different_zones_have_different_shas)
{
    auto ctx = createTectonixContext();

    auto devSha = ctx->eval(R"(builtins.unsafeTectonixInternalTreeSha "//areas/tools/dev")");
    auto tecSha = ctx->eval(R"(builtins.unsafeTectonixInternalTreeSha "//areas/tools/tec")");

    ASSERT_THAT(devSha, IsString());
    ASSERT_THAT(tecSha, IsString());
    // Different zones should have different tree SHAs (different content)
    ASSERT_NE(devSha.string_view(), tecSha.string_view());
}

TEST_F(TectonixTest, treeSha_invalid_path_throws)
{
    auto ctx = createTectonixContext();

    ASSERT_THROW(
        ctx->eval(R"(builtins.unsafeTectonixInternalTreeSha "//does/not/exist")"),
        Error);
}

// ============================================================================
// Phase 4: Builtin Tests - __unsafeTectonixInternalTree
// ============================================================================

// Disabled: requires real store (dummy:// doesn't support fetchToStore)
TEST_F(TectonixTest, DISABLED_tree_returns_store_path)
{
    auto ctx = createTectonixContext();
    // Get a tree SHA first, then fetch it as a store path
    auto sha = ctx->eval(R"(builtins.unsafeTectonixInternalTreeSha "//areas/tools/dev")");
    ASSERT_THAT(sha, IsString());

    auto expr = "builtins.unsafeTectonixInternalTree \"" + std::string(sha.string_view()) + "\"";
    auto v = ctx->eval(expr);

    ASSERT_THAT(v, IsString());
    // Should be a store path
    auto pathStr = v.string_view();
    ASSERT_TRUE(pathStr.find("/nix/store/") == 0 || pathStr.find(settings.nixStore) == 0);
}

TEST_F(TectonixTest, tree_invalid_sha_throws)
{
    auto ctx = createTectonixContext();

    // Invalid SHA (not 40 hex chars)
    ASSERT_THROW(
        ctx->eval(R"(builtins.unsafeTectonixInternalTree "invalid")"),
        Error);
}

TEST_F(TectonixTest, tree_nonexistent_sha_throws)
{
    auto ctx = createTectonixContext();

    // Valid format but non-existent SHA
    ASSERT_THROW(
        ctx->eval(R"(builtins.unsafeTectonixInternalTree "0000000000000000000000000000000000000000")"),
        EvalError);
}

// ============================================================================
// Phase 4: Builtin Tests - __unsafeTectonixInternalZoneSrc
// ============================================================================

// Disabled: requires real store (dummy:// doesn't support addToStoreFromDump)
TEST_F(TectonixTest, DISABLED_zoneSrc_returns_store_path)
{
    auto ctx = createTectonixContext();
    auto v = ctx->eval(R"(builtins.unsafeTectonixInternalZoneSrc "//areas/tools/dev")");

    ASSERT_THAT(v, IsString());
    // Should be a store path
    auto pathStr = v.string_view();
    ASSERT_TRUE(pathStr.find("/nix/store/") == 0 || pathStr.find(settings.nixStore) == 0);
}

TEST_F(TectonixTest, zoneSrc_validates_zone_path)
{
    auto ctx = createTectonixContext();

    // Parent of zone should fail validation
    ASSERT_THROW(
        ctx->eval(R"(builtins.unsafeTectonixInternalZoneSrc "//areas/tools")"),
        EvalError);
}

TEST_F(TectonixTest, zoneSrc_non_zone_path_throws)
{
    auto ctx = createTectonixContext();

    ASSERT_THROW(
        ctx->eval(R"(builtins.unsafeTectonixInternalZoneSrc "//not/a/zone")"),
        EvalError);
}

// ============================================================================
// Phase 4: Builtin Tests - __unsafeTectonixInternalZoneRoot
// ============================================================================

TEST_F(TectonixTest, zoneRoot_returns_path)
{
    auto ctx = createTectonixContext(false); // no checkout path
    auto v = ctx->eval(R"(builtins.unsafeTectonixInternalZoneRoot "//areas/tools/dev")");

    // Check root is null
    ASSERT_THAT(v, IsNull());
}

TEST_F(TectonixTest, zone_invalid_path_throws)
{
    auto ctx = createTectonixContext();

    ASSERT_THROW(
        ctx->eval(R"(builtins.unsafeTectonixInternalZoneRoot "//not/a/zone")"),
        EvalError);
}

// ============================================================================
// Phase 4: Builtin Tests - __unsafeTectonixInternalZoneIsDirty
// ============================================================================

TEST_F(TectonixTest, zoneIsDirty_returns_false)
{
    auto ctx = createTectonixContext();
    auto v = ctx->eval(R"(builtins.unsafeTectonixInternalZoneIsDirty "//areas/tools/dev")");

    // Check dirty is false (clean repo)
    ASSERT_THAT(v, IsFalse());
}

// ============================================================================
// Phase 4: Builtin Tests - __unsafeTectonixInternalSparseCheckoutRoots
// ============================================================================

TEST_F(TectonixTest, sparseCheckoutRoots_returns_list)
{
    auto ctx = createTectonixContext();
    auto v = ctx->eval("builtins.unsafeTectonixInternalSparseCheckoutRoots");

    ASSERT_THAT(v, IsList());
}

TEST_F(TectonixTest, sparseCheckoutRoots_empty_without_checkout)
{
    auto ctx = createTectonixContext(false); // no checkout path
    auto v = ctx->eval("builtins.unsafeTectonixInternalSparseCheckoutRoots");

    ASSERT_THAT(v, IsListOfSize(0));
}

// ============================================================================
// Phase 4: Builtin Tests - __unsafeTectonixInternalDirtyZones
// ============================================================================

TEST_F(TectonixTest, dirtyZones_returns_attrset)
{
    auto ctx = createTectonixContext(true); // with checkout
    auto v = ctx->eval("builtins.unsafeTectonixInternalDirtyZones");

    ASSERT_THAT(v, IsAttrs());
}

TEST_F(TectonixTest, dirtyZones_empty_without_checkout)
{
    auto ctx = createTectonixContext(false); // no checkout
    auto v = ctx->eval("builtins.unsafeTectonixInternalDirtyZones");

    ASSERT_THAT(v, IsAttrsOfSize(0));
}

// ============================================================================
// Phase 3: EvalState Method Tests - getWorldRepo
// ============================================================================

TEST_F(TectonixTest, getWorldRepo_returns_repo)
{
    auto ctx = createTectonixContext();
    auto repo = ctx->state->getWorldRepo();
    ASSERT_NE(&*repo, nullptr);
}

TEST_F(TectonixTest, getWorldRepo_caches_instance)
{
    auto ctx = createTectonixContext();
    auto repo1 = ctx->state->getWorldRepo();
    auto repo2 = ctx->state->getWorldRepo();
    // Should return same instance
    ASSERT_EQ(&*repo1, &*repo2);
}

// ============================================================================
// Phase 3: EvalState Method Tests - getWorldTreeSha
// ============================================================================

TEST_F(TectonixTest, getWorldTreeSha_returns_hash)
{
    auto ctx = createTectonixContext();
    auto hash = ctx->state->getWorldTreeSha("//areas/tools/dev");
    // SHA1 hash is 40 hex chars
    ASSERT_EQ(hash.gitRev().size(), 40u);
}

TEST_F(TectonixTest, getWorldTreeSha_caches_results)
{
    auto ctx = createTectonixContext();
    auto hash1 = ctx->state->getWorldTreeSha("//areas/tools/dev");
    auto hash2 = ctx->state->getWorldTreeSha("//areas/tools/dev");
    ASSERT_EQ(hash1, hash2);
}

TEST_F(TectonixTest, getWorldTreeSha_root_returns_commit_tree)
{
    auto ctx = createTectonixContext();
    // Root path should work
    auto hash = ctx->state->getWorldTreeSha("//");
    ASSERT_EQ(hash.gitRev().size(), 40u);
}

// ============================================================================
// Phase 3: EvalState Method Tests - getManifestJson
// ============================================================================

TEST_F(TectonixTest, getManifestJson_parses_correctly)
{
    auto ctx = createTectonixContext();
    auto & json = ctx->state->getManifestJson();

    ASSERT_TRUE(json.contains("//areas/tools/dev"));
    ASSERT_EQ(json["//areas/tools/dev"]["id"], "W-000001");
}

TEST_F(TectonixTest, getManifestJson_caches_result)
{
    auto ctx = createTectonixContext();
    auto & json1 = ctx->state->getManifestJson();
    auto & json2 = ctx->state->getManifestJson();
    // Should return same instance
    ASSERT_EQ(&json1, &json2);
}

// ============================================================================
// Phase 3: EvalState Method Tests - isTectonixSourceAvailable
// ============================================================================

TEST_F(TectonixTest, isTectonixSourceAvailable_false_without_checkout)
{
    auto ctx = createTectonixContext(false);
    ASSERT_FALSE(ctx->state->isTectonixSourceAvailable());
}

TEST_F(TectonixTest, isTectonixSourceAvailable_true_with_checkout)
{
    auto ctx = createTectonixContext(true);
    ASSERT_TRUE(ctx->state->isTectonixSourceAvailable());
}

// ============================================================================
// Phase 7: Error Handling Tests
// ============================================================================

TEST_F(TectonixTest, missing_git_dir_throws)
{
    bool readOnly = true;
    fetchers::Settings fetchSettings{};
    EvalSettings evalSettings{readOnly};
    evalSettings.nixPath = {};
    evalSettings.tectonixGitDir = ""; // empty
    evalSettings.tectonixGitSha = commitSha;

    EvalState evalState(
        LookupPath{},
        openStore("dummy://"),
        fetchSettings,
        evalSettings,
        nullptr
    );

    ASSERT_THROW(evalState.getWorldRepo(), Error);
}

TEST_F(TectonixTest, missing_git_sha_throws)
{
    bool readOnly = true;
    fetchers::Settings fetchSettings{};
    EvalSettings evalSettings{readOnly};
    evalSettings.nixPath = {};
    evalSettings.tectonixGitDir = (repoPath / ".git").string();
    evalSettings.tectonixGitSha = ""; // empty

    EvalState evalState(
        LookupPath{},
        openStore("dummy://"),
        fetchSettings,
        evalSettings,
        nullptr
    );

    ASSERT_THROW(evalState.getWorldGitAccessor(), Error);
}

TEST_F(TectonixTest, missing_git_sha_tree_sha_throws)
{
    bool readOnly = true;
    fetchers::Settings fetchSettings{};
    EvalSettings evalSettings{readOnly};
    evalSettings.nixPath = {};
    evalSettings.tectonixGitDir = (repoPath / ".git").string();
    evalSettings.tectonixGitSha = ""; // empty

    EvalState evalState(
        LookupPath{},
        openStore("dummy://"),
        fetchSettings,
        evalSettings,
        nullptr
    );

    ASSERT_THROW(evalState.getWorldTreeSha("//areas/tools/dev"), Error);
}

TEST_F(TectonixTest, invalid_sha_throws)
{
    bool readOnly = true;
    fetchers::Settings fetchSettings{};
    EvalSettings evalSettings{readOnly};
    evalSettings.nixPath = {};
    evalSettings.tectonixGitDir = (repoPath / ".git").string();
    evalSettings.tectonixGitSha = "0000000000000000000000000000000000000000"; // nonexistent

    EvalState evalState(
        LookupPath{},
        openStore("dummy://"),
        fetchSettings,
        evalSettings,
        nullptr
    );

    ASSERT_THROW(evalState.getWorldGitAccessor(), Error);
}

// ============================================================================
// Phase 5: Thread-Safety Tests
// ============================================================================

TEST_F(TectonixTest, concurrent_manifest_access)
{
    auto ctx = createTectonixContext();

    std::vector<std::thread> threads;
    std::vector<const nlohmann::json *> results(8);

    // Multiple threads calling getManifestJson
    for (size_t i = 0; i < 8; i++) {
        threads.emplace_back([&, i]() {
            results[i] = &ctx->state->getManifestJson();
        });
    }

    for (auto & t : threads) {
        t.join();
    }

    // Verify all got same instance
    for (size_t i = 1; i < 8; i++) {
        ASSERT_EQ(results[0], results[i]);
    }
}

TEST_F(TectonixTest, concurrent_tree_sha_computation)
{
    auto ctx = createTectonixContext();

    std::vector<std::thread> threads;
    std::vector<std::string> results(8);  // Store as gitRev strings

    // Multiple threads computing tree SHAs for same path
    for (size_t i = 0; i < 8; i++) {
        threads.emplace_back([&, i]() {
            results[i] = ctx->state->getWorldTreeSha("//areas/tools/dev").gitRev();
        });
    }

    for (auto & t : threads) {
        t.join();
    }

    // Verify all got same hash
    for (size_t i = 1; i < 8; i++) {
        ASSERT_EQ(results[0], results[i]);
    }
}

TEST_F(TectonixTest, concurrent_world_repo_access)
{
    auto ctx = createTectonixContext();

    std::vector<std::thread> threads;
    std::vector<GitRepo *> results(8);

    // Multiple threads calling getWorldRepo
    for (size_t i = 0; i < 8; i++) {
        threads.emplace_back([&, i]() {
            results[i] = &*ctx->state->getWorldRepo();
        });
    }

    for (auto & t : threads) {
        t.join();
    }

    // Verify all got same instance
    for (size_t i = 1; i < 8; i++) {
        ASSERT_EQ(results[0], results[i]);
    }
}

TEST_F(TectonixTest, concurrent_different_tree_shas)
{
    auto ctx = createTectonixContext();

    std::vector<std::thread> threads;
    std::vector<std::string> zonePaths = {
        "//areas/tools/dev",
        "//areas/tools/tec",
        "//areas/platform/core"
    };
    std::map<std::string, std::string> results;  // Store as string (gitRev)
    std::mutex resultsMutex;

    // Multiple threads computing different tree SHAs
    for (const auto & path : zonePaths) {
        for (int i = 0; i < 3; i++) {
            threads.emplace_back([&, path]() {
                auto hash = ctx->state->getWorldTreeSha(path);
                auto hashStr = hash.gitRev();
                std::lock_guard<std::mutex> lock(resultsMutex);
                auto it = results.find(path);
                if (it != results.end()) {
                    // Verify consistency
                    ASSERT_EQ(it->second, hashStr);
                } else {
                    results.emplace(path, hashStr);
                }
            });
        }
    }

    for (auto & t : threads) {
        t.join();
    }

    // Verify we got results for all zones
    ASSERT_EQ(results.size(), 3u);
}

// ============================================================================
// Phase 7: Additional Error Handling Tests
// ============================================================================

TEST_F(TectonixTest, manifest_non_string_id_throws)
{
    // Create a repo with invalid manifest (id is number not string)
    std::filesystem::path badRepoPath = createTempDir();
    AutoDelete delBadRepo(badRepoPath, true);

    git_repository * repo = nullptr;
    ASSERT_EQ(git_repository_init(&repo, badRepoPath.string().c_str(), 0), 0);

    std::filesystem::create_directories(badRepoPath / ".meta");
    std::ofstream manifest(badRepoPath / ".meta/manifest.json");
    manifest << R"({"//zone": {"id": 12345}})";  // id is number, not string
    manifest.close();

    git_index * index = nullptr;
    ASSERT_EQ(git_repository_index(&index, repo), 0);
    ASSERT_EQ(git_index_add_bypath(index, ".meta/manifest.json"), 0);
    ASSERT_EQ(git_index_write(index), 0);

    git_oid treeOid;
    ASSERT_EQ(git_index_write_tree(&treeOid, index), 0);

    git_tree * tree = nullptr;
    ASSERT_EQ(git_tree_lookup(&tree, repo, &treeOid), 0);

    git_signature * sig = nullptr;
    ASSERT_EQ(git_signature_now(&sig, "test", "test@test.com"), 0);

    git_oid commitOid;
    ASSERT_EQ(git_commit_create_v(&commitOid, repo, "HEAD", sig, sig, nullptr, "test", tree, 0), 0);

    char sha[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(sha, sizeof(sha), &commitOid);

    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(index);
    git_repository_free(repo);

    // Create EvalState with bad manifest
    bool readOnly = true;
    fetchers::Settings fetchSettings{};
    EvalSettings evalSettings{readOnly};
    evalSettings.nixPath = {};
    evalSettings.tectonixGitDir = (badRepoPath / ".git").string();
    evalSettings.tectonixGitSha = sha;

    EvalState evalState(
        LookupPath{},
        openStore("dummy://"),
        fetchSettings,
        evalSettings,
        nullptr
    );

    // Should throw when accessing manifest
    Value v;
    Expr * e = evalState.parseExprFromString(
        "builtins.unsafeTectonixInternalManifest",
        evalState.rootPath(CanonPath::root));

    ASSERT_THROW({
        evalState.eval(e, v);
        evalState.forceValue(v, noPos);
    }, Error);
}

TEST_F(TectonixTest, zone_path_traversal_throws)
{
    auto ctx = createTectonixContext();

    // Path traversal attempt should fail
    ASSERT_THROW(
        ctx->state->getWorldTreeSha("//areas/../.git"),
        Error);
}

TEST_F(TectonixTest, nonexistent_git_dir_throws)
{
    bool readOnly = true;
    fetchers::Settings fetchSettings{};
    EvalSettings evalSettings{readOnly};
    evalSettings.nixPath = {};
    evalSettings.tectonixGitDir = "/nonexistent/path/to/repo/.git";
    evalSettings.tectonixGitSha = "0000000000000000000000000000000000000000";

    EvalState evalState(
        LookupPath{},
        openStore("dummy://"),
        fetchSettings,
        evalSettings,
        nullptr
    );

    ASSERT_THROW(evalState.getWorldRepo(), Error);
}

TEST_F(TectonixTest, treeSha_for_nonexistent_subpath_throws)
{
    auto ctx = createTectonixContext();

    // Path that doesn't exist in the tree
    ASSERT_THROW(
        ctx->state->getWorldTreeSha("//areas/tools/dev/nonexistent/deep/path"),
        Error);
}

TEST_F(TectonixTest, manifest_missing_id_field_throws)
{
    // Create a repo with manifest missing id field
    std::filesystem::path badRepoPath = createTempDir();
    AutoDelete delBadRepo(badRepoPath, true);

    git_repository * repo = nullptr;
    ASSERT_EQ(git_repository_init(&repo, badRepoPath.string().c_str(), 0), 0);

    std::filesystem::create_directories(badRepoPath / ".meta");
    std::ofstream manifest(badRepoPath / ".meta/manifest.json");
    manifest << R"({"//zone": {"name": "test"}})";  // missing "id" field
    manifest.close();

    git_index * index = nullptr;
    ASSERT_EQ(git_repository_index(&index, repo), 0);
    ASSERT_EQ(git_index_add_bypath(index, ".meta/manifest.json"), 0);
    ASSERT_EQ(git_index_write(index), 0);

    git_oid treeOid;
    ASSERT_EQ(git_index_write_tree(&treeOid, index), 0);

    git_tree * tree = nullptr;
    ASSERT_EQ(git_tree_lookup(&tree, repo, &treeOid), 0);

    git_signature * sig = nullptr;
    ASSERT_EQ(git_signature_now(&sig, "test", "test@test.com"), 0);

    git_oid commitOid;
    ASSERT_EQ(git_commit_create_v(&commitOid, repo, "HEAD", sig, sig, nullptr, "test", tree, 0), 0);

    char sha[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(sha, sizeof(sha), &commitOid);

    git_signature_free(sig);
    git_tree_free(tree);
    git_index_free(index);
    git_repository_free(repo);

    // Create EvalState with bad manifest
    bool readOnly = true;
    fetchers::Settings fetchSettings{};
    EvalSettings evalSettings{readOnly};
    evalSettings.nixPath = {};
    evalSettings.tectonixGitDir = (badRepoPath / ".git").string();
    evalSettings.tectonixGitSha = sha;

    EvalState evalState(
        LookupPath{},
        openStore("dummy://"),
        fetchSettings,
        evalSettings,
        nullptr
    );

    // Should throw when accessing manifest
    Value v;
    Expr * e = evalState.parseExprFromString(
        "builtins.unsafeTectonixInternalManifest",
        evalState.rootPath(CanonPath::root));

    ASSERT_THROW({
        evalState.eval(e, v);
        evalState.forceValue(v, noPos);
    }, Error);
}

} // namespace nix

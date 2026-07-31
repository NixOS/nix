#include "nix/store/globals.hh"
#include "nix/store/dummy-store.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/git-utils.hh"
#include "nix/util/url.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <git2.h>
#include <gtest/gtest.h>

#include <filesystem>

namespace {

template<class T, void (*F)(T *)>
struct Deleter
{
    void operator()(T * p) const
    {
        if (p)
            F(p);
    }
};

template<class T, void (*F)(T *)>
using Handle = std::unique_ptr<T, Deleter<T, F>>;

using RepoHandle = Handle<git_repository, git_repository_free>;
using IndexHandle = Handle<git_index, git_index_free>;
using TreeHandle = Handle<git_tree, git_tree_free>;
using SigHandle = Handle<git_signature, git_signature_free>;
using RefHandle = Handle<git_reference, git_reference_free>;
using CommitHandle = Handle<git_commit, git_commit_free>;
using SubmoduleHandle = Handle<git_submodule, git_submodule_free>;

#define CHECK_LIBGIT(expr)                                                                        \
    do {                                                                                          \
        int _rc = (expr);                                                                         \
        if (_rc < 0) {                                                                            \
            auto * _err = git_error_last();                                                       \
            FAIL() << #expr << " failed (" << _rc << "): " << (_err ? _err->message : "unknown"); \
        }                                                                                         \
    } while (0)

static void commitAll(git_repository * repo, const char * msg)
{
    IndexHandle idx;
    {
        git_index * raw = nullptr;
        CHECK_LIBGIT(git_repository_index(&raw, repo));
        idx.reset(raw);
    }
    CHECK_LIBGIT(git_index_add_all(idx.get(), nullptr, 0, nullptr, nullptr));
    CHECK_LIBGIT(git_index_write(idx.get()));

    git_oid treeId{};
    CHECK_LIBGIT(git_index_write_tree(&treeId, idx.get()));
    TreeHandle tree;
    {
        git_tree * raw = nullptr;
        CHECK_LIBGIT(git_tree_lookup(&raw, repo, &treeId));
        tree.reset(raw);
    }

    SigHandle sig;
    {
        git_signature * raw = nullptr;
        CHECK_LIBGIT(git_signature_now(&raw, "you", "you@example.com"));
        sig.reset(raw);
    }

    git_oid commitId{};
    if (git_repository_is_empty(repo) == 1) {
        CHECK_LIBGIT(git_commit_create_v(&commitId, repo, "HEAD", sig.get(), sig.get(), nullptr, msg, tree.get(), 0));
        CHECK_LIBGIT(git_reference_create(nullptr, repo, "refs/heads/main", &commitId, true, nullptr));
        CHECK_LIBGIT(git_repository_set_head(repo, "refs/heads/main"));
    } else {
        RefHandle head;
        {
            git_reference * raw = nullptr;
            CHECK_LIBGIT(git_repository_head(&raw, repo));
            head.reset(raw);
        }
        CommitHandle parent;
        {
            git_commit * raw = nullptr;
            CHECK_LIBGIT(git_commit_lookup(&raw, repo, git_reference_target(head.get())));
            parent.reset(raw);
        }
        const git_commit * parents[] = {parent.get()};
        CHECK_LIBGIT(git_commit_create(
            &commitId,
            repo,
            "HEAD",
            sig.get(),
            sig.get(),
            /*message_encoding=*/nullptr,
            msg,
            tree.get(),
            /*parent_count=*/1,
            &parents[0]));
    }
}

} // namespace

namespace nix::fetchers {

class GitTest : public ::testing::Test
{
    std::unique_ptr<AutoDelete> delTmpDir;

protected:
    std::filesystem::path tmpDir;

    void SetUp() override
    {
        tmpDir = createTempDir();
        delTmpDir = std::make_unique<AutoDelete>(tmpDir, /*recursive=*/true);
        nix::initLibStore(/*loadConfig=*/false);
        git_libgit2_init();
    }

    void TearDown() override
    {
        delTmpDir.reset();
    }
};

// Regression test for https://github.com/NixOS/nix/issues/13215
TEST_F(GitTest, submodulePeriodSupport)
{
    auto storePath = tmpDir / "store";
    auto repoPath = tmpDir / "repo";
    auto submodulePath = tmpDir / "submodule";

    // Set up our git directories: one top level and a submodule
    // the submodule in the .gitmodules has the branch listed as '.'

    // 1) Create sub repo
    {
        git_repository * raw = nullptr;
        CHECK_LIBGIT(git_repository_init(&raw, submodulePath.string().c_str(), /*is_bare=*/0));
        RepoHandle sub(raw);
        writeFile(submodulePath / "lib.txt", "hello from submodule\n");
        commitAll(sub.get(), "init sub");
    }

    // 2) Create super repo
    RepoHandle super;
    {
        git_repository * raw = nullptr;
        CHECK_LIBGIT(git_repository_init(&raw, repoPath.string().c_str(), /*is_bare=*/0));
        super.reset(raw);
    }

    writeFile(repoPath / "README.md", "# super\n");
    commitAll(super.get(), "init super");

    // 3) Add submodule at deps/sub
    {
        git_repository * raw = nullptr;
        git_clone_options cloneOpts = GIT_CLONE_OPTIONS_INIT;
        // clone from local subPath into superPath/deps/sub
        CHECK_LIBGIT(
            git_clone(&raw, submodulePath.string().c_str(), (repoPath / "deps" / "sub").string().c_str(), &cloneOpts));
        RepoHandle sub(raw);
    }

    // 4) Add submodule and set branch="."
    SubmoduleHandle sm;
    {
        git_submodule * raw = nullptr;
        CHECK_LIBGIT(git_submodule_add_setup(
            &raw,
            super.get(),
            "../submodule",
            "deps/sub",
            /*use_gitlink=*/1));
        sm.reset(raw);
    }
    CHECK_LIBGIT(git_submodule_set_branch(super.get(), git_submodule_name(sm.get()), /*branch=*/"."));
    CHECK_LIBGIT(git_submodule_sync(sm.get()));

    // 5) Finalize now that the worktree exists; libgit2 can read its HEAD OID
    CHECK_LIBGIT(git_submodule_add_finalize(sm.get()));
    // 6) Commit the addition in super
    commitAll(super.get(), "Add submodule with branch='.'");

    auto store = [] {
        auto cfg = make_ref<DummyStoreConfig>(StoreReference::Params{});
        cfg->readOnly = false;
        return cfg->openStore();
    }();

    auto settings = fetchers::Settings{};
    auto input = fetchers::Input::fromAttrs(
        settings,
        {
            {"url", "file://" + encodeUrlPath(pathToUrlPath(repoPath))},
            {"submodules", Explicit{true}},
            {"type", "git"},
            {"ref", "main"},
        });

    auto [accessor, i] = input.getAccessor(settings, *store);

    ASSERT_EQ(accessor->readFile(CanonPath("deps/sub/lib.txt")), "hello from submodule\n");
}

TEST_F(GitTest, getRevCountShallowThrows)
{
    auto repoPath = tmpDir / "repo";

    // Create a repo with two commits
    RepoHandle repo;
    {
        git_repository * raw = nullptr;
        CHECK_LIBGIT(git_repository_init(&raw, repoPath.string().c_str(), /*is_bare=*/0));
        repo.reset(raw);
    }
    writeFile(repoPath / "file.txt", "first\n");
    commitAll(repo.get(), "first");
    writeFile(repoPath / "file.txt", "second\n");
    commitAll(repo.get(), "second");

    // Mark HEAD as a shallow boundary by writing .git/shallow
    RefHandle headRef;
    {
        git_reference * raw = nullptr;
        CHECK_LIBGIT(git_repository_head(&raw, repo.get()));
        headRef.reset(raw);
    }
    char oidStr[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(oidStr, sizeof(oidStr), git_reference_target(headRef.get()));
    writeFile(repoPath / ".git" / "shallow", std::string(oidStr) + "\n");
    headRef.reset();
    repo.reset();

    auto nixRepo = GitRepo::openRepo(repoPath, {});

    ASSERT_TRUE(nixRepo->isShallow());

    auto rev = nixRepo->resolveRef("HEAD");
    // getRevCount on a shallow repo should throw, not silently return 1
    auto expectedMsg =
        fmt("Git commit '%s' has an incomplete history "
            "(shallow boundary; 0 of 1 parents locally available). "
            "To resolve this, either enable the shallow parameter in your flake URL (?shallow=1) "
            "or set the shallow parameter to true in builtins.fetchGit, "
            "or fetch the complete history for this branch.",
            rev.gitRev());
    EXPECT_THAT(
        [&] { nixRepo->getRevCount(rev); },
        ::testing::ThrowsMessage<Error>(testing::HasSubstrIgnoreANSIMatcher(expectedMsg)));
}

TEST_F(GitTest, getRevCountShallowPartialWorks)
{
    auto repoPath = tmpDir / "repo";

    // Create a repo: init -> A -> B on main, init -> A2 -> B2 on feature
    RepoHandle repo;
    {
        git_repository * raw = nullptr;
        CHECK_LIBGIT(git_repository_init(&raw, repoPath.string().c_str(), /*is_bare=*/0));
        repo.reset(raw);
    }
    writeFile(repoPath / "file.txt", "init\n");
    commitAll(repo.get(), "init");

    writeFile(repoPath / "file.txt", "A\n");
    commitAll(repo.get(), "A");
    writeFile(repoPath / "file.txt", "B\n");
    commitAll(repo.get(), "B");

    // Create feature branch from init (HEAD~2)
    git_reference * headRaw = nullptr;
    CHECK_LIBGIT(git_repository_head(&headRaw, repo.get()));
    RefHandle headRef(headRaw);

    git_commit * headCommitRaw = nullptr;
    CHECK_LIBGIT(git_commit_lookup(&headCommitRaw, repo.get(), git_reference_target(headRef.get())));
    CommitHandle headCommit(headCommitRaw);

    git_commit * parentRaw = nullptr;
    CHECK_LIBGIT(git_commit_parent(&parentRaw, headCommit.get(), 0));
    CommitHandle parent(parentRaw);

    git_commit * initRaw = nullptr;
    CHECK_LIBGIT(git_commit_parent(&initRaw, parent.get(), 0));
    CommitHandle initCommit(initRaw);

    git_reference * featureRaw = nullptr;
    CHECK_LIBGIT(git_branch_create(&featureRaw, repo.get(), "feature", initCommit.get(), /*force=*/0));
    RefHandle featureRef(featureRaw);

    CHECK_LIBGIT(git_repository_set_head(repo.get(), "refs/heads/feature"));
    git_checkout_options checkoutOpts = GIT_CHECKOUT_OPTIONS_INIT;
    checkoutOpts.checkout_strategy = GIT_CHECKOUT_FORCE;
    CHECK_LIBGIT(git_checkout_head(repo.get(), &checkoutOpts));

    writeFile(repoPath / "file.txt", "A2\n");
    commitAll(repo.get(), "A2");
    writeFile(repoPath / "file.txt", "B2\n");
    commitAll(repo.get(), "B2");

    // Mark feature's HEAD (B2) as a shallow boundary
    git_reference * featureHeadRaw = nullptr;
    CHECK_LIBGIT(git_repository_head(&featureHeadRaw, repo.get()));
    RefHandle featureHeadRef(featureHeadRaw);
    char oidStr[GIT_OID_SHA1_HEXSIZE + 1];
    git_oid_tostr(oidStr, sizeof(oidStr), git_reference_target(featureHeadRef.get()));
    writeFile(repoPath / ".git" / "shallow", std::string(oidStr) + "\n");
    featureHeadRef.reset();
    repo.reset();

    auto nixRepo = GitRepo::openRepo(repoPath, {});
    ASSERT_TRUE(nixRepo->isShallow());

    // main has complete history (init -> A -> B = 3 commits), should work
    auto mainRev = nixRepo->resolveRef("refs/heads/main");
    ASSERT_EQ(nixRepo->getRevCount(mainRev), 3);

    // feature HEAD is at a shallow boundary, should throw
    auto featureRev = nixRepo->resolveRef("HEAD");
    ASSERT_THROW(nixRepo->getRevCount(featureRev), Error);
}

TEST_F(GitTest, getRevCountAuthorNamedParent)
{
    auto repoPath = tmpDir / "repo";

    RepoHandle repo;
    {
        git_repository * raw = nullptr;
        CHECK_LIBGIT(git_repository_init(&raw, repoPath.string().c_str(), /*is_bare=*/0));
        repo.reset(raw);
    }

    // Create a commit whose author name is "parent" — this should not
    // confuse the raw header parser into thinking there's an extra parent.
    writeFile(repoPath / "file.txt", "hello\n");
    IndexHandle idx;
    {
        git_index * raw = nullptr;
        CHECK_LIBGIT(git_repository_index(&raw, repo.get()));
        idx.reset(raw);
    }
    CHECK_LIBGIT(git_index_add_all(idx.get(), nullptr, 0, nullptr, nullptr));
    CHECK_LIBGIT(git_index_write(idx.get()));

    git_oid treeId{};
    CHECK_LIBGIT(git_index_write_tree(&treeId, idx.get()));
    TreeHandle tree;
    {
        git_tree * raw = nullptr;
        CHECK_LIBGIT(git_tree_lookup(&raw, repo.get(), &treeId));
        tree.reset(raw);
    }

    SigHandle sig;
    {
        git_signature * raw = nullptr;
        CHECK_LIBGIT(git_signature_now(&raw, "parent", "parent@example.com"));
        sig.reset(raw);
    }

    git_oid commitId{};
    CHECK_LIBGIT(
        git_commit_create_v(&commitId, repo.get(), "HEAD", sig.get(), sig.get(), nullptr, "init", tree.get(), 0));
    CHECK_LIBGIT(git_reference_create(nullptr, repo.get(), "refs/heads/main", &commitId, true, nullptr));
    CHECK_LIBGIT(git_repository_set_head(repo.get(), "refs/heads/main"));

    auto nixRepo = GitRepo::openRepo(repoPath, {});
    auto rev = nixRepo->resolveRef("HEAD");
    // Root commit (no parents): rawParents should be 0 despite "parent" in author field
    ASSERT_EQ(nixRepo->getRevCount(rev), 1);

    // Add a second commit (also authored by "parent") so the first becomes a real parent.
    // The second commit's header has rawParents==1 AND "parent" in author/committer.
    writeFile(repoPath / "file.txt", "world\n");
    CHECK_LIBGIT(git_index_add_all(idx.get(), nullptr, 0, nullptr, nullptr));
    CHECK_LIBGIT(git_index_write(idx.get()));
    git_oid treeId2{};
    CHECK_LIBGIT(git_index_write_tree(&treeId2, idx.get()));
    TreeHandle tree2;
    {
        git_tree * raw = nullptr;
        CHECK_LIBGIT(git_tree_lookup(&raw, repo.get(), &treeId2));
        tree2.reset(raw);
    }
    CommitHandle parentCommit;
    {
        git_commit * raw = nullptr;
        CHECK_LIBGIT(git_commit_lookup(&raw, repo.get(), &commitId));
        parentCommit.reset(raw);
    }
    const git_commit * parents[] = {parentCommit.get()};
    git_oid commitId2{};
    CHECK_LIBGIT(git_commit_create(
        &commitId2, repo.get(), "HEAD", sig.get(), sig.get(), nullptr, "second", tree2.get(), 1, &parents[0]));
    repo.reset();

    nixRepo = GitRepo::openRepo(repoPath, {});
    rev = nixRepo->resolveRef("HEAD");
    ASSERT_EQ(nixRepo->getRevCount(rev), 2);
}

} // namespace nix::fetchers

#include "nix/store/store-open.hh"
#include "nix/store/globals.hh"
#include "nix/store/dummy-store.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/git-utils.hh"

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

#define CHECK_LIBGIT(expr) ASSERT_TRUE((expr) >= 0) << git_error_last()

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

using namespace nix;

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
            {"url", "file://" + repoPath.string()},
            {"submodules", Explicit{true}},
            {"type", "git"},
            {"ref", "main"},
        });

    auto [accessor, i] = input.getAccessor(store);

    ASSERT_EQ(accessor->readFile(CanonPath("deps/sub/lib.txt")), "hello from submodule\n");
}

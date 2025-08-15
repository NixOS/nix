#include <gtest/gtest.h>
#include "nix/store/store-open.hh"

#include <nix/fetchers/fetch-settings.hh>
#include <nix/fetchers/fetchers.hh>
#include <git2.h>
#include <nix/fetchers/git-utils.hh>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

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
using handle = std::unique_ptr<T, Deleter<T, F>>;

using repo_h = handle<git_repository, git_repository_free>;
using idx_h = handle<git_index, git_index_free>;
using tree_h = handle<git_tree, git_tree_free>;
using sig_h = handle<git_signature, git_signature_free>;
using ref_h = handle<git_reference, git_reference_free>;
using cmt_h = handle<git_commit, git_commit_free>;
using sub_h = handle<git_submodule, git_submodule_free>;

#define CHECK(x)         \
    do {                 \
        int _e = (x);    \
        if (_e < 0)      \
            die(_e, #x); \
    } while (0)

static void die(int err, const char * what)
{
    const git_error * e = git_error_last();
    throw std::runtime_error(
        std::string(what) + " failed: " + std::to_string(err) + " " + (e && e->message ? e->message : ""));
}

static void writefile(const fs::path & p, std::string_view s)
{
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    if (!f)
        throw std::runtime_error("open " + p.string());
    f << s;
}

static void commit_all(git_repository * repo, const char * msg)
{
    idx_h idx;
    {
        git_index * raw = nullptr;
        CHECK(git_repository_index(&raw, repo));
        idx.reset(raw);
    }
    CHECK(git_index_add_all(idx.get(), nullptr, 0, nullptr, nullptr));
    CHECK(git_index_write(idx.get()));

    git_oid tree_id{};
    CHECK(git_index_write_tree(&tree_id, idx.get()));
    tree_h tree;
    {
        git_tree * raw = nullptr;
        CHECK(git_tree_lookup(&raw, repo, &tree_id));
        tree.reset(raw);
    }

    sig_h sig;
    {
        git_signature * raw = nullptr;
        CHECK(git_signature_now(&raw, "you", "you@example.com"));
        sig.reset(raw);
    }

    git_oid commit_id{};
    if (git_repository_is_empty(repo) == 1) {
        CHECK(git_commit_create_v(&commit_id, repo, "HEAD", sig.get(), sig.get(), nullptr, msg, tree.get(), 0));
        CHECK(git_reference_create(nullptr, repo, "refs/heads/main", &commit_id, true, nullptr));
        CHECK(git_repository_set_head(repo, "refs/heads/main"));
    } else {
        ref_h head;
        {
            git_reference * raw = nullptr;
            CHECK(git_repository_head(&raw, repo));
            head.reset(raw);
        }
        cmt_h parent;
        {
            git_commit * raw = nullptr;
            CHECK(git_commit_lookup(&raw, repo, git_reference_target(head.get())));
            parent.reset(raw);
        }
        const git_commit * parents[] = {parent.get()};
        CHECK(git_commit_create(
            &commit_id, repo, "HEAD", sig.get(), sig.get(), nullptr, msg, tree.get(), 1, &parents[0]));
    }
}

namespace nix {

class GitTest : public ::testing::Test
{
protected:
    fs::path tmp;

    void SetUp() override
    {
        tmp = createTempDir();
        // Initialize libstore
        nix::initLibStore(false);
        // Initialize libgit2
        git_libgit2_init();
    }

    void TearDown() override
    {
        // Clean up; ignore errors on some platforms if files are readonly.
        std::error_code ec;
        fs::remove_all(tmp, ec);
        git_libgit2_shutdown();
    }
};

TEST_F(GitTest, submodule_period_support)
{
    const fs::path storePath = tmp / "store";
    const fs::path repoPath = tmp / "repo";
    const fs::path submodulePath = tmp / "submodule";
    const fs::path submodulePathRelFromSuper = fs::path("..") / submodulePath.filename(); // "../submodule"

    // Set up our git directories: one top level and a submodule
    // the submodule in the .gitmodules has the branch listed as '.'
    // https://github.com/NixOS/nix/issues/13215

    // 1) Create sub repo
    {
        git_repository * raw = nullptr;
        CHECK(git_repository_init(&raw, submodulePath.string().c_str(), 0));
        repo_h sub(raw);
        writefile(submodulePath / "lib.txt", "hello from submodule\n");
        commit_all(sub.get(), "init sub");
    }

    // 2) Create super repo
    repo_h super;
    {
        git_repository * raw = nullptr;
        CHECK(git_repository_init(&raw, repoPath.string().c_str(), 0));
        super.reset(raw);
    }
    writefile(repoPath / "README.md", "# super\n");
    commit_all(super.get(), "init super");

    // 3) Add submodule at deps/sub
    {
        git_repository * raw = nullptr;
        git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
        // clone from local subPath into superPath/deps/sub
        CHECK(
            git_clone(&raw, submodulePath.string().c_str(), (repoPath / "deps" / "sub").string().c_str(), &clone_opts));
        repo_h sub(raw);
    }

    // 4) Add submodule and set branch="."
    sub_h sm;
    {
        git_submodule * raw = nullptr;
        CHECK(git_submodule_add_setup(
            &raw,
            super.get(),
            // URL recorded in .gitmodules; keep it relative if you like
            (fs::path("..") / submodulePath.filename()).string().c_str(),
            "deps/sub",
            /*use_gitlink*/ 1));
        sm.reset(raw);
    }
    CHECK(git_submodule_set_branch(super.get(), git_submodule_name(sm.get()), "."));
    CHECK(git_submodule_sync(sm.get()));

    // 5) Finalize now that the worktree exists; libgit2 can read its HEAD OID
    CHECK(git_submodule_add_finalize(sm.get()));
    // 6) Commit the addition in super
    commit_all(super.get(), "Add submodule with branch='.'");

    // Create a temporary store
    // TODO: can this be an in-memory store somehow?
    Path storeTmpDir = createTempDir();
    auto storeTmpDirAutoDelete = AutoDelete(storeTmpDir, true);
    ref<Store> store = openStore(storeTmpDir);

    auto settings = fetchers::Settings{};
    auto input = fetchers::Input::fromAttrs(
        settings,
        {{"url", "file://" + repoPath.string()}, {"submodules", Explicit{true}}, {"type", "git"}, {"ref", "main"}});

    auto [accessor, i] = input.getAccessor(store);
}

} // namespace nix

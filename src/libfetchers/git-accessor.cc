#include "input-accessor.hh"

#include <git2/blob.h>
#include <git2/commit.h>
#include <git2/errors.h>
#include <git2/global.h>
#include <git2/object.h>
#include <git2/repository.h>
#include <git2/tree.h>

namespace nix {

template<auto del>
struct Deleter
{
    template <typename T>
    void operator()(T * p) const { del(p); };
};

struct GitInputAccessor : InputAccessor
{
    typedef std::unique_ptr<git_repository, Deleter<git_repository_free>> Repository;
    typedef std::unique_ptr<git_tree_entry, Deleter<git_tree_entry_free>> TreeEntry;
    typedef std::unique_ptr<git_tree, Deleter<git_tree_free>> Tree;
    typedef std::unique_ptr<git_blob, Deleter<git_blob_free>> Blob;

    Repository repo;
    Tree root;

    GitInputAccessor(const CanonPath & path, const Hash & rev)
    {
        if (git_libgit2_init() < 0)
            throw Error("initialising libgit2': %s", path, git_error_last()->message);

        git_repository * _repo;
        if (git_repository_open(&_repo, path.c_str()))
            throw Error("opening Git repository '%s': %s", path, git_error_last()->message);
        repo = Repository(_repo);

        git_oid oid;
        if (git_oid_fromstr(&oid, rev.gitRev().c_str()))
            throw Error("cannot convert '%s' to a Git OID", rev.gitRev());

        git_object * obj = nullptr;
        if (git_object_lookup(&obj, repo.get(), &oid, GIT_OBJECT_ANY)) {
            auto err = git_error_last();
            throw Error("getting Git object '%s': %s", rev.gitRev(), err->message);
        }

        if (git_object_peel((git_object * *) &root, obj, GIT_OBJECT_TREE)) {
            auto err = git_error_last();
            throw Error("peeling Git object '%s': %s", rev.gitRev(), err->message);
        }
    }

    std::string readFile(const CanonPath & path) override
    {
        auto blob = getBlob(path);

        auto data = std::string_view((const char *) git_blob_rawcontent(blob.get()), git_blob_rawsize(blob.get()));

        return std::string(data);
    }

    bool pathExists(const CanonPath & path) override
    {
        return path.isRoot() ? true : (bool) lookup(path);
    }

    Stat lstat(const CanonPath & path) override
    {
        if (path.isRoot())
            return Stat { .type = tDirectory };

        auto entry = need(path);

        auto mode = git_tree_entry_filemode(entry);

        if (mode == GIT_FILEMODE_TREE)
            return Stat { .type = tDirectory };

        else if (mode == GIT_FILEMODE_BLOB)
            return Stat { .type = tRegular };

        else if (mode == GIT_FILEMODE_BLOB_EXECUTABLE)
            return Stat { .type = tRegular, .isExecutable = true };

        else if (mode == GIT_FILEMODE_LINK)
            return Stat { .type = tSymlink };

        else if (mode == GIT_FILEMODE_COMMIT)
            // Treat submodules as an empty directory.
            return Stat { .type = tDirectory };

        else
            throw Error("file '%s' has an unsupported Git file type");

    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        return std::visit(overloaded {
            [&](Tree tree) {
                DirEntries res;

                auto count = git_tree_entrycount(tree.get());

                for (size_t n = 0; n < count; ++n) {
                    auto entry = git_tree_entry_byindex(tree.get(), n);
                    // FIXME: add to cache
                    res.emplace(std::string(git_tree_entry_name(entry)), DirEntry{});
                }

                return res;
            },
            [&](Submodule) {
                return DirEntries();
            }
        }, getTree(path));
    }

    std::string readLink(const CanonPath & path) override
    {
        throw UnimplementedError("GitInputAccessor::readLink");
    }

    std::map<CanonPath, TreeEntry> lookupCache;

    /* Recursively look up 'path' relative to the root. */
    git_tree_entry * lookup(const CanonPath & path)
    {
        if (path.isRoot()) return nullptr;

        auto i = lookupCache.find(path);
        if (i == lookupCache.end()) {
            git_tree_entry * entry = nullptr;
            if (auto err = git_tree_entry_bypath(&entry, root.get(), std::string(path.rel()).c_str())) {
                if (err != GIT_ENOTFOUND)
                    throw Error("looking up '%s': %s", showPath(path), git_error_last()->message);
            }

            i = lookupCache.emplace(path, TreeEntry(entry)).first;
        }

        return &*i->second;
    }

    git_tree_entry * need(const CanonPath & path)
    {
        auto entry = lookup(path);
        if (!entry)
            throw Error("'%s' does not exist", showPath(path));
        return entry;
    }

    struct Submodule { };

    std::variant<Tree, Submodule> getTree(const CanonPath & path)
    {
        if (path.isRoot()) {
            git_tree * tree = nullptr;
            if (git_tree_dup(&tree, root.get()))
                throw Error("duplicating directory '%s': %s", showPath(path), git_error_last()->message);
            return Tree(tree);
        }

        auto entry = need(path);

        if (git_tree_entry_type(entry) == GIT_OBJECT_COMMIT)
            return Submodule();

        if (git_tree_entry_type(entry) != GIT_OBJECT_TREE)
            throw Error("'%s' is not a directory", showPath(path));

        git_tree * tree = nullptr;
        if (git_tree_entry_to_object((git_object * *) &tree, repo.get(), entry))
            throw Error("looking up directory '%s': %s", showPath(path), git_error_last()->message);

        return Tree(tree);
    }

    Blob getBlob(const CanonPath & path)
    {
        auto notRegular = [&]()
        {
            throw Error("'%s' is not a regular file", showPath(path));
        };

        if (path.isRoot()) notRegular();

        auto entry = need(path);

        if (git_tree_entry_type(entry) != GIT_OBJECT_BLOB)
            notRegular();

        git_blob * blob = nullptr;
        if (git_tree_entry_to_object((git_object * *) &blob, repo.get(), entry))
            throw Error("looking up regular file '%s': %s", showPath(path), git_error_last()->message);

        return Blob(blob);
    }
};

ref<InputAccessor> makeGitInputAccessor(const CanonPath & path, const Hash & rev)
{
    return make_ref<GitInputAccessor>(path, rev);
}


}

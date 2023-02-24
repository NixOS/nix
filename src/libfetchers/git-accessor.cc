#include "git-utils.hh"
#include "input-accessor.hh"

#include <boost/core/span.hpp>

#include <git2/blob.h>
#include <git2/commit.h>
#include <git2/errors.h>
#include <git2/global.h>
#include <git2/object.h>
#include <git2/repository.h>
#include <git2/tree.h>

#include "tarfile.hh"
#include <archive_entry.h>

#include <unordered_set>
#include <queue>

namespace std {

template<> struct hash<git_oid>
{
    size_t operator()(const git_oid & oid) const
    {
        return * (size_t *) oid.id;
    }
};

std::ostream & operator << (std::ostream & str, const git_oid & oid)
{
    str << git_oid_tostr_s(&oid);
    return str;
}

bool operator == (const git_oid & oid1, const git_oid & oid2)
{
    return git_oid_equal(&oid1, &oid2);
}

}

namespace nix {

template<auto del>
struct Deleter
{
    template <typename T>
    void operator()(T * p) const { del(p); };
};

typedef std::unique_ptr<git_repository, Deleter<git_repository_free>> Repository;
typedef std::unique_ptr<git_tree_entry, Deleter<git_tree_entry_free>> TreeEntry;
typedef std::unique_ptr<git_tree, Deleter<git_tree_free>> Tree;
typedef std::unique_ptr<git_treebuilder, Deleter<git_treebuilder_free>> TreeBuilder;
typedef std::unique_ptr<git_blob, Deleter<git_blob_free>> Blob;
typedef std::unique_ptr<git_object, Deleter<git_object_free>> Object;
typedef std::unique_ptr<git_commit, Deleter<git_commit_free>> Commit;

static void initLibGit2()
{
    if (git_libgit2_init() < 0)
        throw Error("initialising libgit2: %s", git_error_last()->message);
}

static Repository openRepo(const CanonPath & path)
{
    initLibGit2();
    git_repository * _repo;
    if (git_repository_open(&_repo, path.c_str()))
        throw Error("opening Git repository '%s': %s", path, git_error_last()->message);
    return Repository(_repo);
}

git_oid hashToOID(const Hash & hash)
{
    git_oid oid;
    if (git_oid_fromstr(&oid, hash.gitRev().c_str()))
        throw Error("cannot convert '%s' to a Git OID", hash.gitRev());
    return oid;
}

Object lookupObject(git_repository * repo, const git_oid & oid)
{
    git_object * obj = nullptr;
    if (git_object_lookup(&obj, repo, &oid, GIT_OBJECT_ANY)) {
        auto err = git_error_last();
        throw Error("getting Git object '%s': %s", oid, err->message);
    }

    return Object(obj);
}

template<typename T>
T peelObject(git_repository * repo, git_object * obj, git_object_t type)
{
    typename T::pointer obj2 = nullptr;
    if (git_object_peel((git_object * *) &obj2, obj, type)) {
        auto err = git_error_last();
        throw Error("peeling Git object '%s': %s", git_object_id(obj), err->message);
    }

    return T(obj2);
}

struct GitInputAccessor : InputAccessor
{
    Repository repo;
    Tree root;

    GitInputAccessor(Repository && repo_, const Hash & rev)
        : repo(std::move(repo_))
        , root(peelObject<Tree>(repo.get(), lookupObject(repo.get(), hashToOID(rev)).get(), GIT_OBJECT_TREE))
    {
    }

    std::string readBlob(const CanonPath & path, bool symlink)
    {
        auto blob = getBlob(path, symlink);

        auto data = std::string_view((const char *) git_blob_rawcontent(blob.get()), git_blob_rawsize(blob.get()));

        return std::string(data);
    }

    std::string readFile(const CanonPath & path) override
    {
        return readBlob(path, false);
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
        return readBlob(path, true);
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

    Blob getBlob(const CanonPath & path, bool expectSymlink)
    {
        auto notExpected = [&]()
        {
            throw Error(
                expectSymlink
                ? "'%s' is not a symlink"
                : "'%s' is not a regular file",
                showPath(path));
        };

        if (path.isRoot()) notExpected();

        auto entry = need(path);

        if (git_tree_entry_type(entry) != GIT_OBJECT_BLOB)
            notExpected();

        auto mode = git_tree_entry_filemode(entry);
        if (expectSymlink) {
            if (mode != GIT_FILEMODE_LINK)
                notExpected();
        } else {
            if (mode != GIT_FILEMODE_BLOB && mode != GIT_FILEMODE_BLOB_EXECUTABLE)
                notExpected();
        }

        git_blob * blob = nullptr;
        if (git_tree_entry_to_object((git_object * *) &blob, repo.get(), entry))
            throw Error("looking up file '%s': %s", showPath(path), git_error_last()->message);

        return Blob(blob);
    }
};

ref<InputAccessor> makeGitInputAccessor(const CanonPath & path, const Hash & rev)
{
    return make_ref<GitInputAccessor>(openRepo(path), rev);
}

static Repository openTarballCache()
{
    static CanonPath repoDir(getCacheDir() + "/nix/tarball-cache");

    initLibGit2();

    if (pathExists(repoDir.abs()))
        return openRepo(repoDir);
    else {
        git_repository * _repo;
        if (git_repository_init(&_repo, repoDir.c_str(), true))
            throw Error("creating Git repository '%s': %s", repoDir, git_error_last()->message);
        return Repository(_repo);
    }
}

TarballInfo importTarball(Source & source)
{
    auto repo = openTarballCache();

    TarArchive archive(source);

    struct PendingDir
    {
        std::string name;
        TreeBuilder builder;
    };

    std::vector<PendingDir> pendingDirs;

    auto pushBuilder = [&](std::string name)
    {
        git_treebuilder * b;
        if (git_treebuilder_new(&b, repo.get(), nullptr))
            throw Error("creating a tree builder: %s", git_error_last()->message);
        pendingDirs.push_back({ .name = std::move(name), .builder = TreeBuilder(b) });
    };

    auto popBuilder = [&]() -> std::pair<git_oid, std::string>
    {
        assert(!pendingDirs.empty());
        auto pending = std::move(pendingDirs.back());
        git_oid oid;
        if (git_treebuilder_write(&oid, pending.builder.get()))
            throw Error("creating a tree object: %s", git_error_last()->message);
        pendingDirs.pop_back();
        return {oid, pending.name};
    };

    auto addToTree = [&](const std::string & name, const git_oid & oid, git_filemode_t mode)
    {
        assert(!pendingDirs.empty());
        auto & pending = pendingDirs.back();
        if (git_treebuilder_insert(nullptr, pending.builder.get(), name.c_str(), &oid, mode))
            throw Error("adding a file to a tree builder: %s", git_error_last()->message);
    };

    auto updateBuilders = [&](boost::span<const std::string> names)
    {
        // Find the common prefix of pendingDirs and names.
        size_t prefixLen = 0;
        for (; prefixLen < names.size() && prefixLen + 1 < pendingDirs.size(); ++prefixLen)
            if (names[prefixLen] != pendingDirs[prefixLen + 1].name)
                break;

        // Finish the builders that are not part of the common prefix.
        for (auto n = pendingDirs.size(); n > prefixLen + 1; --n) {
            auto [oid, name] = popBuilder();
            addToTree(name, oid, GIT_FILEMODE_TREE);
        }

        // Create builders for the new directories.
        for (auto n = prefixLen; n < names.size(); ++n)
            pushBuilder(names[n]);

    };

    pushBuilder("");

    size_t componentsToStrip = 1;

    time_t lastModified = 0;

    for (;;) {
        // FIXME: merge with extract_archive
        struct archive_entry * entry;
        int r = archive_read_next_header(archive.archive, &entry);
        if (r == ARCHIVE_EOF) break;
        auto path = archive_entry_pathname(entry);
        if (!path)
            throw Error("cannot get archive member name: %s", archive_error_string(archive.archive));
        if (r == ARCHIVE_WARN)
            warn(archive_error_string(archive.archive));
        else
            archive.check(r);

        lastModified = std::max(lastModified, archive_entry_mtime(entry));

        auto pathComponents = tokenizeString<std::vector<std::string>>(path, "/");

        boost::span<const std::string> pathComponents2{pathComponents};

        if (pathComponents2.size() <= componentsToStrip) continue;
        pathComponents2 = pathComponents2.subspan(componentsToStrip);

        updateBuilders(
            archive_entry_filetype(entry) == AE_IFDIR
            ? pathComponents2
            : pathComponents2.first(pathComponents2.size() - 1));

        switch (archive_entry_filetype(entry)) {

        case AE_IFDIR:
            // Nothing to do right now.
            break;

        case AE_IFREG: {

            git_writestream * stream = nullptr;
            if (git_blob_create_from_stream(&stream, repo.get(), nullptr))
                throw Error("creating a blob stream object: %s", git_error_last()->message);

            while (true) {
                std::vector<unsigned char> buf(128 * 1024);
                auto n = archive_read_data(archive.archive, buf.data(), buf.size());
                if (n < 0)
                    throw Error("cannot read file '%s' from tarball", path);
                if (n == 0) break;
                if (stream->write(stream, (const char *) buf.data(), n))
                    throw Error("writing a blob for tarball member '%s': %s", path, git_error_last()->message);
            }

            git_oid oid;
            if (git_blob_create_from_stream_commit(&oid, stream))
                throw Error("creating a blob object for tarball member '%s': %s", path, git_error_last()->message);

            addToTree(*pathComponents.rbegin(), oid,
                archive_entry_mode(entry) & S_IXUSR
                ? GIT_FILEMODE_BLOB_EXECUTABLE
                : GIT_FILEMODE_BLOB);

            break;
        }

        case AE_IFLNK: {
            auto target = archive_entry_symlink(entry);

            git_oid oid;
            if (git_blob_create_from_buffer(&oid, repo.get(), target, strlen(target)))
                throw Error("creating a blob object for tarball symlink member '%s': %s", path, git_error_last()->message);

            addToTree(*pathComponents.rbegin(), oid, GIT_FILEMODE_LINK);

            break;
        }

        default:
            throw Error("file '%s' in tarball has unsupported file type", path);
        }
    }

    updateBuilders({});

    auto [oid, _name] = popBuilder();

    return TarballInfo {
        .treeHash = Hash::parseAny(git_oid_tostr_s(&oid), htSHA1),
        .lastModified = lastModified
    };
}

ref<InputAccessor> makeTarballCacheAccessor(const Hash & rev)
{
    return make_ref<GitInputAccessor>(openTarballCache(), rev);
}

bool tarballCacheContains(const Hash & treeHash)
{
    auto repo = openTarballCache();

    auto oid = hashToOID(treeHash);

    git_object * obj = nullptr;
    if (auto errCode = git_object_lookup(&obj, repo.get(), &oid, GIT_OBJECT_TREE)) {
        if (errCode == GIT_ENOTFOUND) return false;
        auto err = git_error_last();
        throw Error("getting Git object '%s': %s", treeHash.gitRev(), err->message);
    }

    return true;
}

struct GitRepoImpl : GitRepo
{
    Repository repo;

    GitRepoImpl(const CanonPath & path)
        : repo(std::move(nix::openRepo(path)))
    { }

    uint64_t getRevCount(const Hash & rev) override
    {
        std::unordered_set<git_oid> done;
        std::queue<Commit> todo;

        todo.push(peelObject<Commit>(repo.get(), lookupObject(repo.get(), hashToOID(rev)).get(), GIT_OBJECT_COMMIT));

        while (auto commit = pop(todo)) {
            if (!done.insert(*git_commit_id(commit->get())).second) continue;

            for (size_t n = 0; n < git_commit_parentcount(commit->get()); ++n) {
                git_commit * parent;
                if (git_commit_parent(&parent, commit->get(), n))
                    throw Error("getting parent of Git commit '%s': %s", *git_commit_id(commit->get()), git_error_last()->message);
                todo.push(Commit(parent));
            }
        }

        return done.size();
    }

    uint64_t getLastModified(const Hash & rev) override
    {
        auto commit = peelObject<Commit>(repo.get(), lookupObject(repo.get(), hashToOID(rev)).get(), GIT_OBJECT_COMMIT);

        return git_commit_time(commit.get());
    }

    bool isShallow() override
    {
        return git_repository_is_shallow(repo.get());
    }
};

ref<GitRepo> GitRepo::openRepo(const CanonPath & path)
{
    return make_ref<GitRepoImpl>(path);
}

}

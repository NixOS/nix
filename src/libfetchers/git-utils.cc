#include "git-utils.hh"
#include "fs-input-accessor.hh"
#include "input-accessor.hh"
#include "filtering-input-accessor.hh"
#include "memory-input-accessor.hh"
#include "cache.hh"
#include "finally.hh"
#include "processes.hh"
#include "signals.hh"
#include "users.hh"
#include "fs-sink.hh"

#include <git2/attr.h>
#include <git2/blob.h>
#include <git2/commit.h>
#include <git2/config.h>
#include <git2/describe.h>
#include <git2/errors.h>
#include <git2/global.h>
#include <git2/object.h>
#include <git2/refs.h>
#include <git2/remote.h>
#include <git2/repository.h>
#include <git2/revparse.h>
#include <git2/status.h>
#include <git2/submodule.h>
#include <git2/tree.h>

#include <iostream>
#include <unordered_set>
#include <queue>
#include <regex>
#include <span>

namespace std {

template<> struct hash<git_oid>
{
    size_t operator()(const git_oid & oid) const
    {
        return * (size_t *) oid.id;
    }
};

}

std::ostream & operator << (std::ostream & str, const git_oid & oid)
{
    str << git_oid_tostr_s(&oid);
    return str;
}

bool operator == (const git_oid & oid1, const git_oid & oid2)
{
    return git_oid_equal(&oid1, &oid2);
}

namespace nix {

struct GitInputAccessor;

// Some wrapper types that ensure that the git_*_free functions get called.
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
typedef std::unique_ptr<git_reference, Deleter<git_reference_free>> Reference;
typedef std::unique_ptr<git_describe_result, Deleter<git_describe_result_free>> DescribeResult;
typedef std::unique_ptr<git_status_list, Deleter<git_status_list_free>> StatusList;
typedef std::unique_ptr<git_remote, Deleter<git_remote_free>> Remote;
typedef std::unique_ptr<git_config, Deleter<git_config_free>> GitConfig;
typedef std::unique_ptr<git_config_iterator, Deleter<git_config_iterator_free>> ConfigIterator;

// A helper to ensure that we don't leak objects returned by libgit2.
template<typename T>
struct Setter
{
    T & t;
    typename T::pointer p = nullptr;

    Setter(T & t) : t(t) { }

    ~Setter() { if (p) t = T(p); }

    operator typename T::pointer * () { return &p; }
};

Hash toHash(const git_oid & oid)
{
    #ifdef GIT_EXPERIMENTAL_SHA256
    assert(oid.type == GIT_OID_SHA1);
    #endif
    Hash hash(HashAlgorithm::SHA1);
    memcpy(hash.hash, oid.id, hash.hashSize);
    return hash;
}

static void initLibGit2()
{
    if (git_libgit2_init() < 0)
        throw Error("initialising libgit2: %s", git_error_last()->message);
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
    Object obj;
    if (git_object_lookup(Setter(obj), repo, &oid, GIT_OBJECT_ANY)) {
        auto err = git_error_last();
        throw Error("getting Git object '%s': %s", oid, err->message);
    }
    return obj;
}

template<typename T>
T peelObject(git_repository * repo, git_object * obj, git_object_t type)
{
    T obj2;
    if (git_object_peel((git_object * *) (typename T::pointer *) Setter(obj2), obj, type)) {
        auto err = git_error_last();
        throw Error("peeling Git object '%s': %s", git_object_id(obj), err->message);
    }
    return obj2;
}

struct GitRepoImpl : GitRepo, std::enable_shared_from_this<GitRepoImpl>
{
    /** Location of the repository on disk. */
    std::filesystem::path path;
    Repository repo;

    GitRepoImpl(std::filesystem::path _path, bool create, bool bare)
        : path(std::move(_path))
    {
        initLibGit2();

        if (pathExists(path.native())) {
            if (git_repository_open(Setter(repo), path.c_str()))
                throw Error("opening Git repository '%s': %s", path, git_error_last()->message);
        } else {
            if (git_repository_init(Setter(repo), path.c_str(), bare))
                throw Error("creating Git repository '%s': %s", path, git_error_last()->message);
        }
    }

    operator git_repository * ()
    {
        return repo.get();
    }

    uint64_t getRevCount(const Hash & rev) override
    {
        std::unordered_set<git_oid> done;
        std::queue<Commit> todo;

        todo.push(peelObject<Commit>(*this, lookupObject(*this, hashToOID(rev)).get(), GIT_OBJECT_COMMIT));

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
        auto commit = peelObject<Commit>(*this, lookupObject(*this, hashToOID(rev)).get(), GIT_OBJECT_COMMIT);

        return git_commit_time(commit.get());
    }

    bool isShallow() override
    {
        return git_repository_is_shallow(*this);
    }

    Hash resolveRef(std::string ref) override
    {
        Object object;
        if (git_revparse_single(Setter(object), *this, ref.c_str()))
            throw Error("resolving Git reference '%s': %s", ref, git_error_last()->message);
        auto oid = git_object_id(object.get());
        return toHash(*oid);
    }

    std::vector<Submodule> parseSubmodules(const std::filesystem::path & configFile)
    {
        GitConfig config;
        if (git_config_open_ondisk(Setter(config), configFile.c_str()))
            throw Error("parsing .gitmodules file: %s", git_error_last()->message);

        ConfigIterator it;
        if (git_config_iterator_glob_new(Setter(it), config.get(), "^submodule\\..*\\.(path|url|branch)$"))
            throw Error("iterating over .gitmodules: %s", git_error_last()->message);

        std::map<std::string, std::string> entries;

        while (true) {
            git_config_entry * entry = nullptr;
            if (auto err = git_config_next(&entry, it.get())) {
                if (err == GIT_ITEROVER) break;
                throw Error("iterating over .gitmodules: %s", git_error_last()->message);
            }
            entries.emplace(entry->name + 10, entry->value);
        }

        std::vector<Submodule> result;

        for (auto & [key, value] : entries) {
            if (!hasSuffix(key, ".path")) continue;
            std::string key2(key, 0, key.size() - 5);
            auto path = CanonPath(value);
            result.push_back(Submodule {
                .path = path,
                .url = entries[key2 + ".url"],
                .branch = entries[key2 + ".branch"],
            });
        }

        return result;
    }

    // Helper for statusCallback below.
    static int statusCallbackTrampoline(const char * path, unsigned int statusFlags, void * payload)
    {
        return (*((std::function<int(const char * path, unsigned int statusFlags)> *) payload))(path, statusFlags);
    }

    WorkdirInfo getWorkdirInfo() override
    {
        WorkdirInfo info;

        /* Get the head revision, if any. */
        git_oid headRev;
        if (auto err = git_reference_name_to_id(&headRev, *this, "HEAD")) {
            if (err != GIT_ENOTFOUND)
                throw Error("resolving HEAD: %s", git_error_last()->message);
        } else
            info.headRev = toHash(headRev);

        /* Get all tracked files and determine whether the working
           directory is dirty. */
        std::function<int(const char * path, unsigned int statusFlags)> statusCallback = [&](const char * path, unsigned int statusFlags)
        {
            if (!(statusFlags & GIT_STATUS_INDEX_DELETED) &&
                !(statusFlags & GIT_STATUS_WT_DELETED))
                info.files.insert(CanonPath(path));
            if (statusFlags != GIT_STATUS_CURRENT)
                info.isDirty = true;
            return 0;
        };

        git_status_options options = GIT_STATUS_OPTIONS_INIT;
        options.flags |= GIT_STATUS_OPT_INCLUDE_UNMODIFIED;
        options.flags |= GIT_STATUS_OPT_EXCLUDE_SUBMODULES;
        if (git_status_foreach_ext(*this, &options, &statusCallbackTrampoline, &statusCallback))
            throw Error("getting working directory status: %s", git_error_last()->message);

        /* Get submodule info. */
        auto modulesFile = path / ".gitmodules";
        if (pathExists(modulesFile))
            info.submodules = parseSubmodules(modulesFile);

        return info;
    }

    std::optional<std::string> getWorkdirRef() override
    {
        Reference ref;
        if (git_reference_lookup(Setter(ref), *this, "HEAD"))
            throw Error("looking up HEAD: %s", git_error_last()->message);

        if (auto target = git_reference_symbolic_target(ref.get()))
            return target;

        return std::nullopt;
    }

    std::vector<std::tuple<Submodule, Hash>> getSubmodules(const Hash & rev, bool exportIgnore) override;

    std::string resolveSubmoduleUrl(
        const std::string & url,
        const std::string & base) override
    {
        git_buf buf = GIT_BUF_INIT;
        if (git_submodule_resolve_url(&buf, *this, url.c_str()))
            throw Error("resolving Git submodule URL '%s'", url);
        Finally cleanup = [&]() { git_buf_dispose(&buf); };

        std::string res(buf.ptr);

        if (!hasPrefix(res, "/") && res.find("://") == res.npos)
            res = parseURL(base + "/" + res).canonicalise().to_string();

        return res;
    }

    bool hasObject(const Hash & oid_) override
    {
        auto oid = hashToOID(oid_);

        Object obj;
        if (auto errCode = git_object_lookup(Setter(obj), *this, &oid, GIT_OBJECT_ANY)) {
            if (errCode == GIT_ENOTFOUND) return false;
            auto err = git_error_last();
            throw Error("getting Git object '%s': %s", oid, err->message);
        }

        return true;
    }

    /**
     * A 'GitInputAccessor' with no regard for export-ignore or any other transformations.
     */
    ref<GitInputAccessor> getRawAccessor(const Hash & rev);

    ref<InputAccessor> getAccessor(const Hash & rev, bool exportIgnore) override;

    ref<InputAccessor> getAccessor(const WorkdirInfo & wd, bool exportIgnore, MakeNotAllowedError e) override;

    ref<GitFileSystemObjectSink> getFileSystemObjectSink() override;

    static int sidebandProgressCallback(const char * str, int len, void * payload)
    {
        auto act = (Activity *) payload;
        act->result(resFetchStatus, trim(std::string_view(str, len)));
        return getInterrupted() ? -1 : 0;
    }

    static int transferProgressCallback(const git_indexer_progress * stats, void * payload)
    {
        auto act = (Activity *) payload;
        act->result(resFetchStatus,
            fmt("%d/%d objects received, %d/%d deltas indexed, %.1f MiB",
                stats->received_objects,
                stats->total_objects,
                stats->indexed_deltas,
                stats->total_deltas,
                stats->received_bytes / (1024.0 * 1024.0)));
        return getInterrupted() ? -1 : 0;
    }

    void fetch(
        const std::string & url,
        const std::string & refspec,
        bool shallow) override
    {
        Activity act(*logger, lvlTalkative, actFetchTree, fmt("fetching Git repository '%s'", url));

        // TODO: implement git-credential helper support (preferably via libgit2, which as of 2024-01 does not support that)
        //       then use code that was removed in this commit (see blame)

        auto dir = this->path;
        Strings gitArgs;
        if (shallow) {
            gitArgs = { "-C", dir, "fetch", "--quiet", "--force", "--depth", "1", "--", url, refspec };
        }
        else {
            gitArgs = { "-C", dir, "fetch", "--quiet", "--force", "--", url, refspec };
        }

        runProgram(RunOptions {
            .program = "git",
            .searchPath = true,
            // FIXME: git stderr messes up our progress indicator, so
            // we're using --quiet for now. Should process its stderr.
            .args = gitArgs,
            .input = {},
            .isInteractive = true
        });
    }

    void verifyCommit(
        const Hash & rev,
        const std::vector<fetchers::PublicKey> & publicKeys) override
    {
        // Create ad-hoc allowedSignersFile and populate it with publicKeys
        auto allowedSignersFile = createTempFile().second;
        std::string allowedSigners;
        for (const fetchers::PublicKey & k : publicKeys) {
            if (k.type != "ssh-dsa"
                && k.type != "ssh-ecdsa"
                && k.type != "ssh-ecdsa-sk"
                && k.type != "ssh-ed25519"
                && k.type != "ssh-ed25519-sk"
                && k.type != "ssh-rsa")
                throw Error("Unknown key type '%s'.\n"
                    "Please use one of\n"
                    "- ssh-dsa\n"
                    "  ssh-ecdsa\n"
                    "  ssh-ecdsa-sk\n"
                    "  ssh-ed25519\n"
                    "  ssh-ed25519-sk\n"
                    "  ssh-rsa", k.type);
            allowedSigners += "* " + k.type + " " + k.key + "\n";
        }
        writeFile(allowedSignersFile, allowedSigners);

        // Run verification command
        auto [status, output] = runProgram(RunOptions {
                .program = "git",
                .args = {
                    "-c",
                    "gpg.ssh.allowedSignersFile=" + allowedSignersFile,
                    "-C", path,
                    "verify-commit",
                    rev.gitRev()
                },
                .mergeStderrToStdout = true,
        });

        /* Evaluate result through status code and checking if public
           key fingerprints appear on stderr. This is neccessary
           because the git command might also succeed due to the
           commit being signed by gpg keys that are present in the
           users key agent. */
        std::string re = R"(Good "git" signature for \* with .* key SHA256:[)";
        for (const fetchers::PublicKey & k : publicKeys){
            // Calculate sha256 fingerprint from public key and escape the regex symbol '+' to match the key literally
            auto fingerprint = trim(hashString(HashAlgorithm::SHA256, base64Decode(k.key)).to_string(nix::HashFormat::Base64, false), "=");
            auto escaped_fingerprint = std::regex_replace(fingerprint, std::regex("\\+"), "\\+" );
            re += "(" + escaped_fingerprint + ")";
        }
        re += "]";
        if (status == 0 && std::regex_search(output, std::regex(re)))
            printTalkative("Signature verification on commit %s succeeded.", rev.gitRev());
        else
            throw Error("Commit signature verification on commit %s failed: %s", rev.gitRev(), output);
    }

    Hash treeHashToNarHash(const Hash & treeHash) override
    {
        auto accessor = getAccessor(treeHash, false);

        fetchers::Attrs cacheKey({{"_what", "treeHashToNarHash"}, {"treeHash", treeHash.gitRev()}});

        if (auto res = fetchers::getCache()->lookup(cacheKey))
            return Hash::parseAny(fetchers::getStrAttr(*res, "narHash"), HashAlgorithm::SHA256);

        auto narHash = accessor->hashPath(CanonPath::root);

        fetchers::getCache()->upsert(cacheKey, fetchers::Attrs({{"narHash", narHash.to_string(HashFormat::SRI, true)}}));

        return narHash;
    }
};

ref<GitRepo> GitRepo::openRepo(const std::filesystem::path & path, bool create, bool bare)
{
    return make_ref<GitRepoImpl>(path, create, bare);
}

/**
 * Raw git tree input accessor.
 */
struct GitInputAccessor : InputAccessor
{
    ref<GitRepoImpl> repo;
    Tree root;

    GitInputAccessor(ref<GitRepoImpl> repo_, const Hash & rev)
        : repo(repo_)
        , root(peelObject<Tree>(*repo, lookupObject(*repo, hashToOID(rev)).get(), GIT_OBJECT_TREE))
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

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        if (path.isRoot())
            return Stat { .type = tDirectory };

        auto entry = lookup(path);
        if (!entry)
            return std::nullopt;

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

    Hash getSubmoduleRev(const CanonPath & path)
    {
        auto entry = need(path);

        if (git_tree_entry_type(entry) != GIT_OBJECT_COMMIT)
            throw Error("'%s' is not a submodule", showPath(path));

        return toHash(*git_tree_entry_id(entry));
    }

    std::unordered_map<CanonPath, TreeEntry> lookupCache;

    /* Recursively look up 'path' relative to the root. */
    git_tree_entry * lookup(const CanonPath & path)
    {
        auto i = lookupCache.find(path);
        if (i != lookupCache.end()) return i->second.get();

        auto parent = path.parent();
        if (!parent) return nullptr;

        auto name = path.baseName().value();

        auto parentTree = lookupTree(*parent);
        if (!parentTree) return nullptr;

        auto count = git_tree_entrycount(parentTree->get());

        git_tree_entry * res = nullptr;

        /* Add all the tree entries to the cache to speed up
           subsequent lookups. */
        for (size_t n = 0; n < count; ++n) {
            auto entry = git_tree_entry_byindex(parentTree->get(), n);

            TreeEntry copy;
            if (git_tree_entry_dup(Setter(copy), entry))
                throw Error("dupping tree entry: %s", git_error_last()->message);

            auto entryName = std::string_view(git_tree_entry_name(entry));

            if (entryName == name)
                res = copy.get();

            auto path2 = *parent;
            path2.push(entryName);
            lookupCache.emplace(path2, std::move(copy)).first->second.get();
        }

        return res;
    }

    std::optional<Tree> lookupTree(const CanonPath & path)
    {
        if (path.isRoot()) {
            Tree tree;
            if (git_tree_dup(Setter(tree), root.get()))
                throw Error("duplicating directory '%s': %s", showPath(path), git_error_last()->message);
            return tree;
        }

        auto entry = lookup(path);
        if (!entry || git_tree_entry_type(entry) != GIT_OBJECT_TREE)
            return std::nullopt;

        Tree tree;
        if (git_tree_entry_to_object((git_object * *) (git_tree * *) Setter(tree), *repo, entry))
            throw Error("looking up directory '%s': %s", showPath(path), git_error_last()->message);

        return tree;
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
            Tree tree;
            if (git_tree_dup(Setter(tree), root.get()))
                throw Error("duplicating directory '%s': %s", showPath(path), git_error_last()->message);
            return tree;
        }

        auto entry = need(path);

        if (git_tree_entry_type(entry) == GIT_OBJECT_COMMIT)
            return Submodule();

        if (git_tree_entry_type(entry) != GIT_OBJECT_TREE)
            throw Error("'%s' is not a directory", showPath(path));

        Tree tree;
        if (git_tree_entry_to_object((git_object * *) (git_tree * *) Setter(tree), *repo, entry))
            throw Error("looking up directory '%s': %s", showPath(path), git_error_last()->message);

        return tree;
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

        Blob blob;
        if (git_tree_entry_to_object((git_object * *) (git_blob * *) Setter(blob), *repo, entry))
            throw Error("looking up file '%s': %s", showPath(path), git_error_last()->message);

        return blob;
    }
};

struct GitExportIgnoreInputAccessor : CachingFilteringInputAccessor {
    ref<GitRepoImpl> repo;
    std::optional<Hash> rev;

    GitExportIgnoreInputAccessor(ref<GitRepoImpl> repo, ref<InputAccessor> next, std::optional<Hash> rev)
        : CachingFilteringInputAccessor(next, [&](const CanonPath & path) {
            return RestrictedPathError(fmt("'%s' does not exist because it was fetched with exportIgnore enabled", path));
        })
        , repo(repo)
        , rev(rev)
    { }

    bool gitAttrGet(const CanonPath & path, const char * attrName, const char * & valueOut)
    {
        const char * pathCStr = path.rel_c_str();

        if (rev) {
            git_attr_options opts = GIT_ATTR_OPTIONS_INIT;
            opts.attr_commit_id = hashToOID(*rev);
            // TODO: test that gitattributes from global and system are not used
            //       (ie more or less: home and etc - both of them!)
            opts.flags = GIT_ATTR_CHECK_INCLUDE_COMMIT | GIT_ATTR_CHECK_NO_SYSTEM;
            return git_attr_get_ext(
                &valueOut,
                *repo,
                &opts,
                pathCStr,
                attrName
                );
        }
        else {
            return git_attr_get(
                &valueOut,
                *repo,
                GIT_ATTR_CHECK_INDEX_ONLY | GIT_ATTR_CHECK_NO_SYSTEM,
                pathCStr,
                attrName);
        }
    }

    bool isExportIgnored(const CanonPath & path)
    {
        const char *exportIgnoreEntry = nullptr;

        // GIT_ATTR_CHECK_INDEX_ONLY:
        // > It will use index only for creating archives or for a bare repo
        // > (if an index has been specified for the bare repo).
        // -- https://github.com/libgit2/libgit2/blob/HEAD/include/git2/attr.h#L113C62-L115C48
        if (gitAttrGet(path, "export-ignore", exportIgnoreEntry)) {
            if (git_error_last()->klass == GIT_ENOTFOUND)
                return false;
            else
                throw Error("looking up '%s': %s", showPath(path), git_error_last()->message);
        }
        else {
            // Official git will silently reject export-ignore lines that have
            // values. We do the same.
            return GIT_ATTR_IS_TRUE(exportIgnoreEntry);
        }
    }

    bool isAllowedUncached(const CanonPath & path) override
    {
        return !isExportIgnored(path);
    }

};

struct GitFileSystemObjectSinkImpl : GitFileSystemObjectSink
{
    ref<GitRepoImpl> repo;

    struct PendingDir
    {
        std::string name;
        TreeBuilder builder;
    };

    std::vector<PendingDir> pendingDirs;

    size_t componentsToStrip = 1;

    void pushBuilder(std::string name)
    {
        git_treebuilder * b;
        if (git_treebuilder_new(&b, *repo, nullptr))
            throw Error("creating a tree builder: %s", git_error_last()->message);
        pendingDirs.push_back({ .name = std::move(name), .builder = TreeBuilder(b) });
    };

    GitFileSystemObjectSinkImpl(ref<GitRepoImpl> repo) : repo(repo)
    {
        pushBuilder("");
    }

    std::pair<git_oid, std::string> popBuilder()
    {
        assert(!pendingDirs.empty());
        auto pending = std::move(pendingDirs.back());
        git_oid oid;
        if (git_treebuilder_write(&oid, pending.builder.get()))
            throw Error("creating a tree object: %s", git_error_last()->message);
        pendingDirs.pop_back();
        return {oid, pending.name};
    };

    void addToTree(const std::string & name, const git_oid & oid, git_filemode_t mode)
    {
        assert(!pendingDirs.empty());
        auto & pending = pendingDirs.back();
        if (git_treebuilder_insert(nullptr, pending.builder.get(), name.c_str(), &oid, mode))
            throw Error("adding a file to a tree builder: %s", git_error_last()->message);
    };

    void updateBuilders(std::span<const std::string> names)
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

    bool prepareDirs(const std::vector<std::string> & pathComponents, bool isDir)
    {
        std::span<const std::string> pathComponents2{pathComponents};

        if (pathComponents2.size() <= componentsToStrip) return false;
        pathComponents2 = pathComponents2.subspan(componentsToStrip);

        updateBuilders(
            isDir
            ? pathComponents2
            : pathComponents2.first(pathComponents2.size() - 1));

        return true;
    }

    void createRegularFile(
        const Path & path,
        std::function<void(CreateRegularFileSink &)> func) override
    {
        auto pathComponents = tokenizeString<std::vector<std::string>>(path, "/");
        if (!prepareDirs(pathComponents, false)) return;

        git_writestream * stream = nullptr;
        if (git_blob_create_from_stream(&stream, *repo, nullptr))
            throw Error("creating a blob stream object: %s", git_error_last()->message);

        struct CRF : CreateRegularFileSink {
            const Path & path;
            GitFileSystemObjectSinkImpl & back;
            git_writestream * stream;
            bool executable = false;
            CRF(const Path & path, GitFileSystemObjectSinkImpl & back, git_writestream * stream)
                : path(path), back(back), stream(stream)
            {}
            void operator () (std::string_view data) override
            {
                if (stream->write(stream, data.data(), data.size()))
                    throw Error("writing a blob for tarball member '%s': %s", path, git_error_last()->message);
            }
            void isExecutable() override
            {
                executable = true;
            }
        } crf { path, *this, stream };
        func(crf);

        git_oid oid;
        if (git_blob_create_from_stream_commit(&oid, stream))
            throw Error("creating a blob object for tarball member '%s': %s", path, git_error_last()->message);

        addToTree(*pathComponents.rbegin(), oid,
            crf.executable
            ? GIT_FILEMODE_BLOB_EXECUTABLE
            : GIT_FILEMODE_BLOB);
    }

    void createDirectory(const Path & path) override
    {
        auto pathComponents = tokenizeString<std::vector<std::string>>(path, "/");
        (void) prepareDirs(pathComponents, true);
    }

    void createSymlink(const Path & path, const std::string & target) override
    {
        auto pathComponents = tokenizeString<std::vector<std::string>>(path, "/");
        if (!prepareDirs(pathComponents, false)) return;

        git_oid oid;
        if (git_blob_create_from_buffer(&oid, *repo, target.c_str(), target.size()))
            throw Error("creating a blob object for tarball symlink member '%s': %s", path, git_error_last()->message);

        addToTree(*pathComponents.rbegin(), oid, GIT_FILEMODE_LINK);
    }

    Hash sync() override {
        updateBuilders({});

        auto [oid, _name] = popBuilder();

        return toHash(oid);
    }
};

ref<GitInputAccessor> GitRepoImpl::getRawAccessor(const Hash & rev)
{
    auto self = ref<GitRepoImpl>(shared_from_this());
    return make_ref<GitInputAccessor>(self, rev);
}

ref<InputAccessor> GitRepoImpl::getAccessor(const Hash & rev, bool exportIgnore)
{
    auto self = ref<GitRepoImpl>(shared_from_this());
    ref<GitInputAccessor> rawGitAccessor = getRawAccessor(rev);
    if (exportIgnore) {
        return make_ref<GitExportIgnoreInputAccessor>(self, rawGitAccessor, rev);
    }
    else {
        return rawGitAccessor;
    }
}

ref<InputAccessor> GitRepoImpl::getAccessor(const WorkdirInfo & wd, bool exportIgnore, MakeNotAllowedError makeNotAllowedError)
{
    auto self = ref<GitRepoImpl>(shared_from_this());
    /* In case of an empty workdir, return an empty in-memory tree. We
       cannot use AllowListInputAccessor because it would return an
       error for the root (and we can't add the root to the allow-list
       since that would allow access to all its children). */
    ref<InputAccessor> fileAccessor =
        wd.files.empty()
        ? makeEmptyInputAccessor()
        : AllowListInputAccessor::create(
            makeFSInputAccessor(path),
            std::set<CanonPath> { wd.files },
            std::move(makeNotAllowedError)).cast<InputAccessor>();
    if (exportIgnore)
        return make_ref<GitExportIgnoreInputAccessor>(self, fileAccessor, std::nullopt);
    else
        return fileAccessor;
}

ref<GitFileSystemObjectSink> GitRepoImpl::getFileSystemObjectSink()
{
    return make_ref<GitFileSystemObjectSinkImpl>(ref<GitRepoImpl>(shared_from_this()));
}

std::vector<std::tuple<GitRepoImpl::Submodule, Hash>> GitRepoImpl::getSubmodules(const Hash & rev, bool exportIgnore)
{
    /* Read the .gitmodules files from this revision. */
    CanonPath modulesFile(".gitmodules");

    auto accessor = getAccessor(rev, exportIgnore);
    if (!accessor->pathExists(modulesFile)) return {};

    /* Parse it and get the revision of each submodule. */
    auto configS = accessor->readFile(modulesFile);

    auto [fdTemp, pathTemp] = createTempFile("nix-git-submodules");
    writeFull(fdTemp.get(), configS);

    std::vector<std::tuple<Submodule, Hash>> result;

    auto rawAccessor = getRawAccessor(rev);

    for (auto & submodule : parseSubmodules(pathTemp)) {
        auto rev = rawAccessor->getSubmoduleRev(submodule.path);
        result.push_back({std::move(submodule), rev});
    }

    return result;
}

ref<GitRepo> getTarballCache()
{
    static auto repoDir = std::filesystem::path(getCacheDir()) / "nix" / "tarball-cache";

    return GitRepo::openRepo(repoDir, true, true);
}

}

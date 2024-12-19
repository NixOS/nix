#include "git-utils.hh"
#include "cache.hh"
#include "finally.hh"
#include "processes.hh"
#include "signals.hh"
#include "users.hh"
#include "fs-sink.hh"
#include "sync.hh"
#include "thread-pool.hh"
#include "pool.hh"

#include <git2/attr.h>
#include <git2/blob.h>
#include <git2/commit.h>
#include <git2/config.h>
#include <git2/describe.h>
#include <git2/errors.h>
#include <git2/global.h>
#include <git2/indexer.h>
#include <git2/object.h>
#include <git2/odb.h>
#include <git2/refs.h>
#include <git2/remote.h>
#include <git2/repository.h>
#include <git2/revparse.h>
#include <git2/status.h>
#include <git2/submodule.h>
#include <git2/sys/odb_backend.h>
#include <git2/sys/mempack.h>
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

struct GitSourceAccessor;

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
typedef std::unique_ptr<git_odb, Deleter<git_odb_free>> ObjectDb;
typedef std::unique_ptr<git_packbuilder, Deleter<git_packbuilder_free>> PackBuilder;
typedef std::unique_ptr<git_indexer, Deleter<git_indexer_free>> Indexer;

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

Object lookupObject(git_repository * repo, const git_oid & oid, git_object_t type = GIT_OBJECT_ANY)
{
    Object obj;
    if (git_object_lookup(Setter(obj), repo, &oid, type)) {
        auto err = git_error_last();
        throw Error("getting Git object '%s': %s", oid, err->message);
    }
    return obj;
}

template<typename T>
T peelObject(git_object * obj, git_object_t type)
{
    T obj2;
    if (git_object_peel((git_object * *) (typename T::pointer *) Setter(obj2), obj, type)) {
        auto err = git_error_last();
        throw Error("peeling Git object '%s': %s", *git_object_id(obj), err->message);
    }
    return obj2;
}

template<typename T>
T dupObject(typename T::pointer obj)
{
    T obj2;
    if (git_object_dup((git_object * *) (typename T::pointer *) Setter(obj2), (git_object *) obj))
        throw Error("duplicating object '%s': %s", *git_object_id((git_object *) obj), git_error_last()->message);
    return obj2;
}

/**
 * Peel the specified object (i.e. follow tag and commit objects) to
 * either a blob or a tree.
 */
static Object peelToTreeOrBlob(git_object * obj)
{
    /* git_object_peel() doesn't handle blob objects, so handle those
       specially. */
    if (git_object_type(obj) == GIT_OBJECT_BLOB)
        return dupObject<Object>(obj);
    else
        return peelObject<Object>(obj, GIT_OBJECT_TREE);
}

struct PackBuilderContext {
    std::exception_ptr exception;

    void handleException(const char * activity, int errCode)
    {
        switch (errCode) {
            case GIT_OK:
                break;
            case GIT_EUSER:
                if (!exception)
                    panic("PackBuilderContext::handleException: user error, but exception was not set");

                std::rethrow_exception(exception);
            default:
                throw Error("%s: %i, %s", Uncolored(activity), errCode, git_error_last()->message);
        }
    }
};

extern "C" {

/**
 * A `git_packbuilder_progress` implementation that aborts the pack building if needed.
 */
static int packBuilderProgressCheckInterrupt(int stage, uint32_t current, uint32_t total, void *payload)
{
    PackBuilderContext & args = * (PackBuilderContext *) payload;
    try {
        checkInterrupt();
        return GIT_OK;
    } catch (const std::exception & e) {
        args.exception = std::current_exception();
        return GIT_EUSER;
    }
};
static git_packbuilder_progress PACKBUILDER_PROGRESS_CHECK_INTERRUPT = &packBuilderProgressCheckInterrupt;

} // extern "C"

static void initRepoAtomically(std::filesystem::path &path, bool bare)
{
    if (pathExists(path.string())) return;

    Path tmpDir = createTempDir(os_string_to_string(PathViewNG { std::filesystem::path(path).parent_path() }));
    AutoDelete delTmpDir(tmpDir, true);
    Repository tmpRepo;

    if (git_repository_init(Setter(tmpRepo), tmpDir.c_str(), bare))
        throw Error("creating Git repository %s: %s", path, git_error_last()->message);
    try {
        std::filesystem::rename(tmpDir, path);
    } catch (std::filesystem::filesystem_error & e) {
        // Someone may race us to create the repository.
        if (e.code() == std::errc::file_exists
            // `path` may be attempted to be deleted by s::f::rename, in which case the code is:
            || e.code() == std::errc::directory_not_empty) {
            return;
        }
        else
            throw SysError("moving temporary git repository from %s to %s", tmpDir, path);
    }
    // we successfully moved the repository, so the temporary directory no longer exists.
    delTmpDir.cancel();
}

struct GitRepoImpl : GitRepo, std::enable_shared_from_this<GitRepoImpl>
{
    /** Location of the repository on disk. */
    std::filesystem::path path;

    bool bare;

    /**
     * libgit2 repository. Note that new objects are not written to disk,
     * because we are using a mempack backend. For writing to disk, see
     * `flush()`, which is also called by `GitFileSystemObjectSink::sync()`.
     */
    Repository repo;

    /**
     * In-memory object store for efficient batched writing to packfiles.
     * Owned by `repo`.
     */
    git_odb_backend * mempack_backend;

    GitRepoImpl(std::filesystem::path _path, bool create, bool bare)
        : path(std::move(_path))
        , bare(bare)
    {
        initLibGit2();

        initRepoAtomically(path, bare);
        if (git_repository_open(Setter(repo), path.string().c_str()))
            throw Error("opening Git repository %s: %s", path, git_error_last()->message);

        #if 0
        ObjectDb odb;
        if (git_repository_odb(Setter(odb), repo.get()))
            throw Error("getting Git object database: %s", git_error_last()->message);

        // mempack_backend will be owned by the repository, so we are not expected to free it ourselves.
        if (git_mempack_new(&mempack_backend))
            throw Error("creating mempack backend: %s", git_error_last()->message);

        if (git_odb_add_backend(odb.get(), mempack_backend, 999))
            throw Error("adding mempack backend to Git object database: %s", git_error_last()->message);
        #endif
    }

    operator git_repository * ()
    {
        return repo.get();
    }

    void flush() override {
        checkInterrupt();

        git_buf buf = GIT_BUF_INIT;
        Finally _disposeBuf { [&] { git_buf_dispose(&buf); } };
        PackBuilder packBuilder;
        PackBuilderContext packBuilderContext;
        git_packbuilder_new(Setter(packBuilder), *this);
        git_packbuilder_set_callbacks(packBuilder.get(), PACKBUILDER_PROGRESS_CHECK_INTERRUPT, &packBuilderContext);
        git_packbuilder_set_threads(packBuilder.get(), 0 /* autodetect */);

        packBuilderContext.handleException(
            "preparing packfile",
            git_mempack_write_thin_pack(mempack_backend, packBuilder.get())
        );
        checkInterrupt();
        packBuilderContext.handleException(
            "writing packfile",
            git_packbuilder_write_buf(&buf, packBuilder.get())
        );
        checkInterrupt();

        std::string repo_path = std::string(git_repository_path(repo.get()));
        while (!repo_path.empty() && repo_path.back() == '/')
            repo_path.pop_back();
        std::string pack_dir_path = repo_path + "/objects/pack";

        // TODO (performance): could the indexing be done in a separate thread?
        //                     we'd need a more streaming variation of
        //                     git_packbuilder_write_buf, or incur the cost of
        //                     copying parts of the buffer to a separate thread.
        //                     (synchronously on the git_packbuilder_write_buf thread)
        Indexer indexer;
        git_indexer_progress stats;
        if (git_indexer_new(Setter(indexer), pack_dir_path.c_str(), 0, nullptr, nullptr))
            throw Error("creating git packfile indexer: %s", git_error_last()->message);

        // TODO: provide index callback for checkInterrupt() termination
        //       though this is about an order of magnitude faster than the packbuilder
        //       expect up to 1 sec latency due to uninterruptible git_indexer_append.
        constexpr size_t chunkSize = 128 * 1024;
        for (size_t offset = 0; offset < buf.size; offset += chunkSize) {
            if (git_indexer_append(indexer.get(), buf.ptr + offset, std::min(chunkSize, buf.size - offset), &stats))
                throw Error("appending to git packfile index: %s", git_error_last()->message);
            checkInterrupt();
        }

        if (git_indexer_commit(indexer.get(), &stats))
            throw Error("committing git packfile index: %s", git_error_last()->message);

        if (git_mempack_reset(mempack_backend))
            throw Error("resetting git mempack backend: %s", git_error_last()->message);

        checkInterrupt();
    }

    uint64_t getRevCount(const Hash & rev) override
    {
        std::unordered_set<git_oid> done;
        std::queue<Commit> todo;

        todo.push(peelObject<Commit>(lookupObject(*this, hashToOID(rev)).get(), GIT_OBJECT_COMMIT));

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
        auto commit = peelObject<Commit>(lookupObject(*this, hashToOID(rev)).get(), GIT_OBJECT_COMMIT);

        return git_commit_time(commit.get());
    }

    bool isShallow() override
    {
        return git_repository_is_shallow(*this);
    }

    void setRemote(const std::string & name, const std::string & url) override
    {
        if (git_remote_set_url(*this, name.c_str(), url.c_str()))
            throw Error("setting remote '%s' URL to '%s': %s", name, url, git_error_last()->message);
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
        if (git_config_open_ondisk(Setter(config), configFile.string().c_str()))
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
            {
                info.files.insert(CanonPath(path));
                if (statusFlags != GIT_STATUS_CURRENT)
                    info.dirtyFiles.insert(CanonPath(path));
            } else
                info.deletedFiles.insert(CanonPath(path));
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
        if (pathExists(modulesFile.string()))
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

    std::string resolveSubmoduleUrl(const std::string & url) override
    {
        git_buf buf = GIT_BUF_INIT;
        if (git_submodule_resolve_url(&buf, *this, url.c_str()))
            throw Error("resolving Git submodule URL '%s'", url);
        Finally cleanup = [&]() { git_buf_dispose(&buf); };

        std::string res(buf.ptr);
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
     * A 'GitSourceAccessor' with no regard for export-ignore or any other transformations.
     */
    ref<GitSourceAccessor> getRawAccessor(const Hash & rev);

    ref<SourceAccessor> getAccessor(const Hash & rev, bool exportIgnore) override;

    ref<SourceAccessor> getAccessor(const WorkdirInfo & wd, bool exportIgnore, MakeNotAllowedError e) override;

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
            gitArgs = { "-C", dir.string(), "fetch", "--quiet", "--force", "--depth", "1", "--", url, refspec };
        }
        else {
            gitArgs = { "-C", dir.string(), "fetch", "--quiet", "--force", "--", url, refspec };
        }

        runProgram(RunOptions {
            .program = "git",
            .lookupPath = true,
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
                    "-C", path.string(),
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
            std::string keyDecoded;
            try {
                keyDecoded = base64Decode(k.key);
            } catch (Error & e) {
                e.addTrace({}, "while decoding public key '%s' used for git signature", k.key);
            }
            auto fingerprint = trim(hashString(HashAlgorithm::SHA256, keyDecoded).to_string(nix::HashFormat::Base64, false), "=");
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

        fetchers::Cache::Key cacheKey{"treeHashToNarHash", {{"treeHash", treeHash.gitRev()}}};

        if (auto res = fetchers::getCache()->lookup(cacheKey))
            return Hash::parseAny(fetchers::getStrAttr(*res, "narHash"), HashAlgorithm::SHA256);

        auto narHash = accessor->hashPath(CanonPath::root);

        fetchers::getCache()->upsert(cacheKey, fetchers::Attrs({{"narHash", narHash.to_string(HashFormat::SRI, true)}}));

        return narHash;
    }

    Hash dereferenceSingletonDirectory(const Hash & oid_) override
    {
        auto oid = hashToOID(oid_);

        auto _tree = lookupObject(*this, oid, GIT_OBJECT_TREE);
        auto tree = (const git_tree *) &*_tree;

        if (git_tree_entrycount(tree) == 1) {
            auto entry = git_tree_entry_byindex(tree, 0);
            auto mode = git_tree_entry_filemode(entry);
            if (mode == GIT_FILEMODE_TREE)
                oid = *git_tree_entry_id(entry);
        }

        return toHash(oid);
    }
};

ref<GitRepo> GitRepo::openRepo(const std::filesystem::path & path, bool create, bool bare)
{
    return make_ref<GitRepoImpl>(path, create, bare);
}

/**
 * Raw git tree input accessor.
 */
struct GitSourceAccessor : SourceAccessor
{
    ref<GitRepoImpl> repo;
    Object root;

    GitSourceAccessor(ref<GitRepoImpl> repo_, const Hash & rev)
        : repo(repo_)
        , root(peelToTreeOrBlob(lookupObject(*repo, hashToOID(rev)).get()))
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
            return Stat { .type = git_object_type(root.get()) == GIT_OBJECT_TREE ? tDirectory : tRegular };

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

    /**
     * If `path` exists and is a submodule, return its
     * revision. Otherwise return nothing.
     */
    std::optional<Hash> getSubmoduleRev(const CanonPath & path)
    {
        auto entry = lookup(path);

        if (!entry || git_tree_entry_type(entry) != GIT_OBJECT_COMMIT)
            return std::nullopt;

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
            if (git_object_type(root.get()) == GIT_OBJECT_TREE)
                return dupObject<Tree>((git_tree *) &*root);
            else
                return std::nullopt;
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
            if (git_object_type(root.get()) == GIT_OBJECT_TREE)
                return dupObject<Tree>((git_tree *) &*root);
            else
                throw Error("Git root object '%s' is not a directory", *git_object_id(root.get()));
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
        if (!expectSymlink && git_object_type(root.get()) == GIT_OBJECT_BLOB)
            return dupObject<Blob>((git_blob *) &*root);

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

struct GitExportIgnoreSourceAccessor : CachingFilteringSourceAccessor {
    ref<GitRepoImpl> repo;
    std::optional<Hash> rev;

    GitExportIgnoreSourceAccessor(ref<GitRepoImpl> repo, ref<SourceAccessor> next, std::optional<Hash> rev)
        : CachingFilteringSourceAccessor(next, [&](const CanonPath & path) {
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

    Pool<GitRepoImpl> repoPool;

    unsigned int concurrency = std::min(std::thread::hardware_concurrency(), 4U);

    ThreadPool workers{concurrency};

    GitFileSystemObjectSinkImpl(ref<GitRepoImpl> repo)
        : repo(repo)
        , repoPool(
            std::numeric_limits<size_t>::max(),
            [repo]() -> ref<GitRepoImpl>
            {
                return make_ref<GitRepoImpl>(repo->path, false, repo->bare);
            })
    { }

    struct Directory;

    struct Directory
    {
        using Child = std::pair<git_filemode_t, std::variant<Directory, git_oid>>;
        std::map<std::string, Child> children;
        std::optional<git_oid> oid;

        Child & lookup(const CanonPath & path)
        {
            assert(!path.isRoot());
            auto parent = path.parent();
            auto cur = this;
            for (auto & name : *parent) {
                auto i = cur->children.find(std::string(name));
                if (i == cur->children.end())
                    throw Error("path '%s' does not exist", path);
                auto dir = std::get_if<Directory>(&i->second.second);
                if (!dir)
                    throw Error("path '%s' has a non-directory parent", path);
                cur = dir;
            }

            auto i = cur->children.find(std::string(*path.baseName()));
            if (i == cur->children.end())
                throw Error("path '%s' does not exist", path);
            return i->second;
        }
    };

    struct State
    {
        Directory root;
    };

    Sync<State> _state;

    void addNode(State & state, const CanonPath & path, Directory::Child && child)
    {
        assert(!path.isRoot());
        auto parent = path.parent();

        Directory * cur = &state.root;

        for (auto & i : *parent) {
            auto child = std::get_if<Directory>(&cur->children.emplace(
                    std::string(i),
                    Directory::Child{GIT_FILEMODE_TREE, {Directory()}}).first->second.second);
            assert(child);
            cur = child;
        }

        // FIXME: handle conflicts
        cur->children.emplace(std::string(*path.baseName()), std::move(child));
    }

    void createRegularFile(
        const CanonPath & path,
        std::function<void(CreateRegularFileSink &)> func) override
    {
        struct CRF : CreateRegularFileSink {
            std::string data;
            bool executable = false;

            void operator () (std::string_view data) override
            {
                this->data += data;
            }

            void isExecutable() override
            {
                executable = true;
            }
        } crf;

        func(crf);

        workers.enqueue([this, path, data{std::move(crf.data)}, executable(crf.executable)]()
        {
            auto repo(repoPool.get());

            // FIXME: leak
            git_writestream * stream = nullptr;
            if (git_blob_create_from_stream(&stream, *repo, nullptr))
                throw Error("creating a blob stream object: %s", git_error_last()->message);

            if (stream->write(stream, data.data(), data.size()))
                throw Error("writing a blob for tarball member '%s': %s", path, git_error_last()->message);

            git_oid oid;
            if (git_blob_create_from_stream_commit(&oid, stream))
                throw Error("creating a blob object for tarball member '%s': %s", path, git_error_last()->message);

            auto state(_state.lock());
            addNode(*state, path,
                Directory::Child{
                    executable
                    ? GIT_FILEMODE_BLOB_EXECUTABLE
                    : GIT_FILEMODE_BLOB,
                    oid});
        });
    }

    void createDirectory(const CanonPath & path) override
    {
        if (path.isRoot()) return;
        auto state(_state.lock());
        addNode(*state, path, {GIT_FILEMODE_TREE, Directory()});
    }

    void createSymlink(const CanonPath & path, const std::string & target) override
    {
        workers.enqueue([this, path, target]()
        {
            auto repo(repoPool.get());

            git_oid oid;
            if (git_blob_create_from_buffer(&oid, *repo, target.c_str(), target.size()))
                throw Error("creating a blob object for tarball symlink member '%s': %s", path, git_error_last()->message);

            auto state(_state.lock());
            addNode(*state, path, Directory::Child{GIT_FILEMODE_LINK, oid});
        });
    }

    std::map<CanonPath, CanonPath> hardLinks;

    void createHardlink(const CanonPath & path, const CanonPath & target) override
    {
        hardLinks.insert_or_assign(path, target);
    }

    Hash flush() override
    {
        workers.process();

        /* Create hard links. */
        {
            auto state(_state.lock());
            for (auto & [path, target] : hardLinks) {
                if (target.isRoot()) continue;
                auto [mode, child] = state->root.lookup(target);
                auto oid = std::get_if<git_oid>(&child);
                if (!oid)
                    throw Error("cannot create a hard link from '%s' to directory '%s'", path, target);
                addNode(*state, path, {mode, *oid});
            }
        }

        ThreadPool workers2{concurrency};

        auto & root = _state.lock()->root;

        processGraph<Directory *>(
            workers2,
            {&root},
            [&](Directory * const & node) -> std::set<Directory *>
            {
                std::set<Directory *> edges;
                for (auto & child : node->children)
                    if (auto dir = std::get_if<Directory>(&child.second.second))
                        edges.insert(dir);
                return edges;
            },
            [&](Directory * const & node)
            {
                auto repo(repoPool.get());

                git_treebuilder * b;
                if (git_treebuilder_new(&b, *repo, nullptr))
                    throw Error("creating a tree builder: %s", git_error_last()->message);
                TreeBuilder builder(b);

                for (auto & [name, child] : node->children) {
                    auto oid_p = std::get_if<git_oid>(&child.second);
                    auto oid = oid_p ? *oid_p : std::get<Directory>(child.second).oid.value();
                    if (git_treebuilder_insert(nullptr, builder.get(), name.c_str(), &oid, child.first))
                        throw Error("adding a file to a tree builder: %s", git_error_last()->message);
                }

                git_oid oid;
                if (git_treebuilder_write(&oid, builder.get()))
                    throw Error("creating a tree object: %s", git_error_last()->message);
                node->oid = oid;
            },
            true);

        #if 0
        repo->flush();
        #endif

        return toHash(root.oid.value());
    }
};

ref<GitSourceAccessor> GitRepoImpl::getRawAccessor(const Hash & rev)
{
    auto self = ref<GitRepoImpl>(shared_from_this());
    return make_ref<GitSourceAccessor>(self, rev);
}

ref<SourceAccessor> GitRepoImpl::getAccessor(const Hash & rev, bool exportIgnore)
{
    auto self = ref<GitRepoImpl>(shared_from_this());
    ref<GitSourceAccessor> rawGitAccessor = getRawAccessor(rev);
    if (exportIgnore) {
        return make_ref<GitExportIgnoreSourceAccessor>(self, rawGitAccessor, rev);
    }
    else {
        return rawGitAccessor;
    }
}

ref<SourceAccessor> GitRepoImpl::getAccessor(const WorkdirInfo & wd, bool exportIgnore, MakeNotAllowedError makeNotAllowedError)
{
    auto self = ref<GitRepoImpl>(shared_from_this());
    /* In case of an empty workdir, return an empty in-memory tree. We
       cannot use AllowListSourceAccessor because it would return an
       error for the root (and we can't add the root to the allow-list
       since that would allow access to all its children). */
    ref<SourceAccessor> fileAccessor =
        wd.files.empty()
        ? makeEmptySourceAccessor()
        : AllowListSourceAccessor::create(
            makeFSSourceAccessor(path),
            std::set<CanonPath> { wd.files },
            std::move(makeNotAllowedError)).cast<SourceAccessor>();
    if (exportIgnore)
        return make_ref<GitExportIgnoreSourceAccessor>(self, fileAccessor, std::nullopt);
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
        /* Filter out .gitmodules entries that don't exist or are not
           submodules. */
        if (auto rev = rawAccessor->getSubmoduleRev(submodule.path))
            result.push_back({std::move(submodule), *rev});
    }

    return result;
}

ref<GitRepo> getTarballCache()
{
    static auto repoDir = std::filesystem::path(getCacheDir()) / "tarball-cache";

    return GitRepo::openRepo(repoDir, true, true);
}

GitRepo::WorkdirInfo GitRepo::getCachedWorkdirInfo(const std::filesystem::path & path)
{
    static Sync<std::map<std::filesystem::path, WorkdirInfo>> _cache;
    {
        auto cache(_cache.lock());
        auto i = cache->find(path);
        if (i != cache->end()) return i->second;
    }
    auto workdirInfo = GitRepo::openRepo(path)->getWorkdirInfo();
    _cache.lock()->emplace(path, workdirInfo);
    return workdirInfo;
}

}

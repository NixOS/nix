#include "nix/fetchers/git-utils.hh"
#include "nix/fetchers/git-lfs-fetch.hh"
#include "nix/fetchers/cache.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/util/base-n.hh"
#include "nix/util/finally.hh"
#include "nix/util/os-string.hh"
#include "nix/util/processes.hh"
#include "nix/util/signals.hh"
#include "nix/util/users.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/sync.hh"
#include "nix/util/util.hh"
#include "nix/util/thread-pool.hh"
#include "nix/util/pool.hh"
#include "nix/util/deleter.hh"

#include <git2/attr.h>
#include <git2/blob.h>
#include <git2/branch.h>
#include <git2/commit.h>
#include <git2/config.h>
#include <git2/describe.h>
#include <git2/errors.h>
#include <git2/global.h>
#include <git2/indexer.h>
#include <git2/object.h>
#include <git2/odb.h>
#include <git2/odb_backend.h>
#include <git2/refs.h>
#include <git2/remote.h>
#include <git2/repository.h>
#include <git2/revparse.h>
#include <git2/status.h>
#include <git2/submodule.h>
#include <git2/sys/odb_backend.h>
#include <git2/sys/repository.h>
#include <git2/sys/mempack.h>
#include <git2/tag.h>
#include <git2/tree.h>

#include <boost/unordered/concurrent_flat_set.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>
#include <iostream>
#include <regex>
#include <ranges>
#include <future>

namespace std {

template<>
struct hash<git_oid>
{
    size_t operator()(const git_oid & oid) const
    {
        return *(size_t *) oid.id;
    }
};

} // namespace std

std::ostream & operator<<(std::ostream & str, const git_oid & oid)
{
    str << git_oid_tostr_s(&oid);
    return str;
}

bool operator==(const git_oid & oid1, const git_oid & oid2)
{
    return git_oid_equal(&oid1, &oid2);
}

namespace nix {

struct GitSourceAccessor;

namespace {

struct GitError final : public CloneableError<GitError, Error>
{
    template<typename... Ts>
    GitError(const git_error & error, Ts &&... args)
        : CloneableError("")
    {
        auto hf = HintFmt(std::forward<Ts>(args)...);
        err.msg = HintFmt("%1%: %2% (libgit2 error code = %3%)", Uncolored(hf.str()), error.message, error.klass);
    }

    template<typename... Ts>
    GitError(Ts &&... args)
        : GitError(
              []() -> const git_error & {
                  const git_error * p = git_error_last();
                  assert(p && "git_error_last() is unexpectedly null");
                  return *p;
              }(),
              std::forward<Ts>(args)...)
    {
    }
};

struct GitIndexerSink final : public BufferedSink
{
    git_indexer * indexer;
    git_indexer_progress stats{};

    GitIndexerSink(git_indexer * indexer)
        : BufferedSink(1 * 1024 * 1024)
        , indexer(indexer)
    {
        assert(indexer);
    }

    GitIndexerSink(GitIndexerSink &&) = delete;
    GitIndexerSink(const GitIndexerSink &) = delete;
    GitIndexerSink & operator=(GitIndexerSink &&) = delete;
    GitIndexerSink & operator=(const GitIndexerSink &) = delete;
    ~GitIndexerSink() = default;

    void writeUnbuffered(std::string_view data) override
    {
        checkInterrupt();
        if (git_indexer_append(indexer, data.data(), data.size(), &stats))
            throw GitError("appending to git packfile index");
    }
};

} // namespace

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

static Hash toHash(const git_oid & oid)
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
    static std::once_flag initialized;
    std::call_once(initialized, []() {
        if (git_libgit2_init() < 0)
            throw GitError("initialising libgit2");
    });
}

static git_oid hashToOID(const Hash & hash)
{
    git_oid oid;
    if (git_oid_fromstr(&oid, hash.gitRev().c_str()))
        throw GitError("cannot convert '%s' to a Git OID", hash.gitRev());
    return oid;
}

static Object lookupObject(git_repository * repo, const git_oid & oid, git_object_t type = GIT_OBJECT_ANY)
{
    Object obj;
    if (git_object_lookup(Setter(obj), repo, &oid, type)) {
        throw GitError("getting Git object '%s'", oid);
    }
    return obj;
}

template<typename T>
static T peelObject(git_object * obj, git_object_t type)
{
    T obj2;
    if (git_object_peel((git_object **) (typename T::pointer *) Setter(obj2), obj, type)) {
        throw Error("peeling Git object '%s'", *git_object_id(obj));
    }
    return obj2;
}

template<typename T>
static T dupObject(typename T::pointer obj)
{
    T obj2;
    if (git_object_dup((git_object **) (typename T::pointer *) Setter(obj2), (git_object *) obj))
        throw GitError("duplicating object '%s'", *git_object_id((git_object *) obj));
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

struct PackBuilderContext
{
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
static int packBuilderProgressCheckInterrupt(int stage, uint32_t current, uint32_t total, void * payload)
{
    PackBuilderContext & args = *(PackBuilderContext *) payload;
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

static void initRepoAtomically(std::filesystem::path & path, GitRepo::Options options)
{
    if (pathExists(path))
        return;

    if (!options.create)
        throw Error("Git repository %s does not exist.", PathFmt(path));

    std::filesystem::path tmpDir = createTempDir(path.parent_path());
    AutoDelete delTmpDir(tmpDir, true);
    Repository tmpRepo;

    if (git_repository_init(Setter(tmpRepo), tmpDir.string().c_str(), options.bare))
        throw GitError("creating Git repository %s", PathFmt(path));
    try {
        std::filesystem::rename(tmpDir, path);
    } catch (std::filesystem::filesystem_error & e) {
        // Someone may race us to create the repository.
        if (e.code() == std::errc::file_exists
            // `path` may be attempted to be deleted by s::f::rename, in which case the code is:
            || e.code() == std::errc::directory_not_empty) {
            return;
        } else
            throw SystemError(
                e.code(), "moving temporary git repository from %s to %s", PathFmt(tmpDir), PathFmt(path));
    }
    // we successfully moved the repository, so the temporary directory no longer exists.
    delTmpDir.cancel();
}

struct GitRepoImpl : GitRepo, std::enable_shared_from_this<GitRepoImpl>
{
    /** Location of the repository on disk. */
    std::filesystem::path path;

    Options options;

    /**
     * libgit2 repository. Note that new objects are not written to disk,
     * because we are using a mempack backend. For writing to disk, see
     * `flush()`, which is also called by `flush()` on the various merkle file
     * interfaces.
     */
    Repository repo;

    /**
     * In-memory object store for efficient batched writing to packfiles.
     * Owned by `repo`.
     */
    git_odb_backend * mempackBackend = nullptr;

    /**
     * On-disk packfile object store.
     * Owned by `repo`.
     */
    git_odb_backend * packBackend = nullptr;

    /**
     * Read the pack directory only the once, when this repo was created
     * (`git_odb_backend_pack` does that), rather than re-scanning it
     * whenever an object is not found --- which costs about six syscalls
     * per miss.
     *
     * Done by nulling out the backend's `refresh` callback; the vtable is
     * semi-public interface in libgit2.
     *
     * @see GitRepoPoolImpl for when this is safe.
     */
    void readPackDirOnlyOnce()
    {
        if (packBackend)
            packBackend->refresh = nullptr;
    }

    GitRepoImpl(std::filesystem::path _path, Options _options)
        : path(std::move(_path))
        , options(_options)
    {
        initLibGit2();

        initRepoAtomically(path, options);
        if (git_repository_open(Setter(repo), path.string().c_str()))
            throw GitError("opening Git repository %s", PathFmt(path));

        ObjectDb odb;
        if (options.packfilesOnly) {
            /* Create a fresh object database because by default the repo also
               loose object backends. We are not using any of those for the
               tarball cache, but libgit2 still does a bunch of unnecessary
               syscalls that always fail with ENOENT. NOTE: We are only creating
               a libgit2 object here and not modifying the repo. Think of this as
               enabling the specific backend.
               */

            if (git_odb_new(Setter(odb)))
                throw GitError("creating Git object database");

            if (git_odb_backend_pack(&packBackend, (path / "objects").string().c_str()))
                throw GitError("creating pack backend");

            if (git_odb_add_backend(odb.get(), packBackend, 1))
                throw GitError("adding pack backend to Git object database");
        } else {
            if (git_repository_odb(Setter(odb), repo.get()))
                throw GitError("getting Git object database");
        }

        // mempack_backend will be owned by the repository, so we are not expected to free it ourselves.
        if (git_mempack_new(&mempackBackend))
            throw GitError("creating mempack backend");

        if (git_odb_add_backend(odb.get(), mempackBackend, 999))
            throw GitError("adding mempack backend to Git object database");

        if (options.packfilesOnly) {
            if (git_repository_set_odb(repo.get(), odb.get()))
                throw GitError("setting Git object database");
        }
    }

    operator git_repository *()
    {
        return repo.get();
    }

    void flush() override
    {
        std::size_t objectCount;
        if (git_mempack_object_count(&objectCount, mempackBackend))
            throw GitError("querying the number of objects in a git memory packer backend");

        if (!objectCount)
            /* Nothing to do. */
            return;

        checkInterrupt();

        PackBuilder packBuilder;
        PackBuilderContext packBuilderContext;
        if (git_packbuilder_new(Setter(packBuilder), *this))
            throw GitError("creating git pack builder");

        if (git_packbuilder_set_callbacks(packBuilder.get(), PACKBUILDER_PROGRESS_CHECK_INTERRUPT, &packBuilderContext))
            throw GitError("setting git pack builder callbacks");

        git_packbuilder_set_threads(packBuilder.get(), 0 /* autodetect */);

        packBuilderContext.handleException(
            "preparing packfile", git_mempack_write_thin_pack(mempackBackend, packBuilder.get()));
        checkInterrupt();

        auto packFilesPath = std::filesystem::path(git_repository_path(repo.get())) / "objects/pack";

        Indexer indexer;
        if (git_indexer_new(Setter(indexer), packFilesPath.string().c_str(), 0, nullptr, nullptr))
            throw GitError("creating git packfile indexer");

        struct State
        {
            Indexer & indexer;
            PackBuilderContext & packBuilderContext;
            GitIndexerSink sink{indexer.get()};
        };

        State state{
            .indexer = indexer,
            .packBuilderContext = packBuilderContext,
        };

        packBuilderContext.handleException(
            "writing packfile",
            git_packbuilder_foreach(
                packBuilder.get(),
                [](void * buf, size_t size, void * payload) -> int {
                    auto & state = *static_cast<State *>(payload);
                    try {
                        state.sink(std::string_view(static_cast<const char *>(buf), size));
                    } catch (...) {
                        state.packBuilderContext.exception = std::current_exception();
                        return GIT_EUSER;
                    }
                    return GIT_OK;
                },
                &state));

        state.sink.flush();

        auto & stats = state.sink.stats;

        if (git_indexer_commit(indexer.get(), &stats))
            throw GitError("committing git packfile index");

        if (git_mempack_reset(mempackBackend))
            throw GitError("resetting git mempack backend");

        debug(
            "committed index and pack file to pack-%s.{idx,pack}, objects = %d, deltas = %d",
            git_indexer_name(indexer.get()),
            stats.total_objects,
            stats.total_deltas);

        checkInterrupt();
    }

    std::unique_ptr<merkle::DirectorySinkWithFinalize> makeDirectorySink() override;
    std::unique_ptr<merkle::RegularFileSinkWithFinalize> makeRegularFileSink() override;

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
            throw GitError("setting remote '%s' URL to '%s'", name, url);
    }

    Hash resolveRef(std::string ref) override
    {
        Object object;

        // Using the rev-parse notation which libgit2 supports, make sure we peel
        // the ref ultimately down to the underlying commit.
        // This is to handle the case where it may be an annotated tag which itself has
        // an object_id.
        std::string peeledRef = ref + "^{commit}";
        if (git_revparse_single(Setter(object), *this, peeledRef.c_str()))
            throw GitError("resolving Git reference '%s'", ref);
        auto oid = git_object_id(object.get());
        return toHash(*oid);
    }

    std::vector<Submodule> parseSubmodules(const std::filesystem::path & configFile)
    {
        GitConfig config;
        if (git_config_open_ondisk(Setter(config), configFile.string().c_str()))
            throw GitError("parsing .gitmodules file");

        ConfigIterator it;
        if (git_config_iterator_glob_new(Setter(it), config.get(), "^submodule\\..*\\.(path|url|branch)$"))
            throw GitError("iterating over .gitmodules");

        StringMap entries;

        while (true) {
            git_config_entry * entry = nullptr;
            if (auto err = git_config_next(&entry, it.get())) {
                if (err == GIT_ITEROVER)
                    break;
                throw GitError("iterating over .gitmodules");
            }
            entries.emplace(entry->name + 10, entry->value);
        }

        std::vector<Submodule> result;

        for (auto & [key, value] : entries) {
            if (!hasSuffix(key, ".path"))
                continue;
            std::string key2(key, 0, key.size() - 5);
            auto path = CanonPath(value);
            result.push_back(
                Submodule{
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
                throw GitError("resolving HEAD");
        } else
            info.headRev = toHash(headRev);

        /* Get all tracked files and determine whether the working
           directory is dirty. */
        std::function<int(const char * path, unsigned int statusFlags)> statusCallback = [&](const char * path,
                                                                                             unsigned int statusFlags) {
            if (!(statusFlags & GIT_STATUS_INDEX_DELETED) && !(statusFlags & GIT_STATUS_WT_DELETED)) {
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
            throw GitError("getting working directory status");

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
            if (errCode == GIT_ENOTFOUND)
                return false;
            throw GitError("getting Git object '%s'", oid);
        }

        return true;
    }

    /**
     * A 'GitSourceAccessor' with no regard for export-ignore.
     */
    ref<GitSourceAccessor> getRawAccessor(const Hash & rev, const GitAccessorOptions & options);

    ref<SourceAccessor>
    getAccessor(const Hash & rev, const GitAccessorOptions & options, std::string displayPrefix) override;

    ref<SourceAccessor>
    getAccessor(const WorkdirInfo & wd, const GitAccessorOptions & options, MakeNotAllowedError e) override;

    void fetch(const std::string & url, const std::string & refspec, bool shallow) override
    {
        Activity act(*logger, lvlTalkative, actFetchTree, fmt("fetching Git repository '%s'", url));

        // TODO: implement git-credential helper support (preferably via libgit2, which as of 2024-01 does not support
        // that)
        //       then use code that was removed in this commit (see blame)

        auto dir = this->path;

        // Remove shallow.lock left behind by a previously interrupted `git fetch`, as it would prevent `git fetch`
        // from running. Note that we already have a repository-wide `PathLock` (see git.cc), so this is safe.
        tryUnlink(dir / "shallow.lock");

        OsStrings gitArgs = {
            OS_STR("-C"),
            dir.native(),
            OS_STR("--git-dir"),
            OS_STR("."),
            OS_STR("fetch"),
            OS_STR("--progress"),
            OS_STR("--force"),
        };
        if (shallow) {
            gitArgs.push_back(OS_STR("--depth"));
            gitArgs.push_back(OS_STR("1"));
        }
        gitArgs.push_back(OS_STR("--"));
        gitArgs.push_back(string_to_os_string(url));
        gitArgs.push_back(string_to_os_string(refspec));

        auto status = runProgram({.program = "git", .args = gitArgs, .isInteractive = true}).first;

        if (status > 0)
            throw Error("Failed to fetch git repository '%s'", url);
    }

    void verifyCommit(const Hash & rev, const std::vector<fetchers::PublicKey> & publicKeys) override
    {
        // Map of SSH key types to their internal OpenSSH representations
        static const boost::unordered_flat_map<std::string_view, std::string_view> keyTypeMap = {
            {"ssh-dsa", "ssh-dsa"},
            {"ssh-ecdsa", "ssh-ecdsa"},
            {"ssh-ecdsa-sk", "sk-ecdsa-sha2-nistp256@openssh.com"},
            {"ssh-ed25519", "ssh-ed25519"},
            {"ssh-ed25519-sk", "sk-ssh-ed25519@openssh.com"},
            {"ssh-rsa", "ssh-rsa"}};

        // Create ad-hoc allowedSignersFile and populate it with publicKeys
        auto allowedSignersFile = createTempFile().second;
        std::string allowedSigners;

        for (const fetchers::PublicKey & k : publicKeys) {
            auto it = keyTypeMap.find(k.type);
            if (it == keyTypeMap.end()) {
                std::string supportedTypes;
                for (const auto & [type, _] : keyTypeMap) {
                    supportedTypes += fmt("  %s\n", type);
                }
                throw Error(
                    "Invalid SSH key type '%s' in publicKeys.\n"
                    "Please use one of:\n%s",
                    k.type,
                    supportedTypes);
            }

            allowedSigners += fmt("* %s %s\n", it->second, k.key);
        }
        writeFile(allowedSignersFile, allowedSigners);

        // Run verification command
        auto [status, output] = runProgram({
            .program = "git",
            .args{
                OS_STR("-c"),
                OS_STR("gpg.ssh.allowedSignersFile=") + allowedSignersFile.native(),
                OS_STR("-C"),
                path.native(),
                OS_STR("verify-commit"),
                string_to_os_string(rev.gitRev()),
            },
            .mergeStderrToStdout = true,
        });

        /* Evaluate result through status code and checking if public
           key fingerprints appear on stderr. This is necessary
           because the git command might also succeed due to the
           commit being signed by gpg keys that are present in the
           users key agent. */
        std::string re = R"(Good "git" signature for \* with .* key SHA256:[)";
        for (const fetchers::PublicKey & k : publicKeys) {
            // Calculate sha256 fingerprint from public key and escape the regex symbol '+' to match the key literally
            std::string keyDecoded;
            try {
                keyDecoded = base64::decode(k.key);
            } catch (Error & e) {
                e.addTrace({}, "while decoding public key '%s' used for git signature", k.key);
                throw;
            }
            auto fingerprint =
                trim(hashString(HashAlgorithm::SHA256, keyDecoded).to_string(nix::HashFormat::Base64, false), "=");
            auto escaped_fingerprint = std::regex_replace(fingerprint, std::regex("\\+"), "\\+");
            re += "(" + escaped_fingerprint + ")";
        }
        re += "]";
        if (status == 0 && std::regex_search(output, std::regex(re)))
            printTalkative("Signature verification on commit %s succeeded.", rev.gitRev());
        else
            throw Error("Commit signature verification on commit %s failed: %s", rev.gitRev(), output);
    }

    Hash treeHashToNarHash(const fetchers::Settings & settings, const Hash & treeHash) override
    {
        auto accessor = getAccessor(treeHash, {}, "");

        fetchers::Cache::Key cacheKey{"treeHashToNarHash", {{"treeHash", treeHash.gitRev()}}};

        if (auto res = settings.getCache()->lookup(cacheKey))
            return Hash::parseAny(fetchers::getStrAttr(*res, "narHash"), HashAlgorithm::SHA256);

        auto narHash = accessor->hashPath(CanonPath::root);

        settings.getCache()->upsert(cacheKey, fetchers::Attrs({{"narHash", narHash.to_string(HashFormat::SRI, true)}}));

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

ref<GitRepo> GitRepo::openRepo(const std::filesystem::path & path, GitRepo::Options options)
{
    return make_ref<GitRepoImpl>(path, options);
}

/**
 * Raw git tree input accessor.
 */

struct GitSourceAccessor final : SourceAccessor
{
private:
    void anchor() override {};
public:
    struct State
    {
        ref<GitRepoImpl> repo;
        Object root;
        std::optional<lfs::Fetch> lfsFetch = std::nullopt;
        GitAccessorOptions options;
    };

    Sync<State> state_;

    GitSourceAccessor(ref<GitRepoImpl> repo_, const Hash & rev, const GitAccessorOptions & options)
        : state_{State{
              .repo = repo_,
              .root = peelToTreeOrBlob(lookupObject(*repo_, hashToOID(rev)).get()),
              .lfsFetch = options.smudgeLfs ? std::make_optional(lfs::Fetch(*repo_, hashToOID(rev))) : std::nullopt,
              .options = options,
          }}
    {
    }

    void readBlob(const CanonPath & path, bool symlink, Sink & sink, std::function<void(uint64_t)> sizeCallback)
    {
        auto state(state_.lock());

        const auto blob = getBlob(*state, path, symlink);

        if (state->lfsFetch) {
            if (state->lfsFetch->shouldFetch(path)) {
                try {
                    // FIXME: do we need to hold the state lock while
                    // doing this?
                    auto contents =
                        std::string((const char *) git_blob_rawcontent(blob.get()), git_blob_rawsize(blob.get()));
                    state->lfsFetch->fetch(contents, path, sink, sizeCallback);
                } catch (Error & e) {
                    e.addTrace({}, "while smudging git-lfs file '%s'", path);
                    throw;
                }
                return;
            }
        }

        auto view = std::string_view((const char *) git_blob_rawcontent(blob.get()), git_blob_rawsize(blob.get()));
        sizeCallback(view.size());
        StringSource source{view};
        source.drainInto(sink);
    }

    void readFile(const CanonPath & path, Sink & sink, fun<void(uint64_t)> sizeCallback) override
    {
        return readBlob(path, false, sink, sizeCallback);
    }

    bool pathExists(const CanonPath & path) override
    {
        auto state(state_.lock());
        return path.isRoot() ? true : (bool) lookup(*state, path);
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        auto state(state_.lock());

        if (path.isRoot())
            return Stat{.type = git_object_type(state->root.get()) == GIT_OBJECT_TREE ? tDirectory : tRegular};

        auto entry = lookup(*state, path);
        if (!entry)
            return std::nullopt;

        auto mode = git_tree_entry_filemode(entry);

        if (mode == GIT_FILEMODE_TREE)
            return Stat{.type = tDirectory};

        else if (mode == GIT_FILEMODE_BLOB)
            return Stat{.type = tRegular};

        else if (mode == GIT_FILEMODE_BLOB_EXECUTABLE)
            return Stat{.type = tRegular, .isExecutable = true};

        else if (mode == GIT_FILEMODE_LINK)
            return Stat{.type = tSymlink};

        else if (mode == GIT_FILEMODE_COMMIT)
            // Treat submodules as an empty directory.
            return Stat{.type = tDirectory};

        else
            throw Error("file '%s' has an unsupported Git file type");
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        auto state(state_.lock());

        return std::visit(
            overloaded{
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
                [&](Submodule) { return DirEntries(); }},
            getTree(*state, path));
    }

    std::string readLink(const CanonPath & path) override
    {
        StringSink s;
        readBlob(path, true, s, [&](uint64_t size) { s.s.reserve(size); });
        return std::move(s.s);
    }

    /**
     * If `path` exists and is a submodule, return its
     * revision. Otherwise return nothing.
     */
    std::optional<Hash> getSubmoduleRev(const CanonPath & path)
    {
        auto state(state_.lock());

        auto entry = lookup(*state, path);

        if (!entry || git_tree_entry_type(entry) != GIT_OBJECT_COMMIT)
            return std::nullopt;

        return toHash(*git_tree_entry_id(entry));
    }

    boost::unordered_flat_map<CanonPath, TreeEntry> lookupCache;

    /* Recursively look up 'path' relative to the root. */
    git_tree_entry * lookup(State & state, const CanonPath & path)
    {
        auto i = lookupCache.find(path);
        if (i != lookupCache.end())
            return i->second.get();

        auto parent = path.parent();
        if (!parent)
            return nullptr;

        auto name = path.baseName().value();

        auto parentTree = lookupTree(state, *parent);
        if (!parentTree)
            return nullptr;

        auto count = git_tree_entrycount(parentTree->get());

        git_tree_entry * res = nullptr;

        /* Add all the tree entries to the cache to speed up
           subsequent lookups. */
        for (size_t n = 0; n < count; ++n) {
            auto entry = git_tree_entry_byindex(parentTree->get(), n);

            TreeEntry copy;
            if (git_tree_entry_dup(Setter(copy), entry))
                throw GitError("dupping tree entry");

            auto entryName = std::string_view(git_tree_entry_name(entry));

            if (entryName == name)
                res = copy.get();

            auto path2 = *parent;
            path2.push(entryName);
            lookupCache.emplace(path2, std::move(copy));
        }

        return res;
    }

    std::optional<Tree> lookupTree(State & state, const CanonPath & path)
    {
        if (path.isRoot()) {
            if (git_object_type(state.root.get()) == GIT_OBJECT_TREE)
                return dupObject<Tree>((git_tree *) &*state.root);
            else
                return std::nullopt;
        }

        auto entry = lookup(state, path);
        if (!entry || git_tree_entry_type(entry) != GIT_OBJECT_TREE)
            return std::nullopt;

        Tree tree;
        if (git_tree_entry_to_object((git_object **) (git_tree **) Setter(tree), *state.repo, entry))
            throw GitError("looking up directory '%s'", showPath(path));

        return tree;
    }

    git_tree_entry * need(State & state, const CanonPath & path)
    {
        auto entry = lookup(state, path);
        if (!entry)
            throw Error("'%s' does not exist", showPath(path));
        return entry;
    }

    struct Submodule
    {};

    std::variant<Tree, Submodule> getTree(State & state, const CanonPath & path)
    {
        if (path.isRoot()) {
            if (git_object_type(state.root.get()) == GIT_OBJECT_TREE)
                return dupObject<Tree>((git_tree *) &*state.root);
            else
                throw Error("Git root object '%s' is not a directory", *git_object_id(state.root.get()));
        }

        auto entry = need(state, path);

        if (git_tree_entry_type(entry) == GIT_OBJECT_COMMIT)
            return Submodule();

        if (git_tree_entry_type(entry) != GIT_OBJECT_TREE)
            throw Error("'%s' is not a directory", showPath(path));

        Tree tree;
        if (git_tree_entry_to_object((git_object **) (git_tree **) Setter(tree), *state.repo, entry))
            throw GitError("looking up directory '%s'", showPath(path));

        return tree;
    }

    Blob getBlob(State & state, const CanonPath & path, bool expectSymlink)
    {
        if (!expectSymlink && git_object_type(state.root.get()) == GIT_OBJECT_BLOB)
            return dupObject<Blob>((git_blob *) &*state.root);

        auto notExpected = [&]() {
            throw Error(expectSymlink ? "'%s' is not a symlink" : "'%s' is not a regular file", showPath(path));
        };

        if (path.isRoot())
            notExpected();

        auto entry = need(state, path);

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
        if (git_tree_entry_to_object((git_object **) (git_blob **) Setter(blob), *state.repo, entry))
            throw GitError("looking up file '%s'", showPath(path));

        return blob;
    }
};

struct GitExportIgnoreSourceAccessor final : CachingFilteringSourceAccessor
{
private:
    void anchor() override {};
public:
    ref<GitRepoImpl> repo;
    std::optional<Hash> rev;

    GitExportIgnoreSourceAccessor(ref<GitRepoImpl> repo, ref<SourceAccessor> next, std::optional<Hash> rev)
        : CachingFilteringSourceAccessor(
              next,
              [&](const CanonPath & path) {
                  return RestrictedPathError(
                      fmt("'%s' does not exist because it was fetched with exportIgnore enabled", path));
              })
        , repo(repo)
        , rev(rev)
    {
    }

    bool gitAttrGet(const CanonPath & path, const char * attrName, const char *& valueOut)
    {
        const char * pathCStr = path.rel_c_str();

        if (rev) {
            git_attr_options opts = GIT_ATTR_OPTIONS_INIT;
            opts.attr_commit_id = hashToOID(*rev);
            // TODO: test that gitattributes from global and system are not used
            //       (ie more or less: home and etc - both of them!)
            opts.flags = GIT_ATTR_CHECK_INCLUDE_COMMIT | GIT_ATTR_CHECK_NO_SYSTEM;
            return git_attr_get_ext(&valueOut, *repo, &opts, pathCStr, attrName);
        } else {
            return git_attr_get(
                &valueOut, *repo, GIT_ATTR_CHECK_INDEX_ONLY | GIT_ATTR_CHECK_NO_SYSTEM, pathCStr, attrName);
        }
    }

    bool isExportIgnored(const CanonPath & path)
    {
        const char * exportIgnoreEntry = nullptr;

        // GIT_ATTR_CHECK_INDEX_ONLY:
        // > It will use index only for creating archives or for a bare repo
        // > (if an index has been specified for the bare repo).
        // -- https://github.com/libgit2/libgit2/blob/HEAD/include/git2/attr.h#L113C62-L115C48
        if (gitAttrGet(path, "export-ignore", exportIgnoreEntry)) {
            if (git_error_last()->klass == GIT_ENOTFOUND)
                return false;
            else
                throw GitError("looking up '%s'", showPath(path));
        } else {
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

namespace {

/**
 * A handle to a `GitRepoImpl` that is either borrowed from a pool
 * (independent phase) or references a single shared repo (dependent
 * phase). The pool handle, if any, is returned to the pool on
 * destruction.
 */
struct GitRepoHandle
{
    /**
     * Borrowed from the pool, or simply a shared reference to a repo with no
     * interaction with any pool. Kept only to hold on to the repo; use `repo`
     * to get at it.
     */
    std::variant<Pool<GitRepoImpl>::Handle, ref<GitRepoImpl>> owner;

    GitRepoImpl * repo;

    GitRepoHandle(Pool<GitRepoImpl>::Handle handle)
        : owner(std::move(handle))
        , repo(&*std::get<Pool<GitRepoImpl>::Handle>(owner))
    {
    }

    GitRepoHandle(ref<GitRepoImpl> repo)
        : owner(repo)
        , repo(&*repo)
    {
    }

    GitRepoImpl & operator*() const
    {
        return *repo;
    }

    GitRepoImpl * operator->() const
    {
        return repo;
    }
};

struct GitRegularFileSinkImpl : merkle::RegularFileSinkWithFinalize
{
    using WriteStream = std::unique_ptr<git_writestream, decltype([](git_writestream * stream) {
                                            if (stream)
                                                stream->free(stream);
                                        })>;

    std::function<GitRepoHandle()> getRepo;

    /**
     * Writer acquired lazily. Only set when we have a stream (large file).
     */
    std::optional<GitRepoHandle> writer;

    /**
     * In-memory buffer for small files.
     */
    std::string contents;

    /**
     * Stream for large files. Only created when contents exceeds maxBufferSize.
     */
    WriteStream stream;

    /**
     * Maximum file size that gets buffered in memory before flushing to a
     * git_writestream backed by a temporary objects/streamed_git2_* file.
     * We should avoid that for common cases, since creating (and deleting)
     * a temporary file for each blob is expensive.
     */
    static constexpr std::size_t maxBufferSize = 1024 * 1024; /* 1 MiB */

    GitRegularFileSinkImpl(std::function<GitRepoHandle()> getRepo)
        : getRepo(std::move(getRepo))
    {
    }

    void writeToStream(git_writestream & stream, std::string_view data)
    {
        if (stream.write(&stream, data.data(), data.size()))
            throw GitError("writing to blob stream");
    }

    void operator()(std::string_view data) override
    {
        /* Already in slow path. Just write to the stream. */
        if (stream) {
            writeToStream(*stream, data);
            return;
        }

        contents += data;
        if (contents.size() > maxBufferSize) {
            /* Lazily acquire a writer and create the stream. */
            writer.emplace(getRepo());
            git_writestream * streamRaw = nullptr;
            if (git_blob_create_from_stream(&streamRaw, **writer, nullptr))
                throw GitError("creating blob stream");
            stream = WriteStream{streamRaw};
            writeToStream(*stream, contents);
            contents.clear();
        }
    }

    Hash finalize() && override
    {
        git_oid oid;

        if (stream) {
            /* Large file: finalize the stream we created earlier. */
            assert(writer);
            /* Call .release(), since git_blob_create_from_stream_commit
               acquires ownership and frees the stream. */
            if (git_blob_create_from_stream_commit(&oid, stream.release()))
                throw GitError("finalizing blob stream");
            writer.reset();
        } else {
            /* Small file: get a repo and create blob from buffer. */
            auto handle = getRepo();
            if (git_blob_create_from_buffer(&oid, *handle, contents.data(), contents.size()))
                throw GitError("creating blob from buffer");
        }

        return toHash(oid);
    }
};

struct GitDirectorySinkImpl : merkle::DirectorySinkWithFinalize
{
    TreeBuilder builder;

    GitDirectorySinkImpl(GitRepoImpl & repo)
    {
        git_treebuilder * b;
        if (git_treebuilder_new(&b, repo, nullptr))
            throw GitError("creating a tree builder");
        builder = TreeBuilder(b);
    }

    void insertChild(std::string_view name, merkle::TreeEntry entry) override
    {
        auto oid = hashToOID(entry.hash);
        if (git_treebuilder_insert(
                nullptr, builder.get(), std::string(name).c_str(), &oid, static_cast<git_filemode_t>(entry.mode)))
            throw GitError("adding '%s' to a tree builder", name);
    }

    Hash finalize() && override
    {
        git_oid oid;
        if (git_treebuilder_write(&oid, builder.get()))
            throw GitError("creating a tree object");
        return toHash(oid);
    }
};

} // namespace

std::unique_ptr<merkle::DirectorySinkWithFinalize> GitRepoImpl::makeDirectorySink()
{
    return std::make_unique<GitDirectorySinkImpl>(*this);
}

std::unique_ptr<merkle::RegularFileSinkWithFinalize> GitRepoImpl::makeRegularFileSink()
{
    return std::make_unique<GitRegularFileSinkImpl>(
        [self = ref<GitRepoImpl>(shared_from_this())]() -> GitRepoHandle { return {self}; });
}

ref<GitSourceAccessor> GitRepoImpl::getRawAccessor(const Hash & rev, const GitAccessorOptions & options)
{
    auto self = ref<GitRepoImpl>(shared_from_this());
    return make_ref<GitSourceAccessor>(self, rev, options);
}

ref<SourceAccessor>
GitRepoImpl::getAccessor(const Hash & rev, const GitAccessorOptions & options, std::string displayPrefix)
{
    auto self = ref<GitRepoImpl>(shared_from_this());
    ref<GitSourceAccessor> rawGitAccessor = getRawAccessor(rev, options);
    rawGitAccessor->setPathDisplay(std::move(displayPrefix));
    if (options.exportIgnore)
        return make_ref<GitExportIgnoreSourceAccessor>(self, rawGitAccessor, rev);
    else
        return rawGitAccessor;
}

ref<SourceAccessor> GitRepoImpl::getAccessor(
    const WorkdirInfo & wd, const GitAccessorOptions & options, MakeNotAllowedError makeNotAllowedError)
{
    auto self = ref<GitRepoImpl>(shared_from_this());
    ref<SourceAccessor> fileAccessor =
        AllowListSourceAccessor::create(
            // Follow the final symlink to the repo. Older nix versions used to do this (maybe somewhat accidentally).
            makeFSSourceAccessor(path, /*trackLastModified=*/false, FinalSymlink::Follow),
            /*allowedPrefixes=*/wd.files,
            // Always allow access to the root, but not its children.
            /*allowedPaths=*/{CanonPath::root},
            std::move(makeNotAllowedError))
            .cast<SourceAccessor>();
    if (options.exportIgnore)
        fileAccessor = make_ref<GitExportIgnoreSourceAccessor>(self, fileAccessor, std::nullopt);
    return fileAccessor;
}

/**
 * A pool of `GitRepoImpl` instances for parallel merkle object writing.
 *
 * libgit2 repositories are not thread-safe, so concurrent writes
 * require separate repository handles. This pool manages those handles.
 *
 * When `disablePackRefresh` is true, we monkey-patch the pack backend to
 * only read the pack directory once. Otherwise it will do a readdir for
 * each added oid when it's not found and that translates to ~6
 * syscalls. Since we are never writing pack files until flushing we can
 * force the odb backend to read the directory just once. It's very
 * convenient that the vtable is semi-public interface and is up for
 * grabs.
 *
 * This is purely an optimization for when we are writing independent
 * data concurrently. For example, when we are only writing blobs to the
 * git repo. Blobs are leaf nodes which cannot reference other objects,
 * and so there is far less need for synchronisation.
 *
 * The first `makeDirectorySink` call flushes all idle pool members to
 * disk and opens the directory repo handle, which reads the pack
 * directory as it is created --- so that it sees those packfiles --- and
 * gets the same treatment, since nothing new will appear while we are
 * writing the directories.
 *
 * This comes up in our use-case of the tarball cache. We write all the
 * files from the tarball to git, and only then write the directories.
 * When writing files, libgit2 calls refresh() if the backend provides
 * it when an oid isn't found. We are only writing objects to a mempack
 * (it has higher priority) and there isn't a realistic use-case where a
 * previously missing object would appear from thin air on the disk
 * (unless another process happens to be unpacking a similar tarball to
 * the cache at the same time, but that's a very unrealistic scenario).
 * After we are done writing the files, we write the directories to a
 * single dedicated repo handle (`singleRepo`).
 */
struct GitRepoPoolImpl : GitRepoPool
{
    std::filesystem::path path;
    GitRepo::Options options;

    /**
     * When true, new pool members have `refresh` disabled on their
     * pack backend. Cleared once we start writing directories --- not
     * because refreshing is wanted then, but because the handle we write
     * them with disables it for itself; see `beginDependentPhase`.
     */
    bool disablePackRefresh = true;

    Pool<GitRepoImpl> pool;

    /**
     * Single repo handle used once we start writing directories. All
     * sinks share this handle, avoiding per-sink pool overhead and extra
     * packfile flushes. Only initialized by `beginDependentPhase`.
     */
    std::shared_ptr<GitRepoImpl> singleRepo;

    GitRepoHandle getRepo()
    {
        if (singleRepo)
            return {ref<GitRepoImpl>(singleRepo)};
        return {pool.get()};
    }

    GitRepoPoolImpl(const std::filesystem::path & path, GitRepo::Options options)
        : path(path)
        , options(options)
        , pool(std::numeric_limits<size_t>::max(), [this]() -> ref<GitRepoImpl> {
            auto repo = make_ref<GitRepoImpl>(this->path, this->options);

            if (disablePackRefresh)
                repo->readPackDirOnlyOnce();

            return repo;
        })
    {
    }

    /**
     * Switch from writing independent objects to writing ones that refer
     * to them: flush every idle pool member to disk, and open a handle
     * that can see the packfiles just written.
     *
     * Idempotent, and called lazily, so that a caller that only ever
     * writes blobs never pays for it.
     *
     * TODO: it would be nicer to merge the pool's mempacks, and let
     * libgit2 decide when to spill to disk, rather than forcing a
     * packfile per pool member just so the trees can refer to the blobs.
     * libgit2 1.9 offers no way to do that: `sys/mempack.h` has no merge
     * operation, and a `git_odb_backend` cannot be added to a second odb
     * (`add_backend_internal` asserts it is unowned, and there is no
     * refcount, so sharing one would double-free).
     *
     * The ordering requirement is avoidable from the other end, though.
     * `git_treebuilder_write` validates nothing; it is
     * `git_treebuilder_insert` that checks that each child exists, via
     * `git_object__is_valid`, which is a no-op when
     * `GIT_OPT_ENABLE_STRICT_OBJECT_CREATION` is off. Turning that off
     * would let the trees be written to any handle, so every handle
     * could just flush once at the end, and this phase would not need to
     * exist. It is a process-global setting, however, so we would be
     * giving up that check everywhere.
     */
    void beginDependentPhase()
    {
        if (singleRepo)
            return;

        auto repos = pool.clear();
        ThreadPool workers{repos.size()};
        for (auto & repo : repos)
            workers.enqueue([repo]() { repo->flush(); });
        workers.process();

        /* Opened after the flush, so that it sees those packfiles. */
        singleRepo = make_ref<GitRepoImpl>(path, options);

        /* It read the pack directory as it was created, so it can see
           the packfiles we just wrote; nothing more will appear while we
           write the directories. */
        singleRepo->readPackDirOnlyOnce();

        disablePackRefresh = false;
    }

    void flush() override
    {
        /* Also handles the case where no directory was ever created. */
        beginDependentPhase();

        // Flush the directory repo's mempack to disk.
        singleRepo->flush();
        singleRepo.reset();
        disablePackRefresh = true;
    }

    std::unique_ptr<merkle::DirectorySinkWithFinalize> makeDirectorySink() override
    {
        beginDependentPhase();
        return singleRepo->makeDirectorySink();
    }

    std::unique_ptr<merkle::RegularFileSinkWithFinalize> makeRegularFileSink() override
    {
        if (singleRepo)
            return singleRepo->makeRegularFileSink();
        return std::make_unique<GitRegularFileSinkImpl>([this]() { return getRepo(); });
    }

    merkle::TreeEntry makeSymlink(const std::string & target) override
    {
        auto handle = getRepo();
        git_oid oid;
        if (git_blob_create_from_buffer(&oid, *handle, target.data(), target.size()))
            throw GitError("creating a blob object for symlink");
        return merkle::TreeEntry{merkle::Mode::Symlink, toHash(oid)};
    }

    uint64_t getRevCount(const Hash & rev) override
    {
        boost::concurrent_flat_set<git_oid, std::hash<git_oid>> done;

        auto startRepo = pool.get();
        auto startCommit = peelObject<Commit>(lookupObject(*startRepo, hashToOID(rev)).get(), GIT_OBJECT_COMMIT);
        auto startOid = *git_commit_id(startCommit.get());
        done.insert(startOid);

        ThreadPool threadPool;

        auto process = [&done, &threadPool, &pool = pool](this const auto & process, const git_oid & oid) -> void {
            auto repo(pool.get());

            auto _commit = lookupObject(*repo, oid, GIT_OBJECT_COMMIT);
            auto commit = (const git_commit *) &*_commit;

            for (auto n : std::views::iota(0U, git_commit_parentcount(commit))) {
                auto parentOid = git_commit_parent_id(commit, n);
                if (!parentOid) {
                    throw Error(
                        "Failed to retrieve the parent of Git commit '%s': %s. "
                        "This may be due to an incomplete repository history. "
                        "To resolve this, either enable the shallow parameter in your flake URL (?shallow=1) "
                        "or add set the shallow parameter to true in builtins.fetchGit, "
                        "or fetch the complete history for this branch.",
                        *git_commit_id(commit),
                        git_error_last()->message);
                }
                if (done.insert(*parentOid))
                    threadPool.enqueue(std::bind(process, *parentOid));
            }
        };

        threadPool.enqueue(std::bind(process, startOid));

        threadPool.process();

        return done.size();
    }
};

ref<GitRepoPool> GitRepoPool::create(const std::filesystem::path & path, GitRepo::Options options)
{
    return make_ref<GitRepoPoolImpl>(path, options);
}

std::vector<std::tuple<GitRepoImpl::Submodule, Hash>> GitRepoImpl::getSubmodules(const Hash & rev, bool exportIgnore)
{
    /* Read the .gitmodules files from this revision. */
    CanonPath modulesFile(".gitmodules");

    auto accessor = getAccessor(rev, {.exportIgnore = exportIgnore}, "");
    if (!accessor->pathExists(modulesFile))
        return {};

    /* Parse it and get the revision of each submodule. */
    auto configS = accessor->readFile(modulesFile);

    auto [fdTemp, pathTemp] = createTempFile("nix-git-submodules");
    AutoDelete delTemp(pathTemp, /*recursive=*/false);
    try {
        writeFull(fdTemp.get(), configS);
    } catch (SystemError & e) {
        e.addTrace({}, "while writing .gitmodules file to temporary file");
        throw;
    }

    std::vector<std::tuple<Submodule, Hash>> result;

    auto rawAccessor = getRawAccessor(rev, {});

    for (auto & submodule : parseSubmodules(pathTemp)) {
        /* Filter out .gitmodules entries that don't exist or are not
           submodules. */
        if (auto rev = rawAccessor->getSubmoduleRev(submodule.path))
            result.push_back({std::move(submodule), *rev});
    }

    delTemp.deletePath();
    return result;
}

namespace fetchers {

static std::filesystem::path tarballCacheDir()
{
    static auto repoDir = std::filesystem::path(getCacheDir()) / "tarball-cache-v2";
    return repoDir;
}

static constexpr GitRepo::Options tarballCacheOptions{.create = true, .bare = true, .packfilesOnly = true};

ref<GitRepo> Settings::getTarballCache() const
{
    return GitRepo::openRepo(tarballCacheDir(), tarballCacheOptions);
}

ref<GitRepoPool> Settings::getTarballWriterPool() const
{
    return GitRepoPool::create(tarballCacheDir(), tarballCacheOptions);
}

} // namespace fetchers

static Sync<std::map<std::filesystem::path, GitRepo::WorkdirInfo>> workdirInfoCache_;

GitRepo::WorkdirInfo GitRepo::getCachedWorkdirInfo(const std::filesystem::path & path)
{
    auto & _cache = workdirInfoCache_;
    {
        auto cache(_cache.lock());
        auto i = cache->find(path);
        if (i != cache->end())
            return i->second;
    }
    auto workdirInfo = GitRepo::openRepo(path, {})->getWorkdirInfo();
    _cache.lock()->emplace(path, workdirInfo);
    return workdirInfo;
}

void GitRepo::invalidateWorkdirInfoCache()
{
    workdirInfoCache_.lock()->clear();
}

bool isLegalRefName(const std::string & refName)
{
    initLibGit2();

    /* Check for cases that don't get rejected by libgit2.
     * FIXME: libgit2 should reject this. */
    if (refName == "@")
        return false;

    /* libgit2 doesn't barf on DEL symbol.
     * FIXME: libgit2 should reject this. */
    if (refName.find('\177') != refName.npos)
        return false;

    for (auto * func : {
             git_reference_name_is_valid,
             git_branch_name_is_valid,
             git_tag_name_is_valid,
         }) {
        int valid = 0;
        if (func(&valid, refName.c_str()))
            throw Error("checking git reference '%s': %s", refName, git_error_last()->message);
        if (valid)
            return true;
    }

    return false;
}

} // namespace nix

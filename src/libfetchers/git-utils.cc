#include "git-utils.hh"
#include "fs-input-accessor.hh"
#include "input-accessor.hh"
#include "filtering-input-accessor.hh"
#include "cache.hh"
#include "finally.hh"
#include "processes.hh"
#include "signals.hh"

#include <boost/core/span.hpp>

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
#include <git2/status.h>
#include <git2/submodule.h>
#include <git2/tree.h>

#include <iostream>
#include <unordered_set>
#include <queue>
#include <regex>

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
    CanonPath path;
    Repository repo;

    GitRepoImpl(CanonPath _path, bool create, bool bare)
        : path(std::move(_path))
    {
        initLibGit2();

        if (pathExists(path.abs())) {
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
        // Handle revisions used as refs.
        {
            git_oid oid;
            if (git_oid_fromstr(&oid, ref.c_str()) == 0)
                return toHash(oid);
        }

        // Resolve short names like 'master'.
        Reference ref2;
        if (!git_reference_dwim(Setter(ref2), *this, ref.c_str()))
            ref = git_reference_name(ref2.get());

        // Resolve full references like 'refs/heads/master'.
        Reference ref3;
        if (git_reference_lookup(Setter(ref3), *this, ref.c_str()))
            throw Error("resolving Git reference '%s': %s", ref, git_error_last()->message);

        auto oid = git_reference_target(ref3.get());
        if (!oid)
            throw Error("cannot get OID for Git reference '%s'", git_reference_name(ref3.get()));

        return toHash(*oid);
    }

    std::vector<Submodule> parseSubmodules(const CanonPath & configFile)
    {
        GitConfig config;
        if (git_config_open_ondisk(Setter(config), configFile.abs().c_str()))
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
        auto modulesFile = path + ".gitmodules";
        if (pathExists(modulesFile.abs()))
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

    static int sidebandProgressCallback(const char * str, int len, void * payload)
    {
        auto act = (Activity *) payload;
        act->result(resFetchStatus, trim(std::string_view(str, len)));
        return _isInterrupted ? -1 : 0;
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
        return _isInterrupted ? -1 : 0;
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

        runProgram(RunOptions {
            .program = "git",
            .searchPath = true,
            // FIXME: git stderr messes up our progress indicator, so
            // we're using --quiet for now. Should process its stderr.
            .args = { "-C", path.abs(), "fetch", "--quiet", "--force", "--", url, refspec },
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
                    "-C", path.abs(),
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
};

ref<GitRepo> GitRepo::openRepo(const CanonPath & path, bool create, bool bare)
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
        if (path.isRoot()) return nullptr;

        auto i = lookupCache.find(path);
        if (i == lookupCache.end()) {
            TreeEntry entry;
            if (auto err = git_tree_entry_bypath(Setter(entry), root.get(), std::string(path.rel()).c_str())) {
                if (err != GIT_ENOTFOUND)
                    throw Error("looking up '%s': %s", showPath(path), git_error_last()->message);
            }

            i = lookupCache.emplace(path, std::move(entry)).first;
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
    ref<InputAccessor> fileAccessor =
        AllowListInputAccessor::create(
                makeFSInputAccessor(path),
                std::set<CanonPath> { wd.files },
                std::move(makeNotAllowedError));
    if (exportIgnore) {
        return make_ref<GitExportIgnoreInputAccessor>(self, fileAccessor, std::nullopt);
    }
    else {
        return fileAccessor;
    }
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

    for (auto & submodule : parseSubmodules(CanonPath(pathTemp))) {
        auto rev = rawAccessor->getSubmoduleRev(submodule.path);
        result.push_back({std::move(submodule), rev});
    }

    return result;
}


}

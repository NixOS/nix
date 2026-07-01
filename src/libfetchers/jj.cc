#include "nix/fetchers/fetchers.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fmt.hh"
#include "nix/util/os-string.hh"
#include "nix/util/processes.hh"
#include "nix/util/util.hh"
#include "nix/util/strings.hh"
#include "nix/util/executable-path.hh"
#include "nix/util/memo.hh"
#include "nix/util/logging.hh"
#include "nix/util/finally.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/users.hh"
#include "nix/fetchers/cache.hh"
#include "nix/fetchers/filtering-source-accessor.hh"
#include "nix/store/store-api.hh"
#include "nix/store/pathlocks.hh"
#include "nix/util/url-parts.hh"
#include "nix/fetchers/fetch-settings.hh"

#include <regex>

using namespace std::string_literals;

namespace nix::fetchers {

/* The template passed to `jj log` to extract metadata about the `@`
   (working-copy) commit. Fields are NUL-separated (so that bookmark names
   containing spaces are handled correctly):

     1. commit_id  -- the underlying (git-compatible) commit hash
     2. committer timestamp, in seconds since the epoch
     3. whether the commit has conflicts ("1"/"0")
     4..N. the names of the local bookmarks pointing at this commit, if any
*/
static constexpr std::string_view jjLogTemplate =
    R"(commit_id ++ "\0" ++ committer.timestamp().utc().format("%s") ++ "\0" ++ if(conflict, "1", "0") ++ "\0" ++ local_bookmarks.map(|b| b.name()).join("\0"))";

static RunOptions jjOptions(const std::filesystem::path & repoDir, OsStrings args, bool ignoreWorkingCopy)
{
    OsStrings allArgs{
        // Pin an identity so that snapshotting the working copy never fails or
        // prompts in environments without a jj/user config. This does not change
        // the author of the existing `@` commit (it only matters when a *new*
        // commit is created), so it is safe for the read-only operations we do.
        OS_STR("--config"),
        OS_STR("user.name=nix"),
        OS_STR("--config"),
        OS_STR("user.email=nix@localhost"),
        // Deterministic, non-interactive output.
        OS_STR("--color"),
        OS_STR("never"),
        OS_STR("--config"),
        OS_STR("ui.paginate=never"),
    };
    if (ignoreWorkingCopy)
        allArgs.push_back(OS_STR("--ignore-working-copy"));
    for (auto & arg : args)
        allArgs.push_back(std::move(arg));

    return {
        .program = "jj",
        .lookupPath = true,
        .args = std::move(allArgs),
        // jj prints file paths relative to the working directory, so run it from
        // the repository root to get root-relative paths.
        .chdir = repoDir,
    };
}

// runProgram wrapper that uses jjOptions instead of stock RunOptions.
static std::string runJj(const std::filesystem::path & repoDir, OsStrings args, bool ignoreWorkingCopy = false)
{
    auto res = runProgram(jjOptions(repoDir, std::move(args), ignoreWorkingCopy));

    if (!statusOk(res.first))
        throw ExecError(res.first, "jj %1%", statusToString(res.first));

    return res.second;
}

struct JjInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const Settings & settings, const ParsedURL & url, bool requireTree) const override
    {
        // `jj+file` is a local working copy. `jj+git+<transport>` is a remote
        // repository (jj's only remote backend is Git) that we clone via
        // `jj git clone`. `parseUrlScheme` only splits on the first `+`, so we
        // enumerate the schemes explicitly.
        bool isLocal = url.scheme == "jj+file";
        bool isRemote = url.scheme == "jj+git+https" || url.scheme == "jj+git+http"
                        || url.scheme == "jj+git+ssh" || url.scheme == "jj+git+file";

        if (!isLocal && !isRemote)
            return {};

        auto url2(url);
        url2.query.clear();
        // Strip only the "jj+" application layer, leaving the inner scheme:
        // "file" (local) or "git+<transport>" (remote). The remaining scheme
        // self-describes the backend, so no separate flag is needed.
        url2.scheme = std::string(url.scheme, 3);

        Attrs attrs;
        attrs.emplace("type", "jj");

        for (auto & [name, value] : url.query) {
            if (name == "rev" || name == "ref")
                attrs.emplace(name, value);
            else if (name == "shallow")
                attrs.emplace(name, Explicit<bool>{value == "1" || value == "true"});
            // `dir` is meaningful to the flake layer (it selects a subdirectory),
            // not to this fetcher, which always copies the whole working copy. Drop
            // it so it doesn't leak into the stored URL (cf. git.cc).
            else if (name == "dir")
                continue;
            else
                url2.query.emplace(name, value);
        }

        attrs.emplace("url", url2.to_string());

        return inputFromAttrs(settings, attrs);
    }

    std::string_view schemeName() const override
    {
        return "jj";
    }

    std::string schemeDescription() const override
    {
        return "a Jujutsu (jj) working copy or remote repository";
    }

    const std::map<std::string, AttributeInfo> & allowedAttrs() const override
    {
        static const std::map<std::string, AttributeInfo> attrs = {
            {
                "url",
                {},
            },
            {
                "ref",
                {},
            },
            {
                "rev",
                {},
            },
            {
                "revCount",
                {},
            },
            {
                "lastModified",
                {},
            },
            {
                "narHash",
                {},
            },
            {
                "name",
                {},
            },
            {
                "shallow",
                {},
            },
        };
        return attrs;
    }

    std::optional<Input> inputFromAttrs(const Settings & settings, const Attrs & attrs) const override
    {
        parseURL(getStrAttr(attrs, "url"));

        if (auto ref = maybeGetStrAttr(attrs, "ref")) {
            if (!std::regex_match(*ref, refRegex))
                throw BadURL("invalid Jujutsu bookmark name '%s'", *ref);
        }

        Input input{};
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        url.scheme = "jj+" + url.scheme;
        if (auto rev = input.getRev())
            url.query.insert_or_assign("rev", rev->gitRev());
        if (auto ref = input.getRef())
            url.query.insert_or_assign("ref", *ref);
        if (getShallowAttr(input))
            url.query.insert_or_assign("shallow", "1");
        return url;
    }

    Input applyOverrides(const Input & input, std::optional<std::string> ref, std::optional<Hash> rev) const override
    {
        auto res(input);
        if (rev)
            res.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref) {
            if (!std::regex_match(*ref, refRegex))
                throw BadURL("invalid Jujutsu bookmark name '%s'", *ref);
            res.attrs.insert_or_assign("ref", *ref);
        }
        return res;
    }

    std::optional<std::filesystem::path> getSourcePath(const Input & input) const override
    {
        if (isRemote(input))
            return {};
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme == "file" && !input.getRef() && !input.getRev())
            return urlPathToPath(url.path);
        return {};
    }

    void putFile(
        const Input & input,
        const CanonPath & path,
        std::string_view contents,
        std::optional<std::string> commitMsg) const override
    {
        auto repoPath = getSourcePath(input);
        if (!repoPath)
            throw Error(
                "cannot commit '%s' to Jujutsu repository '%s' because it's not a working tree",
                path,
                input.to_string());

        writeFile(*repoPath / path.rel(), contents);

        // Unlike Git and Mercurial, Jujutsu automatically tracks new files when
        // it next snapshots the working copy, so there is nothing to "add". The
        // change becomes part of the `@` commit on the next jj invocation.
    }

    /* A remote (Git-backed) repository is stored with a `git`/`git+<transport>`
       URL scheme; a local working copy uses a plain `file` scheme. */
    bool isRemote(const Input & input) const
    {
        return parseUrlScheme(parseURL(getStrAttr(input.attrs, "url")).scheme).application == "git";
    }

    bool getShallowAttr(const Input & input) const
    {
        return maybeGetBoolAttr(input.attrs, "shallow").value_or(false);
    }

    std::filesystem::path getActualPath(const Input & input) const
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme != "file")
            throw Error(
                "Jujutsu input '%s' is not a local working copy; only file:// URLs are supported",
                input.to_string());
        return absPath(urlPathToPath(url.path));
    }

    /* A jj Git backend with a truncated history records a `shallow` file in its
       Git object store, which jj keeps at `<repo>/.git` (referenced from
       `.jj/repo/store/git_target`). revCount is meaningless in that case, so we
       refuse it, as the Git fetcher does. */
    bool isShallow(const std::filesystem::path & repoPath) const
    {
        return pathExists(repoPath / ".git" / "shallow");
    }

    /* Whether `rev` already exists in the repository, used to avoid an
       unnecessary `jj git fetch` (cf. mercurial.cc). */
    bool revExistsInCache(const std::filesystem::path & repoPath, const Hash & rev) const
    {
        try {
            return chomp(runJj(
                       repoPath,
                       {OS_STR("log"),
                        OS_STR("-r"),
                        string_to_os_string(rev.gitRev()),
                        OS_STR("--no-graph"),
                        OS_STR("-T"),
                        OS_STR(R"("1")")},
                       /*ignoreWorkingCopy=*/true))
                   == "1";
        } catch (ExecError &) {
            return false;
        }
    }

    /* Count the number of ancestors of the `@` commit (including itself),
       caching the result by commit hash since this is independent of the
       working-copy state. */
    uint64_t getRevCount(ref<Cache> cache, const std::filesystem::path & repoPath, const Hash & rev) const
    {
        if (isShallow(repoPath))
            throw Error(
                "%s is a shallow Jujutsu repository, so 'revCount' is not available", PathFmt(repoPath));

        Cache::Key key{"jjRevCount", {{"rev", rev.gitRev()}}};

        if (auto revCountAttrs = cache->lookup(key))
            return getIntAttr(*revCountAttrs, "revCount");

        Activity act(*logger, lvlChatty, actUnknown, fmt("getting Jujutsu revision count of '%s'", PathFmt(repoPath)));

        // Exclude jj's virtual root commit (`root()`, the all-zeros commit), which
        // has no git counterpart, so the count matches the Git fetcher's.
        auto output = runJj(
            repoPath,
            {OS_STR("log"),
             OS_STR("-r"),
             string_to_os_string("::" + rev.gitRev() + " ~ root()"),
             OS_STR("--no-graph"),
             OS_STR("-T"),
             OS_STR("\"x\\n\"")},
            /*ignoreWorkingCopy=*/true);

        uint64_t revCount = 0;
        for (auto & line : tokenizeString<std::vector<std::string>>(output, "\n"))
            if (!line.empty())
                revCount++;

        cache->upsert(key, Attrs{{"revCount", revCount}});

        return revCount;
    }

    static MakeNotAllowedError makeNotAllowedError(std::filesystem::path repoPath)
    {
        return [repoPath{std::move(repoPath)}](const CanonPath & path) -> RestrictedPathError {
            if (pathExists(repoPath / path.rel()))
                return RestrictedPathError(
                    "Path '%1%' in the repository %2% is not tracked by Jujutsu.\n"
                    "\n"
                    "To make it visible to Nix, run:\n"
                    "\n"
                    "jj file track %1%",
                    path.rel(),
                    PathFmt(repoPath));
            else
                return RestrictedPathError(
                    "Path '%s' does not exist in Jujutsu repository %s.", path.rel(), PathFmt(repoPath));
        };
    }

    struct Metadata
    {
        Hash rev;
        uint64_t lastModified;
        bool hasConflict;
        /* The names of the local bookmarks pointing at the revision, if any. */
        std::vector<std::string> bookmarks;
    };

    Metadata readMetadata(const std::filesystem::path & repoPath, const std::string & revset, bool ignoreWorkingCopy)
        const
    {
        auto output = runJj(
            repoPath,
            {OS_STR("log"),
             OS_STR("-r"),
             string_to_os_string(revset),
             OS_STR("--no-graph"),
             OS_STR("-T"),
             string_to_os_string(std::string(jjLogTemplate))},
            ignoreWorkingCopy);

        auto fields = tokenizeString<std::vector<std::string>>(output, "\0"s);
        if (fields.size() < 3)
            throw Error("unexpected output from 'jj log' for repository %s", PathFmt(repoPath));

        return Metadata{
            .rev = Hash::parseAny(chomp(fields[0]), HashAlgorithm::SHA1),
            .lastModified = string2Int<uint64_t>(chomp(fields[1])).value_or(0),
            .hasConflict = chomp(fields[2]) == "1",
            .bookmarks = std::vector<std::string>(fields.begin() + 3, fields.end()),
        };
    }

    void setAttrs(
        const Settings & settings, Input & input, const std::filesystem::path & repoPath, const Metadata & meta) const
    {
        auto rev = meta.rev;
        input.attrs.insert_or_assign("rev", rev.gitRev());
        input.attrs.insert_or_assign("lastModified", meta.lastModified);
        input.attrs.insert_or_assign(
            "revCount", makeLazyAttr([this, cache{settings.getCache()}, repoPath, rev]() -> ResolvedAttr {
                return getRevCount(cache, repoPath, rev);
            }));
        // Expose a bookmark as `ref` when it unambiguously names a single one and
        // the caller didn't request a specific ref.
        if (!input.getRef() && meta.bookmarks.size() == 1)
            input.attrs.insert_or_assign("ref", meta.bookmarks[0]);
    }

    std::pair<ref<SourceAccessor>, Input> getAccessorFromWorkdir(
        const Settings & settings, Store & store, const std::filesystem::path & repoPath, Input input) const
    {
        /* Snapshot the working copy and read metadata about the `@` commit. We
           deliberately do *not* pass `--ignore-working-copy` here: snapshotting
           is what makes `@` reflect the current on-disk state (jj has no notion
           of a separate "dirty" state). */
        auto meta = readMetadata(repoPath, "@", /*ignoreWorkingCopy=*/false);

        if (meta.hasConflict)
            warn("Jujutsu working copy %s has unresolved conflicts; conflict markers will be included", PathFmt(repoPath));

        /* Enumerate the files tracked in the `@` commit. The snapshot has already
           happened above, so we can skip it here. We NUL-separate the paths so
           that filenames containing newlines are handled correctly. */
        auto fileList = runJj(
            repoPath,
            {OS_STR("file"), OS_STR("list"), OS_STR("-T"), OS_STR(R"(path ++ "\0")")},
            /*ignoreWorkingCopy=*/true);

        std::set<CanonPath> files;
        for (auto & line : tokenizeString<std::vector<std::string>>(fileList, "\0"s)) {
            if (line.empty())
                continue;
            // Defensive: ignore anything outside the repository root.
            if (hasPrefix(line, "../"))
                continue;
            files.insert(CanonPath(line));
        }

        ref<SourceAccessor> accessor =
            AllowListSourceAccessor::create(
                makeFSSourceAccessor(repoPath),
                std::move(files),
                /*allowedPaths=*/{CanonPath::root},
                makeNotAllowedError(repoPath))
                .cast<SourceAccessor>();

        setAttrs(settings, input, repoPath, meta);

        accessor->setPathDisplay("«" + input.to_string() + "»");

        return {accessor, std::move(input)};
    }

    /* Materialise `rev` from `repoPath` into the store by checking it out into a
       temporary jj workspace and reading it back from disk.

       jj has no non-mutating "export a revision's tree to a directory" command
       yet; once it does, that would be a simpler replacement for this temporary
       workspace. Tracked upstream as:
         - https://github.com/jj-vcs/jj/issues/4884 (`jj file export -d <dir>`)
         - https://github.com/jj-vcs/jj/issues/9554 (`jj archive`)

       A workspace checkout is backend-agnostic and reproduces file modes and
       symlinks natively. */
    StorePath
    checkoutRevToStore(Store & store, const std::filesystem::path & repoPath, const std::string & rev, std::string_view name)
        const
    {
        auto tmpDir = createTempDir();
        AutoDelete delTmpDir(tmpDir, true);

        /* The workspace name is the (unique) basename of the temporary directory,
           so concurrent fetches don't collide. `jj workspace add` snapshots the
           main working copy (it rejects `--ignore-working-copy`). */
        auto workspaceName = tmpDir.filename().string();

        runJj(
            repoPath,
            {OS_STR("workspace"),
             OS_STR("add"),
             OS_STR("--name"),
             string_to_os_string(workspaceName),
             OS_STR("--revision"),
             string_to_os_string(rev),
             tmpDir.native()});

        /* Forget the temporary workspace afterwards, before its directory is
           removed. If the process is killed first, a stale registration is left
           behind; its `nix-...` name makes it easy to find with `jj workspace
           forget`. */
        Finally forgetWorkspace([&] {
            try {
                runJj(repoPath, {OS_STR("workspace"), OS_STR("forget"), string_to_os_string(workspaceName)});
            } catch (...) {
                ignoreExceptionInDestructor();
            }
        });

        /* The checkout also contains the workspace's own `.jj` directory, so
           filter to the tracked files (which excludes it). */
        auto fileList = runJj(
            repoPath,
            {OS_STR("file"),
             OS_STR("list"),
             OS_STR("-r"),
             string_to_os_string(rev),
             OS_STR("-T"),
             OS_STR(R"(path ++ "\0")")},
            /*ignoreWorkingCopy=*/true);

        std::set<CanonPath> files;
        for (auto & line : tokenizeString<std::vector<std::string>>(fileList, "\0"s))
            if (!line.empty())
                files.insert(CanonPath(line));

        ref<SourceAccessor> accessor =
            AllowListSourceAccessor::create(
                makeFSSourceAccessor(tmpDir),
                std::move(files),
                /*allowedPaths=*/{CanonPath::root},
                makeNotAllowedError(tmpDir))
                .cast<SourceAccessor>();

        /* Copy into the store eagerly: the temporary workspace is removed when we
           return, so we can't hand back a lazy accessor over it. */
        return store.addToStore(std::string(name), {accessor, CanonPath::root});
    }

    /* Fetch an explicitly-requested revision or bookmark of a local repository. */
    std::pair<ref<SourceAccessor>, Input> getAccessorFromRev(
        const Settings & settings, Store & store, const std::filesystem::path & repoPath, Input input) const
    {
        auto revset = input.getRev() ? input.getRev()->gitRev() : *input.getRef();

        auto meta = readMetadata(repoPath, revset, /*ignoreWorkingCopy=*/true);

        if (meta.hasConflict)
            warn("Jujutsu revision '%s' has unresolved conflicts; conflict markers will be included", revset);

        auto storePath = checkoutRevToStore(store, repoPath, meta.rev.gitRev(), input.getName());
        auto accessor = store.requireStoreObjectAccessor(storePath);

        setAttrs(settings, input, repoPath, meta);

        accessor->setPathDisplay("«" + input.to_string() + "»");

        return {accessor, std::move(input)};
    }

    /* Fetch a remote repository: clone it into a cache via `jj git clone`
       (jj's only remote backend is Git), resolve the requested ref/rev, and
       materialise that revision. Mirrors mercurial.cc's clone→cache→materialise
       flow. */
    std::pair<ref<SourceAccessor>, Input>
    getAccessorFromRemote(const Settings & settings, Store & store, Input input) const
    {
        auto origRev = input.getRev();
        auto name = input.getName();
        auto shallow = getShallowAttr(input);

        /* The stored URL is `git+<transport>://…`; `jj git clone` wants the bare
           git URL, so strip the `git+` layer. */
        auto storedUrl = parseURL(getStrAttr(input.attrs, "url"));
        storedUrl.scheme = std::string(parseUrlScheme(storedUrl.scheme).transport);
        auto url = storedUrl.to_string();

        /* The cache is a full jj clone; keep shallow and non-shallow clones of the
           same URL separate. */
        auto cacheDir =
            getCacheDir() / "jj"
            / (hashString(HashAlgorithm::SHA256, url).to_string(HashFormat::Nix32, false)
               + (shallow ? "-shallow" : ""));

        /* Key the caches on `shallow` too: a shallow and a full clone of the same
           URL live in different cache directories, and the lazy revCount closure
           below captures `cacheDir`, so the two must not share cache entries. */
        auto revInfoKey = [&](const Hash & rev) {
            return Cache::Key{
                "jjRemoteRev",
                {{"store", store.storeDir}, {"name", name}, {"rev", rev.gitRev()}, {"shallow", Explicit<bool>{shallow}}}};
        };

        auto makeResult = [&](const Attrs & infoAttrs, const StorePath & storePath, const Hash & rev)
            -> std::pair<ref<SourceAccessor>, Input> {
            input.attrs.insert_or_assign("rev", rev.gitRev());
            input.attrs.insert_or_assign("lastModified", getIntAttr(infoAttrs, "lastModified"));
            /* revCount needs the full history; omit it for shallow clones, as the
               Git fetcher does. */
            if (!shallow)
                input.attrs.insert_or_assign(
                    "revCount", makeLazyAttr([this, cache{settings.getCache()}, cacheDir, rev]() -> ResolvedAttr {
                        return getRevCount(cache, cacheDir, rev);
                    }));
            auto accessor = store.requireStoreObjectAccessor(storePath);
            accessor->setPathDisplay("«" + input.to_string() + "»");
            return {accessor, std::move(input)};
        };

        /* Resolve a moving ref to a rev via a TTL-bounded cache (cf. hgRefToRev). */
        std::optional<Cache::Key> refToRevKey;
        if (auto ref = input.getRef())
            refToRevKey = Cache::Key{"jjRemoteRefToRev", {{"url", url}, {"ref", *ref}, {"shallow", Explicit<bool>{shallow}}}};

        if (!input.getRev() && refToRevKey)
            if (auto res = settings.getCache()->lookupWithTTL(*refToRevKey))
                input.attrs.insert_or_assign("rev", getRevAttr(*res, "rev").gitRev());

        if (auto rev = input.getRev())
            if (auto res = settings.getCache()->lookupStorePath(revInfoKey(*rev), store))
                return makeResult(res->value, res->storePath, *rev);

        createDirs(cacheDir.parent_path());
        PathLocks cacheDirLock({cacheDir.string()});

        bool haveRev =
            input.getRev() && pathExists(cacheDir / ".jj") && revExistsInCache(cacheDir, *input.getRev());

        if (!haveRev) {
            Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Jujutsu repository '%s'", url));

            auto cloneFresh = [&] {
                if (pathExists(cacheDir))
                    deletePath(cacheDir);
                OsStrings args{OS_STR("git"), OS_STR("clone")};
                if (shallow) {
                    args.push_back(OS_STR("--depth"));
                    args.push_back(OS_STR("1"));
                }
                // `--` guards against a URL that could be mistaken for an option.
                args.push_back(OS_STR("--"));
                args.push_back(string_to_os_string(url));
                args.push_back(cacheDir.native());
                /* Run in the (existing) parent; `jj git clone` creates cacheDir. */
                runJj(cacheDir.parent_path(), std::move(args));
            };

            if (pathExists(cacheDir / ".jj")) {
                /* `jj git fetch` has no `--depth`, so updating a shallow cache
                   deepens it; that never yields wrong results (the shallow cache
                   dir is keyed separately). */
                try {
                    runJj(
                        cacheDir,
                        {OS_STR("git"), OS_STR("fetch"), OS_STR("--remote"), OS_STR("origin")},
                        /*ignoreWorkingCopy=*/true);
                } catch (ExecError &) {
                    /* The cache may be corrupt (e.g. from an interrupted earlier
                       fetch); recover by re-cloning from scratch. */
                    warn("re-cloning Jujutsu repository '%s' after a failed update", url);
                    cloneFresh();
                }
            } else
                cloneFresh();
        }

        /* Resolve the requested ref/rev (default: the remote's default branch). */
        auto revset = input.getRev() ? input.getRev()->gitRev()
                      : input.getRef() ? *input.getRef()
                                       : std::string("trunk()");

        auto meta = readMetadata(cacheDir, revset, /*ignoreWorkingCopy=*/true);

        if (meta.hasConflict)
            warn("Jujutsu revision '%s' has unresolved conflicts; conflict markers will be included", revset);

        if (!origRev && refToRevKey)
            settings.getCache()->upsert(*refToRevKey, {{"rev", meta.rev.gitRev()}});

        if (auto res = settings.getCache()->lookupStorePath(revInfoKey(meta.rev), store))
            return makeResult(res->value, res->storePath, meta.rev);

        auto storePath = checkoutRevToStore(store, cacheDir, meta.rev.gitRev(), name);

        Attrs infoAttrs({{"lastModified", (uint64_t) meta.lastModified}});
        settings.getCache()->upsert(revInfoKey(meta.rev), store, infoAttrs, storePath);

        return makeResult(infoAttrs, storePath, meta.rev);
    }

    std::pair<ref<SourceAccessor>, Input> getAccessor(const Settings & settings, Store & store, const Input & _input)
        const override
    {
        Input input(_input);

        /* Flake references to a local `.jj` path are routed here automatically
           (see `flakeref.cc`), so give a clear error if the `jj` command isn't
           available rather than a cryptic exec failure. */
        if (!ExecutablePath::load().findName(OS_STR("jj")))
            throw Error(
                "the 'jj' command is required to fetch Jujutsu inputs, but it was not found in PATH.\n"
                "\n"
                "Install Jujutsu (https://jj-vcs.github.io/).");

        if (isRemote(input))
            return getAccessorFromRemote(settings, store, std::move(input));

        auto repoPath = getActualPath(input);

        if (!pathExists(repoPath / ".jj"))
            throw Error("%s is not a Jujutsu repository (it has no '.jj' directory)", PathFmt(repoPath));

        return input.getRev() || input.getRef()
                   ? getAccessorFromRev(settings, store, repoPath, std::move(input))
                   : getAccessorFromWorkdir(settings, store, repoPath, std::move(input));
    }

    bool isLocked(const Settings & settings, const Input & input) const override
    {
        return (bool) input.getRev();
    }

    std::optional<std::string> getFingerprint(Store & store, const Input & input) const override
    {
        if (auto rev = input.getRev())
            return rev->gitRev();
        else
            return std::nullopt;
    }
};

static auto rJjInputScheme = OnStartup([] { registerInputScheme(std::make_unique<JjInputScheme>()); });

} // namespace nix::fetchers

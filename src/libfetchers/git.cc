#include "fetchers.hh"
#include "cache.hh"
#include "globals.hh"
#include "tarfile.hh"
#include "store-api.hh"
#include "url-parts.hh"
#include "pathlocks.hh"
#include "util.hh"
#include "git.hh"

#include "fetch-settings.hh"

#include <regex>
#include <string.h>
#include <sys/time.h>
#include <sys/wait.h>

using namespace std::string_literals;

// CHANGES:
// allRefs is no longer needed
// A bit slower for local repositories
// A lot faster for remote repositories
//
// BUG:
//   fetchGit { url = "local"; ref = "abbreviated tag"} works but
//   fetchGit { url = "remote"; ref = "abbreviated tag"} does not work
//
// Checklist:
// - Caching between commits works (when fetching a commit with already fetched ancestors only the missing commits are fetched).
// - Compared performance to previous implementation
// - Dirty local, local and remote repos behave the same way.
// - All three layers of caching respect the correct expiration.
// - shallow works
// - submodules works
// - Documentation is updated
// - Debug prints are removed

namespace nix::fetchers {

namespace {

/// Explicit initial branch of our bare repo to suppress warnings from new version of git.
/// The value itself does not matter, since we always fetch a specific revision.
/// It is set with `-c init.defaultBranch=` instead of `--initial-branch=` to stay compatible with
/// old version of git, which will ignore unrecognized `-c` options.
const std::string gitInitialBranch = "__nix_dummy_branch";

/// Get the git hash of an empty tree
///
/// The hash of the empty tree is a bit special in that git knows some things about it even though it is not an object in the repo.
/// For example we can run git diff or git archive with it.
///
/// @param gitDir The directory of the git repository. Determines the hashing algorithm.
/// @return The hash of the empty tree.
std::string getEmptyTreeHash(const Path &gitDir) { return chomp(runProgram("git", true, {"-C", gitDir, "hash-object", "-t", "tree", "/dev/null"})); }

bool isCacheFileWithinTtl(const struct stat &st) {
	time_t now = time(0);
	return st.st_mtime + settings.tarballTtl > now;
}

/// Get a path to a bare git repo in the nix git cache
///
/// @param key A key that identifies the repo. Usually the URL.
/// @return The cache path. You should lock it before writing to it.
Path getCachePath(std::string_view key) {
	auto cacheDir = getCacheDir() + "/nix/gitv3/" + hashString(htSHA256, key).to_string(Base32, false);
	// Create the repo if it does not exist
	if (!pathExists(cacheDir)) {
		createDirs(dirOf(cacheDir));
		PathLocks cacheDirLock({cacheDir + ".lock"});
		runProgram("git", true, {"-c", "init.defaultBranch=" + gitInitialBranch, "init", "--bare", cacheDir});
	}
	return cacheDir;
}

/// Check if a revision is present in a git repository
///
/// @param gitDir A path to a bare git repository or .git directory
/// @param revision A git revision. Usually a commit hash
/// @return true if the revision is present in the repository
bool revisionIsInRepo(const Path gitDir, const std::string revision) {
	try {
		runProgram("git", true, {"-C", gitDir, "cat-file", "-e", revision});
		return true;
	} catch (ExecError &e) {
		if (!WIFEXITED(e.status)) {
			throw;
		}
		return false;
	}
}

/// Resolves the revision and full reference for a given reference in a git repo
///
/// If a abbreviated reference is passed (e.g. 'master') it is also resolved to a full reference (e.g. 'refs/heads/master')
///
/// @param gitUrl A git url that will be used with `git ls-remote` to resolve the revision.
/// @param reference A git reference or HEAD.
/// @return The resolved revision and full reference. nullopt if the reference could not be resolved.
std::optional<std::string> resolveRevision(const Path &gitUrl, const std::string &reference) {
	// Run ls-remote to find a revision for the reference
	auto [status, output] = runProgram(RunOptions{
		.program = "git",
		.args = {"ls-remote", gitUrl, reference},
		.isInteractive = true,
	});
	if (status != 0)
		return std::nullopt;

	std::string_view line = output;
	line = line.substr(0, line.find("\n"));
	if (const auto parseResult = git::parseLsRemoteLine(line)) {
		if (parseResult->kind == git::LsRemoteRefLine::Kind::Symbolic) {
			throw Error("Should git should never resolve a symbolic revision without  '--symref'");
		}

		// parseResult->reference is always defined if parseResult->kind is not git::LsRemoteRefLine::Kind::Symbolic
		// which we asserted above
		auto const fullReference = parseResult->reference.value();
		auto const revision = parseResult->target;

		debug("resolved reference '%s' in repo '%s' to revision '%s' and full reference '%s'", reference, gitUrl, revision, fullReference);
		return revision;
	}
	return std::nullopt;
}

/// Resolves the revision and full reference for a given reference in a git repo.
///
/// Tries a lookup in the local git cache first. If the revision is not in the cache it is resolved using the repository at gitUrl.
///
/// If a abbreviated reference is passed (e.g. 'something') it is treated as a heads reference (e.g. 'refs/heads/something').
/// The only supported special reference is 'HEAD'.
///
/// @param gitUrl The url of the git repo. Can be any [git url](https://git-scm.com/docs/git-fetch#_git_urls).
/// @param reference A full git reference or and abbreviated branch (ref/heads) reference.
/// @param isLocal Skip the cache and look up directly in the local repository.
/// @return The resolved revision and full reference. nullopt if the reference could not be resolved.
std::string readRevisionCached(const std::string &gitUrl, const std::optional<std::string> &reference, bool isLocal) {
	Path cacheDir = getCachePath(gitUrl);

	// TODO: Currently every input reference is treated as a /ref/heads reference if it is no full ref.
	// This means that tag references need to be prefixed with 'refs/tags/' otherwise they would not work.
	// We theoretically fully support abbreviated references, but that may break compatibility with older versions
	// On the other hand reference resolution is always impure so it probably can be changed without breaking anything.
	std::string referenceOrHead = reference.value_or("HEAD");
	Path fullRef = referenceOrHead.compare(0, 5, "refs/") == 0 ? referenceOrHead : referenceOrHead == "HEAD" ? "HEAD" : "refs/heads/" + referenceOrHead;

	if (isLocal) {
		auto const localRevision = resolveRevision(gitUrl, fullRef);
		if (localRevision) {
			return localRevision.value();
		}
		if (fullRef == "HEAD") {
			// If HEAD can not be found the repository has probably no commits.
			return getEmptyTreeHash(gitUrl);
		}
		throw Error("failed resolve revision for reference '%s' in local repository '%s'", fullRef, gitUrl);
	}

	auto cachedFullRef = fullRef == "HEAD" ? "refs/heads/__nix_cache_HEAD" : fullRef;
	Path cachedReferenceFile = cacheDir + "/" + cachedFullRef;

	struct stat st;
	auto existsInCache = (stat(cachedReferenceFile.c_str(), &st) == 0);
	if (existsInCache) {
		auto cacheIsFresh = isCacheFileWithinTtl(st);

		if (cacheIsFresh) {
			// Returning a cached revision if available and fresh
			std::string cachedRevision = trim(readFile(cachedReferenceFile));
			debug("using cached revision '%s' for reference '%s' in repository '%s'", cachedRevision, fullRef, gitUrl);
			return cachedRevision;
		}

		auto freshRevision = resolveRevision(gitUrl, fullRef);
		if (freshRevision) {
			// Returning a fresh revision if the cache exists but is expired
			PathLocks cacheDirLock({cacheDir + ".lock"});
			createDirs(dirOf(cachedReferenceFile));
			auto const revision = freshRevision.value();
			writeFile(cachedReferenceFile, revision);
			return revision;
		}

		auto cachedRevision = trim(readFile(cachedReferenceFile));
		warn("failed to resolve revision for reference '%s' in repository '%s'; using expired cached revision '%s'", fullRef, gitUrl, cachedRevision);
		return cachedRevision;
	}

	// Return a fresh revision if there is none in the cache
	auto const freshRevision = resolveRevision(gitUrl, fullRef);
	if (!freshRevision) {
		throw Error("failed to resolve revision for reference '%s' in repository '%s'", fullRef, gitUrl);
	}

	PathLocks cacheDirLock({cacheDir + ".lock"});
	createDirs(dirOf(cachedReferenceFile));
	auto const revision = freshRevision.value();
	writeFile(cachedReferenceFile, revision);
	return revision;
}

/// All information that is required to fetch a submodule
struct SubmoduleInfo {
	/// The name of the submodule
	std::string name;
	/// The url of the submodule
	///
	/// If the url in .gitmodules is relative it is relative to the origin of the superrepo.
	std::string url;
	/// The path of the submodule relative to the root of the superrepo
	std::string path;
	/// The revision of the submodule
	std::string revision;
	/// Whether the submodule url points to a local possibly dirty submodule
	/// If this is set revision can be ignored
	bool dirtyLocal;
};

/// Extracts information about the submodules at a given revision in a git repo
///
/// `revision` and `gitDir` specify the repo to extract the submodule information from.
///
/// If the source is a dirty local worktree you can specify the path to that worktree as localWorkdir.
/// If you do and the submodule is available in the local worktree the submodule url will be set to the local path.
/// In that case the return value will also contain a dirtyLocal flag.
///
/// @param revision A git revision. Usually a commit hash.
/// @param gitDir A path to a bare git repository or .git directory
/// @param localWorkdir A path to a local git worktree where the repo is checked out.
/// @return A vector of submodule information. Empty if there are no submodules.
std::vector<SubmoduleInfo> readGitmodules(const std::string &revision, const Path &gitDir, const std::optional<std::string> &localWorkdir) {
	auto gitmodulesFileFlag = "--blob=" + revision + ":.gitmodules";
	// Run ls-remote to find a revision for the reference
	auto [status, output] = runProgram(RunOptions{
		.program = "git",
		.args = {"-C", gitDir, "config", gitmodulesFileFlag, "--name-only", "--get-regexp", "path"},
	});
	if (status != 0) {
		return {};
	}

	std::string_view sv = output;
	auto lines = tokenizeString<std::vector<std::string>>(sv, "\n");
	std::vector<SubmoduleInfo> submodules;
	for (std::string_view line : lines) {
		if (line.length() == 0) {
			continue;
		}
		const static std::regex line_regex("^submodule[.](.+)[.]path$");
		std::match_results<std::string_view::const_iterator> match;
		if (!std::regex_match(line.cbegin(), line.cend(), match, line_regex)) {
			throw Error(".gitmodules file seems invalid");
		}

		std::string submoduleName = match[1];

		std::string path;
		std::string url;
		std::string submoduleRevision;
		bool dirtyLocal = false;

		{
			auto output = runProgram("git", true, Strings{"-C", gitDir, "config", gitmodulesFileFlag, "--get", "submodule." + submoduleName + ".path"},
									 std::nullopt, true);
			path = output.substr(0, output.find("\n"));
		}

		{
			auto output = runProgram("git", true, Strings{"-C", gitDir, "config", gitmodulesFileFlag, "--get", "submodule." + submoduleName + ".url"},
									 std::nullopt, true);
			url = output.substr(0, output.find("\n"));

			if (url.rfind("./", 0) == 0 || url.rfind("../", 0) == 0) {
				// If the submodule is relative its URL is relative to the origin of the superrepo
				auto [status, output] = runProgram(RunOptions{
					.program = "git",
					.args = {"-C", gitDir, "remote", "get-url", "origin"},
					.isInteractive = true,
				});
				auto line = chomp(output);
				auto relativeSubmoduleRoot = line.size() ? line : gitDir;
				url = relativeSubmoduleRoot + "/" + url;
			}
		}

		{
			if (localWorkdir) {
				auto localRepodir = localWorkdir.value();
				auto output = runProgram("git", true, Strings{"-C", localRepodir, "submodule", "status", path});
				auto line = output.substr(0, output.find("\n"));
				auto prefix = line.substr(0, 1);
				auto hash = chomp(line.substr(1, line.find(" ", 1)));

				if (prefix != "-") {
					// Submodule is available in the local worktree
					url = localRepodir + "/" + path;
					dirtyLocal = true;
				}
				submoduleRevision = hash;
			} else {
				auto output = runProgram("git", true, Strings{"-C", gitDir, "ls-tree", "--object-only", revision, path}, {}, true);
				auto line = output.substr(0, output.find("\n"));
				if (line.size() == 0) {
					throw Error("failed to resolve submodule '%s' in revision '%s'", submoduleName, revision);
				}
				submoduleRevision = line;
			}
		}

		SubmoduleInfo info({submoduleName, url, path, submoduleRevision, dirtyLocal});
		submodules.push_back(info);
	}

	return submodules;
}

/// Get the diff between a revision and a dirty workdir
///
/// A workdir is dirty if it has staged changes, or unstaged changes to tracked files.
///
/// @param workDir A path to a git repo or workdir
/// @param headRevision The revision that is currently HEAD of the workdir
/// @param submodules Also check if submodules are changed or dirty
/// @return A diff between the `headRevision` and the dirty workdir if the workdir is dirty.
std::optional<std::string> getWorkdirDiff(const Path &workDir, const std::string &headRevision, bool submodules) {
	auto gitStatusArguments =
		Strings({"-C", workDir, "diff-index", "--binary", std::string("--ignore-submodules=") + (submodules ? "none" : "all"), "-p", headRevision});
	auto output = runProgram("git", true, gitStatusArguments);

	return chomp(output).length() > 0 ? std::optional<std::string>(output) : std::nullopt;
}

/// Get a path to a bare git repo containing the specified revision
///
/// If the cached repo already contains the revision it just returns the path to the repo.
/// Otherwise it fetches the revision into the repo and returns the path to the repo.
///
/// The returned path is a cache dir path without a look. That is fine as long as there are only read-only operations on the fetched revision.
///
/// @param gitUrl A [git url](https://git-scm.com/docs/git-fetch#_git_urls) to the repository.
/// @param revision A git revision. Usually a commit hash.
/// @return A path to a bare git repo containing the specified revision. The path is to be treated as read-only.
Path fetchRevisionIntoCache(const std::string gitUrl, const std::string revision) {
	Path repoDir = getCachePath(gitUrl);

	if (revisionIsInRepo(repoDir, revision)) {
		return repoDir;
	}

	Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Git repository '%s'", gitUrl));
	PathLocks cacheDirLock({repoDir + ".lock"});
	try {
		// Fetch the revision into the local repo
        // When using `git fetch` git tries to detect which revisions we already have and only fetch the ones we dont have. However git only considers revisions that are the ancestor of a reference. We want that git considers every revision we already have. By creating a reference for every revision when we fetch it we can be sure that every revision we have locally is a ancestor of a reference.
        // This is not optimal as we do not remove a reference if we later get a reference to their children. This could lead to a lot of unnecessary references but that is probably not a real problem.
        std::string const referencesForRevisionsPrefix = "refs/__nix_refs_for_revs/" ;
		runProgram("git", true, {"-C", repoDir, "-c", "fetch.negotiationAlgorithm=consecutive", "fetch", "--no-tags", "--recurse-submodules=no","--quiet", "--no-write-fetch-head", "--", gitUrl, revision + ":" + referencesForRevisionsPrefix + revision}, {}, true);
    } catch (Error &e) {
		// Failing the fetch is always fatal. We do not want to continue with a partial repo.
		throw Error("failed to fetch revision '%s' from '%s'", revision, gitUrl);
	}

	return repoDir;
}

/// Get a path to a bare git repo containing the specified revision
///
/// If the cached repo already contains the revision it just returns the path to the repo.
/// Otherwise it fetches the revision into the repo and returns the path to the repo.
///
/// The returned path is a cache dir path without a look. That is fine as long as there are only read-only operations on the fetched revision.
///
/// @param gitUrl A [git url](https://git-scm.com/docs/git-fetch#_git_urls) to the repository.
/// @param revision A git revision. Usually a commit hash.
/// @return A path to a bare git repo containing the specified revision. The path is to be treated as read-only.
std::pair<Path, std::string> getLocalRepoContainingRevision(const std::string gitUrl, const std::string revision, bool local, bool allowDirty,
															bool submodules) {
	if (!local) {
		Path repoDir = fetchRevisionIntoCache(gitUrl, revision);
		return {absPath(repoDir), revision};
	}

	if (!allowDirty) {
		return {absPath(gitUrl), revision};
	}

	auto dirtyDiff = getWorkdirDiff(gitUrl, revision, submodules);
	if (!dirtyDiff.has_value()) {
		// Not dirty
		return {absPath(gitUrl), revision};
	}

	Path repoDir = fetchRevisionIntoCache(gitUrl, revision);
	PathLocks cacheDirLock({repoDir + ".lock"});
	runProgram("git", true, {"-C", repoDir, "read-tree", revision});
	runProgram("git", true, Strings{"-C", repoDir, "apply", "--cached", "--binary", "-"}, dirtyDiff);
	auto treeHash = chomp(runProgram("git", true, {"-C", repoDir, "write-tree"}));

	return {absPath(repoDir), treeHash};
}

/// Copy all files from at a specific revision in a git repo to a target directory
///
/// @param gitDir A path to a git working tree
/// @param targetDir The tree will be placed here
/// @param revision The revision that will get copied
void copyAllFilesFromRevision(const Path &gitDir, const Path &targetDir, const std::string &revision) {
    auto source = sinkToSource([&](Sink &sink) { runProgram2({.program = "git", .args = {"-C", gitDir, "archive", revision}, .standardOut = &sink}); });
	unpackTarfile(*source, targetDir);
}

/// Place the tree of a git repo at a given revision at a given path
///
/// @param url A [git url](https://git-scm.com/docs/git-fetch#_git_urls) to the repository.
/// @param targetDir The path to place the tree at.
/// @param revision A git revision. Usually a commit hash.
/// @param submodules Whether to recursively fetch submodules.
/// @param shallow Whether to accept shallow git repositories.
/// @param isLocal Whether the repo is local or not. If set the repository is not put into cache.
/// @param allowDirty Whether to use a dirty worktree if available.
/// @return A path to a bare git repo containing the specified revision. If the repo is local it is just the path to the repo. The path is to be treated as
/// read-only.
std::pair<Path, bool> placeRevisionTreeAtPath(const std::string &url, const Path &targetDir, const std::string &inputRevision, const bool submodules,
											  const bool shallow, const bool isLocal = false, const bool allowDirty = false) {
	printTalkative("using revision %s of repo '%s'", inputRevision, url);

	auto [gitDir, revision] = getLocalRepoContainingRevision(url, inputRevision, isLocal, allowDirty, submodules);
	auto isLocalAndDirty = isLocal && allowDirty && revision != inputRevision;

	bool isShallow = chomp(runProgram("git", true, {"-C", gitDir, "rev-parse", "--is-shallow-repository"})) == "true";
	if (isShallow && !shallow)
		throw Error("'%s' is a shallow Git repository, but shallow repositories are only allowed when `shallow = true;` is specified.", gitDir);

	if (isLocalAndDirty) {
		if (!fetchSettings.allowDirty)
			throw Error("Git tree '%s' is dirty", url);

		if (fetchSettings.warnDirty)
			warn("Git tree '%s' is dirty", url);
	}

	copyAllFilesFromRevision(gitDir, targetDir, revision);

	// Also fetch and place all gitmodules
	if (submodules) {
		auto gitmodules = readGitmodules(revision, gitDir, isLocalAndDirty ? std::optional<std::string>(url) : std::nullopt);
		for (auto gitmodule : gitmodules) {
			auto submoduleDir = targetDir + "/" + gitmodule.path;
			createDirs(submoduleDir);
			placeRevisionTreeAtPath(gitmodule.url, submoduleDir, gitmodule.revision, submodules, shallow, gitmodule.dirtyLocal, gitmodule.dirtyLocal);
		}
	}

	return {gitDir, isLocalAndDirty};
}

/// Create a result for the fetch function
std::pair<StorePath, Input> makeResult(const Input &input, StorePath &&storePath, const Attrs &infoAttrs, const std::string &revision, bool shallow,
									   bool dirty) {
	Input _input = Input(input);
	if (dirty) {
		_input.attrs.insert_or_assign("dirtyRev", revision + "-dirty");
		// TODO: Think about removing dirtyShortRev. It is not really used and is inconsistent with the non dirty path, as just shortRev does not exist
		_input.attrs.insert_or_assign("dirtyShortRev", revision.substr(0, 7) + "-dirty");
	} else {
		_input.attrs.insert_or_assign("rev", revision);
	}

	if (!shallow)
		_input.attrs.insert_or_assign("revCount", getIntAttr(infoAttrs, "revCount"));
	_input.attrs.insert_or_assign("lastModified", getIntAttr(infoAttrs, "lastModified"));
	return {std::move(storePath), std::move(_input)};
}
} // end namespace

struct GitInputScheme : InputScheme
{
    std::optional<Input> inputFromURL(const ParsedURL & url, bool requireTree) const override
    {
        if (url.scheme != "git" &&
            url.scheme != "git+http" &&
            url.scheme != "git+https" &&
            url.scheme != "git+ssh" &&
            url.scheme != "git+file") return {};

        auto url2(url);
        if (hasPrefix(url2.scheme, "git+")) url2.scheme = std::string(url2.scheme, 4);
        url2.query.clear();

        Attrs attrs;
        attrs.emplace("type", "git");

        for (auto & [name, value] : url.query) {
            if (name == "rev" || name == "ref")
                attrs.emplace(name, value);
            else if (name == "shallow" || name == "submodules" || name == "allRefs")
                attrs.emplace(name, Explicit<bool> { value == "1" });
            else
                url2.query.emplace(name, value);
        }

        attrs.emplace("url", url2.to_string());

        return inputFromAttrs(attrs);
    }

    std::optional<Input> inputFromAttrs(const Attrs & attrs) const override
    {
        if (maybeGetStrAttr(attrs, "type") != "git") return {};

        for (auto & [name, value] : attrs)
            if (name != "type" && name != "url" && name != "ref" && name != "rev" && name != "shallow" && name != "submodules" && name != "lastModified" && name != "revCount" && name != "narHash" && name != "allRefs" && name != "name" && name != "dirtyRev" && name != "dirtyShortRev")
                throw Error("unsupported Git input attribute '%s'", name);

        parseURL(getStrAttr(attrs, "url"));
        maybeGetBoolAttr(attrs, "shallow");
        maybeGetBoolAttr(attrs, "submodules");
        maybeGetBoolAttr(attrs, "allRefs");

        if (auto ref = maybeGetStrAttr(attrs, "ref")) {
            if (std::regex_search(*ref, badGitRefRegex))
                throw BadURL("invalid Git branch/tag name '%s'", *ref);
        }

        Input input;
        input.attrs = attrs;
        return input;
    }

    ParsedURL toURL(const Input & input) const override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme != "git") url.scheme = "git+" + url.scheme;
        if (auto rev = input.getRev()) url.query.insert_or_assign("rev", rev->gitRev());
        if (auto ref = input.getRef()) url.query.insert_or_assign("ref", *ref);
        if (maybeGetBoolAttr(input.attrs, "shallow").value_or(false))
            url.query.insert_or_assign("shallow", "1");
        return url;
    }

    bool hasAllInfo(const Input & input) const override
    {
        bool maybeDirty = !input.getRef();
        bool shallow = maybeGetBoolAttr(input.attrs, "shallow").value_or(false);
        return
            maybeGetIntAttr(input.attrs, "lastModified")
            && (shallow || maybeDirty || maybeGetIntAttr(input.attrs, "revCount"));
    }

    Input applyOverrides(
        const Input & input,
        std::optional<std::string> ref,
        std::optional<Hash> rev) const override
    {
        auto res(input);
        if (rev) res.attrs.insert_or_assign("rev", rev->gitRev());
        if (ref) res.attrs.insert_or_assign("ref", *ref);
        // TODO: I do not understand why this check is required
        if (!res.getRef() && res.getRev())
            throw Error("Git input '%s' has a commit hash but no branch/tag name", res.to_string());
        return res;
    }

    void clone(const Input & input, const Path & destDir) const override
    {
        auto [isLocal, actualUrl] = getActualUrl(getStrAttr(input.attrs, "url"));

        Strings args = {"clone"};

        args.push_back(actualUrl);

        if (auto ref = input.getRef()) {
            args.push_back("--branch");
            args.push_back(*ref);
        }

        if (input.getRev()) throw UnimplementedError("cloning a specific revision is not implemented");

        args.push_back(destDir);

        runProgram("git", true, args, {}, true);
    }

    std::optional<Path> getSourcePath(const Input & input) override
    {
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        if (url.scheme == "file" && !input.getRef() && !input.getRev())
            return url.path;
        return {};
    }

    void markChangedFile(const Input & input, std::string_view file, std::optional<std::string> commitMsg) override
    {
        auto sourcePath = getSourcePath(input);
        assert(sourcePath);
        auto gitDir = ".git";

        runProgram("git", true,
            { "-C", *sourcePath, "--git-dir", gitDir, "add", "--intent-to-add", "--", std::string(file) });

        if (commitMsg)
            runProgram("git", true,
                { "-C", *sourcePath, "--git-dir", gitDir, "commit", std::string(file), "-m", *commitMsg });
    }

	std::pair<bool, std::string> getActualUrl(const std::string &url) const {
		// file:// URIs are normally not cloned (but otherwise treated the
		// same as remote URIs, i.e. we don't use the working tree or
		// HEAD). Exception: If _NIX_FORCE_HTTP is set, or the repo is a bare git
		// repo, treat as a remote URI to force a clone.
		static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1"; // for testing
		auto parsedUrl = parseURL(url);
		bool isBareRepository = parsedUrl.scheme == "file" && !pathExists(parsedUrl.path + "/.git");
		bool isLocal = parsedUrl.scheme == "file" && !forceHttp && !isBareRepository;
		return {isLocal, isLocal ? parsedUrl.path : parsedUrl.base};
	}

	std::pair<StorePath, Input> fetch(ref<Store> store, const Input &input) override {
		// Verify that the hash type is valid if a revision is specified
		if (input.getRev().has_value() && !(input.getRev()->type == htSHA1 || input.getRev()->type == htSHA256)) {
			throw Error("Hash '%s' is not supported by Git. Supported types are sha1 and sha256.", input.getRev()->to_string(Base16, true));
		}

		// Move important attributes to local variables
		std::string name = input.getName();
		std::optional<std::string> reference = input.getRef();
		std::optional<std::string> inputRevision = input.getRev() ? std::optional(input.getRev()->gitRev()) : std::nullopt;

		// Resolve the actual url
		auto [isLocal, actualUrl] = getActualUrl(getStrAttr(input.attrs, "url"));

		// Decide whether we are open to using a dirty local repo
		auto allowDirty = !reference && !inputRevision && isLocal;

		// Resolve reference to revision if necessary
		std::string revision = inputRevision.has_value() ? inputRevision.value() : readRevisionCached(actualUrl, reference, isLocal);

		// Lookup revision in cache and return if it is there
		bool shallow = maybeGetBoolAttr(input.attrs, "shallow").value_or(false);
		bool submodules = maybeGetBoolAttr(input.attrs, "submodules").value_or(false);
		auto cacheType = std::string("git") + (shallow ? "-shallow" : "") + (submodules ? "-submodules" : "");
		if (!allowDirty) {
			if (auto res = getCache()->lookup(store, Attrs({{"name", name}, {"type", cacheType}, {"url", actualUrl}, {"rev", revision}}))) {
				return makeResult(input, std::move(res->second), res->first, revision, shallow, false);
			}
		}

		// Fetch the correct revision (or dirty if we allow it)
		Path tmpDir = createTempDir();
		AutoDelete delTmpDir(tmpDir, true);
		auto [repoDir, isDirty] = placeRevisionTreeAtPath(actualUrl, tmpDir, revision, submodules, shallow, isLocal, allowDirty);

		// Collect infoAttrs
		Attrs infoAttrs({
			{"rev", revision},
			{"lastModified", (isLocal && revision == getEmptyTreeHash(actualUrl))
								 ? 0ull
								 : std::stoull(runProgram("git", true, {"-C", repoDir, "log", "-1", "--format=%ct", "--no-show-signature", revision}))},
		});
		if (!shallow) {
			infoAttrs.insert_or_assign("revCount", std::stoull(runProgram("git", true, {"-C", repoDir, "rev-list", "--count", revision})));
		}

		// Add to store and return
		auto storePath = store->addToStore(name, tmpDir, FileIngestionMethod::Recursive, htSHA256, defaultPathFilter);
		if (!isDirty) {
			getCache()->add(store, Attrs({{"name", name}, {"type", cacheType}, {"url", actualUrl}, {"rev", revision}}), infoAttrs, storePath, true);
		}
		return makeResult(input, std::move(storePath), infoAttrs, revision, shallow, isDirty);
	}
};

static auto rGitInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitInputScheme>()); });

} // namespace nix::fetchers

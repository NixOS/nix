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

namespace nix::fetchers {

namespace {

// Explicit initial branch of our bare repo to suppress warnings from new version of git.
// The value itself does not matter, since we always fetch a specific revision.
// It is set with `-c init.defaultBranch=` instead of `--initial-branch=` to stay compatible with
// old version of git, which will ignore unrecognized `-c` options.
const std::string gitInitialBranch = "__nix_dummy_branch";

bool isCacheFileWithinTtl(time_t now, const struct stat &st) { return st.st_mtime + settings.tarballTtl > now; }

Path getCachePath(std::string_view key) { return getCacheDir() + "/nix/gitv3/" + hashString(htSHA256, key).to_string(Base32, false); }

/// Check if a revision is present in a git repository
///
/// @param gitDir A path to a bare git repository or .git directory
/// @param revision A git revision. Usually a commit hash
/// @return true if the revision is present in the repository
bool checkIfRevisionIsInRepo(const Path gitDir, const std::string revision) {
	try {
		runProgram("git", true, {"-C", gitDir, "cat-file", "-e", revision});
		return true;
	} catch (ExecError &e) {
		if (!WIFEXITED(e.status)) {
			throw;
		}
	}
	return false;

	// // TODO: It is probably faster to check for the object directly instead of calling git.
	// //       However that could lead to problems with shallow repositories but im not sure.
	// std::string path = repoDir + "/objects/" + revision.substr(0, 2) + "/" +
	// revision.substr(2);
}

/// A git revision and the full reference it was resolved from
struct RevisionWithFullReference {
	/// A git revision. Typically a commit hash.
	std::string revision;
	/// A full git reference. Usually starts with 'refs/' except for some special cases like 'HEAD'
	std::string fullReference;
};

/// Resolves the revision and full reference for a given reference in a git repo
///
/// If a abbreviated reference is passed (e.g. 'master') it is also resolved to a full reference (e.g. 'refs/heads/master')
///
/// @param gitUrl A git url that will be used with `git ls-remote` to resolve the revision.
/// @param reference A git reference or HEAD.
/// @return The resolved revision and full reference. nullopt if the reference could not be resolved.
std::optional<RevisionWithFullReference> readRevision(const Path &gitUrl, const std::string &reference) {
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
		return RevisionWithFullReference{revision, fullReference};
	}
	return std::nullopt;
}

/// Resolves the revision and full reference for a given reference in a git repo.
///
/// Tries a lookup in the local git cache first. If the revision is not in the cache it is fetched from the remote.
///
/// If a abbreviated reference is passed (e.g. 'something') it is treated as a heads reference (e.g. 'refs/heads/something').
/// The only supported special reference is 'HEAD'.
///
/// @param gitUrl The url of the git repo. Can be any [git url](https://git-scm.com/docs/git-fetch#_git_urls).
/// @param reference A full git reference or and abbreviated branch (ref/heads) reference.
/// @return The resolved revision and full reference. nullopt if the reference could not be resolved.
std::string readRevisionCached(const std::string &gitUrl, const std::string &reference) {
	Path cacheDir = getCachePath(gitUrl);

	// TODO: Currently every input reference is treated as a /ref/heads reference if it is no full ref.
	// This means that tag references need to be prefixed with 'refs/tags/' otherwise they would not work.
	// We theoretically fully support abbreviated references, but that may break compatibility with older versions
	// On the other hand reference resolution is always impure so it probably can be changed without breaking anything.
	Path fullRef = reference.compare(0, 5, "refs/") == 0 ? reference : reference == "HEAD" ? "HEAD" : "refs/heads/" + reference;

	Path referenceFile = cacheDir + "/" + fullRef;

	struct stat st;
	auto existsInCache = (stat(referenceFile.c_str(), &st) == 0);

	std::optional<std::string> cachedRevision;
	if (existsInCache) {
		time_t now = time(0);
		auto cacheIsFresh = isCacheFileWithinTtl(now, st);

		if (cacheIsFresh) {
			// Returning a cached revision if available and fresh
			// FIXME: If the repo is local we dont have to call git but can just read the ref file
			std::string cachedRevision = trim(readFile(referenceFile));

			debug("using cached revision '%s' for reference '%s' in repo '%s'", cachedRevision, fullRef, gitUrl);
			return cachedRevision;
		}

		auto revisionWithFullReference = readRevision(gitUrl, reference);
		if (revisionWithFullReference) {
			// Returning a fresh revision if the cache exists but is expired
			createDirs(dirOf(cacheDir));
			PathLocks cacheDirLock({cacheDir + ".lock"});
			if (!pathExists(cacheDir)) {
				runProgram("git", true, {"-c", "init.defaultBranch=" + gitInitialBranch, "init", "--bare", cacheDir});
			}
			createDirs(dirOf(referenceFile));
			auto const revision = revisionWithFullReference->revision;
			writeFile(referenceFile, revision);
			return revision;
		}

		warn("failed to resolve a fresh revision for reference '%s' in repo '%s'; using expired cached revision '%s'", fullRef, gitUrl, *cachedRevision);
		auto cachedRevision = trim(readFile(referenceFile));
		return cachedRevision;
	}

	// Return a fresh revision if there is none in the cache
	auto const revisionWithFullReference = readRevision(gitUrl, reference);
	if (!revisionWithFullReference) {
		throw Error("failed to resolve revision for reference '%s' in repository '%s'", fullRef, gitUrl);
	}

	createDirs(dirOf(cacheDir));
	PathLocks cacheDirLock({cacheDir + ".lock"});
	if (!pathExists(cacheDir)) {
		runProgram("git", true, {"-c", "init.defaultBranch=" + gitInitialBranch, "init", "--bare", cacheDir});
	}
	createDirs(dirOf(referenceFile));

	auto const revision = revisionWithFullReference->revision;
	writeFile(referenceFile, revision);
	return revision;
}

/// Helper function to split a string into a vector of strings
///
/// @param input The string to split
/// @param delimiter The delimiter to split at
/// @return A vector of strings
std::vector<std::string_view> split(const std::string_view &input, const std::string_view &delimiter) {
	size_t start = 0, end;
	auto delimiterLength = delimiter.length();
	std::vector<std::string_view> res;

	while ((end = input.find(delimiter, start)) != std::string_view::npos) {
		std::string_view token = input.substr(start, end - start);
		start = end + delimiterLength;
		res.push_back(token);
	}

	res.push_back(input.substr(start));
	return res;
}

/// Information about a git submodule
///
/// This should contain all information for that is required to fetch the submodule
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
};

/// Extracts information about the submodules at a given revision in a git repo
///
/// @param revision A git revision. Usually a commit hash.
/// @param gitDir A path to a bare git repository or .git directory
/// @return A vector of submodule information. Empty if there are no submodules.
std::vector<SubmoduleInfo> readGitmodules(const std::string &revision, const Path &gitDir) {
	auto gitmodulesBlob = revision + ":.gitmodules";
	// Run ls-remote to find a revision for the reference
	auto [status, output] = runProgram(RunOptions{
		.program = "git",
		.args = {"-C", gitDir, "config", "--blob", gitmodulesBlob, "--name-only", "--get-regexp", "path"},
		.isInteractive = true,
	});
	if (status != 0) {
		return {};
	}

	std::vector<std::string> submoduleNames;
	std::string_view sv = output;
	auto lines = split(sv, "\n");
	for (auto line : lines) {
		if (line.length() == 0) {
			continue;
		}
		const static std::regex line_regex("^submodule[.](.+)[.]path$");
		std::match_results<std::string_view::const_iterator> match;
		if (!std::regex_match(line.cbegin(), line.cend(), match, line_regex)) {
			throw Error(".gitmodules file seems invalid");
		}

		if (match[1].length() == 0) {
			throw Error("Gitmodules contains empty line");
		}

		auto submoduleName = match[1];
		submoduleNames.push_back(submoduleName);
	}

	std::vector<SubmoduleInfo> submodules;
	for (auto submoduleName : submoduleNames) {
		std::string path;
		std::string url;
		std::string submoduleRevision;

		{
			auto [status, output] = runProgram(RunOptions{
				.program = "git",
				.args = {"-C", gitDir, "config", "--blob", gitmodulesBlob, "--get", "submodule." + submoduleName + ".path"},
				.isInteractive = true,
			});
			if (status != 0) {
				throw Error("Failed to read .gitmodules");
			}
			std::string_view lines = output;
			auto line = lines.substr(0, lines.find("\n"));

			if (line.length() == 0) {
				throw Error("Length of the path should not be 0");
			}

			path = line;
		}

		{
			auto [status, output] = runProgram(RunOptions{
				.program = "git",
				.args = {"-C", gitDir, "config", "--blob", gitmodulesBlob, "--get", "submodule." + submoduleName + ".url"},
				.isInteractive = true,
			});
			if (status != 0) {
				throw Error("Failed to read .gitmodules");
			}
			std::string_view lines = output;
			auto line = lines.substr(0, lines.find("\n"));

			if (line.length() == 0) {
				throw Error("Length of the url should not be 0");
			}

			url = line;

			if (url.rfind("./", 0) == 0 || url.rfind("../", 0) == 0) {
				// If the submodule is relative its URL is relative to the origin of the superrepo
				auto [status, output] = runProgram(RunOptions{
					.program = "git",
					.args = {"-C", gitDir, "remote", "get-url", "origin"},
					.isInteractive = true,
				});

				std::string_view lines = output;
				auto line = lines.substr(0, lines.find("\n"));

				auto relativeSubmoduleRoot = line.length() ? line : gitDir;

				url = relativeSubmoduleRoot + "/" + url;
			}
		}

		{
			auto [status, output] = runProgram(RunOptions{
				.program = "git",
				.args = {"-C", gitDir, "ls-tree", "--object-only", revision, path},
				.isInteractive = true,
			});
			if (status != 0) {
				throw Error("Failed to get revision for submodule");
			}
			std::string_view lines = output;
			auto line = lines.substr(0, lines.find("\n"));

			if (line.length() == 0) {
				throw Error("Length of the revision should not be 0");
			}

			submoduleRevision = line;
		}

		SubmoduleInfo info({submoduleName, url, path, submoduleRevision});

		submodules.push_back(info);
	}

	return submodules;
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
Path getLocalRepoContainingRevision(const std::string gitUrl, const std::string revision) {
	Path cacheDir = getCachePath(gitUrl);
	std::string const repoDir = cacheDir;

	// Create the repo if it does not exist
	createDirs(dirOf(cacheDir));
	PathLocks cacheDirLock({cacheDir + ".lock"});
	if (!pathExists(cacheDir)) {
		runProgram("git", true, {"-c", "init.defaultBranch=" + gitInitialBranch, "init", "--bare", repoDir});
	}

	// Return if the revision is already in the repo
	if (checkIfRevisionIsInRepo(repoDir, revision)) {
		return repoDir;
	}

	Activity act(*logger, lvlTalkative, actUnknown, fmt("fetching Git repository '%s'", gitUrl));

	// FIXME: git stderr messes up our progress indicator, so
	// we're using --quiet for now. Should process its stderr.
	try {
		// Fetch the revision into the local repo
		runProgram("git", true, {"-C", repoDir, "fetch", "--no-tags", "--quiet", "--force", "--", gitUrl, revision}, {}, true);
	} catch (Error &e) {
		// Failing the fetch is always fatal.
		//
		// Previously there was a check here for whether a old version of the reference was in the
		// repositiory. In that case a warning was printed and the old reference was used later on.
		// In the current implementation references are cached in the revision resolution step.
		//
		// However in the previous implementation the whole reference was always fetched when the
		// even when specific revision was requested. This lead to overfetching revisions and putting
		// them into the cache. This may have lead to more cache hits, but I assume in most cases it
		// just resulted in increase network traffic and disk usage.
		throw Error("Failed to fetch revision '%s' from '%s'", revision, gitUrl);
	}

	return repoDir;
}

/// Place the tree of a git repo at a given revision at a given path
///
/// @param url A [git url](https://git-scm.com/docs/git-fetch#_git_urls) to the repository.
/// @param targetDir The path to place the tree at.
/// @param revision A git revision. Usually a commit hash.
/// @param submodules Whether to recursively fetch submodules.
/// @param shallow Whether to accept shallow git repositories.
/// @param isLocal Whether the repo is local or not. If set the repository is not put into cache.
/// @return A path to a bare git repo containing the specified revision. If the repo is local it is just the path to the repo. The path is to be treated as
/// read-only.
Path placeRevisionTreeAtPath(const std::string &url, const Path &targetDir, const std::string &revision, const bool submodules, const bool shallow,
							 const bool isLocal = false) {
	printTalkative("using revision %s of repo '%s'", revision, url);

	Path gitDir = isLocal ? url : getLocalRepoContainingRevision(url, revision);

	bool isShallow = chomp(runProgram("git", true, {"-C", gitDir, "rev-parse", "--is-shallow-repository"})) == "true";
	if (isShallow && !shallow)
		throw Error("'%s' is a shallow Git repository, but shallow repositories are only allowed when `shallow = true;` is specified.", gitDir);

	// FIXME: should pipe this, or find some better way to extract a
	// revision.
	auto source = sinkToSource([&](Sink &sink) { runProgram2({.program = "git", .args = {"-C", gitDir, "archive", revision}, .standardOut = &sink}); });
	unpackTarfile(*source, targetDir);

	// Also fetch and place all gitmodules
	if (submodules) {
		auto gitmodules = readGitmodules(revision, gitDir);
		for (auto gitmodule : gitmodules) {
			auto submoduleDir = targetDir + "/" + gitmodule.path;
			createDirs(submoduleDir);
			placeRevisionTreeAtPath(gitmodule.url, submoduleDir, gitmodule.revision, submodules, shallow);
		}
	}

	return gitDir;
}

/// Get the revision for a given reference (or HEAD)
///
/// Set noCache to true to skip a lookup in the local git cache
/// This is the expected behaviour for local repositories
///
/// @param revision A git revision. Usually a commit hash.
/// @param reference A git reference (branch or tag name) or HEAD. If unset, `HEAD` is used.
/// @param gitUrl A git url that will be used with `git ls-remote` to resolve the revision.
/// @param noCache Do not use the local git cache. Useful for local repositories.
/// @return The resolved revision. If `revision` is set it is returned. Otherwise `reference` is resolved to a revision.
std::string getRevision(const std::optional<std::string> &revision, const std::optional<std::string> &reference, const std::string &gitUrl, bool noCache) {
	if (revision) {
		return revision.value();
	}

	auto const referenceOrHead = reference.value_or("HEAD");

	if (noCache) {
		auto const localRevision = readRevision(gitUrl, referenceOrHead);
		if (!localRevision) {
			throw Error("Could not find revision for reference '%s' in repository '%s'", referenceOrHead, gitUrl);
		}
		return localRevision->revision;
	}

	auto const gotRevision = readRevisionCached(gitUrl, referenceOrHead);

	return gotRevision;
}

std::pair<StorePath, Input> makeResult(const Input &input, StorePath &&storePath, const Attrs &infoAttrs, const std::string &revision, bool shallow) {
	Input _input = Input(input);
	_input.attrs.insert_or_assign("rev", revision);
	if (!shallow)
		_input.attrs.insert_or_assign("revCount", getIntAttr(infoAttrs, "revCount"));
	_input.attrs.insert_or_assign("lastModified", getIntAttr(infoAttrs, "lastModified"));
	return {std::move(storePath), std::move(_input)};
}

/// Check if a git workdir is dirty
///
/// @param workDir A path to a git repo or workdir
/// @param submodules Whether to check submodules as well
/// @param gitDir Optional path to a .git directory. Defaults to .git
/// @return true if the workdir is dirty
bool isWorkdirDirty(const Path &workDir, bool submodules, const Path &gitDir = ".git") {
	// If there is no HEAD untracked files should mark the tree as dirty.
	// If there is a HEAD untracked files should not mark the tree as dirty.
	// TODO: Remove the inconsistent behaviour
	auto headExists = readRevision(workDir, "HEAD").has_value();
	std::string untrackedFlag = headExists ? "-uno" : "-unormal";

	// Using git status --porcelain is preferrable here because it also works with a HEAD pointing to unborn branches.
	// This way we do not have to check for unborn HEAD seperatly.
	auto gitStatusOps = Strings({"-C", workDir, "--git-dir", gitDir, "status", "--porcelain", untrackedFlag});
	if (!submodules) {
		// Changes in submodules should only make the tree dirty
		// when those submodules will be copied as well.
		gitStatusOps.emplace_back("--ignore-submodules");
	}

	auto [status, output] = runProgram(RunOptions{
		.program = "git",
		.args = gitStatusOps,
		.isInteractive = true,
	});

	return chomp(output).length() > 0;
}

/// Fetch git repo from the workdir
std::pair<StorePath, Input> fetchFromWorkdir(ref<Store> store, const Input &input, const Path &workDir, bool submodules, const Path &gitDir = ".git") {
	/// TODO: git stash create can probably be used to integrate this branch with the stash
	Input _input = Input(input);
	if (!fetchSettings.allowDirty)
		throw Error("Git tree '%s' is dirty", workDir);

	if (fetchSettings.warnDirty)
		warn("Git tree '%s' is dirty", workDir);

	Path actualPath(absPath(workDir));

	auto gitOpts = Strings({"-C", actualPath, "--git-dir", gitDir, "ls-files", "-z"});
	if (submodules)
		gitOpts.emplace_back("--recurse-submodules");

	auto files = tokenizeString<std::set<std::string>>(runProgram("git", true, gitOpts), "\0"s);

	PathFilter filter = [&](const Path &p) -> bool {
		assert(hasPrefix(p, actualPath));
		std::string file(p, actualPath.size() + 1);

		auto st = lstat(p);

		if (S_ISDIR(st.st_mode)) {
			auto prefix = file + "/";
			auto i = files.lower_bound(prefix);
			return i != files.end() && hasPrefix(*i, prefix);
		}

		return files.count(file);
	};

	auto storePath = store->addToStore(input.getName(), actualPath, FileIngestionMethod::Recursive, htSHA256, filter);

	try {
		// FIXME: maybe we should use the timestamp of the last modified dirty file?
		_input.attrs.insert_or_assign(
			"lastModified",
			std::stoull(runProgram("git", true, {"-C", actualPath, "--git-dir", gitDir, "log", "-1", "--format=%ct", "--no-show-signature", "HEAD"})));
		_input.attrs.insert_or_assign("dirtyRev",
			chomp(runProgram("git", true, {"-C", actualPath, "--git-dir", gitDir, "rev-parse", "--verify", "HEAD"})) + "-dirty");
		_input.attrs.insert_or_assign(
			"dirtyShortRev", chomp(runProgram("git", true, {"-C", actualPath, "--git-dir", gitDir, "rev-parse", "--verify", "--short", "HEAD"})) + "-dirty");
	} catch (Error &e) {
		// This path is used if the default branch is unborn. This leads to HEAD not existing. This is usually directly after git init when no commit exists.
		// I do not think that this case is used a lot in the wild.
		//
		// The behaviour for git repos without commits is also inconsistent to the behaviour for git repos with commits.
		// Without commits even untracked files mark the repo as dirty, with commits they do not.
		//
		// TODO: We could avoid a lot of weirdness by just forbidding fetching empty git repositories.
		_input.attrs.insert_or_assign("lastModified", 0ull);
	}

	return {std::move(storePath), _input};
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
        auto [isLocal, actualUrl] = getActualUrl(input);

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

    std::pair<bool, std::string> getActualUrl(const Input & input) const
    {
        // file:// URIs are normally not cloned (but otherwise treated the
        // same as remote URIs, i.e. we don't use the working tree or
        // HEAD). Exception: If _NIX_FORCE_HTTP is set, or the repo is a bare git
        // repo, treat as a remote URI to force a clone.
        static bool forceHttp = getEnv("_NIX_FORCE_HTTP") == "1"; // for testing
        auto url = parseURL(getStrAttr(input.attrs, "url"));
        bool isBareRepository = url.scheme == "file" && !pathExists(url.path + "/.git");
        bool isLocal = url.scheme == "file" && !forceHttp && !isBareRepository;
        return {isLocal, isLocal ? url.path : url.base};
    }

	std::pair<StorePath, Input> fetch(ref<Store> store, const Input &input) override {
		std::string name = input.getName();
		auto [isLocal, actualUrl] = getActualUrl(input);

		// Verify that the hash type is valid if a revision is specified
		if (input.getRev().has_value() && !(input.getRev()->type == htSHA1 || input.getRev()->type == htSHA256)) {
			throw Error("Hash '%s' is not supported by Git. Supported types are sha1 and sha256.", input.getRev()->to_string(Base16, true));
		}

		std::optional<std::string> reference = input.getRef();
		std::optional<std::string> inputRevision = input.getRev() ? std::optional(input.getRev()->gitRev()) : std::nullopt;

		bool shallow = maybeGetBoolAttr(input.attrs, "shallow").value_or(false);
		bool submodules = maybeGetBoolAttr(input.attrs, "submodules").value_or(false);
		auto cacheType = std::string("git") + (shallow ? "-shallow" : "") + (submodules ? "-submodules" : "");

		// If this is a local directory and no ref or revision is given, allow
		// fetching directly from a dirty workdir.
		if (!reference && !inputRevision && isLocal) {
			auto dirty = isWorkdirDirty(actualUrl, submodules);
			if (dirty) {
				return fetchFromWorkdir(store, input, actualUrl, submodules);
			}
		}

		std::string revision = getRevision(inputRevision, reference, actualUrl, isLocal);

		// Lookup resolved revision in cache
		if (auto res = getCache()->lookup(store, Attrs({{"name", name}, {"type", cacheType}, {"url", actualUrl}, {"rev", revision}}))) {
			return makeResult(input, std::move(res->second), res->first, revision, shallow);
		}

		Path tmpDir = createTempDir();
		AutoDelete delTmpDir(tmpDir, true);
		auto repoDir = placeRevisionTreeAtPath(actualUrl, tmpDir, revision, submodules, shallow, isLocal);
		auto storePath = store->addToStore(name, tmpDir, FileIngestionMethod::Recursive, htSHA256, defaultPathFilter);

		auto lastModified = std::stoull(runProgram("git", true, {"-C", repoDir, "log", "-1", "--format=%ct", "--no-show-signature", revision}));

		Attrs infoAttrs({
			{"rev", revision},
			{"lastModified", lastModified},
		});

		if (!shallow)
			infoAttrs.insert_or_assign("revCount", std::stoull(runProgram("git", true, {"-C", repoDir, "rev-list", "--count", revision})));

		getCache()->add(store, Attrs({{"name", name}, {"type", cacheType}, {"url", actualUrl}, {"rev", revision}}), infoAttrs, storePath, true);

		return makeResult(input, std::move(storePath), infoAttrs, revision, shallow);
	}
};

static auto rGitInputScheme = OnStartup([] { registerInputScheme(std::make_unique<GitInputScheme>()); });

} // namespace nix::fetchers

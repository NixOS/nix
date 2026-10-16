#include "nix/fetchers/git-lfs-fetch.hh"
#include "nix/fetchers/git-utils.hh"
#include "fetchers-config-private.hh"
#include "nix/store/filetransfer.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/file-system.hh"
#include "nix/util/os-string.hh"
#include "nix/util/processes.hh"
#include "nix/util/url.hh"
#include "nix/util/users.hh"
#include "nix/util/util.hh"
#include "nix/util/hash.hh"
#include "nix/util/json-utils.hh"
#include "nix/store/ssh.hh"
#include "nix/util/deleter.hh"

#include "nix/util/base-n.hh"

#include <git2/attr.h>
#include <git2/config.h>
#include <git2/errors.h>

#include <nlohmann/json.hpp>

#include <array>
#include <span>

namespace nix::lfs {

static void downloadToSink(
    const std::string & url,
    const std::optional<std::string> & authHeader,
    Sink & sink,
    std::string sha256Expected,
    size_t sizeExpected)
{
    FileTransferRequest request(parseURL(url));
    Headers headers;
    if (authHeader.has_value())
        headers.push_back({"Authorization", *authHeader});
    request.headers = headers;

    HashSink hashSink(HashAlgorithm::SHA256);
    TeeSink teeSink(hashSink, sink);

    getFileTransfer()->download(std::move(request), teeSink);

    auto hashResult = hashSink.finish();

    if (sizeExpected != hashResult.numBytesDigested)
        throw Error(
            "size mismatch while fetching %s: expected %d but got %d", url, sizeExpected, hashResult.numBytesDigested);

    auto sha256Actual = hashResult.hash.to_string(HashFormat::Base16, false);
    if (sha256Actual != sha256Expected)
        throw Error(
            "hash mismatch while fetching %s: expected sha256:%s but got sha256:%s", url, sha256Expected, sha256Actual);
}

LfsApiInfo getLfsApi(ParsedURL url)
{
    assert(url.authority.has_value());
    if (url.scheme == "ssh") {
        auto args = getNixSshOpts();

        if (url.authority->port)
            args.push_back(string_to_os_string(fmt("-p%d", *url.authority->port)));

        std::ostringstream hostnameAndUser;
        if (url.authority->user)
            hostnameAndUser << *url.authority->user << "@";
        hostnameAndUser << url.authority->host;
        args.push_back(string_to_os_string(std::move(hostnameAndUser).str()));

        args.push_back(OS_STR("--"));
        args.push_back(OS_STR("git-lfs-authenticate"));
        // FIXME %2F encode slashes? Does this command take/accept percent encoding?
        args.push_back(string_to_os_string(url.renderPath(/*encode=*/false)));
        args.push_back(OS_STR("download"));

        auto [status, output] = runProgram({{.program = sshProgram(), .args = args}});

        if (output.empty())
            throw Error(
                "git-lfs-authenticate: no output (cmd: '%s %s')",
                sshProgram().string(),
                concatMapStringsSep(
                    " ", args, [](const OsString & s) { return escapeShellArgAlways(os_string_to_string(s)); }));

        auto queryResp = nlohmann::json::parse(output);
        auto headerIt = queryResp.find("header");
        if (headerIt == queryResp.end())
            throw Error("no header in git-lfs-authenticate response");
        auto authIt = headerIt->find("Authorization");
        if (authIt == headerIt->end())
            throw Error("no Authorization in git-lfs-authenticate response");

        return {queryResp.at("href").get<std::string>(), authIt->get<std::string>()};
    }

    /**
     * Try to mimic what git-lfs will do to plain remotes
     * https://github.com/git-lfs/git-lfs/blob/main/docs/api/server-discovery.md
     *
     * Try to be smarter with remotes ending in a /, like
     * `https://github.com/NixOS/nix/`. This should be
     * `https://github.com/NixOS/nix.git/info/lfs`, not
     * `https://github.com/NixOS/nix/.git/info/lfs`
     */
    bool hasDotGit = false;
    for (auto it = url.path.rbegin(); it != url.path.rend(); ++it) {
        if (it->empty())
            continue;
        if (!it->ends_with(".git"))
            *it += ".git";
        hasDotGit = true;
        break;
    }
    if (!hasDotGit) {
        if (url.path.size() > 1) // e.g. {"", ""} (single trailing slash)
            url.path.back() = ".git";
        else if (url.path.size() == 1) // {""}
            url.path.push_back(".git");
        else { // {}
            url.path.push_back("");
            url.path.push_back(".git");
        }
    }
    if (url.path.back().empty())
        url.path.back() = "info";
    else
        url.path.push_back("info");
    url.path.push_back("lfs");

    return {url.to_string(), std::nullopt};
}

typedef std::unique_ptr<git_config, Deleter<git_config_free>> GitConfig;
typedef std::unique_ptr<git_config_entry, Deleter<git_config_entry_free>> GitConfigEntry;

static std::string getLfsEndpointUrl(git_repository * repo)
{
    GitConfig config;
    if (!git_repository_config(Setter(config), repo)) {
        GitConfigEntry entry;
        if (!git_config_get_entry(Setter(entry), config.get(), "lfs.url")) {
            auto value = std::string(entry->value);
            if (!value.empty()) {
                debug("Found explicit lfs.url value: %s", value);
                return value;
            }
        }

        // Preserve the configured remote URL verbatim. git_remote_url() expands
        // url.*.insteadOf rules, which may rewrite Git transport URLs to an
        // endpoint that does not serve the repository's LFS API.
        if (!git_config_get_entry(Setter(entry), config.get(), "remote.origin.url"))
            return std::string(entry->value);
    }

    return "";
}

static std::optional<std::string> getGitCredentialAuthHeader(const ParsedURL & url)
{
    if (!url.authority)
        return std::nullopt;

    Pipe input;
    Pipe output;
    input.create();
    output.create();

    const std::array<FdRedirection, 2> redirects = {{
        {.from = input.readSide.get(), .to = FdRedirection::stdInput},
        {.from = output.writeSide.get(), .to = FdRedirection::stdOut},
    }};
    auto pid = spawnProgram(
        {.program = GIT_PROGRAM, .args = {OS_STR("credential"), OS_STR("fill")}}, redirects);

    input.readSide.close();
    output.writeSide.close();

    auto host = url.authority->host;
    if (url.authority->port)
        host += fmt(":%d", *url.authority->port);

    auto credentialRequest = fmt("protocol=%s\nhost=%s\n", url.scheme, host);
    auto path = url.renderPath(/*encode=*/false);
    if (!path.empty()) {
        if (path.front() == '/')
            path.erase(0, 1);
        credentialRequest += fmt("path=%s\n", path);
    }
    credentialRequest += '\n';
    writeFull(input.writeSide.get(), credentialRequest);
    input.writeSide.close();

    auto credentials = drainFD(output.readSide.get());
    if (!statusOk(pid.wait()))
        return std::nullopt;

    std::optional<std::string> username;
    std::optional<std::string> password;
    for (const auto & line : tokenizeString<Strings>(credentials, "\n")) {
        const auto separator = line.find('=');
        if (separator == std::string::npos)
            continue;
        const auto key = std::string_view(line).substr(0, separator);
        const auto value = line.substr(separator + 1);
        if (key == "username")
            username = value;
        else if (key == "password")
            password = value;
    }
    if (!username || !password)
        return std::nullopt;

    const auto userpass = fmt("%s:%s", *username, *password);
    return fmt(
        "Basic %s",
        base64::encode(std::as_bytes(std::span<const char>{userpass.data(), userpass.size()})));
}

static std::optional<Pointer> parseLfsPointer(std::string_view content, std::string_view filename)
{
    // https://github.com/git-lfs/git-lfs/blob/2ef4108/docs/spec.md
    //
    // example git-lfs pointer file:
    // version https://git-lfs.github.com/spec/v1
    // oid sha256:f5e02aa71e67f41d79023a128ca35bad86cf7b6656967bfe0884b3a3c4325eaf
    // size 10000000
    // (ending \n)

    if (!content.starts_with("version ")) {
        // Invalid pointer file
        return std::nullopt;
    }

    if (!content.starts_with("version https://git-lfs.github.com/spec/v1")) {
        // In case there's new spec versions in the future, but for now only v1 exists
        debug("Invalid version found on potential lfs pointer file, skipping");
        return std::nullopt;
    }

    std::string oid;
    std::string size;

    for (auto & line : tokenizeString<Strings>(content, "\n")) {
        if (line.starts_with("version ")) {
            continue;
        }
        if (line.starts_with("oid sha256:")) {
            oid = line.substr(11); // skip "oid sha256:"
            continue;
        }
        if (line.starts_with("size ")) {
            size = line.substr(5); // skip "size "
            continue;
        }

        debug("Custom extension '%s' found, ignoring", line);
    }

    if (oid.length() != 64 || !std::all_of(oid.begin(), oid.end(), ::isxdigit)) {
        debug("Invalid sha256 %s, skipping", oid);
        return std::nullopt;
    }

    if (size.length() == 0 || !std::all_of(size.begin(), size.end(), ::isdigit)) {
        debug("Invalid size %s, skipping", size);
        return std::nullopt;
    }

    return std::make_optional(Pointer{oid, std::stoul(size)});
}

Fetch::Fetch(git_repository * repo, git_oid rev)
{
    this->repo = repo;
    this->rev = rev;

    const auto remoteUrl = lfs::getLfsEndpointUrl(repo);

    this->url = nix::fixGitURL(remoteUrl).canonicalise();
}

bool Fetch::shouldFetch(const CanonPath & path) const
{
    const char * attr = nullptr;
    git_attr_options opts = GIT_ATTR_OPTIONS_INIT;
    opts.attr_commit_id = this->rev;
    opts.flags = GIT_ATTR_CHECK_INCLUDE_COMMIT | GIT_ATTR_CHECK_NO_SYSTEM;
    if (git_attr_get_ext(&attr, (git_repository *) (this->repo), &opts, path.rel_c_str(), "filter"))
        throw Error("cannot get git-lfs attribute: %s", git_error_last()->message);
    debug("Git filter for '%s' is '%s'", path, attr ? attr : "null");
    return attr != nullptr && !std::string(attr).compare("lfs");
}

static nlohmann::json pointerToPayload(const std::vector<Pointer> & items)
{
    nlohmann::json jArray = nlohmann::json::array();
    for (const auto & pointer : items)
        jArray.push_back({{"oid", pointer.oid}, {"size", pointer.size}});
    return jArray;
}

std::vector<nlohmann::json> Fetch::fetchUrls(const std::vector<Pointer> & pointers) const
{
    auto api = lfs::getLfsApi(this->url);
    auto url = api.endpoint + "/objects/batch";
    const auto & authHeader = api.authHeader;
    nlohmann::json oidList = pointerToPayload(pointers);
    nlohmann::json data = {{"operation", "download"}};
    data["objects"] = oidList;
    auto payload = data.dump();
    auto uploadBatch = [&](const std::optional<std::string> & authorization) {
        FileTransferRequest request(parseURL(url));
        request.method = HttpMethod::Post;
        Headers headers;
        if (authorization.has_value())
            headers.push_back({"Authorization", *authorization});
        headers.push_back({"Content-Type", "application/vnd.git-lfs+json"});
        headers.push_back({"Accept", "application/vnd.git-lfs+json"});
        request.headers = headers;
        StringSource source{payload};
        request.data = {source};
        return getFileTransfer()->upload(request);
    };

    std::optional<FileTransferResult> result;
    try {
        result = uploadBatch(authHeader);
    } catch (const FileTransferError & error) {
        if (authHeader.has_value() || error.error != FileTransfer::Unauthorized)
            throw;

        if (!credentialHelperTried) {
            credentialHelperTried = true;
            credentialAuthHeader = getGitCredentialAuthHeader(this->url);
        }
        if (!credentialAuthHeader)
            throw;

        result = uploadBatch(credentialAuthHeader);
    }
    auto responseString = result->data;

    std::vector<nlohmann::json> objects;
    // example resp here:
    // {"objects":[{"oid":"f5e02aa71e67f41d79023a128ca35bad86cf7b6656967bfe0884b3a3c4325eaf","size":10000000,"actions":{"download":{"href":"https://gitlab.com/b-camacho/test-lfs.git/gitlab-lfs/objects/f5e02aa71e67f41d79023a128ca35bad86cf7b6656967bfe0884b3a3c4325eaf","header":{"Authorization":"Basic
    // Yi1jYW1hY2hvOmV5SjBlWEFpT2lKS1YxUWlMQ0poYkdjaU9pSklVekkxTmlKOS5leUprWVhSaElqcDdJbUZqZEc5eUlqb2lZaTFqWVcxaFkyaHZJbjBzSW1wMGFTSTZJbUptTURZNFpXVTFMVEprWmpVdE5HWm1ZUzFpWWpRMExUSXpNVEV3WVRReU1qWmtaaUlzSW1saGRDSTZNVGN4TkRZeE16ZzBOU3dpYm1KbUlqb3hOekUwTmpFek9EUXdMQ0psZUhBaU9qRTNNVFEyTWpFd05EVjkuZk9yMDNkYjBWSTFXQzFZaTBKRmJUNnJTTHJPZlBwVW9lYllkT0NQZlJ4QQ=="}}},"authenticated":true}]}

    try {
        auto resp = nlohmann::json::parse(responseString);
        if (resp.contains("objects"))
            objects.insert(objects.end(), resp["objects"].begin(), resp["objects"].end());
        else
            throw Error("response does not contain 'objects'");

        return objects;
    } catch (const nlohmann::json::parse_error & e) {
        printMsg(lvlTalkative, "Full response: '%1%'", responseString);
        throw Error("response did not parse as json: %s", e.what());
    }
}

void Fetch::fetch(
    const std::string & content,
    const CanonPath & pointerFilePath,
    Sink & sink,
    std::function<void(uint64_t)> sizeCallback) const
{
    debug("trying to fetch '%s' using git-lfs", pointerFilePath);

    if (content.length() >= 1024) {
        warn("encountered file '%s' that should have been a git-lfs pointer, but is too large", pointerFilePath);
        sizeCallback(content.length());
        sink(content);
        return;
    }

    const auto pointer = parseLfsPointer(content, pointerFilePath.rel());
    if (pointer == std::nullopt) {
        warn("encountered file '%s' that should have been a git-lfs pointer, but is invalid", pointerFilePath);
        sizeCallback(content.length());
        sink(content);
        return;
    }

    auto cacheDir = getCacheDir() / "git-lfs";
    std::string key = hashString(HashAlgorithm::SHA256, pointerFilePath.rel()).to_string(HashFormat::Base16, false)
                      + "/" + pointer->oid;
    auto cachePath = cacheDir / key;
    AutoCloseFD cacheFile(openFileReadonly(cachePath, FinalSymlink::DontFollow));
    if (cacheFile) {
        debug("using cache entry %s -> %s", key, PathFmt(cachePath));
        FdSource cacheSource(cacheFile.get());
        auto size = getFileSize(cacheFile.get());
        sizeCallback(size);
        cacheSource.drainInto(sink, size);
        return;
    }
    debug("did not find cache entry for %s", key);

    std::vector<Pointer> pointers;
    pointers.push_back(pointer.value());
    const auto objUrls = fetchUrls(pointers);

    const auto obj = objUrls[0];
    try {
        // Use the committed pointer's oid/size for integrity, not server's claim
        std::string sha256 = pointer->oid;
        std::string ourl = obj.at("actions").at("download").at("href");
        auto authHeader = [&]() -> std::optional<std::string> {
            const auto & download = obj.at("actions").at("download");
            auto headerIt = download.find("header");
            if (headerIt == download.end())
                return std::nullopt;
            auto authIt = headerIt->find("Authorization");
            if (authIt == headerIt->end())
                return std::nullopt;
            return std::string(*authIt);
        }();
        const uint64_t size = pointer->size;

        auto objOid = getString(valueAt(getObject(obj), "oid"));
        auto objSize = getUnsigned(valueAt(getObject(obj), "size"));
        if (objOid != pointer->oid || objSize != pointer->size) {
            throw Error(
                "LFS server returned mismatched oid/size for '%s' (got oid=%s size=%d, expected oid=%s size=%d)",
                pointerFilePath,
                objOid,
                objSize,
                pointer->oid,
                pointer->size);
        }

        debug("creating cache entry %s -> %s", key, PathFmt(cachePath));

        if (!pathExists(cachePath.parent_path()))
            createDirs(cachePath.parent_path());
        auto [tempFile, tempPath] = createTempFile(cachePath.parent_path(), {});
        AutoDelete tempDeleter(tempPath);
        FdSink tempSink(tempFile.get());
        downloadToSink(ourl, authHeader, tempSink, sha256, size);
        tempSink.flush();

        std::filesystem::rename(tempPath, cachePath);
        tempDeleter.cancel();

        FdSource cacheSource(tempFile.get());
        cacheSource.restart();
        sizeCallback(size);
        cacheSource.drainInto(sink, size);

        debug("%s fetched with git-lfs", pointerFilePath);
    } catch (const nlohmann::json::out_of_range & e) {
        throw Error("bad json from /info/lfs/objects/batch: %s %s", obj, e.what());
    }
}

} // namespace nix::lfs

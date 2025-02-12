#include "git-lfs-fetch.hh"
#include "git-utils.hh"
#include "filetransfer.hh"
#include "processes.hh"
#include "url.hh"
#include "users.hh"
#include "hash.hh"

#include <git2/attr.h>
#include <git2/config.h>
#include <git2/errors.h>
#include <git2/remote.h>

#include <nlohmann/json.hpp>

namespace nix::lfs {

// if authHeader is "", downloadToSink assumes no auth is expected
static void downloadToSink(
    const std::string & url,
    const std::string & authHeader,
    // FIXME: passing a StringSink is superfluous, we may as well
    // return a string. Or use an abstract Sink for streaming.
    StringSink & sink,
    std::string sha256Expected,
    size_t sizeExpected)
{
    FileTransferRequest request(url);
    Headers headers;
    if (!authHeader.empty())
        headers.push_back({"Authorization", authHeader});
    request.headers = headers;
    getFileTransfer()->download(std::move(request), sink);

    auto sizeActual = sink.s.length();
    if (sizeExpected != sizeActual)
        throw Error("size mismatch while fetching %s: expected %d but got %d", url, sizeExpected, sizeActual);

    auto sha256Actual = hashString(HashAlgorithm::SHA256, sink.s).to_string(HashFormat::Base16, false);
    if (sha256Actual != sha256Expected)
        throw Error(
            "hash mismatch while fetching %s: expected sha256:%s but got sha256:%s", url, sha256Expected, sha256Actual);
}

static std::string getLfsApiToken(const ParsedURL & url)
{
    auto [status, output] = runProgram(RunOptions{
        .program = "ssh",
        .args = {*url.authority, "git-lfs-authenticate", url.path, "download"},
    });

    if (output.empty())
        throw Error(
            "git-lfs-authenticate: no output (cmd: ssh %s git-lfs-authenticate %s download)",
            url.authority.value_or(""),
            url.path);

    auto queryResp = nlohmann::json::parse(output);
    if (!queryResp.contains("header"))
        throw Error("no header in git-lfs-authenticate response");
    if (!queryResp["header"].contains("Authorization"))
        throw Error("no Authorization in git-lfs-authenticate response");

    return queryResp["header"]["Authorization"].get<std::string>();
}

typedef std::unique_ptr<git_config, Deleter<git_config_free>> GitConfig;
typedef std::unique_ptr<git_config_entry, Deleter<git_config_entry_free>> GitConfigEntry;

static std::string getLfsEndpointUrl(git_repository * repo)
{
    GitConfig config;
    if (git_repository_config(Setter(config), repo)) {
        GitConfigEntry entry;
        if (!git_config_get_entry(Setter(entry), config.get(), "lfs.url")) {
            auto value = std::string(entry->value);
            if (!value.empty()) {
                debug("Found explicit lfs.url value: %s", value);
                return value;
            }
        }
    }

    git_remote * remote = nullptr;
    if (git_remote_lookup(&remote, repo, "origin"))
        return "";

    const char * url_c_str = git_remote_url(remote);
    if (!url_c_str)
        return "";

    return std::string(url_c_str);
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

    this->url = nix::parseURL(nix::fixGitURL(remoteUrl)).canonicalise();
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
    ParsedURL httpUrl(url);
    httpUrl.scheme = url.scheme == "ssh" ? "https" : url.scheme;
    FileTransferRequest request(httpUrl.to_string() + "/info/lfs/objects/batch");
    request.post = true;
    Headers headers;
    if (this->url.scheme == "ssh")
        headers.push_back({"Authorization", lfs::getLfsApiToken(this->url)});
    headers.push_back({"Content-Type", "application/vnd.git-lfs+json"});
    headers.push_back({"Accept", "application/vnd.git-lfs+json"});
    request.headers = headers;
    nlohmann::json oidList = pointerToPayload(pointers);
    nlohmann::json data = {{"operation", "download"}};
    data["objects"] = oidList;
    request.data = data.dump();

    FileTransferResult result = getFileTransfer()->upload(request);
    auto responseString = result.data;

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
    StringSink & sink,
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

    Path cacheDir = getCacheDir() + "/git-lfs";
    std::string key = hashString(HashAlgorithm::SHA256, pointerFilePath.rel()).to_string(HashFormat::Base16, false)
                      + "/" + pointer->oid;
    Path cachePath = cacheDir + "/" + key;
    if (pathExists(cachePath)) {
        debug("using cache entry %s -> %s", key, cachePath);
        sink(readFile(cachePath));
        return;
    }
    debug("did not find cache entry for %s", key);

    std::vector<Pointer> pointers;
    pointers.push_back(pointer.value());
    const auto objUrls = fetchUrls(pointers);

    const auto obj = objUrls[0];
    try {
        std::string sha256 = obj.at("oid"); // oid is also the sha256
        std::string ourl = obj.at("actions").at("download").at("href");
        std::string authHeader = "";
        if (obj.at("actions").at("download").contains("header")
            && obj.at("actions").at("download").at("header").contains("Authorization")) {
            authHeader = obj["actions"]["download"]["header"]["Authorization"];
        }
        const uint64_t size = obj.at("size");
        sizeCallback(size);
        downloadToSink(ourl, authHeader, sink, sha256, size);

        debug("creating cache entry %s -> %s", key, cachePath);
        if (!pathExists(dirOf(cachePath)))
            createDirs(dirOf(cachePath));
        writeFile(cachePath, sink.s);

        debug("%s fetched with git-lfs", pointerFilePath);
    } catch (const nlohmann::json::out_of_range & e) {
        throw Error("bad json from /info/lfs/objects/batch: %s %s", obj, e.what());
    }
}

} // namespace nix::lfs

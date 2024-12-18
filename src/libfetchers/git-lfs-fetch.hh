#include <array>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <git2.h>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

#include "serialise.hh"
#include "processes.hh"
#include "url.hh"

namespace fs = std::filesystem;

namespace nix {
namespace lfs {

// git-lfs metadata about a file
struct Md
{
    std::string path; // fs path relative to repo root, no ./ prefix
    std::string oid;  // git-lfs managed object id. you give this to the lfs server
                      // for downloads
    size_t size;      // in bytes
};

struct GitUrl
{
    std::string protocol;
    std::string user;
    std::string host;
    std::string port;
    std::string path;

    std::string toHttp() const
    {
        if (protocol.empty() || host.empty()) {
            return "";
        }
        std::string prefix = ((protocol == "ssh") ? "https" : protocol) + "://";
        return prefix + host + (port.empty() ? "" : ":" + port) + "/" + path;
    }

    // [host, path]
    std::pair<std::string, std::string> toSsh() const
    {
        if (host.empty()) {
            return {"", ""};
        }
        std::string userPart = user.empty() ? "" : user + "@";
        return {userPart + host, path};
    }
};

struct Fetch
{
    // Reference to the repository
    git_repository const * repo;

    // Git commit being fetched
    git_oid rev;

    // from shelling out to ssh, used  for 2 subsequent fetches:
    // list of URLs to fetch from, and fetching the data itself
    std::string token = "";

    // derived from git remote url
    GitUrl gitUrl = GitUrl{};

    Fetch(git_repository * repo, git_oid rev);
    bool shouldFetch(const std::string & path) const;
    void fetch(
        const git_blob * pointerBlob,
        const std::string & pointerFilePath,
        Sink & sink,
        std::function<void(uint64_t)> sizeCallback) const;
    std::vector<nlohmann::json> fetchUrls(const std::vector<Md> & metadatas) const;
};

static size_t writeCallback(void * contents, size_t size, size_t nmemb, std::string * s)
{
    size_t newLength = size * nmemb;
    s->append((char *) contents, newLength);
    return newLength;
}

struct SinkCallbackData
{
    Sink * sink;
    std::string_view sha256Expected;
    HashSink hashSink;

    SinkCallbackData(Sink * sink, std::string_view sha256)
        : sink(sink)
        , sha256Expected(sha256)
        , hashSink(HashAlgorithm::SHA256)
    {
    }
};

static size_t sinkWriteCallback(void * contents, size_t size, size_t nmemb, SinkCallbackData * data)
{
    size_t totalSize = size * nmemb;
    data->hashSink({(char *) contents, totalSize});
    (*data->sink)({(char *) contents, totalSize});
    return totalSize;
}

// if authHeader is "", downloadToSink assumes no auth is expected
void downloadToSink(
    const std::string & url, const std::string & authHeader, Sink & sink, std::string_view sha256Expected)
{
    CURL * curl;
    CURLcode res;

    curl = curl_easy_init();
    SinkCallbackData data(&sink, sha256Expected);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sinkWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    struct curl_slist * headers = nullptr;
    if (!authHeader.empty()) {
        const std::string authHeader_prepend = "Authorization: " + authHeader;
        headers = curl_slist_append(headers, authHeader_prepend.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        throw std::runtime_error(std::string("curl_easy_perform() failed: ") + curl_easy_strerror(res));
    }

    const auto sha256Actual = data.hashSink.finish().first.to_string(HashFormat::Base16, false);
    if (sha256Actual != data.sha256Expected) {
        throw std::runtime_error(
            "sha256 mismatch: while fetching " + url + ": expected " + std::string(data.sha256Expected) + " but got "
            + sha256Actual);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

std::string getLfsApiToken(const GitUrl & u)
{
    const auto [maybeUserAndHost, path] = u.toSsh();
    auto [status, output] = runProgram(RunOptions{
        .program = "ssh",
        .args = {maybeUserAndHost, "git-lfs-authenticate", path, "download"},
    });

    if (output.empty())
        throw std::runtime_error(
            "git-lfs-authenticate: no output (cmd: ssh " + maybeUserAndHost + " git-lfs-authenticate " + path
            + " download)");

    nlohmann::json query_resp = nlohmann::json::parse(output);
    if (!query_resp.contains("header"))
        throw std::runtime_error("no header in git-lfs-authenticate response");
    if (!query_resp["header"].contains("Authorization"))
        throw std::runtime_error("no Authorization in git-lfs-authenticate response");

    std::string res = query_resp["header"]["Authorization"].get<std::string>();

    return res;
}

std::string getLfsEndpointUrl(git_repository * repo)
{
    int err;
    git_remote * remote = NULL;
    err = git_remote_lookup(&remote, repo, "origin");
    if (err < 0) {
        return "";
    }

    const char * url_c_str = git_remote_url(remote);
    if (!url_c_str) {
        return "";
    }

    return std::string(url_c_str);
}

std::optional<Md> parseLfsMetadata(const std::string & content, const std::string & filename)
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

    std::istringstream iss(content);
    std::string line;

    std::string oid;
    std::string size;

    while (getline(iss, line)) {
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

    return std::make_optional(Md{filename, oid, std::stoul(size)});
}

// there's already a ParseURL here
// https://github.com/NixOS/nix/blob/ef6fa54e05cd4134ec41b0d64c1a16db46237f83/src/libutil/url.cc#L13 but that does
// not handle git's custom scp-like syntax
GitUrl parseGitUrl(const std::string & url)
{
    GitUrl result;

    // regular protocols
    const std::regex r_url(R"(^(ssh|git|https?|ftps?)://(?:([^@]+)@)?([^:/]+)(?::(\d+))?/(.*))");

    // "alternative scp-like syntax" https://git-scm.com/docs/git-fetch#_git_urls
    const std::regex r_scp_like_url(R"(^(?:([^@]+)@)?([^:/]+):(/?.*))");

    std::smatch matches;
    if (std::regex_match(url, matches, r_url)) {
        result.protocol = matches[1].str();
        result.user = matches[2].str();
        result.host = matches[3].str();
        result.port = matches[4].str();
        result.path = matches[5].str();
    } else if (std::regex_match(url, matches, r_scp_like_url)) {
        result.protocol = "ssh";

        result.user = matches[1].str();
        result.host = matches[2].str();
        result.path = matches[3].str();
    }

    return result;
}

Fetch::Fetch(git_repository * repo, git_oid rev)
{
    this->repo = repo;
    this->rev = rev;

    const auto remoteUrl = lfs::getLfsEndpointUrl(repo);

    this->gitUrl = parseGitUrl(remoteUrl);
    if (this->gitUrl.protocol == "ssh") {
        this->token = lfs::getLfsApiToken(this->gitUrl);
    }
}

bool Fetch::shouldFetch(const std::string & path) const
{
    const char * attr = nullptr;
    git_attr_options opts = GIT_ATTR_OPTIONS_INIT;
    opts.attr_commit_id = this->rev;
    opts.flags = GIT_ATTR_CHECK_INCLUDE_COMMIT | GIT_ATTR_CHECK_NO_SYSTEM;
    if (git_attr_get_ext(&attr, (git_repository *) (this->repo), &opts, path.c_str(), "filter"))
        throw Error("cannot get git-lfs attribute: %s", git_error_last()->message);
    debug("Git filter for %s is %s", path, attr ? attr : "null");
    return attr != nullptr && !std::string(attr).compare("lfs");
}

nlohmann::json mdToPayload(const std::vector<Md> & items)
{
    nlohmann::json jArray = nlohmann::json::array();
    for (const auto & md : items) {
        jArray.push_back({{"oid", md.oid}, {"size", md.size}});
    }
    return jArray;
}

std::vector<nlohmann::json> Fetch::fetchUrls(const std::vector<Md> & metadatas) const
{
    nlohmann::json oidList = mdToPayload(metadatas);
    nlohmann::json data = {
        {"operation", "download"},
    };
    data["objects"] = oidList;
    auto dataStr = data.dump();

    CURL * curl = curl_easy_init();
    char curlErrBuf[CURL_ERROR_SIZE];
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlErrBuf);
    std::string responseString;
    std::string headerString;
    const auto lfsUrlBatch = gitUrl.toHttp() + "/info/lfs/objects/batch";
    curl_easy_setopt(curl, CURLOPT_URL, lfsUrlBatch.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, dataStr.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    struct curl_slist * headers = NULL;
    if (this->token != "") {
        const auto authHeader = "Authorization: " + token;
        headers = curl_slist_append(headers, authHeader.c_str());
    }

    headers = curl_slist_append(headers, "Content-Type: application/vnd.git-lfs+json");
    headers = curl_slist_append(headers, "Accept: application/vnd.git-lfs+json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseString);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        std::stringstream ss;
        ss << "lfs::fetchUrls: bad response from info/lfs/objects/batch: code " << res << " " << curlErrBuf;
        throw std::runtime_error(ss.str());
    }

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    std::vector<nlohmann::json> objects;
    // example resp here:
    // {"objects":[{"oid":"f5e02aa71e67f41d79023a128ca35bad86cf7b6656967bfe0884b3a3c4325eaf","size":10000000,"actions":{"download":{"href":"https://gitlab.com/b-camacho/test-lfs.git/gitlab-lfs/objects/f5e02aa71e67f41d79023a128ca35bad86cf7b6656967bfe0884b3a3c4325eaf","header":{"Authorization":"Basic
    // Yi1jYW1hY2hvOmV5SjBlWEFpT2lKS1YxUWlMQ0poYkdjaU9pSklVekkxTmlKOS5leUprWVhSaElqcDdJbUZqZEc5eUlqb2lZaTFqWVcxaFkyaHZJbjBzSW1wMGFTSTZJbUptTURZNFpXVTFMVEprWmpVdE5HWm1ZUzFpWWpRMExUSXpNVEV3WVRReU1qWmtaaUlzSW1saGRDSTZNVGN4TkRZeE16ZzBOU3dpYm1KbUlqb3hOekUwTmpFek9EUXdMQ0psZUhBaU9qRTNNVFEyTWpFd05EVjkuZk9yMDNkYjBWSTFXQzFZaTBKRmJUNnJTTHJPZlBwVW9lYllkT0NQZlJ4QQ=="}}},"authenticated":true}]}

    try {
        auto resp = nlohmann::json::parse(responseString);
        if (resp.contains("objects")) {
            objects.insert(objects.end(), resp["objects"].begin(), resp["objects"].end());
        } else {
            throw std::runtime_error("response does not contain 'objects'");
        }

        return objects;
    } catch (const nlohmann::json::parse_error & e) {
        std::stringstream ss;
        ss << "response did not parse as json: " << responseString;
        throw std::runtime_error(ss.str());
    }
}

void Fetch::fetch(
    const git_blob * pointerBlob,
    const std::string & pointerFilePath,
    Sink & sink,
    std::function<void(uint64_t)> sizeCallback) const
{
    debug("Trying to fetch %s using git-lfs", pointerFilePath);
    constexpr git_object_size_t chunkSize = 128 * 1024; // 128 KiB
    auto pointerSize = git_blob_rawsize(pointerBlob);

    if (pointerSize >= 1024) {
        debug("Skip git-lfs, pointer file too large");
        warn("Encountered a file that should have been a pointer, but wasn't: %s", pointerFilePath);
        sizeCallback(pointerSize);
        for (git_object_size_t offset = 0; offset < pointerSize; offset += chunkSize) {
            sink(std::string(
                (const char *) git_blob_rawcontent(pointerBlob) + offset, std::min(chunkSize, pointerSize - offset)));
        }
        return;
    }

    const auto pointerFileContents = std::string((const char *) git_blob_rawcontent(pointerBlob), pointerSize);
    const auto md = parseLfsMetadata(std::string(pointerFileContents), std::string(pointerFilePath));
    if (md == std::nullopt) {
        debug("Skip git-lfs, invalid pointer file");
        warn("Encountered a file that should have been a pointer, but wasn't: %s", pointerFilePath);
        sizeCallback(pointerSize);
        for (git_object_size_t offset = 0; offset < pointerSize; offset += chunkSize) {
            sink(std::string(
                (const char *) git_blob_rawcontent(pointerBlob) + offset, std::min(chunkSize, pointerSize - offset)));
        }
        return;
    }

    std::vector<Md> vMds;
    vMds.push_back(md.value());
    const auto objUrls = fetchUrls(vMds);

    const auto obj = objUrls[0];
    try {
        std::string oid = obj.at("oid");
        std::string ourl = obj.at("actions").at("download").at("href");
        std::string authHeader = "";
        if (obj.at("actions").at("download").contains("header")
            && obj.at("actions").at("download").at("header").contains("Authorization")) {
            authHeader = obj["actions"]["download"]["header"]["Authorization"];
        }
        const uint64_t size = obj.at("size");
        sizeCallback(size);
        downloadToSink(ourl, authHeader, sink, oid); // oid is also the sha256
        debug("%s fetched with git-lfs", pointerFilePath);
    } catch (const nlohmann::json::out_of_range & e) {
        std::stringstream ss;
        ss << "bad json from /info/lfs/objects/batch: " << obj << " " << e.what();
        throw std::runtime_error(ss.str());
    }
}

} // namespace lfs

} // namespace nix

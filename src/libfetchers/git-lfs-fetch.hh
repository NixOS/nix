#include <fstream>
#include <git2.h>
#include <nlohmann/json.hpp>
#include <string>

#include "filetransfer.hh"
#include "processes.hh"
#include "sync.hh"
#include "url.hh"
#include "users.hh"

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

struct Fetch
{
    // Reference to the repository
    git_repository const * repo;

    // Git commit being fetched
    git_oid rev;

    // derived from git remote url
    nix::ParsedURL url;

    Fetch(git_repository * repo, git_oid rev);
    bool shouldFetch(const std::string & path) const;
    void fetch(
        const std::string content,
        const std::string & pointerFilePath,
        StringSink & sink,
        std::function<void(uint64_t)> sizeCallback) const;
    std::vector<nlohmann::json> fetchUrls(const std::vector<Md> & metadatas) const;
};

// if authHeader is "", downloadToSink assumes no auth is expected
void downloadToSink(
    const std::string & url,
    const std::string & authHeader,
    StringSink & sink,
    std::string path,
    std::string sha256Expected,
    size_t sizeExpected)
{
    FileTransferRequest request(url);
    Headers headers;
    if (!authHeader.empty())
        headers.push_back({"Authorization", authHeader});
    request.headers = headers;
    getFileTransfer()->download(std::move(request), sink);
    std::string data = sink.s;

    const auto sizeActual = data.length();
    if (sizeExpected != sizeActual)
        throw Error("size mismatch while fetching %s: expected %d but got %d", url, sizeExpected, sizeActual);

    const auto sha256Actual = hashString(HashAlgorithm::SHA256, data).to_string(HashFormat::Base16, false);
    if (sha256Actual != sha256Expected)
        throw Error(
            "hash mismatch while fetching %s: expected sha256:%s but got sha256:%s", url, sha256Expected, sha256Actual);
}

std::string getLfsApiToken(const ParsedURL & url)
{
    auto [status, output] = runProgram(RunOptions{
        .program = "ssh",
        .args = {*url.authority, "git-lfs-authenticate", url.path, "download"},
    });

    if (output.empty())
        throw std::runtime_error(
            "git-lfs-authenticate: no output (cmd: ssh " + *url.authority + " git-lfs-authenticate " + url.path
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
    git_config * config = NULL;
    if (!git_repository_config(&config, repo))
        ;
    {
        git_config_entry * entry = NULL;
        if (!git_config_get_entry(&entry, config, "lfs.url")) {
            auto value = std::string(entry->value);
            if (!value.empty()) {
                debug("Found explicit lfs.url value: %s", value);
                return value;
            }
        }
        git_config_entry_free(entry);
    }
    git_config_free(config);
    git_remote * remote = NULL;
    if (git_remote_lookup(&remote, repo, "origin"))
        return "";

    const char * url_c_str = git_remote_url(remote);
    if (!url_c_str)
        return "";

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

Fetch::Fetch(git_repository * repo, git_oid rev)
{
    this->repo = repo;
    this->rev = rev;

    const auto remoteUrl = lfs::getLfsEndpointUrl(repo);

    this->url = nix::parseURL(nix::fixGitURL(remoteUrl)).canonicalise();
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
    for (const auto & md : items)
        jArray.push_back({{"oid", md.oid}, {"size", md.size}});
    return jArray;
}

std::vector<nlohmann::json> Fetch::fetchUrls(const std::vector<Md> & metadatas) const
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
    nlohmann::json oidList = mdToPayload(metadatas);
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
            throw std::runtime_error("response does not contain 'objects'");

        return objects;
    } catch (const nlohmann::json::parse_error & e) {
        std::stringstream ss;
        ss << "response did not parse as json: " << responseString;
        throw std::runtime_error(ss.str());
    }
}

void Fetch::fetch(
    const std::string content,
    const std::string & pointerFilePath,
    StringSink & sink,
    std::function<void(uint64_t)> sizeCallback) const
{
    debug("Trying to fetch %s using git-lfs", pointerFilePath);

    if (content.length() >= 1024) {
        debug("Skip git-lfs, pointer file too large");
        warn("Encountered a file that should have been a pointer, but wasn't: %s", pointerFilePath);
        sizeCallback(content.length());
        sink(content);
        return;
    }

    const auto md = parseLfsMetadata(std::string(content), std::string(pointerFilePath));
    if (md == std::nullopt) {
        debug("Skip git-lfs, invalid pointer file");
        warn("Encountered a file that should have been a pointer, but wasn't: %s", pointerFilePath);
        sizeCallback(content.length());
        sink(content);
        return;
    }

    Path cacheDir = getCacheDir() + "/git-lfs";
    std::string key =
        hashString(HashAlgorithm::SHA256, pointerFilePath).to_string(HashFormat::Base16, false) + "/" + md->oid;
    Path cachePath = cacheDir + "/" + key;
    if (pathExists(cachePath)) {
        debug("using cache entry %s -> %s", key, cachePath);
        std::ifstream stream(cachePath);
        const auto chunkSize = 128 * 1024; // 128 KiB
        char buffer[chunkSize];
        do {
            if (!stream.read(buffer, chunkSize))
                if (!stream.eof())
                    throw Error("I/O error while reading cached file");
            sink(std::string(buffer, stream.gcount()));
        } while (stream.gcount() > 0);
        return;
    }
    debug("did not find cache entry for %s", key);

    std::vector<Md> vMds;
    vMds.push_back(md.value());
    const auto objUrls = fetchUrls(vMds);

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
        downloadToSink(ourl, authHeader, sink, pointerFilePath, sha256, size);

        debug("creating cache entry %s -> %s", key, cachePath);
        if (!pathExists(dirOf(cachePath)))
            createDirs(dirOf(cachePath));
        std::ofstream stream(cachePath);
        if (!stream.write(sink.s.c_str(), size))
            throw Error("I/O error while writing cache file");

        debug("%s fetched with git-lfs", pointerFilePath);
    } catch (const nlohmann::json::out_of_range & e) {
        std::stringstream ss;
        ss << "bad json from /info/lfs/objects/batch: " << obj << " " << e.what();
        throw std::runtime_error(ss.str());
    }
}

} // namespace lfs

} // namespace nix

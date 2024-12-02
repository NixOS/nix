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

// see Fetch::rules
struct AttrRule
{
    std::string pattern;
    std::unordered_map<std::string, std::string> attributes;
    git_pathspec * pathspec = nullptr;

    AttrRule() = default;

    explicit AttrRule(std::string pat)
        : pattern(std::move(pat))
    {
        initPathspec();
    }

    ~AttrRule()
    {
        if (pathspec) {
            git_pathspec_free(pathspec);
        }
    }

    AttrRule(const AttrRule & other)
        : pattern(other.pattern)
        , attributes(other.attributes)
        , pathspec(nullptr)
    {
        if (!pattern.empty()) {
            initPathspec();
        }
    }

    void initPathspec()
    {
        git_strarray patterns = {0};
        const char * pattern_str = pattern.c_str();
        patterns.strings = const_cast<char **>(&pattern_str);
        patterns.count = 1;

        if (git_pathspec_new(&pathspec, &patterns) != 0) {
            throw std::runtime_error("Failed to create git pathspec");
        }
    }
};

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
    // only true after init()
    bool ready = false;

    // from shelling out to ssh, used  for 2 subsequent fetches:
    // list of URLs to fetch from, and fetching the data itself
    std::string token = "";

    // derived from git remote url
    GitUrl gitUrl = GitUrl{};

    // parsed contents of .gitattributes
    // .gitattributes contains a list of path patterns, and list of attributes (=key-value tags) for each pattern
    // paths tagged with `filter=lfs` need to be smudged by downloading from lfs server
    std::vector<AttrRule> rules = {};

    void init(git_repository * repo, const std::string & gitattributesContent);
    bool hasAttribute(const std::string & path, const std::string & attrName, const std::string & attrValue) const;
    void fetch(const git_blob * pointerBlob, const std::string & pointerFilePath, Sink & sink, std::function<void(uint64_t)> sizeCallback) const;
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

std::string git_attr_value_to_string(git_attr_value_t value)
{
    switch (value) {
    case GIT_ATTR_VALUE_UNSPECIFIED:
        return "GIT_ATTR_VALUE_UNSPECIFIED";
    case GIT_ATTR_VALUE_TRUE:
        return "GIT_ATTR_VALUE_TRUE";
    case GIT_ATTR_VALUE_FALSE:
        return "GIT_ATTR_VALUE_FALSE";
    case GIT_ATTR_VALUE_STRING:
        return "GIT_ATTR_VALUE_STRING";
    default:
        return "Unknown value";
    }
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
// https://github.com/b-camacho/nix/blob/ef6fa54e05cd4134ec41b0d64c1a16db46237f83/src/libutil/url.cc#L13 but that does
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

std::vector<AttrRule> parseGitAttrFile(const std::string & content)
{
    std::vector<AttrRule> rules;
    std::string content_str(content);
    std::istringstream iss(content_str);
    std::string line;

    while (std::getline(iss, line)) {
        if (line.empty() || line[0] == '#')
            continue;

        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        size_t pattern_end = line.find_first_of(" \t"); // matches space OR tab
        if (pattern_end == std::string::npos)
            continue;

        AttrRule rule;
        rule.pattern = line.substr(0, pattern_end);
        // Workaround for libgit2 matching issues when the pattern starts with a recursive glob
        // These should effectively match the same files
        // https://github.com/libgit2/libgit2/issues/6946
        if (rule.pattern.starts_with("/**/")) {
            rule.pattern = rule.pattern.substr(4);
        }
        while (rule.pattern.starts_with("**/")) {
            rule.pattern = rule.pattern.substr(3);
        }

        git_strarray patterns = {0};
        const char * pattern_str = rule.pattern.c_str();
        patterns.strings = const_cast<char **>(&pattern_str);
        patterns.count = 1;

        if (git_pathspec_new(&rule.pathspec, &patterns) != 0) {
            auto error = git_error_last();
            std::stringstream ss;
            ss << "git_pathspec_new parsing '" << line << "': " << (error ? error->message : "unknown error")
               << std::endl;
            warn(ss.str());
            continue;
        }

        size_t attr_start = line.find_first_not_of(" \t", pattern_end);
        if (attr_start != std::string::npos) {
            std::string_view rest(line);
            rest.remove_prefix(attr_start);

            while (!rest.empty()) {
                size_t attr_end = rest.find_first_of(" \t");
                std::string_view attr = rest.substr(0, attr_end);

                if (attr[0] == '-') {
                    rule.attributes[std::string(attr.substr(1))] = "false";
                } else if (auto equals_pos = attr.find('='); equals_pos != std::string_view::npos) {
                    auto key = attr.substr(0, equals_pos);
                    auto value = attr.substr(equals_pos + 1);
                    rule.attributes[std::string(key)] = std::string(value);
                } else {
                    rule.attributes[std::string(attr)] = "true";
                }

                if (attr_end == std::string_view::npos)
                    break;

                rest = rest.substr(attr_end);
                size_t next_attr = rest.find_first_not_of(" \t");
                if (next_attr == std::string_view::npos)
                    break;
                rest = rest.substr(next_attr);
            }
        }

        rules.push_back(std::move(rule));
    }

    return rules;
}

void Fetch::init(git_repository * repo, const std::string & gitattributesContent)
{
    const auto remoteUrl = lfs::getLfsEndpointUrl(repo);

    this->gitUrl = parseGitUrl(remoteUrl);
    if (this->gitUrl.protocol == "ssh") {
        this->token = lfs::getLfsApiToken(this->gitUrl);
    }
    this->rules = lfs::parseGitAttrFile(gitattributesContent);
    this->ready = true;
}

bool Fetch::hasAttribute(const std::string & path, const std::string & attrName, const std::string & attrValue) const
{
    for (auto it = rules.rbegin(); it != rules.rend(); ++it) {
        int match = git_pathspec_matches_path(
            it->pathspec,
            0, // no flags
            path.c_str());

        if (match > 0) {
            auto attr = it->attributes.find(attrName);
            if (attr != it->attributes.end()) {
                return attr->second == attrValue;
            } else {
            }
        }
    }
    return false;
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

void Fetch::fetch(const git_blob * pointerBlob, const std::string & pointerFilePath, Sink & sink, std::function<void(uint64_t)> sizeCallback) const
{
    constexpr git_object_size_t chunkSize = 128 * 1024; // 128 KiB
    auto pointerSize = git_blob_rawsize(pointerBlob);

    if (pointerSize >= 1024) {
        debug("Skip git-lfs, pointer file too large");
        warn("Encountered a file that should have been a pointer, but wasn't: %s", pointerFilePath);
        sizeCallback(pointerSize);
        for (git_object_size_t offset = 0; offset < pointerSize; offset += chunkSize) {
            sink(std::string((const char *) git_blob_rawcontent(pointerBlob) + offset, std::min(chunkSize, pointerSize - offset)));
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
    } catch (const nlohmann::json::out_of_range & e) {
        std::stringstream ss;
        ss << "bad json from /info/lfs/objects/batch: " << obj << " " << e.what();
        throw std::runtime_error(ss.str());
    }
}

} // namespace lfs


} // namespace nix

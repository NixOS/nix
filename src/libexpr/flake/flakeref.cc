#include "flakeref.hh"
#include "store-api.hh"

#include <regex>

namespace nix {

// A Git ref (i.e. branch or tag name).
const static std::string refRegex = "[a-zA-Z0-9][a-zA-Z0-9_.-]*"; // FIXME: check

// A Git revision (a SHA-1 commit hash).
const static std::string revRegexS = "[0-9a-fA-F]{40}";
std::regex revRegex(revRegexS, std::regex::ECMAScript);

// A Git ref or revision.
const static std::string revOrRefRegex = "(?:(" + revRegexS + ")|(" + refRegex + "))";

// A rev ("e72daba8250068216d79d2aeef40d4d95aff6666"), or a ref
// optionally followed by a rev (e.g. "master" or
// "master/e72daba8250068216d79d2aeef40d4d95aff6666").
const static std::string refAndOrRevRegex = "(?:(" + revRegexS + ")|(?:(" + refRegex + ")(?:/(" + revRegexS + "))?))";

const static std::string flakeAlias = "[a-zA-Z][a-zA-Z0-9_-]*";

// GitHub references.
const static std::string ownerRegex = "[a-zA-Z][a-zA-Z0-9_-]*";
const static std::string repoRegex = "[a-zA-Z][a-zA-Z0-9_-]*";

// URI stuff.
const static std::string schemeRegex = "[a-z+]+";
const static std::string authorityRegex = "[a-zA-Z0-9._~-]*";
const static std::string segmentRegex = "[a-zA-Z0-9._~-]+";
const static std::string pathRegex = "/?" + segmentRegex + "(?:/" + segmentRegex + ")*";

// 'dir' path elements cannot start with a '.'. We also reject
// potentially dangerous characters like ';'.
const static std::string subDirElemRegex = "(?:[a-zA-Z0-9_-]+[a-zA-Z0-9._-]*)";
const static std::string subDirRegex = subDirElemRegex + "(?:/" + subDirElemRegex + ")*";


FlakeRef::FlakeRef(const std::string & uri_, bool allowRelative)
{
    // FIXME: could combine this into one regex.

    static std::regex flakeRegex(
        "(?:flake:)?(" + flakeAlias + ")(?:/(?:" + refAndOrRevRegex + "))?",
        std::regex::ECMAScript);

    static std::regex githubRegex(
        "github:(" + ownerRegex + ")/(" + repoRegex + ")(?:/" + revOrRefRegex + ")?",
        std::regex::ECMAScript);

    static std::regex uriRegex(
        "((" + schemeRegex + "):" +
        "(?://(" + authorityRegex + "))?" +
        "(" + pathRegex + "))",
        std::regex::ECMAScript);

    static std::regex refRegex2(refRegex, std::regex::ECMAScript);

    static std::regex subDirRegex2(subDirRegex, std::regex::ECMAScript);

    auto [uri2, params] = splitUriAndParams(uri_);
    std::string uri(uri2);

    auto handleSubdir = [&](const std::string & name, const std::string & value) {
        if (name == "dir") {
            if (value != "" && !std::regex_match(value, subDirRegex2))
                throw BadFlakeRef("flake '%s' has invalid subdirectory '%s'", uri, value);
            subdir = value;
            return true;
        } else
            return false;
    };

    auto handleGitParams = [&](const std::string & name, const std::string & value) {
        if (name == "rev") {
            if (!std::regex_match(value, revRegex))
                throw BadFlakeRef("invalid Git revision '%s'", value);
            rev = Hash(value, htSHA1);
        } else if (name == "ref") {
            if (!std::regex_match(value, refRegex2))
                throw BadFlakeRef("invalid Git ref '%s'", value);
            ref = value;
        } else if (handleSubdir(name, value))
            ;
        else return false;
        return true;
    };

    std::cmatch match;
    if (std::regex_match(uri.c_str(), match, flakeRegex)) {
        IsAlias d;
        d.alias = match[1];
        if (match[2].matched)
            rev = Hash(match[2], htSHA1);
        else if (match[3].matched) {
            ref = match[3];
            if (match[4].matched)
                rev = Hash(match[4], htSHA1);
        }
        data = d;
    }

    else if (std::regex_match(uri.c_str(), match, githubRegex)) {
        IsGitHub d;
        d.owner = match[1];
        d.repo = match[2];
        if (match[3].matched)
            rev = Hash(match[3], htSHA1);
        else if (match[4].matched) {
            ref = match[4];
        }
        for (auto & param : params) {
            if (handleSubdir(param.first, param.second))
                ;
            else
                throw BadFlakeRef("invalid Git flakeref parameter '%s', in '%s'", param.first, uri);
        }
        data = d;
    }

    else if (std::regex_match(uri.c_str(), match, uriRegex)) {
        auto & scheme = match[2];
        if (scheme == "git" ||
            scheme == "git+http" ||
            scheme == "git+https" ||
            scheme == "git+ssh" ||
            scheme == "git+file" ||
            scheme == "file")
        {
            IsGit d;
            d.uri = match[1];
            for (auto & param : params) {
                if (handleGitParams(param.first, param.second))
                    ;
                else
                    // FIXME: should probably pass through unknown parameters
                    throw BadFlakeRef("invalid Git flakeref parameter '%s', in '%s'", param.first, uri);
            }
            if (rev && !ref)
                throw BadFlakeRef("flake URI '%s' lacks a Git ref", uri);
            data = d;
        } else
            throw BadFlakeRef("unsupported URI scheme '%s' in flake reference '%s'", scheme, uri);
    }

    else if ((hasPrefix(uri, "/") || (allowRelative && (hasPrefix(uri, "./") || hasPrefix(uri, "../") || uri == ".")))
        && uri.find(':') == std::string::npos)
    {
        IsPath d;
        if (allowRelative) {
            d.path = absPath(uri);
            try {
                if (!S_ISDIR(lstat(d.path).st_mode))
                    throw MissingFlake("path '%s' is not a flake (sub)directory", d.path);
            } catch (SysError & e) {
                if (e.errNo == ENOENT || e.errNo == EISDIR)
                    throw MissingFlake("flake '%s' does not exist", d.path);
                throw;
            }
            while (true) {
                if (pathExists(d.path + "/.git")) break;
                subdir = baseNameOf(d.path) + (subdir.empty() ? "" : "/" + subdir);
                d.path = dirOf(d.path);
                if (d.path == "/")
                    throw MissingFlake("path '%s' is not a flake (because it does not reference a Git repository)", uri);
            }
        } else
            d.path = canonPath(uri);
        data = d;
        for (auto & param : params) {
            if (handleGitParams(param.first, param.second))
                ;
            else
                throw BadFlakeRef("invalid Git flakeref parameter '%s', in '%s'", param.first, uri);
        }
    }

    else
        throw BadFlakeRef("'%s' is not a valid flake reference", uri);
}

std::string FlakeRef::to_string() const
{
    std::string string;
    bool first = true;

    auto addParam =
        [&](const std::string & name, std::string value) {
            string += first ? '?' : '&';
            first = false;
            string += name;
            string += '=';
            string += value; // FIXME: escaping
        };

    if (auto refData = std::get_if<FlakeRef::IsAlias>(&data)) {
        string = refData->alias;
        if (ref) string += '/' + *ref;
        if (rev) string += '/' + rev->gitRev();
    }

    else if (auto refData = std::get_if<FlakeRef::IsPath>(&data)) {
        string = refData->path;
        if (ref) addParam("ref", *ref);
        if (rev) addParam("rev", rev->gitRev());
        if (subdir != "") addParam("dir", subdir);
    }

    else if (auto refData = std::get_if<FlakeRef::IsGitHub>(&data)) {
        assert(!(ref && rev));
        string = "github:" + refData->owner + "/" + refData->repo;
        if (ref) { string += '/'; string += *ref; }
        if (rev) { string += '/'; string += rev->gitRev(); }
        if (subdir != "") addParam("dir", subdir);
    }

    else if (auto refData = std::get_if<FlakeRef::IsGit>(&data)) {
        assert(!rev || ref);
        string = refData->uri;

        if (ref) {
            addParam("ref", *ref);
            if (rev)
                addParam("rev", rev->gitRev());
        }

        if (subdir != "") addParam("dir", subdir);
    }

    else abort();

    assert(FlakeRef(string) == *this);

    return string;
}

std::ostream & operator << (std::ostream & str, const FlakeRef & flakeRef)
{
    str << flakeRef.to_string();
    return str;
}

bool FlakeRef::isImmutable() const
{
    return (bool) rev;
}

FlakeRef FlakeRef::baseRef() const // Removes the ref and rev from a FlakeRef.
{
    FlakeRef result(*this);
    result.ref = std::nullopt;
    result.rev = std::nullopt;
    return result;
}

std::optional<FlakeRef> parseFlakeRef(
    const std::string & uri, bool allowRelative)
{
    try {
        return FlakeRef(uri, allowRelative);
    } catch (BadFlakeRef & e) {
        return {};
    }
}

}

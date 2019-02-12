#include "flakeref.hh"

#include <regex>

namespace nix {

// A Git ref (i.e. branch or tag name).
const static std::string refRegex = "[a-zA-Z][a-zA-Z0-9_.-]*"; // FIXME: check

// A Git revision (a SHA-1 commit hash).
const static std::string revRegexS = "[0-9a-fA-F]{40}";
std::regex revRegex(revRegexS, std::regex::ECMAScript);

// A Git ref or revision.
const static std::string revOrRefRegex = "(?:(" + revRegexS + ")|(" + refRegex + "))";

// A rev ("e72daba8250068216d79d2aeef40d4d95aff6666"), or a ref
// optionally followed by a rev (e.g. "master" or
// "master/e72daba8250068216d79d2aeef40d4d95aff6666").
const static std::string refAndOrRevRegex = "(?:(" + revRegexS + ")|(?:(" + refRegex + ")(?:/(" + revRegexS + "))?))";

const static std::string flakeId = "[a-zA-Z][a-zA-Z0-9_-]*";

// GitHub references.
const static std::string ownerRegex = "[a-zA-Z][a-zA-Z0-9_-]*";
const static std::string repoRegex = "[a-zA-Z][a-zA-Z0-9_-]*";

// URI stuff.
const static std::string schemeRegex = "(?:http|https|ssh|git|file)";
const static std::string authorityRegex = "[a-zA-Z0-9._~-]*";
const static std::string segmentRegex = "[a-zA-Z0-9._~-]+";
const static std::string pathRegex = "/?" + segmentRegex + "(?:/" + segmentRegex + ")*";
const static std::string paramRegex = "[a-z]+=[a-zA-Z0-9._-]*";

FlakeRef::FlakeRef(const std::string & uri)
{
    // FIXME: could combine this into one regex.

    static std::regex flakeRegex(
        "(?:flake:)?(" + flakeId + ")(?:/(?:" + refAndOrRevRegex + "))?",
        std::regex::ECMAScript);

    static std::regex githubRegex(
        "github:(" + ownerRegex + ")/(" + repoRegex + ")(?:/" + revOrRefRegex + ")?",
        std::regex::ECMAScript);

    static std::regex uriRegex(
        "((" + schemeRegex + "):" +
        "(?://(" + authorityRegex + "))?" +
        "(" + pathRegex + "))" +
        "(?:[?](" + paramRegex + "(?:&" + paramRegex + ")*))?",
        std::regex::ECMAScript);

    static std::regex refRegex2(refRegex, std::regex::ECMAScript);

    std::cmatch match;
    if (std::regex_match(uri.c_str(), match, flakeRegex)) {
        IsFlakeId d;
        d.id = match[1];
        if (match[2].matched)
            d.rev = Hash(match[2], htSHA1);
        else if (match[3].matched) {
            d.ref = match[3];
            if (match[4].matched)
                d.rev = Hash(match[4], htSHA1);
        }
        data = d;
    }

    else if (std::regex_match(uri.c_str(), match, githubRegex)) {
        IsGitHub d;
        d.owner = match[1];
        d.repo = match[2];
        if (match[3].matched)
            d.rev = Hash(match[3], htSHA1);
        else if (match[4].matched) {
            d.ref = match[4];
        }
        data = d;
    }

    else if (std::regex_match(uri.c_str(), match, uriRegex) && hasSuffix(match[4], ".git")) {
        IsGit d;
        d.uri = match[1];
        for (auto & param : tokenizeString<Strings>(match[5], "&")) {
            auto n = param.find('=');
            assert(n != param.npos);
            std::string name(param, 0, n);
            std::string value(param, n + 1);
            if (name == "rev") {
                if (!std::regex_match(value, revRegex))
                    throw Error("invalid Git revision '%s'", value);
                d.rev = Hash(value, htSHA1);
            } else if (name == "ref") {
                if (!std::regex_match(value, refRegex2))
                    throw Error("invalid Git ref '%s'", value);
                d.ref = value;
            } else
                // FIXME: should probably pass through unknown parameters
                throw Error("invalid Git flake reference parameter '%s', in '%s'", name, uri);
        }
        if (d.rev && !d.ref)
            throw Error("flake URI '%s' lacks a Git ref", uri);
        data = d;
    }

    else
        throw Error("'%s' is not a valid flake reference", uri);
}

std::string FlakeRef::to_string() const
{
    if (auto refData = std::get_if<FlakeRef::IsFlakeId>(&data)) {
        return
            "flake:" + refData->id +
            (refData->ref ? "/" + *refData->ref : "") +
            (refData->rev ? "/" + refData->rev->to_string(Base16, false) : "");
    }

    else if (auto refData = std::get_if<FlakeRef::IsGitHub>(&data)) {
        assert(!refData->ref || !refData->rev);
        return
            "github:" + refData->owner + "/" + refData->repo +
            (refData->ref ? "/" + *refData->ref : "") +
            (refData->rev ? "/" + refData->rev->to_string(Base16, false) : "");
    }

    else if (auto refData = std::get_if<FlakeRef::IsGit>(&data)) {
        assert(refData->ref || !refData->rev);
        return
            refData->uri +
            (refData->ref ? "?ref=" + *refData->ref : "") +
            (refData->rev ? "&rev=" + refData->rev->to_string(Base16, false) : "");
    }

    else abort();
}

}

#pragma once

#include <regex>

namespace nix::fetchers {

// URI stuff.
const static std::string pctEncoded = "%[0-9a-fA-F][0-9a-fA-F]";
const static std::string schemeRegex = "[a-z+]+";
const static std::string authorityRegex =
    "(?:(?:[a-z])*@)?"
    "[a-zA-Z0-9._~-]*";
const static std::string segmentRegex = "[a-zA-Z0-9._~-]+";
const static std::string pathRegex = "(?:/?" + segmentRegex + "(?:/" + segmentRegex + ")*|/?)";
const static std::string pcharRegex =
    "(?:[a-zA-Z0-9-._~!$&'\"()*+,;=:@ ]|" + pctEncoded + ")";
const static std::string queryRegex = "(?:" + pcharRegex + "|[/?])*";

// A Git ref (i.e. branch or tag name).
const static std::string refRegexS = "[a-zA-Z0-9][a-zA-Z0-9_.-]*"; // FIXME: check
extern std::regex refRegex;

// A Git revision (a SHA-1 commit hash).
const static std::string revRegexS = "[0-9a-fA-F]{40}";
extern std::regex revRegex;

// A ref or revision, or a ref followed by a revision.
const static std::string refAndOrRevRegex = "(?:(" + revRegexS + ")|(?:(" + refRegexS + ")(?:/(" + revRegexS + "))?))";

const static std::string flakeIdRegexS = "[a-zA-Z][a-zA-Z0-9_-]*";
extern std::regex flakeIdRegex;

}

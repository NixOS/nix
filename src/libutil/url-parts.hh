#pragma once

#include <string>
#include <regex>

namespace nix {

// URI stuff.
const static std::string pctEncoded = "(?:%[0-9a-fA-F][0-9a-fA-F])";
const static std::string schemeRegex = "(?:[a-z+.-]+)";
const static std::string ipv6AddressRegex = "(?:\\[[0-9a-fA-F:]+\\])";
const static std::string unreservedRegex = "(?:[a-zA-Z0-9-._~])";
const static std::string subdelimsRegex = "(?:[!$&'\"()*+,;=])";
const static std::string hostnameRegex = "(?:(?:" + unreservedRegex + "|" + pctEncoded + "|" + subdelimsRegex + ")*)";
const static std::string hostRegex = "(?:" + ipv6AddressRegex + "|" + hostnameRegex + ")";
const static std::string userRegex = "(?:(?:" + unreservedRegex + "|" + pctEncoded + "|" + subdelimsRegex + "|:)*)";
const static std::string authorityRegex = "(?:" + userRegex + "@)?" + hostRegex + "(?::[0-9]+)?";
const static std::string pcharRegex = "(?:" + unreservedRegex + "|" + pctEncoded + "|" + subdelimsRegex + "|[:@])";
const static std::string queryRegex = "(?:" + pcharRegex + "|[/? \"])*";
const static std::string segmentRegex = "(?:" + pcharRegex + "+)";
const static std::string absPathRegex = "(?:(?:/" + segmentRegex + ")*/?)";
const static std::string pathRegex = "(?:" + segmentRegex + "(?:/" + segmentRegex + ")*/?)";

// A Git ref (i.e. branch or tag name).
const static std::string refRegexS = "[a-zA-Z0-9][a-zA-Z0-9_.-]*"; // FIXME: check
extern std::regex refRegex;

// Instead of defining what a good Git Ref is, we define what a bad Git Ref is
// This is because of the definition of a ref in refs.c in https://github.com/git/git
// See tests/fetchGitRefs.sh for the full definition
const static std::string badGitRefRegexS = "//|^[./]|/\\.|\\.\\.|[[:cntrl:][:space:]:?^~\[]|\\\\|\\*|\\.lock$|\\.lock/|@\\{|[/.]$|^@$|^$";
extern std::regex badGitRefRegex;

// A Git revision (a SHA-1 commit hash).
const static std::string revRegexS = "[0-9a-fA-F]{40}";
extern std::regex revRegex;

// A ref or revision, or a ref followed by a revision.
const static std::string refAndOrRevRegex = "(?:(" + revRegexS + ")|(?:(" + refRegexS + ")(?:/(" + revRegexS + "))?))";

const static std::string flakeIdRegexS = "[a-zA-Z][a-zA-Z0-9_-]*";
extern std::regex flakeIdRegex;

}

#pragma once

#include <regex>

namespace nix::fetchers {

// URI stuff.
const static std::string pctEncoded = "(?:%[0-9a-fA-F][0-9a-fA-F])";
const static std::string schemeRegex = "(?:[a-z+]+)";
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

// A Git revision (a SHA-1 commit hash).
const static std::string revRegexS = "[0-9a-fA-F]{40}";
extern std::regex revRegex;

// A ref or revision, or a ref followed by a revision.
const static std::string refAndOrRevRegex = "(?:(" + revRegexS + ")|(?:(" + refRegexS + ")(?:/(" + revRegexS + "))?))";

const static std::string flakeIdRegexS = "[a-zA-Z][a-zA-Z0-9_-]*";
extern std::regex flakeIdRegex;

}

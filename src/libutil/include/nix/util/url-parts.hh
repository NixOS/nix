#pragma once
///@file

#include <string>
#include <regex>

namespace nix {

// URI stuff.
const static std::string pctEncoded = "(?:%[0-9a-fA-F][0-9a-fA-F])";
const static std::string unreservedRegex = "(?:[a-zA-Z0-9-._~])";
const static std::string subdelimsRegex = "(?:[!$&'\"()*+,;=])";
const static std::string pcharRegex = "(?:" + unreservedRegex + "|" + pctEncoded + "|" + subdelimsRegex + "|[:@])";
const static std::string fragmentRegex = "(?:" + pcharRegex + "|[/? \"^])*";

/// A Git ref (i.e. branch or tag name).
/// \todo check that this is correct.
/// This regex incomplete. See https://git-scm.com/docs/git-check-ref-format
const static std::string refRegexS = "[a-zA-Z0-9@][a-zA-Z0-9_.\\/@+-]*";
extern std::regex refRegex;

/// A Git revision (a SHA-1 commit hash).
const static std::string revRegexS = "[0-9a-fA-F]{40}";
extern std::regex revRegex;

/// A ref or revision, or a ref followed by a revision.
const static std::string refAndOrRevRegex = "(?:(" + revRegexS + ")|(?:(" + refRegexS + ")(?:/(" + revRegexS + "))?))";

} // namespace nix

#pragma once
///@file

#include <string>
#include <regex>

namespace nix {

// URI stuff.
static const std::string pctEncoded = "(?:%[0-9a-fA-F][0-9a-fA-F])";
static const std::string unreservedRegex = "(?:[a-zA-Z0-9-._~])";
static const std::string subdelimsRegex = "(?:[!$&'\"()*+,;=])";
static const std::string pcharRegex = "(?:" + unreservedRegex + "|" + pctEncoded + "|" + subdelimsRegex + "|[:@])";
static const std::string fragmentRegex = "(?:" + pcharRegex + "|[/? \"^])*";

/// A Git ref (i.e. branch or tag name).
/// \todo check that this is correct.
/// This regex incomplete. See https://git-scm.com/docs/git-check-ref-format
static const std::string refRegexS = "[a-zA-Z0-9@][a-zA-Z0-9_.\\/@+-]*";
extern std::regex refRegex;

/// A Git revision (a SHA-1 commit hash).
static const std::string revRegexS = "[0-9a-fA-F]{40}";
extern std::regex revRegex;

/// A ref or revision, or a ref followed by a revision.
static const std::string refAndOrRevRegex = "(?:(" + revRegexS + ")|(?:(" + refRegexS + ")(?:/(" + revRegexS + "))?))";

} // namespace nix

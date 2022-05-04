#pragma once

#include <string>
#include <string_view>
#include <optional>

// Parses the HEAD ref as reported by `git ls-remote --symref`
//
// Returns the head branch name as reported by `git ls-remote --symref`, e.g., if
// ls-remote returns the output below, "main" is returned based on the ref line.
//
//   ref: refs/heads/main       HEAD
//
// If the repository is in 'detached head' state (HEAD is pointing to a rev
// instead of a branch), parseListReferenceForRev("HEAD") may be used instead.
std::optional<std::string> parseListReferenceHeadRef(std::string_view line);

// Parses a reference line from `git ls-remote --symref`, e.g.,
// parseListReferenceForRev("refs/heads/master", line) will return 6926...
// given the line below.
//
// 6926beab444c33fb57b21819b6642d032016bb1e	refs/heads/master
std::optional<std::string> parseListReferenceForRev(std::string_view rev, std::string_view line);

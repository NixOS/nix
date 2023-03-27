#pragma once

#include <string>
#include <string_view>
#include <optional>

namespace nix {

namespace git {

/**
 * A line from the output of `git ls-remote --symref`.
 *
 * These can be of two kinds:
 *
 * - Symbolic references of the form
 *
 *      ref: {target}	{reference}
 *
 *    where {target} is itself a reference and {reference} is optional
 *
 * - Object references of the form
 *
 *      {target}	{reference}
 *
 *    where {target} is a commit id and {reference} is mandatory
 */
struct LsRemoteRefLine {
    enum struct Kind {
        Symbolic,
        Object
    };
    Kind kind;
    std::string target;
    std::optional<std::string> reference;
};

std::optional<LsRemoteRefLine> parseLsRemoteLine(std::string_view line);

}

}

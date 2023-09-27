#pragma once

#include "nixexpr.hh"

namespace nix {

struct Comment
{
    /// The raw content
    std::string content;

    /// The start point of this comment
    PosIdx start;

    /// The end point of this comment (exclusive)
    PosIdx end;
};

} // namespace nix

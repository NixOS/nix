#pragma once

#include "types.hh"
#include "serialise.hh"
#include "archive.hh"

namespace nix {

void restoreGit(const Path & path, Source & source);

void parseGit(ParseSink & sink, Source & source);

static void parse(ParseSink & sink, Source & source, const Path & path);

}

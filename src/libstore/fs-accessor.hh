#pragma once

#include "types.hh"

namespace nix {

/* An abstract class for accessing a filesystem-like structure, such
   as a (possibly remote) Nix store or the contents of a NAR file. */
class FSAccessor
{
public:
    enum Type { tMissing, tRegular, tSymlink, tDirectory };

    struct Stat
    {
        Type type = tMissing;
        uint64_t fileSize = 0; // regular files only
        bool isExecutable = false; // regular files only
        uint64_t narOffset = 0; // regular files only
    };

    virtual ~FSAccessor() { }

    virtual Stat stat(PathView path) = 0;

    virtual StringSet readDirectory(PathView path) = 0;

    virtual std::string readFile(PathView path) = 0;

    virtual std::string readLink(PathView path) = 0;
};

}

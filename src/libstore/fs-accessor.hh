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
#ifndef _WIN32
        bool isExecutable = false; // regular files only
#endif
        uint64_t narOffset = 0; // regular files only
    };

    virtual Stat stat1(const Path & path) = 0;

    virtual StringSet readDirectory(const Path & path) = 0;

    virtual std::string readFile(const Path & path) = 0;

    virtual std::string readLink(const Path & path) = 0;
};

}

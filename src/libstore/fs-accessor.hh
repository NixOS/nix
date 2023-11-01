#pragma once
///@file

#include "types.hh"
#include "source-accessor.hh"

#include <optional>

namespace nix {

/**
 * An abstract class for accessing a filesystem-like structure, such
 * as a (possibly remote) Nix store or the contents of a NAR file.
 */
class FSAccessor
{
public:
    using Type = SourceAccessor::Type;

    struct Stat
    {
        Type type;
        /**
         * For regular files only: the size of the file.
         */
        uint64_t fileSize = 0;
        /**
         * For regular files only: whether this is an executable.
         */
        bool isExecutable = false;
        /**
         * For regular files only: the position of the contents of this
         * file in the NAR.
         */
        uint64_t narOffset = 0;
    };

    virtual ~FSAccessor() { }

    virtual std::optional<Stat> stat(const Path & path) = 0;

    using DirEntries = SourceAccessor::DirEntries;

    virtual DirEntries readDirectory(const Path & path) = 0;

    /**
     * Read a file inside the store.
     *
     * If `requireValidPath` is set to `true` (the default), the path must be
     * inside a valid store path, otherwise it just needs to be physically
     * present (but not necessarily properly registered)
     */
    virtual std::string readFile(const Path & path, bool requireValidPath = true) = 0;

    virtual std::string readLink(const Path & path) = 0;
};

}

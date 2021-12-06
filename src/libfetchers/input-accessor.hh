#pragma once

#include "ref.hh"
#include "types.hh"
#include "archive.hh"

namespace nix {

struct InputAccessor
{
    const size_t number;

    InputAccessor();

    virtual ~InputAccessor()
    { }

    virtual std::string readFile(std::string_view path) = 0;

    virtual bool pathExists(std::string_view path) = 0;

    enum Type { tRegular, tSymlink, tDirectory, tMisc };

    struct Stat
    {
        Type type = tMisc;
        //uint64_t fileSize = 0; // regular files only
        bool isExecutable = false; // regular files only
    };

    virtual Stat lstat(std::string_view path) = 0;

    typedef std::optional<Type> DirEntry;

    typedef std::map<std::string, DirEntry> DirEntries;

    virtual DirEntries readDirectory(std::string_view path) = 0;

    virtual std::string readLink(std::string_view path) = 0;

    virtual void dumpPath(
        const Path & path,
        Sink & sink,
        PathFilter & filter = defaultPathFilter);
};

ref<InputAccessor> makeFSInputAccessor(const Path & root);

struct SourcePath
{
    ref<InputAccessor> accessor;
    Path path;
};

std::ostream & operator << (std::ostream & str, const SourcePath & path);

}

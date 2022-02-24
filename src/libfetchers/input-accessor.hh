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

    virtual std::string readFile(PathView path) = 0;

    virtual bool pathExists(PathView path) = 0;

    enum Type { tRegular, tSymlink, tDirectory, tMisc };

    struct Stat
    {
        Type type = tMisc;
        //uint64_t fileSize = 0; // regular files only
        bool isExecutable = false; // regular files only
    };

    virtual Stat lstat(PathView path) = 0;

    typedef std::optional<Type> DirEntry;

    typedef std::map<std::string, DirEntry> DirEntries;

    virtual DirEntries readDirectory(PathView path) = 0;

    virtual std::string readLink(PathView path) = 0;

    virtual void dumpPath(
        const Path & path,
        Sink & sink,
        PathFilter & filter = defaultPathFilter);
};

ref<InputAccessor> makeFSInputAccessor(
    const Path & root,
    std::optional<PathSet> && allowedPaths = {});

struct MemoryInputAccessor : InputAccessor
{
    virtual void addFile(PathView path, std::string && contents) = 0;
};

ref<MemoryInputAccessor> makeMemoryInputAccessor();

ref<InputAccessor> makeZipInputAccessor(PathView path);

ref<InputAccessor> makePatchingInputAccessor(
    ref<InputAccessor> next,
    const std::vector<std::string> & patches);

struct SourcePath
{
    ref<InputAccessor> accessor;
    Path path;

    std::string_view baseName() const;

    std::string readFile() const
    { return accessor->readFile(path); }

    bool pathExists() const
    { return accessor->pathExists(path); }
};

std::ostream & operator << (std::ostream & str, const SourcePath & path);

}

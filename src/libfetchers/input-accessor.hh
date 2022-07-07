#pragma once

#include "ref.hh"
#include "types.hh"
#include "archive.hh"
#include "canon-path.hh"

namespace nix {

MakeError(RestrictedPathError, Error);

struct InputAccessor : public std::enable_shared_from_this<InputAccessor>
{
    const size_t number;

    InputAccessor();

    virtual ~InputAccessor()
    { }

    virtual std::string readFile(const CanonPath & path) = 0;

    virtual bool pathExists(const CanonPath & path) = 0;

    enum Type { tRegular, tSymlink, tDirectory, tMisc };

    struct Stat
    {
        Type type = tMisc;
        //uint64_t fileSize = 0; // regular files only
        bool isExecutable = false; // regular files only
    };

    virtual Stat lstat(const CanonPath & path) = 0;

    std::optional<Stat> maybeLstat(const CanonPath & path);

    typedef std::optional<Type> DirEntry;

    typedef std::map<std::string, DirEntry> DirEntries;

    virtual DirEntries readDirectory(const CanonPath & path) = 0;

    virtual std::string readLink(const CanonPath & path) = 0;

    virtual void dumpPath(
        const CanonPath & path,
        Sink & sink,
        PathFilter & filter = defaultPathFilter);

    bool operator == (const InputAccessor & x) const
    {
        return number == x.number;
    }

    bool operator < (const InputAccessor & x) const
    {
        return number < x.number;
    }

    virtual std::string showPath(const CanonPath & path);
};

struct FSInputAccessor : InputAccessor
{
    virtual void checkAllowed(const CanonPath & absPath) = 0;

    virtual void allowPath(CanonPath path) = 0;

    virtual bool hasAccessControl() = 0;
};

typedef std::function<RestrictedPathError(const CanonPath & path)> MakeNotAllowedError;

ref<FSInputAccessor> makeFSInputAccessor(
    const CanonPath & root,
    std::optional<std::set<CanonPath>> && allowedPaths = {},
    MakeNotAllowedError && makeNotAllowedError = {});

struct SourcePath;

struct MemoryInputAccessor : InputAccessor
{
    virtual SourcePath addFile(CanonPath path, std::string && contents) = 0;
};

ref<MemoryInputAccessor> makeMemoryInputAccessor();

ref<InputAccessor> makeZipInputAccessor(const CanonPath & path);

ref<InputAccessor> makePatchingInputAccessor(
    ref<InputAccessor> next,
    const std::vector<std::string> & patches);

struct SourcePath
{
    ref<InputAccessor> accessor;
    CanonPath path;

    std::string_view baseName() const;

    SourcePath parent() const;

    std::string readFile() const
    { return accessor->readFile(path); }

    bool pathExists() const
    { return accessor->pathExists(path); }

    InputAccessor::Stat lstat() const
    { return accessor->lstat(path); }

    std::optional<InputAccessor::Stat> maybeLstat() const
    { return accessor->maybeLstat(path); }

    InputAccessor::DirEntries readDirectory() const
    { return accessor->readDirectory(path); }

    std::string readLink() const
    { return accessor->readLink(path); }

    void dumpPath(
        Sink & sink,
        PathFilter & filter = defaultPathFilter) const
    { return accessor->dumpPath(path, sink, filter); }

    std::string to_string() const
    { return accessor->showPath(path); }

    SourcePath operator + (const CanonPath & x) const
    { return {accessor, path + x}; }

    SourcePath operator + (std::string_view c) const
    {  return {accessor, path + c}; }

    bool operator == (const SourcePath & x) const
    {
        return std::tie(accessor, path) == std::tie(x.accessor, x.path);
    }

    bool operator != (const SourcePath & x) const
    {
        return std::tie(accessor, path) != std::tie(x.accessor, x.path);
    }

    bool operator < (const SourcePath & x) const
    {
        return std::tie(accessor, path) < std::tie(x.accessor, x.path);
    }

    SourcePath resolveSymlinks() const;
};

std::ostream & operator << (std::ostream & str, const SourcePath & path);

}

#pragma once

#include "source-accessor.hh"

namespace nix {

/**
 * A source accessor that just forwards every operation to another
 * accessor. This is not useful in itself but can be used as a
 * superclass for accessors that do change some operations.
 */
struct ForwardingSourceAccessor : SourceAccessor
{
    ref<SourceAccessor> next;

    ForwardingSourceAccessor(ref<SourceAccessor> next)
        : next(next)
    {
    }

    std::string readFile(const CanonPath & path) override
    {
        return next->readFile(path);
    }

    void readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback) override
    {
        next->readFile(path, sink, sizeCallback);
    }

    std::optional<Stat> maybeLstat(const CanonPath & path) override
    {
        return next->maybeLstat(path);
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        return next->readDirectory(path);
    }

    std::string readLink(const CanonPath & path) override
    {
        return next->readLink(path);
    }

    std::string showPath(const CanonPath & path) override
    {
        return next->showPath(path);
    }

    std::optional<std::filesystem::path> getPhysicalPath(const CanonPath & path) override
    {
        return next->getPhysicalPath(path);
    }

    std::optional<std::string> getFingerprint(const CanonPath & path) override
    {
        return next->getFingerprint(path);
    }

    void setFingerprint(std::string fingerprint) override
    {
        next->setFingerprint(std::move(fingerprint));
    }
};

}

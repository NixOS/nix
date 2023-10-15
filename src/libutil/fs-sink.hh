#pragma once
///@file

#include "types.hh"
#include "serialise.hh"

namespace nix {

/**
 * \todo Fix this API, it sucks.
 */
struct ParseSink
{
    virtual void createDirectory(const Path & path) { };

    virtual void createRegularFile(const Path & path) { };
    virtual void closeRegularFile() { };
    virtual void isExecutable() { };
    virtual void preallocateContents(uint64_t size) { };
    virtual void receiveContents(std::string_view data) { };

    virtual void createSymlink(const Path & path, const std::string & target) { };
};

struct RestoreSink : ParseSink
{
    Path dstPath;
    AutoCloseFD fd;


    void createDirectory(const Path & path) override;

    void createRegularFile(const Path & path) override;
    void closeRegularFile() override;
    void isExecutable() override;
    void preallocateContents(uint64_t size) override;
    void receiveContents(std::string_view data) override;

    void createSymlink(const Path & path, const std::string & target) override;
};

}

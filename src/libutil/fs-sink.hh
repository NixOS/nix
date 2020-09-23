#pragma once

#include "types.hh"
#include "serialise.hh"

namespace nix {

/* FIXME: fix this API, it sucks. */
struct ParseSink
{
    virtual void createDirectory(const Path & path) { };

    virtual void createRegularFile(const Path & path) { };
    virtual void createExecutableFile(const Path & path) { };
    virtual void isExecutable() { };
    virtual void preallocateContents(uint64_t size) { };
    virtual void receiveContents(unsigned char * data, size_t len) { };

    virtual void createSymlink(const Path & path, const string & target) { };

    virtual void copyFile(const Path & source) { };
    virtual void copyDirectory(const Path & source, const Path & destination) { };
};

struct RestoreSink : ParseSink
{
    Path dstPath;
    AutoCloseFD fd;


    void createDirectory(const Path & path) override;

    void createRegularFile(const Path & path) override;
    void createExecutableFile(const Path & path) override;
    void isExecutable() override;
    void preallocateContents(uint64_t size) override;
    void receiveContents(unsigned char * data, size_t len) override;

    void createSymlink(const Path & path, const string & target) override;

    void copyFile(const Path & source) override;
    void copyDirectory(const Path & source, const Path & destination) override;
};


}

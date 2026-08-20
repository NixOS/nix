#pragma once
///@file

#include "nix/util/serialise.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/file-system.hh"

namespace nix {

/**
 * Actions on an open regular file in the process of creating it.
 *
 * See `FileSystemObjectSink::createRegularFile`.
 */
struct CreateRegularFileSink : virtual Sink
{
private:
    void anchor() override;

public:
    /**
     * If set to true, the sink will not be called with the contents
     * of the file. `preallocateContents()` will still be called to
     * convey the file size. Useful for sinks that want to efficiently
     * discard the contents of the file.
     */
    bool skipContents = false;

    virtual void isExecutable() = 0;

    /**
     * An optimization. By default, do nothing.
     */
    virtual void preallocateContents(uint64_t size) {};
};

struct FileSystemObjectSink
{
private:
    /* VTable anchor to avoid weak linkage of the vtable - it breaks
       dynamic_cast across shared libraries on Darwin. */
    virtual void anchor();

public:
    virtual ~FileSystemObjectSink() = default;

    virtual void createDirectory(const CanonPath & path) = 0;

    using DirectoryCreatedCallback = fun<void(FileSystemObjectSink & dirSink, const CanonPath & dirRelPath)>;

    /**
     * Create a directory and invoke a callback with a pair of sink + CanonPath
     * of the created subdirectory relative to dirSink.
     *
     * @note This allows for UNIX RestoreSink implementations to implement
     * *at-style accessors that always keep an open file descriptor for the
     * freshly created directory. Use this when it's important to disallow any
     * intermediate path components from being symlinks.
     */
    virtual void createDirectory(const CanonPath & path, DirectoryCreatedCallback callback)
    {
        createDirectory(path);
        callback(*this, path);
    }

    /**
     * This function in general is no re-entrant. Only one file can be
     * written at a time.
     */
    virtual void createRegularFile(const CanonPath & path, fun<void(CreateRegularFileSink &)>) = 0;

    virtual void createSymlink(const CanonPath & path, const std::string & target) = 0;
};

/**
 * An extension of `FileSystemObjectSink` that supports file types
 * that are not supported by Nix's FSO model.
 */
struct ExtendedFileSystemObjectSink : virtual FileSystemObjectSink
{
private:
    void anchor() override;

public:
    /**
     * Create a hard link. The target must be the path of a previously
     * encountered file relative to the root of the FSO.
     */
    virtual void createHardlink(const CanonPath & path, const CanonPath & target) = 0;
};

/**
 * Recursively copy file system objects from the source into the sink.
 */
void copyRecursive(
    SourceAccessor & accessor, const CanonPath & sourcePath, FileSystemObjectSink & sink, const CanonPath & destPath);

/**
 * Ignore everything and do nothing
 */
struct NullFileSystemObjectSink : FileSystemObjectSink
{
private:
    void anchor() override;

public:
    void createDirectory(const CanonPath & path) override {}

    void createSymlink(const CanonPath & path, const std::string & target) override {}

    void createRegularFile(const CanonPath & path, fun<void(CreateRegularFileSink &)>) override;
};

class RestoreSinkHooks;

/**
 * Write files at the given path
 */
struct RestoreSink : FileSystemObjectSink
{
private:
    void anchor() override;

    RestoreSinkHooks * hooks = nullptr;

public:
    std::filesystem::path dstPath;
    /**
     * File descriptor for the directory located at dstPath. Used for *at
     * operations relative to this file descriptor. This sink must *never*
     * follow intermediate symlinks (starting from dstPath) in case a file
     * collision is encountered for various reasons like case-insensitivity or
     * other types on normalization. using appropriate *at system calls and traversing
     * only one path component at a time ensures that writing is race-free and is
     * is not susceptible to symlink replacement.
     */
    AutoCloseFD dirFd;
    bool startFsync = false;

    explicit RestoreSink(bool startFsync, RestoreSinkHooks * hooks = nullptr)
        : hooks{hooks}
        , startFsync{startFsync}
    {
    }

    /**
     * @todo Remove. Only keep the callback driven function (at least for recursive traversal).
     */
    void createDirectory(const CanonPath & path) override;

    void createDirectory(const CanonPath & path, DirectoryCreatedCallback callback) override;

    void createRegularFile(const CanonPath & path, fun<void(CreateRegularFileSink &)>) override;

    void createSymlink(const CanonPath & path, const std::string & target) override;
};

/**
 * Hooks invoked when filesystem objects created by `RestoreSink` are complete,
 * so callers can post-process them (e.g. metadata canonicalisation). By doing
 * this in the hooks, the whole operation can be done in a single recursive tree
 * traversal during unpacking.
 *
 * @todo Do something on Windows.
 */
class RestoreSinkHooks
{
    virtual void anchor();

public:
    virtual ~RestoreSinkHooks() = default;

    /**
     * @brief Called when the callback passed to @ref nix::RestoreSink::createDirectory()
     * returns.
     *
     * Naturally, this means that recursive copying and @ref nix::restorePath()
     * drives these in post-order when walking up the directory tree. This makes
     * it suitable for canonicalising permissions of directories.
     *
     * @param dirFd File descriptor of the directory.
     */
    virtual void directoryDone(Descriptor dirFd) = 0;

    /**
     * Called when the callback passed to @ref nix::RestoreSink::createRegularFile() returns.
     *
     * @param fd File descriptor of the file.
     * @param executable Whether @ref nix::CreateRegularFileSink::isExecutable() was called.
     */
    virtual void regularFileCreated(Descriptor fd, bool executable) = 0;

    /**
     * @brief Called after a symlink is created.
     *
     * @param parentFd Directory file descriptor of the symlink parent (immediate one).
     * @param name Relative path of the symlink beneath the parent directory.
     */
    virtual void symlinkCreated(Descriptor parentFd, const CanonPath & name) = 0;
};

/**
 * Restore a single file at the top level, passing along
 * `receiveContents` to the underlying `Sink`. For anything but a single
 * file, set `regular = true` so the caller can fail accordingly.
 */
struct RegularFileSink : FileSystemObjectSink
{
private:
    void anchor() override;

public:
    bool regular = true;
    Sink & sink;

    RegularFileSink(Sink & sink)
        : sink(sink)
    {
    }

    void createDirectory(const CanonPath & path) override
    {
        /* FIXME: Throw an error here. */
        regular = false;
    }

    void createSymlink(const CanonPath & path, const std::string & target) override
    {
        /* FIXME: Throw an error here. */
        regular = false;
    }

    void createRegularFile(const CanonPath & path, fun<void(CreateRegularFileSink &)>) override;
};

} // namespace nix

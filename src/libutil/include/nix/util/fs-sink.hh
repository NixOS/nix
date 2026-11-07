#pragma once
///@file

#include "nix/util/file-descriptor.hh"
#include "nix/util/serialise.hh"
#include "nix/util/source-accessor.hh"
#include "nix/util/file-system.hh"
#include "nix/util/os-filename.hh"

namespace nix {

/**
 * A way to directly and recursively sink file system objects
 *
 * This is a very well-restricted interface to recursively create file
 * system objects "bottom up", and "one layer at a time". For example of
 * some rules this enforces:
 *
 *  - Parent directories must be created before children.
 *
 *  - Child names must be picked before child contents (including file
 *    type).
 *
 *  - Metadata like the executable bit on files must be chosen before
 *    file contents are streamed.
 *
 *  As such, this interface is very good for two tasks in particular:
 *
 *  - Parsing Nix Archives, as the NAR format exactly corresponds to
 *    this. (You can think of this as a NAR Visitor.)
 *
 *  - Creating files on disk, as the above invariants give us the
 *    opportunity to create files in a very controlled manner, avoiding
 *    all sorts of security issues.
 *
 *  What this is *not* good for is:
 *
 *  - Unpacking unstructured file formats like tarballs. (See `TarSink`
 *    for that.)
 *
 *  - Merkle formats/protocols that do not want to consume all layers at
 *    once, but support more asynchronous and lazy IO. (See
 *    `merkle::FileSinkBuilder` for that.)
 */
struct FileSystemObjectSink
{
private:
    /* VTable anchor to avoid weak linkage of the vtable - it breaks
       dynamic_cast across shared libraries on Darwin. */
    virtual void anchor();

public:
    /**
     * Actions on an open regular file in the process of creating it.
     *
     * See `FileSystemObjectSink::createRegularFile`.
     */
    struct OnRegularFile : virtual Sink
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

        /**
         * An optimization. By default, do nothing.
         */
        virtual void preallocateContents(uint64_t size) {};
    };

    /**
     * Actions on an open directory in order to populate it with its
     * children.
     */
    struct OnDirectory
    {
    private:
        virtual void anchor();

    public:
        virtual ~OnDirectory() = default;

        using ChildCreatedCallback = fun<void(FileSystemObjectSink & fsoSink)>;

        /**
         * Create a child of this directory. The callback provides us with a
         * `FileSystemObjectSink` we can think of as "pointing" to the location
         * which we have chosen, allowing us to subsequently choose what we
         * want to put there.
         *
         * @param fileName must not contain any `/` or null bytes. It should
         * also not contain any `\` when Windows host filesystems aim to be
         * supported.
         */
        virtual void createChild(std::string_view fileName, ChildCreatedCallback callback) = 0;
    };

    virtual ~FileSystemObjectSink() = default;

    using DirectoryCreatedCallback = fun<void(OnDirectory & dirSink)>;

    /**
     * Create a directory and invoke a callback with an `OnDirectory`
     * for populating it.
     *
     * @note This allows for `RestoreSink` to implement
     * *at-style accessors that always keep an open file descriptor for the
     * freshly created directory. Use this when it's important to disallow any
     * intermediate path components from being symlinks.
     */
    virtual void createDirectory(DirectoryCreatedCallback callback) = 0;

    using RegularFileCreatedCallback = fun<void(OnRegularFile & regFileSink)>;

    /**
     * This function in general is no re-entrant. Only one file can be
     * written at a time.
     *
     * @param isExecutable whether the file should be marked executable
     */
    virtual void createRegularFile(bool isExecutable, RegularFileCreatedCallback callback) = 0;

    virtual void createSymlink(const std::string & target) = 0;
};

/**
 * Recursively copy file system objects from the source into the sink.
 */
void copyRecursive(SourceAccessor & accessor, const CanonPath & sourcePath, FileSystemObjectSink & sink);

/**
 * Ignore everything and do nothing
 */
struct NullFileSystemObjectSink : FileSystemObjectSink
{
private:
    void anchor() override;

public:
    void createDirectory(DirectoryCreatedCallback) override;

    void createSymlink(const std::string & target) override {}

    void createRegularFile(bool isExecutable, RegularFileCreatedCallback) override;
};

class RestoreSinkHooks;

/**
 * Write files at the given path
 *
 * This sink must *never* follow intermediate symlinks in case a file collision
 * is encountered for various reasons like case-insensitivity or other types of
 * normalization. Using appropriate *at system calls and traversing only one
 * path component at a time ensures that writing is race-free and is not
 * susceptible to symlink replacement.
 */
struct RestoreSink : FileSystemObjectSink
{
private:
    void anchor() override;

    RestoreSinkHooks * hooks = nullptr;

    /**
     * Owns `parentDir`, when this sink was made by `forPath`.
     */
    AutoCloseFD ownedParentDir;

public:
    /**
     * The directory to create the entry in.
     *
     * This is a borrowed descriptor (unless this sink was made by
     * `forPath`); the caller must ensure it remains valid for the
     * lifetime of operations on this sink.
     */
    Descriptor parentDir;

    /**
     * Name of the child to create within the parent directory.
     */
    OsFilename name;

    bool startFsync = false;

    /**
     * Implementation of `OnDirectory` for `RestoreSink`.
     */
    struct Directory : OnDirectory
    {
        AutoCloseFD directory;
        bool startFsync;
        RestoreSinkHooks * hooks;

        Directory(AutoCloseFD directory, bool startFsync = false, RestoreSinkHooks * hooks = nullptr)
            : directory{std::move(directory)}
            , startFsync{startFsync}
            , hooks{hooks}
        {
        }

        void createChild(std::string_view name, ChildCreatedCallback callback) override;
    };

    RestoreSink(Descriptor parentDir, OsFilename name, bool startFsync = false, RestoreSinkHooks * hooks = nullptr)
        : hooks{hooks}
        , parentDir{parentDir}
        , name{std::move(name)}
        , startFsync{startFsync}
    {
    }

    /**
     * Make a sink that creates @p dstPath, by opening the parent
     * directory of @p dstPath.
     *
     * @throws Error if @p dstPath is empty, or its last component is
     * not a valid file name (e.g. `.` or `..`).
     */
    static RestoreSink
    forPath(const std::filesystem::path & dstPath, bool startFsync, RestoreSinkHooks * hooks = nullptr);

    void createDirectory(DirectoryCreatedCallback callback) override;

    void createRegularFile(bool isExecutable, RegularFileCreatedCallback callback) override;

    void createSymlink(const std::string & target) override;
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
     * returns, i.e. once the directory has been populated.
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
     * @param executable Whether the file was created executable.
     */
    virtual void regularFileCreated(Descriptor fd, bool executable) = 0;

    /**
     * @brief Called after a symlink is created.
     *
     * @param parentFd Directory file descriptor of the symlink parent (immediate one).
     * @param name Name of the symlink within the parent directory.
     */
    virtual void symlinkCreated(Descriptor parentFd, const OsFilename & name) = 0;
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

    void createDirectory(DirectoryCreatedCallback) override
    {
        /* FIXME: Throw an error here. */
        regular = false;
    }

    void createSymlink(const std::string & target) override
    {
        /* FIXME: Throw an error here. */
        regular = false;
    }

    void createRegularFile(bool isExecutable, RegularFileCreatedCallback callback) override;
};

} // namespace nix

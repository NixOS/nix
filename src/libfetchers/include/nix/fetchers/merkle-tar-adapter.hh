#pragma once
///@file

#include "nix/util/merkle-files.hh"
#include "nix/util/ref.hh"
#include "nix/util/tarfile.hh"

#include <memory>

namespace nix::merkle {

/**
 * A merkle regular file sink that can be finalized to get the content hash.
 */
struct RegularFileSinkWithFinalize : virtual Sink
{
    /**
     * Finish the file, and return the hash of the blob.
     */
    virtual Hash finalize() && = 0;
};

/**
 * A merkle directory sink that can be finalized to get the tree hash.
 */
struct DirectorySinkWithFinalize : merkle::DirectorySink
{
    /**
     * Finish the directory, and return its tree hash.
     */
    virtual Hash finalize() && = 0;
};

/**
 * Interface for creating merkle file system object sinks.
 *
 * By returning sinks rather than taking callbacks, we allow starting
 * and finishing sink-based creation in an asynchronous manner. This is
 * crucial to being able to adapt messy providers of file system object
 * data (like tarballs) to this interface.
 *
 * A directory can only refer to objects that are already written, so
 * every sink for a directory's children must be finalized before it is
 * created. Implementations are responsible for whatever that ordering
 * requires of them --- see @ref nix::merkle::FileSinkBuilder::makeDirectorySink.
 */
struct FileSinkBuilder
{
    virtual ~FileSinkBuilder() = default;

    /**
     * Create a sink for one directory.
     *
     * Every sink handed out before this call must have been finalized:
     * an implementation is free to assume that, and to do whatever work
     * making earlier objects referenceable requires.
     */
    virtual std::unique_ptr<DirectorySinkWithFinalize> makeDirectorySink() = 0;

    virtual std::unique_ptr<RegularFileSinkWithFinalize> makeRegularFileSink() = 0;

    virtual merkle::TreeEntry makeSymlink(const std::string & target) = 0;

    /**
     * Finish any writes that are still in progress.
     *
     * Until this is called, objects written through this builder are not
     * necessarily available to anything else that reads the same store.
     */
    virtual void flush() = 0;
};

/**
 * Adapter that implements TarSink by building a merkle tree.
 */
struct TarAdapter : TarSink
{
    virtual merkle::TreeEntry flush() = 0;
};

/**
 * Create a TarSink that builds a merkle tree from path-based tar entries.
 *
 * @param store Factory for creating merkle sinks
 */
ref<TarAdapter> makeTarSink(FileSinkBuilder & store);

} // namespace nix::merkle

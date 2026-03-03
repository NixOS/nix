#pragma once
///@file

#include "nix/util/merkle-files.hh"
#include "nix/util/ref.hh"
#include "nix/util/tarfile.hh"

namespace nix::merkle {

/**
 * A merkle regular file sink that can be flushed to get the content hash.
 */
struct RegularFileSinkWithFlush : virtual Sink
{
    /**
     * Flush and return the hash of the blob.
     */
    virtual Hash flush() && = 0;
};

/**
 * A merkle directory sink that can be flushed to get the tree hash.
 */
struct DirectorySinkWithFlush : merkle::DirectorySink
{
    /**
     * Flush the directory and return its tree hash.
     */
    virtual Hash flush() && = 0;
};

/**
 * Interface for creating merkle file system object sinks.
 *
 * By returning sinks rather than taking callbacks, we allow starting
 * and finishing sink-based creation in an asynchronous manner. This is
 * crucial to being able to adapt messy providers of file system object
 * data (like tarballs) to this interface.
 */
struct FileSinkBuilder
{
    virtual ~FileSinkBuilder() = default;

    virtual ref<DirectorySinkWithFlush> makeDirectorySink() = 0;
    virtual ref<RegularFileSinkWithFlush> makeRegularFileSink() = 0;
    virtual merkle::TreeEntry makeSymlink(const std::string & target) = 0;

    /**
     * Flush all pending writes to persistent storage, and set whether
     * subsequently created sinks may reference objects produced by
     * previously created (and completed) sinks.
     *
     * When @p allow is false, the builder may apply optimizations
     * that assume sinks are write-independent.
     *
     * When @p allow is true, those optimizations are disabled so that
     * new sinks can look up objects written by earlier ones.
     */
    virtual void flushAndSetAllowDependentCreation(bool allow) = 0;
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

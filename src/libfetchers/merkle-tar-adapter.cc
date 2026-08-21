#include "nix/fetchers/merkle-tar-adapter.hh"
#include "nix/util/chunked-vector.hh"
#include "nix/util/thread-pool.hh"
#include "nix/util/util.hh"

#include <cassert>
#include <map>
#include <optional>
#include <thread>
#include <variant>

namespace nix {

/* Everything here is local to this file: the names are generic enough
   that they would otherwise be a hazard in a unity build. */
namespace {

struct Directory;

/**
 * A file or symlink.
 *
 * The hash lives in a slot of its own rather than in the tree, because a
 * file is written while the rest of the archive is still being parsed,
 * and that parsing may overwrite this entry in the meantime. Overwriting
 * then just drops the pointer: the write finishes into a slot that nobody
 * reads. It also means a hard link can share the slot rather than copy
 * the hash out of it.
 *
 * @note A symlink is not sunk to the store asynchronously, but always
 * written in one go, because it is presumed to be short; its slot is
 * therefore filled in from the start.
 */
struct File
{
    /**
     * Empty until whatever is writing the file has finished.
     */
    std::optional<Hash> * hash;
    merkle::Mode mode;
};

/**
 * A hard link, resolved once its target has a hash.
 */
struct PendingHardlink
{
    CanonPath target;
};

/**
 * An entry in a directory: another directory, a file, or a hard link to
 * one.
 */
using Child = std::variant<Directory, File, PendingHardlink>;

/**
 * A directory, to be written once all of its children are known.
 */
struct Directory
{
    std::map<std::string, Child, std::less<>> children;

    /**
     * Filled in when the directory is written, which cannot happen until
     * every child has a hash.
     */
    std::optional<Hash> hash;
};

/**
 * Adapter that builds a merkle tree from the flat, path-addressed
 * entries of a tar archive.
 *
 * 1. During tar parsing, build a tree of `Directory`, creating parents
 *    as needed and applying the overwrite semantics of `addNode`. Each
 *    file is handed to the store as soon as it has been parsed.
 * 2. At flush time:
 *    a. Wait for those writes to finish
 *    b. Resolve hard links
 *    c. Write the directories bottom-up (children before parents)
 */
struct TarAdapterImpl : merkle::TarAdapter
{
    merkle::FileSinkBuilder & store;

    /**
     * The root of the tree. Not necessarily a directory: a tar archive
     * may contain a single file (or symlink) and nothing else.
     */
    Child root = Directory{};

    /**
     * Hard links are resolved during flush, once their targets have
     * hashes. Doubles as the worklist for that.
     */
    std::map<CanonPath, CanonPath> hardLinks;

    /**
     * Whether anything at all was created. An archive containing just
     * the root directory is not empty.
     */
    bool created = false;

    /**
     * The hash of each file being written, filled in by the writer.
     *
     * Chunked so that the pointers we hand out stay put as more files
     * are added.
     */
    static constexpr size_t chunkSize = 1024;
    ChunkedVector<std::optional<Hash>, chunkSize, std::numeric_limits<uint32_t>::max() / chunkSize> results;

    /**
     * Writes the files as they are parsed, so that hashing them overlaps
     * with parsing the rest of the archive.
     *
     * Bounded, because each worker occupies a separate writer in the
     * store, which is not necessarily free to have around.
     *
     * Declared last, so that the workers are joined before anything they
     * write into is destroyed.
     */
    ThreadPool workers{std::min(std::thread::hardware_concurrency(), 10U)};

    TarAdapterImpl(merkle::FileSinkBuilder & store)
        : store(store)
    {
    }

    /**
     * Look up an existing entry.
     *
     * @throws if it does not exist, or has a non-directory parent.
     */
    Child & lookup(const CanonPath & path)
    {
        auto * cur = &root;
        for (auto & name : path) {
            auto * dir = std::get_if<Directory>(cur);
            if (!dir)
                throw Error("path '%s' has a non-directory parent", path);
            auto i = dir->children.find(name);
            if (i == dir->children.end())
                throw Error("path '%s' does not exist", path);
            cur = &i->second;
        }
        return *cur;
    }

    /**
     * Insert an entry, creating parent directories as needed, and
     * applying tarball unpacking overwrite semantics.
     *
     * We behave like libarchive without `ARCHIVE_EXTRACT_NO_OVERWRITE`:
     * the last entry wins. The exception is a non-empty directory:
     * libarchive just tries to `unlink()` whatever is in the way, which
     * only succeeds for an empty directory, so replacing a non-empty one
     * is an error.
     */
    void addNode(const CanonPath & path, Child && child)
    {
        auto * cur = &root;

        if (auto parent = path.parent()) {
            for (auto & name : *parent) {
                auto * dir = std::get_if<Directory>(cur);
                if (!dir)
                    throw Error("parent of '%s' is not a directory", path);
                cur = &dir->children.emplace(std::string(name), Directory{}).first->second;
            }

            auto * dir = std::get_if<Directory>(cur);
            if (!dir)
                throw Error("parent of '%s' is not a directory", path);

            std::string name(*path.baseName());
            auto i = dir->children.find(name);
            if (i == dir->children.end()) {
                dir->children.emplace(std::move(name), std::move(child));
                return;
            }
            cur = &i->second;
        }

        /* Overwriting something that is already there. */
        if (auto * prev = std::get_if<Directory>(cur)) {
            /* "Replacing" a directory with a directory is always a-ok. */
            if (std::holds_alternative<Directory>(child))
                return;

            if (!prev->children.empty())
                throw Error("cannot create '%s', conflicting non-empty directory", path);
        }

        *cur = std::move(child);
    }

    void createDirectory(const CanonPath & path) override
    {
        created = true;
        addNode(path, Directory{});
    }

    void createRegularFile(const CanonPath & path, bool isExecutable, fun<void(Sink &)> callback) override
    {
        created = true;

        auto sink = store.makeRegularFileSink();
        callback(*sink);

        /* The file is complete, so start writing it now: that way the
           store is busy with it while we parse the rest of the archive,
           and it lets go of whatever resources it holds for this file as
           soon as it is done. */
        auto & hash = results.add().first;
        workers.enqueue([sink = std::move(sink), &hash]() { hash = std::move(*sink).finalize(); });

        auto mode = isExecutable ? merkle::Mode::Executable : merkle::Mode::Regular;
        addNode(path, File{&hash, mode});
    }

    void createSymlink(const CanonPath & path, const std::string & target) override
    {
        created = true;

        auto entry = store.makeSymlink(target);
        addNode(path, File{&results.add(entry.hash).first, entry.mode});
    }

    void createHardlink(const CanonPath & path, const CanonPath & target) override
    {
        created = true;
        /* Kept in the tree as well as in `hardLinks`, so that it is seen
           by anything that looks at this path in the meantime. */
        addNode(path, PendingHardlink{target});
        hardLinks.insert_or_assign(path, target);
    }

    /**
     * Call `f` on every entry in the tree, children before parents.
     */
    void visit(Child & child, const fun<void(Child &)> & f)
    {
        if (auto * dir = std::get_if<Directory>(&child))
            for (auto & [name, sub] : dir->children)
                visit(sub, f);
        f(child);
    }

    merkle::TreeEntry flush() override
    {
        if (!created)
            throw Error("tar archive is empty");

        /* Wait for the files to be written. An exception in a worker is
           propagated here, so every hash below is filled in. */
        workers.process();

        /* Create the hard links. Loop until they are all resolved, so
           that a hard link to a hard link works. */
        while (!hardLinks.empty()) {
            std::map<CanonPath, CanonPath> stillPending;

            for (auto & [path, target] : hardLinks) {
                try {
                    std::visit(
                        overloaded{
                            [&](Directory &) { throw Error("cannot create a hard link to a directory"); },
                            /* Share the hash slot, rather than copying it. */
                            [&](File & file) { addNode(path, File{file}); },
                            /* The target is itself a hard link that we have
                               not resolved yet. */
                            [&](PendingHardlink &) { stillPending.insert_or_assign(path, target); },
                        },
                        lookup(target));
                } catch (Error & e) {
                    e.addTrace(nullptr, "while creating a hard link from '%s' to '%s'", path, target);
                    throw;
                }
            }

            if (stillPending.size() == hardLinks.size())
                throw Error(
                    "cannot create a hard link from '%s' to '%s': hard link cycle detected",
                    stillPending.begin()->first,
                    stillPending.begin()->second);

            hardLinks = std::move(stillPending);
        }

        /* Write the directories, children before parents. */
        visit(root, [&](Child & child) {
            auto * dir = std::get_if<Directory>(&child);
            if (!dir)
                return;

            auto dirSink = store.makeDirectorySink();
            for (auto & [name, sub] : dir->children)
                dirSink->insertChild(name, toEntry(sub));
            dir->hash = std::move(*dirSink).finalize();
        });

        store.flush();

        return toEntry(root);
    }

    /**
     * The entry for a child that has been written.
     */
    static merkle::TreeEntry toEntry(const Child & child)
    {
        return std::visit(
            overloaded{
                [](const Directory & dir) { return merkle::TreeEntry{merkle::Mode::Directory, dir.hash.value()}; },
                [](const File & file) {
                    /* Filled in by `flush`, which waits for the writers
                       before it gets here. */
                    assert(file.hash->has_value());
                    return merkle::TreeEntry{file.mode, **file.hash};
                },
                [](const PendingHardlink &) -> merkle::TreeEntry { unreachable(); },
            },
            child);
    }
};

} // namespace

} // namespace nix

namespace nix::merkle {

ref<TarAdapter> makeTarSink(FileSinkBuilder & store)
{
    return make_ref<nix::TarAdapterImpl>(store);
}

} // namespace nix::merkle

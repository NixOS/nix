#include "nix/fetchers/merkle-tar-adapter.hh"
#include "nix/util/chunked-vector.hh"
#include "nix/util/thread-pool.hh"
#include "nix/util/strings.hh"
#include "nix/util/util.hh"

#include <cassert>
#include <map>
#include <optional>
#include <memory>
#include <set>
#include <utility>
#include <thread>
#include <variant>

/**
 * Implementation of `nix::merkle::makeTarSink`.
 *
 * A namespace of its own because the names within are generic: internal
 * linkage alone would not keep them from colliding in a unity build.
 */
namespace nix::merkle::tar {

/**
 * Still want internal linkage too, though.
 */
namespace {

struct Directory;

/**
 * A regular file.
 *
 * The hash lives in a slot of its own rather than in the tree, because a
 * file is written while the rest of the archive is still being parsed,
 * and that parsing may overwrite this entry in the meantime. Overwriting
 * then just drops the pointer: the write finishes into a slot that nobody
 * reads. It also means a hard link can share the slot rather than copy
 * the hash out of it.
 */
struct File
{
    /**
     * Empty until whatever is writing the file has finished.
     */
    std::optional<Hash> * hash;

    bool executable;

    merkle::Mode mode() const
    {
        return executable ? merkle::Mode::Executable : merkle::Mode::Regular;
    }
};

/**
 * A symlink.
 *
 * Unlike a regular file, it is not sunk to the store asynchronously but
 * written in one go, because it is presumed to be short --- so it has its
 * hash from the start, and needs no slot.
 */
struct Symlink
{
    Hash hash;

    merkle::Mode mode() const
    {
        return merkle::Mode::Symlink;
    }
};

/**
 * An entry in a directory: another directory, a file, or a symlink. A
 * hard link is resolved when it is created, and so is just another one of
 * whatever it was made from.
 */
using Child = std::variant<Directory, File, Symlink>;

/**
 * A directory, to be written once all of its children are known.
 */
struct Directory
{
    std::map<std::string, Child, std::less<>> children;

    merkle::Mode mode() const
    {
        return merkle::Mode::Directory;
    }
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
     * Whether anything at all was created. It distinguishes an empty
     * tarball from a tarball with a root directory that is empty.
     */
    bool created = false;

    /**
     * Groups of paths that name the same file, because hard links were
     * made between them. Every path in a group maps to the whole group,
     * so that replacing one of them can say what else it was linked to.
     *
     *  addNode, which is where a group loses a member.
     */
    std::map<CanonPath, std::shared_ptr<std::set<CanonPath>>> hardLinked;

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
     * The entry at `path`, or null if there is none --- including when
     * something along the way is not a directory.
     */
    Child * lookup(const CanonPath & path)
    {
        auto * cur = &root;
        for (auto & name : path) {
            auto * dir = std::get_if<Directory>(cur);
            if (!dir)
                return nullptr;
            auto i = dir->children.find(name);
            if (i == dir->children.end())
                return nullptr;
            cur = &i->second;
        }
        return cur;
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

        if (auto i = hardLinked.find(path); i != hardLinked.end()) {
            auto group = i->second;
            hardLinked.erase(i);
            group->erase(path);

            warn(
                "'%s' is replaced, though a hard link makes it the same file as %s. Older versions of Nix "
                "resolved hard links differently, so this archive unpacks to a different store path than it "
                "used to.",
                path,
                concatMapStringsSep(", ", *group, [](const CanonPath & other) { return quoteString(other.abs()); }));

            /* One path on its own is not linked to anything any more. */
            if (group->size() < 2)
                for (auto & other : *group)
                    hardLinked.erase(other);
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

        addNode(path, File{.hash = &hash, .executable = isExecutable});
    }

    void createSymlink(const CanonPath & path, const std::string & target) override
    {
        created = true;

        addNode(path, Symlink{.hash = store.makeSymlink(target).hash});
    }

    void createHardlink(const CanonPath & path, const CanonPath & target) override
    {
        created = true;

        try {
            /* Resolved now rather than at flush, because a hard link refers
               to whatever is at `target` at this point in the archive, the
               way it would refer to an inode. A later entry replacing
               `target` gets a slot of its own, so this link keeps pointing
               at the old one --- which is what libarchive does, as it
               unlinks the file in the way rather than writing through it. */
            auto * child = lookup(target);

            if (!child)
                throw Error("target does not exist");

            std::visit(
                overloaded{
                    [&](const Directory &) { throw Error("target is a directory"); },
                    /* Share the target's hash slot, rather than copying the
                       hash out of it --- which is also why a link made
                       before the target has finished being written works. */
                    [&](const File & file) { addNode(path, File{file}); },
                    [&](const Symlink & link) { addNode(path, Symlink{link}); },
                },
                std::as_const(*child));

            /* `path` and `target` are now names for one file. */
            auto group = [&] {
                if (auto i = hardLinked.find(target); i != hardLinked.end())
                    return i->second;
                auto fresh = std::make_shared<std::set<CanonPath>>(std::set<CanonPath>{target});
                hardLinked.emplace(target, fresh);
                return fresh;
            }();
            group->insert(path);
            hardLinked.insert_or_assign(path, group);
        } catch (Error & e) {
            e.addTrace(nullptr, "while creating a hard link from '%s' to '%s'", path, target);
            throw;
        }
    }

    merkle::TreeEntry flush() override
    {
        if (!created)
            warn(
                "tar archive is empty; interpreting it as an empty directory for now, but this may become an error in the future");

        /* Wait for the files to be written. An exception in a worker is
           propagated here, so every hash below is filled in. */
        workers.process();

        /* Write the directories, children before parents, which the
           recursion gives us for free. */
        auto entry = [&store = store](this const auto & write, const Child & child) -> merkle::TreeEntry {
            return {
                .mode = std::visit([](const auto & sub) { return sub.mode(); }, child),
                .hash = std::visit(
                    overloaded{
                        [&](const Directory & dir) {
                            auto dirSink = store.makeDirectorySink();
                            for (auto & [name, sub] : dir.children)
                                dirSink->insertChild(name, write(sub));
                            return std::move(*dirSink).finalize();
                        },
                        [&](const File & file) {
                            /* Filled in by the writers we waited for just above. */
                            assert(file.hash->has_value());
                            return **file.hash;
                        },
                        [&](const Symlink & link) { return link.hash; },
                    },
                    child),
            };
        }(root);

        store.flush();

        return entry;
    }
};

} // namespace

} // namespace nix::merkle::tar

namespace nix::merkle {

ref<TarAdapter> makeTarSink(FileSinkBuilder & store)
{
    return make_ref<tar::TarAdapterImpl>(store);
}

} // namespace nix::merkle

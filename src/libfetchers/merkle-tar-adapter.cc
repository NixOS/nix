#include "nix/fetchers/merkle-tar-adapter.hh"
#include "nix/util/sync.hh"
#include "nix/util/thread-pool.hh"
#include "nix/util/util.hh"

#include <map>
#include <optional>
#include <set>
#include <variant>

namespace nix {

/**
 * Entry types in the flat path map.
 */

/**
 * A file whose content is still being written to a sink.
 * Will be flushed in Phase 1 and converted to CompletedFile.
 */
struct PendingFile
{
    ref<merkle::RegularFileSinkWithFlush> sink;
    merkle::Mode mode;
};

/**
 * A file or symlink that has been flushed and has a hash.
 *
 * @note Symlinks are not asynchronously sunk to the store, but always written
 * in one go, because they are presumed to be short.
 */
struct CompletedFile
{
    Hash hash;
    merkle::Mode mode;
};

/**
 * Hardlinks store the target path and are resolved during flush.
 */
struct PendingHardlink
{
    CanonPath target;
};

/**
 * Marks a directory entry. Content determined by children.
 */
struct DirectoryMarker
{};

using Entry = std::variant<PendingFile, CompletedFile, PendingHardlink, DirectoryMarker>;

/**
 * Simple tar adapter using a flat path map.
 *
 * 1. During tar parsing, builds a flat map of CanonPath -> Entry
 * 2. At flush time:
 *    a. Flush all file sinks in parallel
 *    b. Build directories using processGraph (children before parents)
 */
struct TarAdapterImpl : merkle::TarAdapter
{
    merkle::FileSinkBuilder & store;

    /**
     * Flat map from path to entry.
     * Includes explicit files, symlinks, and directories.
     * Parent directories are created implicitly when adding children.
     */
    std::map<CanonPath, Entry> entries;

    TarAdapterImpl(merkle::FileSinkBuilder & store)
        : store(store)
    {
    }

    /**
     * Ensure all ancestor directories exist in the map.
     * Throws if an ancestor already exists as a non-directory.
     */
    void ensureParentDirs(const CanonPath & path)
    {
        for (auto ancestor = path.parent(); ancestor; ancestor = ancestor->parent()) {
            auto [it, inserted] = entries.try_emplace(*ancestor, DirectoryMarker{});
            if (!inserted && !std::holds_alternative<DirectoryMarker>(it->second)) {
                throw Error("cannot create '%s': ancestor '%s' is not a directory", path, *ancestor);
            }
        }
        // Ensure root is a directory if we have any non-root paths
        if (!path.isRoot()) {
            auto [it, inserted] = entries.try_emplace(CanonPath::root, DirectoryMarker{});
            if (!inserted && !std::holds_alternative<DirectoryMarker>(it->second)) {
                throw Error("cannot create '%s': root is not a directory", path);
            }
        }
    }

    void createDirectory(const CanonPath & path) override
    {
        ensureParentDirs(path);
        entries.insert_or_assign(path, DirectoryMarker{});
    }

    void createRegularFile(const CanonPath & path, bool isExecutable, fun<void(Sink &)> callback) override
    {
        auto sink = store.makeRegularFileSink();
        callback(*sink);

        auto mode = isExecutable ? merkle::Mode::Executable : merkle::Mode::Regular;
        ensureParentDirs(path);
        entries.insert_or_assign(path, PendingFile{sink, mode});
    }

    void createSymlink(const CanonPath & path, const std::string & target) override
    {
        auto entry = store.makeSymlink(target);
        ensureParentDirs(path);
        entries.insert_or_assign(path, CompletedFile{entry.hash, entry.mode});
    }

    void createHardlink(const CanonPath & path, const CanonPath & target) override
    {
        ensureParentDirs(path);
        entries.insert_or_assign(path, PendingHardlink{target});
    }

    merkle::TreeEntry flush() override
    {
        if (entries.empty())
            throw Error("tar archive is empty");

        // Phase 1: Flush all PendingFile sinks in parallel, collect hardlink paths
        std::set<CanonPath> pendingHardlinks;
        {
            ThreadPool pool;
            for (auto & [path, entry] : entries) {
                if (std::holds_alternative<PendingFile>(entry)) {
                    pool.enqueue([&entry]() {
                        auto & file = std::get<PendingFile>(entry);
                        Hash hash = std::move(*file.sink).flush();
                        entry = CompletedFile{hash, file.mode};
                    });
                } else if (std::holds_alternative<PendingHardlink>(entry)) {
                    pendingHardlinks.insert(path);
                }
            }
            pool.process();
        }

        // Phase 2: Resolve hardlinks, loop until all resolved (handles chains)
        while (!pendingHardlinks.empty()) {
            std::set<CanonPath> stillPending;
            for (auto & path : pendingHardlinks) {
                auto & entry = entries.at(path);
                auto * linkP = std::get_if<PendingHardlink>(&entry);
                assert(linkP);
                auto & link = *linkP;
                auto targetIt = entries.find(link.target);
                if (targetIt == entries.end()) {
                    throw Error("hardlink target '%s' not found", link.target);
                }
                std::visit(
                    overloaded{
                        [&](CompletedFile & file) { entry = file; },
                        [&](PendingFile &) { assert(false); },
                        [&](PendingHardlink &) { stillPending.insert(path); },
                        [&](DirectoryMarker &) { throw Error("hardlink target '%s' is a directory", link.target); },
                    },
                    targetIt->second);
            }
            if (stillPending.size() == pendingHardlinks.size()) {
                throw Error("hardlink cycle detected");
            }
            pendingHardlinks = std::move(stillPending);
        }

        // Persist all blob/symlink writes and enable cross-sink
        // visibility so that directory sinks created below can
        // reference objects written during Phase 1.
        store.flushAndSetAllowDependentCreation(true);

        // Special case: single non-directory entry at root
        if (entries.size() == 1) {
            auto & [path, entry] = *entries.begin();
            if (auto * file = std::get_if<CompletedFile>(&entry)) {
                return {file->mode, file->hash};
            }
            // Must be an empty directory
            auto dirSink = store.makeDirectorySink();
            return {merkle::Mode::Directory, std::move(*dirSink).flush()};
        }

        // Phase 3: Build directories with processGraph (children before parents)
        std::set<CanonPath> dirPaths;
        for (auto & [path, entry] : entries) {
            if (std::holds_alternative<DirectoryMarker>(entry)) {
                dirPaths.insert(path);
            }
        }

        Sync<std::map<CanonPath, Hash>> dirHashes;

        processGraph<CanonPath>(
            dirPaths,
            [&](const CanonPath & path) {
                // Dependencies: immediate child directories
                std::set<CanonPath> deps;
                for (auto & dirPath : dirPaths) {
                    auto parent = dirPath.parent();
                    if ((parent && *parent == path) || (!parent && dirPath != CanonPath::root && path.isRoot())) {
                        deps.insert(dirPath);
                    }
                }
                return deps;
            },
            [&](const CanonPath & path) {
                auto dirSink = store.makeDirectorySink();

                // Add all immediate children
                for (auto & [childPath, childEntry] : entries) {
                    auto parent = childPath.parent();
                    bool isDirectChild =
                        (parent && *parent == path) || (!parent && childPath != CanonPath::root && path.isRoot());
                    if (!isDirectChild)
                        continue;

                    auto name = std::string(childPath.baseName().value());

                    if (auto * file = std::get_if<CompletedFile>(&childEntry)) {
                        dirSink->insertChild(name, {file->mode, file->hash});
                    } else if (std::holds_alternative<DirectoryMarker>(childEntry)) {
                        auto lockedHashes = dirHashes.lock();
                        dirSink->insertChild(name, {merkle::Mode::Directory, lockedHashes->at(childPath)});
                    }
                }

                Hash hash = std::move(*dirSink).flush();
                dirHashes.lock()->emplace(path, hash);
            });

        return {merkle::Mode::Directory, dirHashes.lock()->at(CanonPath::root)};
    }
};

} // namespace nix

namespace nix::merkle {

ref<TarAdapter> makeTarSink(FileSinkBuilder & store)
{
    return make_ref<nix::TarAdapterImpl>(store);
}

} // namespace nix::merkle

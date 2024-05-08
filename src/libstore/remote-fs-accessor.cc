#include <nlohmann/json.hpp>
#include "remote-fs-accessor.hh"
#include "nar-accessor.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace nix {

RemoteFSAccessor::RemoteFSAccessor(ref<Store> store, bool requireValidPath, const Path & cacheDir)
    : store(store)
    , requireValidPath(requireValidPath)
    , cacheDir(cacheDir)
{
    if (cacheDir != "")
        createDirs(cacheDir);
}

Path RemoteFSAccessor::makeCacheFile(std::string_view hashPart, const std::string & ext)
{
    assert(cacheDir != "");
    return fmt("%s/%s.%s", cacheDir, hashPart, ext);
}

ref<SourceAccessor> RemoteFSAccessor::addToCache(std::string_view hashPart, std::string && nar)
{
    if (cacheDir != "") {
        try {
            /* FIXME: do this asynchronously. */
            writeFile(makeCacheFile(hashPart, "nar"), nar);
        } catch (...) {
            ignoreException();
        }
    }

    auto narAccessor = makeNarAccessor(std::move(nar));
    nars.emplace(hashPart, narAccessor);

    if (cacheDir != "") {
        try {
            nlohmann::json j = listNar(narAccessor, CanonPath::root, true);
            writeFile(makeCacheFile(hashPart, "ls"), j.dump());
        } catch (...) {
            ignoreException();
        }
    }

    return narAccessor;
}

std::pair<ref<SourceAccessor>, CanonPath> RemoteFSAccessor::fetch(const CanonPath & path)
{
    auto [storePath, restPath_] = store->toStorePath(path.abs());
    auto restPath = CanonPath(restPath_);

    if (requireValidPath && !store->isValidPath(storePath))
        throw InvalidPath("path '%1%' is not a valid store path", store->printStorePath(storePath));

    auto i = nars.find(std::string(storePath.hashPart()));
    if (i != nars.end()) return {i->second, restPath};

    std::string listing;
    Path cacheFile;

    if (cacheDir != "" && nix::pathExists(cacheFile = makeCacheFile(storePath.hashPart(), "nar"))) {

        try {
            listing = nix::readFile(makeCacheFile(storePath.hashPart(), "ls"));

            auto narAccessor = makeLazyNarAccessor(listing,
                [cacheFile](uint64_t offset, uint64_t length) {

                    AutoCloseFD fd = toDescriptor(open(cacheFile.c_str(), O_RDONLY
                    #ifndef _WIN32
                        | O_CLOEXEC
                    #endif
                        ));
                    if (!fd)
                        throw SysError("opening NAR cache file '%s'", cacheFile);

                    if (lseek(fromDescriptorReadOnly(fd.get()), offset, SEEK_SET) != (off_t) offset)
                        throw SysError("seeking in '%s'", cacheFile);

                    std::string buf(length, 0);
                    readFull(fd.get(), buf.data(), length);

                    return buf;
                });

            nars.emplace(storePath.hashPart(), narAccessor);
            return {narAccessor, restPath};

        } catch (SystemError &) { }

        try {
            auto narAccessor = makeNarAccessor(nix::readFile(cacheFile));
            nars.emplace(storePath.hashPart(), narAccessor);
            return {narAccessor, restPath};
        } catch (SystemError &) { }
    }

    StringSink sink;
    store->narFromPath(storePath, sink);
    return {addToCache(storePath.hashPart(), std::move(sink.s)), restPath};
}

std::optional<SourceAccessor::Stat> RemoteFSAccessor::maybeLstat(const CanonPath & path)
{
    auto res = fetch(path);
    return res.first->maybeLstat(res.second);
}

SourceAccessor::DirEntries RemoteFSAccessor::readDirectory(const CanonPath & path)
{
    auto res = fetch(path);
    return res.first->readDirectory(res.second);
}

std::string RemoteFSAccessor::readFile(const CanonPath & path)
{
    auto res = fetch(path);
    return res.first->readFile(res.second);
}

std::string RemoteFSAccessor::readLink(const CanonPath & path)
{
    auto res = fetch(path);
    return res.first->readLink(res.second);
}

}

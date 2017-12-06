#include "remote-fs-accessor.hh"
#include "nar-accessor.hh"
#include "json.hh"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace nix {

RemoteFSAccessor::RemoteFSAccessor(ref<Store> store, const Path & cacheDir)
    : store(store)
    , cacheDir(cacheDir)
{
    if (cacheDir != "")
        createDirs(cacheDir);
}

Path RemoteFSAccessor::makeCacheFile(const Path & storePath, const std::string & ext)
{
    assert(cacheDir != "");
    return fmt("%s/%s.%s", cacheDir, storePathToHash(storePath), ext);
}

void RemoteFSAccessor::addToCache(const Path & storePath, const std::string & nar,
    ref<FSAccessor> narAccessor)
{
    nars.emplace(storePath, narAccessor);

    if (cacheDir != "") {
        try {
            std::ostringstream str;
            JSONPlaceholder jsonRoot(str);
            listNar(jsonRoot, narAccessor, "", true);
            writeFile(makeCacheFile(storePath, "ls"), str.str());

            /* FIXME: do this asynchronously. */
            writeFile(makeCacheFile(storePath, "nar"), nar);

        } catch (...) {
            ignoreException();
        }
    }
}

std::pair<ref<FSAccessor>, Path> RemoteFSAccessor::fetch(const Path & path_)
{
    auto path = canonPath(path_);

    auto storePath = store->toStorePath(path);
    std::string restPath = std::string(path, storePath.size());

    if (!store->isValidPath(storePath))
        throw InvalidPath(format("path '%1%' is not a valid store path") % storePath);

    auto i = nars.find(storePath);
    if (i != nars.end()) return {i->second, restPath};

    StringSink sink;
    std::string listing;
    Path cacheFile;

    if (cacheDir != "" && pathExists(cacheFile = makeCacheFile(storePath, "nar"))) {

        try {
            listing = nix::readFile(makeCacheFile(storePath, "ls"));

            auto narAccessor = makeLazyNarAccessor(listing,
                [cacheFile](uint64_t offset, uint64_t length) {

                    AutoCloseFD fd = open(cacheFile.c_str(), O_RDONLY | O_CLOEXEC);
                    if (!fd)
                        throw SysError("opening NAR cache file '%s'", cacheFile);

                    if (lseek(fd.get(), offset, SEEK_SET) != (off_t) offset)
                        throw SysError("seeking in '%s'", cacheFile);

                    std::string buf(length, 0);
                    readFull(fd.get(), (unsigned char *) buf.data(), length);

                    return buf;
                });

            nars.emplace(storePath, narAccessor);
            return {narAccessor, restPath};

        } catch (SysError &) { }

        try {
            *sink.s = nix::readFile(cacheFile);

            auto narAccessor = makeNarAccessor(sink.s);
            nars.emplace(storePath, narAccessor);
            return {narAccessor, restPath};

        } catch (SysError &) { }
    }

    store->narFromPath(storePath, sink);
    auto narAccessor = makeNarAccessor(sink.s);
    addToCache(storePath, *sink.s, narAccessor);
    return {narAccessor, restPath};
}

FSAccessor::Stat RemoteFSAccessor::stat(const Path & path)
{
    auto res = fetch(path);
    return res.first->stat(res.second);
}

StringSet RemoteFSAccessor::readDirectory(const Path & path)
{
    auto res = fetch(path);
    return res.first->readDirectory(res.second);
}

std::string RemoteFSAccessor::readFile(const Path & path)
{
    auto res = fetch(path);
    return res.first->readFile(res.second);
}

std::string RemoteFSAccessor::readLink(const Path & path)
{
    auto res = fetch(path);
    return res.first->readLink(res.second);
}

}

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

Path RemoteFSAccessor::makeCacheFile(std::string_view hashPart, const std::string & ext)
{
    assert(cacheDir != "");
    return fmt("%s/%s.%s", cacheDir, hashPart, ext);
}

ref<FSAccessor> RemoteFSAccessor::addToCache(std::string_view hashPart, std::string && nar)
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
            std::ostringstream str;
            JSONPlaceholder jsonRoot(str);
            listNar(jsonRoot, narAccessor, "", true);
            writeFile(makeCacheFile(hashPart, "ls"), str.str());
        } catch (...) {
            ignoreException();
        }
    }

    return narAccessor;
}

std::pair<ref<FSAccessor>, Path> RemoteFSAccessor::fetch(const Path & path_, bool requireValidPath)
{
    auto path = canonPath(path_);

    auto [storePath, restPath] = store->toStorePath(path);

    if (requireValidPath && !store->isValidPath(storePath))
        throw InvalidPath("path '%1%' is not a valid store path", store->printStorePath(storePath));

    auto i = nars.find(std::string(storePath.hashPart()));
    if (i != nars.end()) return {i->second, restPath};

    std::string listing;
    Path cacheFile;

    if (cacheDir != "" && pathExists(cacheFile = makeCacheFile(storePath.hashPart(), "nar"))) {

        try {
            listing = nix::readFile(makeCacheFile(storePath.hashPart(), "ls"));

            auto narAccessor = makeLazyNarAccessor(listing,
                [cacheFile](uint64_t offset, uint64_t length) {

                    AutoCloseFD fd = open(cacheFile.c_str(), O_RDONLY | O_CLOEXEC);
                    if (!fd)
                        throw SysError("opening NAR cache file '%s'", cacheFile);

                    if (lseek(fd.get(), offset, SEEK_SET) != (off_t) offset)
                        throw SysError("seeking in '%s'", cacheFile);

                    std::string buf(length, 0);
                    readFull(fd.get(), buf.data(), length);

                    return buf;
                });

            nars.emplace(storePath.hashPart(), narAccessor);
            return {narAccessor, restPath};

        } catch (SysError &) { }

        try {
            auto narAccessor = makeNarAccessor(nix::readFile(cacheFile));
            nars.emplace(storePath.hashPart(), narAccessor);
            return {narAccessor, restPath};
        } catch (SysError &) { }
    }

    StringSink sink;
    store->narFromPath(storePath, sink);
    return {addToCache(storePath.hashPart(), std::move(sink.s)), restPath};
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

std::string RemoteFSAccessor::readFile(const Path & path, bool requireValidPath)
{
    auto res = fetch(path, requireValidPath);
    return res.first->readFile(res.second);
}

std::string RemoteFSAccessor::readLink(const Path & path)
{
    auto res = fetch(path);
    return res.first->readLink(res.second);
}

}

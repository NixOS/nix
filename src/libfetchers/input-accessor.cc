#include "input-accessor.hh"
#include "store-api.hh"
#include "cache.hh"

namespace nix {

StorePath InputAccessor::fetchToStore(
    ref<Store> store,
    const CanonPath & path,
    std::string_view name,
    FileIngestionMethod method,
    PathFilter * filter,
    RepairFlag repair)
{
    // FIXME: add an optimisation for the case where the accessor is
    // an FSInputAccessor pointing to a store path.

    std::optional<fetchers::Attrs> cacheKey;

    if (!filter && fingerprint) {
        cacheKey = fetchers::Attrs{
            {"_what", "fetchToStore"},
            {"store", store->storeDir},
            {"name", std::string(name)},
            {"fingerprint", *fingerprint},
            {"method", (uint8_t) method},
            {"path", path.abs()}
        };
        if (auto res = fetchers::getCache()->lookup(store, *cacheKey)) {
            debug("store path cache hit for '%s'", showPath(path));
            return res->second;
        }
    } else
        debug("source path '%s' is uncacheable", showPath(path));

    Activity act(*logger, lvlChatty, actUnknown, fmt("copying '%s' to the store", showPath(path)));

    auto source = sinkToSource([&](Sink & sink) {
        if (method == FileIngestionMethod::Recursive)
            dumpPath(path, sink, filter ? *filter : defaultPathFilter);
        else
            readFile(path, sink);
    });

    auto storePath =
        settings.readOnlyMode
        ? store->computeStorePathFromDump(*source, name, method, HashAlgorithm::SHA256).first
        : store->addToStoreFromDump(*source, name, method, HashAlgorithm::SHA256, repair);

    if (cacheKey)
        fetchers::getCache()->add(store, *cacheKey, {}, storePath, true);

    return storePath;
}

std::ostream & operator << (std::ostream & str, const SourcePath & path)
{
    str << path.to_string();
    return str;
}

StorePath SourcePath::fetchToStore(
    ref<Store> store,
    std::string_view name,
    FileIngestionMethod method,
    PathFilter * filter,
    RepairFlag repair) const
{
    return accessor->fetchToStore(store, path, name, method, filter, repair);
}

std::string_view SourcePath::baseName() const
{
    return path.baseName().value_or("source");
}

SourcePath SourcePath::parent() const
{
    auto p = path.parent();
    assert(p);
    return {accessor, std::move(*p)};
}

SourcePath SourcePath::resolveSymlinks() const
{
    auto res = SourcePath(accessor);

    int linksAllowed = 1024;

    std::list<std::string> todo;
    for (auto & c : path)
        todo.push_back(std::string(c));

    while (!todo.empty()) {
        auto c = *todo.begin();
        todo.pop_front();
        if (c == "" || c == ".")
            ;
        else if (c == "..")
            res.path.pop();
        else {
            res.path.push(c);
            if (auto st = res.maybeLstat(); st && st->type == InputAccessor::tSymlink) {
                if (!linksAllowed--)
                    throw Error("infinite symlink recursion in path '%s'", path);
                auto target = res.readLink();
                res.path.pop();
                if (hasPrefix(target, "/"))
                    res.path = CanonPath::root;
                todo.splice(todo.begin(), tokenizeString<std::list<std::string>>(target, "/"));
            }
        }
    }

    return res;
}

}

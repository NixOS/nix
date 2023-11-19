#include "input-accessor.hh"
#include "store-api.hh"

namespace nix {

StorePath InputAccessor::fetchToStore(
    ref<Store> store,
    const CanonPath & path,
    std::string_view name,
    FileIngestionMethod method,
    PathFilter * filter,
    RepairFlag repair)
{
    Activity act(*logger, lvlChatty, actUnknown, fmt("copying '%s' to the store", showPath(path)));

    auto source = sinkToSource([&](Sink & sink) {
        if (method == FileIngestionMethod::Recursive)
            dumpPath(path, sink, filter ? *filter : defaultPathFilter);
        else
            readFile(path, sink);
    });

    auto storePath =
        settings.readOnlyMode
        ? store->computeStorePathFromDump(*source, name, method, htSHA256).first
        : store->addToStoreFromDump(*source, name, method, htSHA256, repair);

    return storePath;
}

SourcePath InputAccessor::root()
{
    return {ref(shared_from_this()), CanonPath::root};
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

SourcePath SourcePath::followSymlinks() const {
    SourcePath path = *this;
    unsigned int followCount = 0, maxFollow = 1000;

    /* If `path' is a symlink, follow it.  This is so that relative
       path references work. */
    while (true) {
        // Basic cycle/depth limit to avoid infinite loops.
        if (++followCount >= maxFollow)
            throw Error("too many levels of symbolic links while traversing the path '%s'; assuming it leads to a cycle after following %d indirections", this->to_string(), maxFollow);
        if (path.lstat().type != InputAccessor::tSymlink) break;
        path = {path.accessor, CanonPath(path.readLink(), path.path.parent().value_or(CanonPath::root))};
    }
    return path;
}

SourcePath SourcePath::resolveSymlinks() const
{
    auto res = accessor->root();

    int linksAllowed = 1000;

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

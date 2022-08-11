#include "input-accessor.hh"
#include "util.hh"
#include "store-api.hh"

#include <atomic>

namespace nix {

static std::atomic<size_t> nextNumber{0};

InputAccessor::InputAccessor()
    : number(++nextNumber)
    , displayPrefix{"/virtual/" + std::to_string(number)}
{
}

// FIXME: merge with archive.cc.
void InputAccessor::dumpPath(
    const CanonPath & path,
    Sink & sink,
    PathFilter & filter)
{
    auto dumpContents = [&](const CanonPath & path)
    {
        // FIXME: pipe
        auto s = readFile(path);
        sink << "contents" << s.size();
        sink(s);
        writePadding(s.size(), sink);
    };

    std::function<void(const CanonPath & path)> dump;

    dump = [&](const CanonPath & path) {
        checkInterrupt();

        auto st = lstat(path);

        sink << "(";

        if (st.type == tRegular) {
            sink << "type" << "regular";
            if (st.isExecutable)
                sink << "executable" << "";
            dumpContents(path);
        }

        else if (st.type == tDirectory) {
            sink << "type" << "directory";

            /* If we're on a case-insensitive system like macOS, undo
               the case hack applied by restorePath(). */
            std::map<std::string, std::string> unhacked;
            for (auto & i : readDirectory(path))
                if (/* archiveSettings.useCaseHack */ false) { // FIXME
                    std::string name(i.first);
                    size_t pos = i.first.find(caseHackSuffix);
                    if (pos != std::string::npos) {
                        debug("removing case hack suffix from '%s'", path + i.first);
                        name.erase(pos);
                    }
                    if (!unhacked.emplace(name, i.first).second)
                        throw Error("file name collision in between '%s' and '%s'",
                            (path + unhacked[name]),
                            (path + i.first));
                } else
                    unhacked.emplace(i.first, i.first);

            for (auto & i : unhacked)
                if (filter((path + i.first).abs())) {
                    sink << "entry" << "(" << "name" << i.first << "node";
                    dump(path + i.second);
                    sink << ")";
                }
        }

        else if (st.type == tSymlink)
            sink << "type" << "symlink" << "target" << readLink(path);

        else throw Error("file '%s' has an unsupported type", path);

        sink << ")";
    };

    sink << narVersionMagic1;
    dump(path);
}

StorePath InputAccessor::fetchToStore(
    ref<Store> store,
    const CanonPath & path,
    std::string_view name,
    PathFilter * filter,
    RepairFlag repair)
{
    // FIXME: add an optimisation for the case where the accessor is
    // an FSInputAccessor pointing to a store path.

    auto source = sinkToSource([&](Sink & sink) {
        dumpPath(path, sink, filter ? *filter : defaultPathFilter);
    });

    auto storePath =
        settings.readOnlyMode
        ? store->computeStorePathFromDump(*source, name).first
        : store->addToStoreFromDump(*source, name, FileIngestionMethod::Recursive, htSHA256, repair);

    return storePath;
}

std::optional<InputAccessor::Stat> InputAccessor::maybeLstat(const CanonPath & path)
{
    // FIXME: merge these into one operation.
    if (!pathExists(path))
        return {};
    return lstat(path);
}

void InputAccessor::setPathDisplay(std::string displayPrefix, std::string displaySuffix)
{
    this->displayPrefix = std::move(displayPrefix);
    this->displaySuffix = std::move(displaySuffix);
}

std::string InputAccessor::showPath(const CanonPath & path)
{
    return displayPrefix + path.abs() + displaySuffix;
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

struct MemoryInputAccessorImpl : MemoryInputAccessor
{
    std::map<CanonPath, std::string> files;

    std::string readFile(const CanonPath & path) override
    {
        auto i = files.find(path);
        if (i == files.end())
            throw Error("file '%s' does not exist", path);
        return i->second;
    }

    bool pathExists(const CanonPath & path) override
    {
        auto i = files.find(path);
        return i != files.end();
    }

    Stat lstat(const CanonPath & path) override
    {
        auto i = files.find(path);
        if (i != files.end())
            return Stat { .type = tRegular, .isExecutable = false };
        throw Error("file '%s' does not exist", path);
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        return {};
    }

    std::string readLink(const CanonPath & path) override
    {
        throw UnimplementedError("MemoryInputAccessor::readLink");
    }

    SourcePath addFile(CanonPath path, std::string && contents) override
    {
        files.emplace(path, std::move(contents));

        return {ref(shared_from_this()), std::move(path)};
    }
};

ref<MemoryInputAccessor> makeMemoryInputAccessor()
{
    return make_ref<MemoryInputAccessorImpl>();
}

StorePath SourcePath::fetchToStore(
    ref<Store> store,
    std::string_view name,
    PathFilter * filter,
    RepairFlag repair) const
{
    return accessor->fetchToStore(store, path, name, filter, repair);
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
    CanonPath res("/");

    int linksAllowed = 1024;

    for (auto & component : path) {
        res.push(component);
        while (true) {
            if (auto st = accessor->maybeLstat(res)) {
                if (!linksAllowed--)
                    throw Error("infinite symlink recursion in path '%s'", path);
                if (st->type != InputAccessor::tSymlink) break;
                auto target = accessor->readLink(res);
                if (hasPrefix(target, "/"))
                    res = CanonPath(target);
                else {
                    res.pop();
                    res.extend(CanonPath(target));
                }
            } else
                break;
        }
    }

    return {accessor, res};
}

}

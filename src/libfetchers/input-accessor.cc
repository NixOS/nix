#include "input-accessor.hh"
#include "util.hh"

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

struct FSInputAccessorImpl : FSInputAccessor
{
    CanonPath root;
    std::optional<std::set<CanonPath>> allowedPaths;
    MakeNotAllowedError makeNotAllowedError;

    FSInputAccessorImpl(
        const CanonPath & root,
        std::optional<std::set<CanonPath>> && allowedPaths,
        MakeNotAllowedError && makeNotAllowedError)
        : root(root)
        , allowedPaths(std::move(allowedPaths))
        , makeNotAllowedError(std::move(makeNotAllowedError))
    {
        displayPrefix = root.isRoot() ? "" : root.abs();
    }

    std::string readFile(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        checkAllowed(absPath);
        return nix::readFile(absPath.abs());
    }

    bool pathExists(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        return isAllowed(absPath) && nix::pathExists(absPath.abs());
    }

    Stat lstat(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        checkAllowed(absPath);
        auto st = nix::lstat(absPath.abs());
        return Stat {
            .type =
                S_ISREG(st.st_mode) ? tRegular :
                S_ISDIR(st.st_mode) ? tDirectory :
                S_ISLNK(st.st_mode) ? tSymlink :
                tMisc,
            .isExecutable = S_ISREG(st.st_mode) && st.st_mode & S_IXUSR
        };
    }

    DirEntries readDirectory(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        checkAllowed(absPath);
        DirEntries res;
        for (auto & entry : nix::readDirectory(absPath.abs())) {
            std::optional<Type> type;
            switch (entry.type) {
            case DT_REG: type = Type::tRegular; break;
            case DT_LNK: type = Type::tSymlink; break;
            case DT_DIR: type = Type::tDirectory; break;
            }
            if (isAllowed(absPath + entry.name))
                res.emplace(entry.name, type);
        }
        return res;
    }

    std::string readLink(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        checkAllowed(absPath);
        return nix::readLink(absPath.abs());
    }

    CanonPath makeAbsPath(const CanonPath & path)
    {
        return root + path;
    }

    void checkAllowed(const CanonPath & absPath) override
    {
        if (!isAllowed(absPath))
            throw makeNotAllowedError
                ? makeNotAllowedError(absPath)
                : RestrictedPathError("access to path '%s' is forbidden", absPath);
    }

    bool isAllowed(const CanonPath & absPath)
    {
        if (!absPath.isWithin(root))
            return false;

        if (allowedPaths) {
            auto p = absPath.removePrefix(root);
            if (!p.isAllowed(*allowedPaths))
                return false;
        }

        return true;
    }

    void allowPath(CanonPath path) override
    {
        if (allowedPaths)
            allowedPaths->insert(std::move(path));
    }

    bool hasAccessControl() override
    {
        return (bool) allowedPaths;
    }

    std::optional<CanonPath> getPhysicalPath(const CanonPath & path) override
    {
        auto absPath = makeAbsPath(path);
        if (isAllowed(absPath))
            return absPath;
        else
            return std::nullopt;
    }
};

ref<FSInputAccessor> makeFSInputAccessor(
    const CanonPath & root,
    std::optional<std::set<CanonPath>> && allowedPaths,
    MakeNotAllowedError && makeNotAllowedError)
{
    return make_ref<FSInputAccessorImpl>(root, std::move(allowedPaths), std::move(makeNotAllowedError));
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

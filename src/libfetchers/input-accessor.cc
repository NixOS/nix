#include "input-accessor.hh"
#include "util.hh"

#include <atomic>

namespace nix {

static std::atomic<size_t> nextNumber{0};

InputAccessor::InputAccessor()
    : number(++nextNumber)
{ }

// FIXME: merge with archive.cc.
const std::string narVersionMagic1 = "nix-archive-1";

static string caseHackSuffix = "~nix~case~hack~";

void InputAccessor::dumpPath(
    const Path & path,
    Sink & sink,
    PathFilter & filter)
{
    auto dumpContents = [&](PathView path)
    {
        // FIXME: pipe
        auto s = readFile(path);
        sink << "contents" << s.size();
        sink(s);
        writePadding(s.size(), sink);
    };

    std::function<void(const std::string & path)> dump;

    dump = [&](const std::string & path) {
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
            std::map<string, string> unhacked;
            for (auto & i : readDirectory(path))
                if (/* archiveSettings.useCaseHack */ false) { // FIXME
                    string name(i.first);
                    size_t pos = i.first.find(caseHackSuffix);
                    if (pos != string::npos) {
                        debug(format("removing case hack suffix from '%s'") % (path + "/" + i.first));
                        name.erase(pos);
                    }
                    if (!unhacked.emplace(name, i.first).second)
                        throw Error("file name collision in between '%s' and '%s'",
                            (path + "/" + unhacked[name]),
                            (path + "/" + i.first));
                } else
                    unhacked.emplace(i.first, i.first);

            for (auto & i : unhacked)
                if (filter(path + "/" + i.first)) {
                    sink << "entry" << "(" << "name" << i.first << "node";
                    dump(path + "/" + i.second);
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

struct FSInputAccessor : InputAccessor
{
    Path root;

    FSInputAccessor(const Path & root)
        : root(root)
    { }

    std::string readFile(PathView path) override
    {
        auto absPath = makeAbsPath(path);
        printError("READ %s", absPath);
        checkAllowed(absPath);
        return nix::readFile(absPath);
    }

    bool pathExists(PathView path) override
    {
        auto absPath = makeAbsPath(path);
        printError("EXISTS %s", absPath);
        return isAllowed(absPath) && nix::pathExists(absPath);
    }

    Stat lstat(PathView path) override
    {
        auto absPath = makeAbsPath(path);
        printError("LSTAT %s", absPath);
        checkAllowed(absPath);
        auto st = nix::lstat(absPath);
        return Stat {
            .type =
                S_ISREG(st.st_mode) ? tRegular :
                S_ISDIR(st.st_mode) ? tDirectory :
                S_ISLNK(st.st_mode) ? tSymlink :
                tMisc,
            .isExecutable = S_ISREG(st.st_mode) && st.st_mode & S_IXUSR
        };
    }

    DirEntries readDirectory(PathView path) override
    {
        auto absPath = makeAbsPath(path);
        printError("READDIR %s", absPath);
        checkAllowed(absPath);
        DirEntries res;
        for (auto & entry : nix::readDirectory(absPath)) {
            std::optional<Type> type;
            switch (entry.type) {
            case DT_REG: type = Type::tRegular; break;
            case DT_LNK: type = Type::tSymlink; break;
            case DT_DIR: type = Type::tDirectory; break;
            }
            res.emplace(entry.name, type);
        }
        return res;
    }

    std::string readLink(PathView path) override
    {
        auto absPath = makeAbsPath(path);
        printError("READLINK %s", absPath);
        checkAllowed(absPath);
        return nix::readLink(absPath);
    }

    Path makeAbsPath(PathView path)
    {
        assert(hasPrefix(path, "/"));
        return canonPath(root + std::string(path));
    }

    void checkAllowed(PathView absPath)
    {
        if (!isAllowed(absPath))
            throw Error("access to path '%s' is not allowed", absPath);
    }

    bool isAllowed(PathView absPath)
    {
        if (!isDirOrInDir(absPath, root))
            return false;
        return true;
    }
};

ref<InputAccessor> makeFSInputAccessor(const Path & root)
{
    return make_ref<FSInputAccessor>(root);
}

std::ostream & operator << (std::ostream & str, const SourcePath & path)
{
    str << path.path; // FIXME
    return str;
}

struct MemoryInputAccessorImpl : MemoryInputAccessor
{
    std::map<Path, std::string> files;

    std::string readFile(PathView path) override
    {
        auto i = files.find((Path) path);
        if (i == files.end())
            throw Error("file '%s' does not exist", path);
        return i->second;
    }

    bool pathExists(PathView path) override
    {
        auto i = files.find((Path) path);
        return i != files.end();
    }

    Stat lstat(PathView path) override
    {
        throw UnimplementedError("MemoryInputAccessor::lstat");
    }

    DirEntries readDirectory(PathView path) override
    {
        return {};
    }

    std::string readLink(PathView path) override
    {
        throw UnimplementedError("MemoryInputAccessor::readLink");
    }

    void addFile(PathView path, std::string && contents) override
    {
        files.emplace(path, std::move(contents));
    }
};

ref<MemoryInputAccessor> makeMemoryInputAccessor()
{
    return make_ref<MemoryInputAccessorImpl>();
}

}

#include <atomic>
#include "nix/util/source-accessor.hh"

namespace nix {

static std::atomic<size_t> nextNumber{0};

bool SourceAccessor::Stat::isNotNARSerialisable()
{
    return this->type != tRegular && this->type != tSymlink && this->type != tDirectory;
}

std::string SourceAccessor::Stat::typeString()
{
    switch (this->type) {
    case tRegular:
        return "regular";
    case tSymlink:
        return "symlink";
    case tDirectory:
        return "directory";
    case tChar:
        return "character device";
    case tBlock:
        return "block device";
    case tSocket:
        return "socket";
    case tFifo:
        return "fifo";
    case tUnknown:
    default:
        return "unknown";
    }
    return "unknown";
}

SourceAccessor::SourceAccessor()
    : number(++nextNumber)
    , displayPrefix{"«unknown»"}
{
}

bool SourceAccessor::pathExists(const CanonPath & path)
{
    return maybeLstat(path).has_value();
}

std::string SourceAccessor::readFile(const CanonPath & path)
{
    StringSink sink;
    std::optional<uint64_t> size;
    readFile(path, sink, [&](uint64_t _size) { size = _size; });
    assert(size && *size == sink.s.size());
    return std::move(sink.s);
}

void SourceAccessor::readFile(const CanonPath & path, Sink & sink, std::function<void(uint64_t)> sizeCallback)
{
    auto s = readFile(path);
    sizeCallback(s.size());
    sink(s);
}

Hash SourceAccessor::hashPath(const CanonPath & path, PathFilter & filter, HashAlgorithm ha)
{
    HashSink sink(ha);
    dumpPath(path, sink, filter);
    return sink.finish().hash;
}

SourceAccessor::Stat SourceAccessor::lstat(const CanonPath & path)
{
    if (auto st = maybeLstat(path))
        return *st;
    else
        throw FileNotFound("path '%s' does not exist", showPath(path));
}

SourceAccessor::Stat posixStatToAccessorStat(const struct ::stat & st)
{
    using enum SourceAccessor::Type;
    return SourceAccessor::Stat{
        .type = S_ISREG(st.st_mode)   ? tRegular
                : S_ISDIR(st.st_mode) ? tDirectory
                : S_ISLNK(st.st_mode) ? tSymlink
                : S_ISCHR(st.st_mode) ? tChar
                : S_ISBLK(st.st_mode) ? tBlock
                :
#ifdef S_ISSOCK
                S_ISSOCK(st.st_mode) ? tSocket
                :
#endif
                S_ISFIFO(st.st_mode) ? tFifo
                                     : tUnknown,
        .fileSize = S_ISREG(st.st_mode) ? std::optional<uint64_t>(st.st_size) : std::nullopt,
        .isExecutable = S_ISREG(st.st_mode) && st.st_mode & S_IXUSR,
    };
}

void SourceAccessor::setPathDisplay(std::string displayPrefix, std::string displaySuffix)
{
    this->displayPrefix = std::move(displayPrefix);
    this->displaySuffix = std::move(displaySuffix);
}

std::string SourceAccessor::showPath(const CanonPath & path)
{
    return displayPrefix + path.abs() + displaySuffix;
}

CanonPath SourceAccessor::resolveSymlinks(const CanonPath & path, SymlinkResolution mode)
{
    auto res = CanonPath::root;

    int linksAllowed = 1024;

    std::list<std::string> todo;
    for (auto & c : path)
        todo.push_back(std::string(c));

    while (!todo.empty()) {
        auto c = *todo.begin();
        todo.pop_front();
        if (c == "" || c == ".")
            ;
        else if (c == "..") {
            if (!res.isRoot())
                res.pop();
        } else {
            res.push(c);
            if (mode == SymlinkResolution::Full || !todo.empty()) {
                if (auto st = maybeLstat(res); st && st->type == SourceAccessor::tSymlink) {
                    if (!linksAllowed--)
                        throw Error("infinite symlink recursion in path '%s'", showPath(path));
                    auto target = readLink(res);
                    if (isAbsolute(target)) {
                        res = CanonPath::root;
                    } else {
                        res.pop();
                    }
                    todo.splice(todo.begin(), tokenizeString<std::list<std::string>>(target, "/"));
                }
            }
        }
    }

    return res;
}

} // namespace nix

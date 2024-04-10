#include "source-accessor.hh"
#include "archive.hh"

namespace nix {

static std::atomic<size_t> nextNumber{0};

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
    readFile(path, sink, [&](uint64_t _size)
    {
        size = _size;
    });
    assert(size && *size == sink.s.size());
    return std::move(sink.s);
}

void SourceAccessor::readFile(
    const CanonPath & path,
    Sink & sink,
    std::function<void(uint64_t)> sizeCallback)
{
    auto s = readFile(path);
    sizeCallback(s.size());
    sink(s);
}

Hash SourceAccessor::hashPath(
    const CanonPath & path,
    PathFilter & filter,
    HashAlgorithm ha)
{
    HashSink sink(ha);
    dumpPath(path, sink, filter);
    return sink.finish().first;
}

SourceAccessor::Stat SourceAccessor::lstat(const CanonPath & path)
{
    if (auto st = maybeLstat(path))
        return *st;
    else
        throw Error("path '%s' does not exist", showPath(path));
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

CanonPath SourceAccessor::resolveSymlinks(
    const CanonPath & path,
    SymlinkResolution mode)
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
        else if (c == "..")
            res.pop();
        else {
            res.push(c);
            if (mode == SymlinkResolution::Full || !todo.empty()) {
                if (auto st = maybeLstat(res); st && st->type == SourceAccessor::tSymlink) {
                    if (!linksAllowed--)
                        throw Error("infinite symlink recursion in path '%s'", showPath(path));
                    auto target = readLink(res);
                    res.pop();
                    if (hasPrefix(target, "/"))
                        res = CanonPath::root;
                    todo.splice(todo.begin(), tokenizeString<std::list<std::string>>(target, "/"));
                }
            }
        }
    }

    return res;
}

}

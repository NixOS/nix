#include <atomic>
#include "nix/util/source-accessor.hh"
#include "nix/util/path-iterator.hh"

namespace nix {

void SourceAccessorError::anchor() {}

void FileNotFound::anchor() {}

void NotASymlink::anchor() {}

void NotADirectory::anchor() {}

void NotARegularFile::anchor() {}

void RestrictedPathError::anchor() {}

void SymlinkNotAllowed::anchor() {}

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
    readFile(path, sink, [&](uint64_t _size) {
        size = _size;
        sink.s.reserve(_size);
    });
    assert(size && *size == sink.s.size());
    return std::move(sink.s);
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

void SourceAccessor::setPathDisplay(std::string displayPrefix, std::string displaySuffix)
{
    this->displayPrefix = std::move(displayPrefix);
    this->displaySuffix = std::move(displaySuffix);
}

std::string SourceAccessor::showPath(const CanonPath & path)
{
    return displayPrefix + path.abs() + displaySuffix;
}

/**
 * Resolve the symlinks in @p path, and any `..` along with them, as they
 * are reached: so `link/..` is the parent of the link's target, not the
 * directory containing the link.
 */
static CanonPath resolveComponents(SourceAccessor & accessor, const UnresolvedPath & path, SymlinkResolution mode)
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
            /* Not empty, `.` or `..`, which are handled above, and with no
               `/`, which the components are split on; and no file names
               (and so no symlink targets they came from) contain NUL. */
            res.push(FilenameView::fromStringUnchecked(c));
            if (mode == SymlinkResolution::Full || !todo.empty()) {
                if (auto st = accessor.maybeLstat(res); st && st->type == SourceAccessor::tSymlink) {
                    if (!linksAllowed--)
                        throw Error("infinite symlink recursion at '%s'", accessor.showPath(res));
                    auto target = accessor.readLink(res);
                    /* Not `std::filesystem::path::is_absolute`, which on
                       Windows requires a drive letter too. */
                    if (target.starts_with('/')) {
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

CanonPath SourceAccessor::resolveSymlinks(const CanonPath & path, SymlinkResolution mode)
try {
    return resolveComponents(*this, path, mode);
} catch (Error & e) {
    e.addTrace({}, "while resolving the symlinks in '%s'", showPath(path));
    throw;
}

CanonPath SourceAccessor::resolvePath(const UnresolvedPath & path)
try {
    /* Split it after the last `..`: what comes before needs its symlinks
       resolved to find what the `..` is the parent of, what comes after
       is kept as given. Without any `..`, the first part is just `/`,
       which resolves without looking at anything. */
    auto & s = path.abs();
    /* Where the last `..` ends, or 0 if there is none. */
    size_t split = 0;
    for (size_t start = 1; start < s.size();) {
        auto end = std::min(s.find('/', start), s.size());
        if (std::string_view{s}.substr(start, end - start) == "..")
            split = end;
        start = end + 1;
    }

    auto res = resolveComponents(
        *this, UnresolvedPath::fromUnchecked(s.substr(0, std::max(split, (size_t) 1))), SymlinkResolution::Full);
    /* Neither `.` nor `..`, which `UnresolvedPath` removed or the
       first part took. */
    if (split < s.size())
        res.extend(CanonPath::fromUnchecked(UnresolvedPath::fromUnchecked(s.substr(split))));
    return res;
} catch (Error & e) {
    e.addTrace({}, "while resolving '%s'", path.abs());
    throw;
}

} // namespace nix

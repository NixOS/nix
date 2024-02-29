#include "source-path.hh"

namespace nix {

std::string_view SourcePath::baseName() const
{ return path.baseName().value_or("source"); }

SourcePath SourcePath::parent() const
{
    auto p = path.parent();
    assert(p);
    return {accessor, std::move(*p)};
}

std::string SourcePath::readFile() const
{ return accessor->readFile(path); }

bool SourcePath::pathExists() const
{ return accessor->pathExists(path); }

InputAccessor::Stat SourcePath::lstat() const
{ return accessor->lstat(path); }

std::optional<InputAccessor::Stat> SourcePath::maybeLstat() const
{ return accessor->maybeLstat(path); }

InputAccessor::DirEntries SourcePath::readDirectory() const
{ return accessor->readDirectory(path); }

std::string SourcePath::readLink() const
{ return accessor->readLink(path); }

void SourcePath::dumpPath(
    Sink & sink,
    PathFilter & filter) const
{ return accessor->dumpPath(path, sink, filter); }

std::optional<std::filesystem::path> SourcePath::getPhysicalPath() const
{ return accessor->getPhysicalPath(path); }

std::string SourcePath::to_string() const
{ return accessor->showPath(path); }

SourcePath SourcePath::operator / (const CanonPath & x) const
{ return {accessor, path / x}; }

SourcePath SourcePath::operator / (std::string_view c) const
{ return {accessor, path / c}; }

bool SourcePath::operator==(const SourcePath & x) const
{
    return std::tie(*accessor, path) == std::tie(*x.accessor, x.path);
}

bool SourcePath::operator!=(const SourcePath & x) const
{
    return std::tie(*accessor, path) != std::tie(*x.accessor, x.path);
}

bool SourcePath::operator<(const SourcePath & x) const
{
    return std::tie(*accessor, path) < std::tie(*x.accessor, x.path);
}

SourcePath SourcePath::resolveSymlinks(SymlinkResolution mode) const
{
    auto res = SourcePath(accessor);

    int linksAllowed = 1024;

    std::list<std::string> todo;
    for (auto & c : path)
        todo.push_back(std::string(c));

    bool resolve_last = mode == SymlinkResolution::Full;

    while (!todo.empty()) {
        auto c = *todo.begin();
        todo.pop_front();
        if (c == "" || c == ".")
            ;
        else if (c == "..")
            res.path.pop();
        else {
            res.path.push(c);
            if (resolve_last || !todo.empty()) {
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
    }

    return res;
}

std::ostream & operator<<(std::ostream & str, const SourcePath & path)
{
    str << path.to_string();
    return str;
}

}

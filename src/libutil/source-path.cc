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

std::optional<CanonPath> SourcePath::getPhysicalPath() const
{ return accessor->getPhysicalPath(path); }

std::string SourcePath::to_string() const
{ return accessor->showPath(path); }

SourcePath SourcePath::operator+(const CanonPath & x) const
{ return {accessor, path + x}; }

SourcePath SourcePath::operator+(std::string_view c) const
{  return {accessor, path + c}; }

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
    auto res = SourcePath(accessor);

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

std::ostream & operator<<(std::ostream & str, const SourcePath & path)
{
    str << path.to_string();
    return str;
}

}

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

std::ostream & operator<<(std::ostream & str, const SourcePath & path)
{
    str << path.to_string();
    return str;
}

}

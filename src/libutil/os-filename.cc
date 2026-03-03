#include "nix/util/os-filename.hh"

#include "nix/util/error.hh"

#include <cassert>

namespace nix {

void OsFilename::validateAssert() const
{
    assert(!name.empty() && "OsFilename cannot be empty");
    assert(!name.has_root_path() && "OsFilename cannot have a root path");
    assert(!name.has_parent_path() && "OsFilename cannot have a parent path");
    assert(name.filename() == name && "OsFilename must be a single filename");
    assert(name != "." && "OsFilename cannot be '.'");
    assert(name != ".." && "OsFilename cannot be '..'");
}

void OsFilename::validateThrow(const std::filesystem::path & p)
{
    if (p.empty())
        throw Error("filename cannot be empty");
    if (p.has_root_path())
        throw Error("filename cannot have a root path: '%s'", p.string());
    if (p.has_parent_path())
        throw Error("filename cannot have a parent path: '%s'", p.string());
    if (p.filename() != p)
        throw Error("not a single filename: '%s'", p.string());
    if (p == ".")
        throw Error("filename cannot be '.'");
    if (p == "..")
        throw Error("filename cannot be '..'");
}

OsFilename OsFilename::fromPathThrowing(std::filesystem::path p)
{
    validateThrow(p);
    return OsFilename{std::move(p)};
}

} // namespace nix

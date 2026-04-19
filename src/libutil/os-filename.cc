#include "nix/util/os-filename.hh"

#include <cassert>

namespace nix {

void OsFilename::validate() const
{
    assert(!name.empty() && "OsFilename cannot be empty");
    assert(!name.has_root_path() && "OsFilename cannot have a root path");
    assert(!name.has_parent_path() && "OsFilename cannot have a parent path");
    assert(name.filename() == name && "OsFilename must be a single filename");
    assert(name != "." && "OsFilename cannot be '.'");
    assert(name != ".." && "OsFilename cannot be '..'");
}

} // namespace nix

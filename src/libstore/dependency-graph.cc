#include "nix/store/dependency-graph.hh"
#include "nix/store/dependency-graph-impl.hh"

#include <string>

namespace nix {

// Explicit instantiations for common types
template class DependencyGraph<StorePath>;
template class DependencyGraph<std::string>;
template class DependencyGraph<StorePath, FileListEdgeProperty>;

} // namespace nix

#include "nix/store/dependency-graph-impl.hh"

namespace nix {

// Explicit instantiations for common types
template class DependencyGraph<StorePath>;
template class DependencyGraph<std::string>;
template class DependencyGraph<StorePath, FileListEdgeProperty>;

} // namespace nix

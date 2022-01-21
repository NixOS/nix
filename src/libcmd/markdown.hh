#include "types.hh"

#if HAVE_LOWDOWN
namespace nix {

std::string renderMarkdownToTerminal(std::string_view markdown);

}
#endif

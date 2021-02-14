#pragma once

namespace nix {

// A forward declaration from `util.hh`
bool shouldANSI();

/* Some ANSI escape sequences. */
#define ANSI_NORMAL  "\e[0m"
#define ANSI_BOLD    "\e[1m"
#define ANSI_FAINT   "\e[2m"
#define ANSI_ITALIC  "\e[3m"
#define ANSI_RED     (::std::string( ::nix::shouldANSI() ? "\e[31;1m" : ""))
#define ANSI_GREEN   (::std::string( ::nix::shouldANSI() ? "\e[32;1m" : ""))
#define ANSI_YELLOW  (::std::string( ::nix::shouldANSI() ? "\e[33;1m" : ""))
#define ANSI_BLUE    (::std::string( ::nix::shouldANSI() ? "\e[34;1m" : ""))
#define ANSI_MAGENTA (::std::string( ::nix::shouldANSI() ? "\e[35;1m" : ""))
#define ANSI_CYAN    (::std::string( ::nix::shouldANSI() ? "\e[36;1m" : ""))

}

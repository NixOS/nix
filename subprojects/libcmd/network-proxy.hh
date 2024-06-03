#pragma once
///@file

#include "types.hh"

namespace nix {

/**
 * Environment variables relating to network proxying. These are used by
 * a few misc commands.
 *
 * See the Environment section of https://curl.se/docs/manpage.html for details.
 */
extern const StringSet networkProxyVariables;

/**
 * Heuristically check if there is a proxy connection by checking for defined
 * proxy variables.
 */
bool haveNetworkProxyConnection();

}

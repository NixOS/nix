#pragma once
///@file

#include "nix/util/signature/local-keys.hh"

namespace nix {

class Settings;

/**
 * @todo use more narrow settings, or rethink whether this is a good
 * idea at all, vs always associating keys with stores.
 */
PublicKeys getDefaultPublicKeys(const Settings & settings);

} // namespace nix

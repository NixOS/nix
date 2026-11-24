#pragma once

///@file

namespace nix {

/**
 * This should be called after settings are initialized, but before
 * anything else.
 *
 * @throws Error on ::dlopen errors or if a plugin tries to re-enter this
 * function during initialisation.
 */
void initPlugins();

} // namespace nix

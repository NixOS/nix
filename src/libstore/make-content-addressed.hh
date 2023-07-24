#pragma once
///@file

#include "store-api.hh"

namespace nix {

/** Rewrite a closure of store paths to be completely content addressed.
 */
std::map<StorePath, StorePath> makeContentAddressed(
    Store & srcStore,
    Store & dstStore,
    const StorePathSet & rootPaths);

/** Rewrite a closure of a store path to be completely content addressed.
 *
 * This is a convenience function for the case where you only have one root path.
 */
StorePath makeContentAddressed(
    Store & srcStore,
    Store & dstStore,
    const StorePath & rootPath);

}

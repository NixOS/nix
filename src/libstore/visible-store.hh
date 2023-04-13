#pragma once
///@file

#include "store-api.hh"

namespace nix {

/**
 * A Store that exposes all store objects.
 *
 * ### Privacy and Security
 *
 * For the base `Store` class, we aim for `StorePath`s to act as
 * capabilities: only store objects which are reachable from the store
 * objects the user has (i.e. those directly-referenced objects and
 * their reference closure).
 *
 * A `VisibleStore` breaks this by exposing these methods that allow
 * discovering other store objects, outside the "reachable set" as
 * defined above. This is necessary to implement certain operations, but
 * care must taken exposing this functionality to the user as it makes
 * e.g. secret management and other security properties trickier to get
 * right.
 */
struct VisibleStore : virtual Store
{
    inline static std::string operationName = "Query all valid paths";

    /**
     * Query the set of all valid paths. Note that for some store
     * backends, the name part of store paths may be replaced by 'x'
     * (i.e. you'll get `/nix/store/<hash>-x` rather than
     * `/nix/store/<hash>-<name>`). Use `queryPathInfo()` to obtain the
     * full store path.
     *
     * \todo should return a set of `std::variant<StorePath, HashPart>`
     * to get rid of this hack.
     */
    virtual StorePathSet queryAllValidPaths() = 0;
};

}

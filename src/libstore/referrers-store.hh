#pragma once
///@file

#include "visible-store.hh"

namespace nix {

/**
 * A store that allows querying referrers.
 *
 * The referrers relation is the dual of the references relation, the
 * latter being the "regular" one we are usually interested in.
 *
 * This is no inherent reason why this should be a subclass of
 * `VisibleStore`; it just so happens that every extent store object we
 * have to day that implements `queryReferrers()` also implements
 * `queryAllValidPaths()`. If that ceases to be the case, we can revisit
 * this; until this having this interface inheritance means fewer
 * interface combinations to think about.
 */
struct ReferrersStore : virtual VisibleStore
{
    inline static std::string operationName = "Query referrers";

    /**
     * Queries the set of incoming FS references for a store path.
     * The result is not cleared.
     *
     * @param path The path of the store object we care about incoming
     * references to.
     *
     * @param [out] referrers The set in which to collect the referrers
     * of `path`.
     */
    virtual void queryReferrers(const StorePath & path, StorePathSet & referrers) = 0;

    /**
     * @param [out] out Place in here the set of all store paths in the
     * file system co-closure of `storePath`; that is, all paths than
     * directly or indirectly refer from it. `out` is not cleared.
     *
     * Whereas `Store::computeFSClosure` uses the `references` relation,
     * this function uses the dual of it which is the `referrers`
     * relation.
     */
    virtual void computeFSCoClosure(const StorePathSet & paths,
        StorePathSet & out,
        bool includeOutputs = false, bool includeDerivers = false);

    void computeFSCoClosure(const StorePath & path,
        StorePathSet & out,
        bool includeOutputs = false, bool includeDerivers = false);

};

}

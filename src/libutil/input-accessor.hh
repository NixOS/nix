#pragma once
///@file

#include "source-accessor.hh"
#include "ref.hh"
#include "repair-flag.hh"

namespace nix {

MakeError(RestrictedPathError, Error);

struct InputAccessor : virtual SourceAccessor, std::enable_shared_from_this<InputAccessor>
{
    std::optional<std::string> fingerprint;

    /**
     * Whether this is a store path using
     * FileIngestionMethod::Recursive. This is used to optimize
     * `fetchToStore()`.
     */
    bool isStorePath = false;

    /**
     * Return the maximum last-modified time of the files in this
     * tree, if available.
     */
    virtual std::optional<time_t> getLastModified()
    {
        return std::nullopt;
    }

};

}

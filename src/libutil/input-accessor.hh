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
     * Return the maximum last-modified time of the files in this
     * tree, if available.
     */
    virtual std::optional<time_t> getLastModified()
    {
        return std::nullopt;
    }

};

}

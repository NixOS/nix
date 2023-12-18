#pragma once
///@file

#include "source-accessor.hh"
#include "ref.hh"
// #include "types.hh"
// #include "file-system.hh"
#include "repair-flag.hh"
#include "file-ingestion-method.hh"

namespace nix {

MakeError(RestrictedPathError, Error);

struct SourcePath;
class StorePath;
class Store;

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

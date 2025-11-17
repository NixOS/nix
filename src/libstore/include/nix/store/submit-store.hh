#pragma once
///@file

#include "nix/store/store-api.hh"

namespace nix {

struct SubmitStore : public virtual Store
{
private:
    void anchor() override;

public:
    inline static std::string operationName = "Submit outputs for a currently running derivation";

    /**
     * Submit an output for the current derivation.
     * Only makes sense when running within a recursive-nix derivation
     */
    virtual void submitOutput(const SingleDerivedPath & path, const OutputName & output) = 0;

    static SubmitStore & require(Store & store);
};

} // namespace nix

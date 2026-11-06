#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/store/content-address.hh"
#include "nix/store/derived-path.hh"
#include "nix/store/outputs-spec.hh"
#include "nix/store/path-info.hh"
#include "nix/util/file-content-address.hh"
#include "nix/util/hash.hh"
#include "nix/util/ref.hh"
#include "nix/util/serialise.hh"

#include <string>
#include <string_view>

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

    /**
     * Add to store, scanning references.
     * Only within a recursive-nix derivation, as there would otherwise be no known
     * set of valid store paths
     */
    virtual ref<const ValidPathInfo> addToStoreScanning(
        Source & dump,
        std::string_view name,
        FileSerialisationMethod dumpMethod = FileSerialisationMethod::NixArchive,
        ContentAddressMethod hashMethod = ContentAddressMethod::Raw::NixArchive,
        HashAlgorithm hashAlgo = HashAlgorithm::SHA256) = 0;

    static SubmitStore & require(Store & store);
};

} // namespace nix

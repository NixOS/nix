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

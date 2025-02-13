#pragma once
///@file

#include <variant>

#include "hash.hh"
#include "path.hh"
#include "derived-path.hh"
#include <nlohmann/json_fwd.hpp>
#include "comparator.hh"
#include "signature/signer.hh"

namespace nix {

class Store;
struct OutputsSpec;

/**
 * A general `Realisation` key.
 *
 * This is similar to a `DerivedPath::Built`, except it is only a single
 * step: `drvPath` is a `StorePath` rather than a `DerivedPath`.
 */
struct DrvOutput
{
    /**
     * The store path to the derivation
     */
    StorePath drvPath;

    /**
     * The name of the output.
     */
    OutputName outputName;

    /**
     * Skips the store dir on the `drvPath`
     */
    std::string to_string() const;

    /**
     * Skips the store dir on the `drvPath`
     */
    static DrvOutput from_string(std::string_view);

    /**
     * Includes the store dir on `drvPath`
     */
    std::string render(const StoreDirConfig & store) const;

    /**
     * Includes the store dir on `drvPath`
     */
    static DrvOutput parse(const StoreDirConfig & store, std::string_view);

    nlohmann::json toJSON(const StoreDirConfig & store) const;
    static DrvOutput fromJSON(const StoreDirConfig & store, const nlohmann::json & json);

    bool operator==(const DrvOutput &) const = default;
    auto operator<=>(const DrvOutput &) const = default;
};

struct UnkeyedRealisation
{
    StorePath outPath;

    StringSet signatures;

    nlohmann::json toJSON(const StoreDirConfig & store) const;
    static UnkeyedRealisation fromJSON(const StoreDirConfig & store, const nlohmann::json & json);

    std::string fingerprint(const StoreDirConfig & store, const DrvOutput & key) const;

    void sign(const StoreDirConfig & store, const DrvOutput & key, const Signer &);

    bool checkSignature(
        const StoreDirConfig & store,
        const DrvOutput & key,
        const PublicKeys & publicKeys,
        const std::string & sig) const;

    size_t checkSignatures(const StoreDirConfig & store, const DrvOutput & key, const PublicKeys & publicKeys) const;

    /**
     * Just check the `outPath`. Signatures don't matter for this.
     * Callers must ensure that the corresponding key is the same for
     * most use-cases.
     */
    bool isCompatibleWith(const UnkeyedRealisation & other) const
    {
        return outPath == other.outPath;
    }

    const StorePath & getPath() const
    {
        return outPath;
    }

    // TODO sketchy that it avoids signatures
    GENERATE_CMP(UnkeyedRealisation, me->outPath);
};

struct Realisation : UnkeyedRealisation
{
    DrvOutput id;

    nlohmann::json toJSON(const StoreDirConfig & store) const;
    static Realisation fromJSON(const StoreDirConfig & store, const nlohmann::json & json);

    bool operator==(const Realisation &) const = default;
    auto operator<=>(const Realisation &) const = default;
};

/**
 * Collection type for a single derivation's outputs' `Realisation`s.
 *
 * Since these are the outputs of a single derivation, we know the
 * output names are unique so we can use them as the map key.
 */
typedef std::map<OutputName, UnkeyedRealisation> SingleDrvOutputs;

/**
 * Collection type for multiple derivations' outputs' `Realisation`s.
 *
 * `DrvOutput` is used because in general the derivations are not all
 * the same, so we need to identify firstly which derivation, and
 * secondly which output of that derivation.
 */
typedef std::map<DrvOutput, UnkeyedRealisation> DrvOutputs;

/**
 * Filter a SingleDrvOutputs to include only specific output names
 *
 * Moves the `outputs` input.
 */
SingleDrvOutputs filterDrvOutputs(const OutputsSpec &, SingleDrvOutputs && outputs);

struct OpaquePath
{
    StorePath path;

    const StorePath & getPath() const
    {
        return path;
    }

    bool operator==(const OpaquePath &) const = default;
    auto operator<=>(const OpaquePath &) const = default;
};

/**
 * A store path with all the history of how it went into the store
 */
struct RealisedPath
{
    /**
     * A path is either the result of the realisation of a derivation or
     * an opaque blob that has been directly added to the store
     */
    using Raw = std::variant<Realisation, OpaquePath>;
    Raw raw;

    using Set = std::set<RealisedPath>;

    RealisedPath(StorePath path)
        : raw(OpaquePath{path})
    {
    }

    RealisedPath(Realisation r)
        : raw(r)
    {
    }

    /**
     * Get the raw store path associated to this
     */
    const StorePath & path() const;

    bool operator==(const RealisedPath &) const = default;
    auto operator<=>(const RealisedPath &) const = default;
};

class MissingRealisation : public Error
{
public:
    MissingRealisation(const StoreDirConfig & store, DrvOutput & outputId)
        : MissingRealisation(store, outputId.drvPath, outputId.outputName)
    {
    }
    MissingRealisation(const StoreDirConfig & store, const StorePath & drvPath, const OutputName & outputName);
    MissingRealisation(
        const StoreDirConfig & store,
        const SingleDerivedPath & drvPath,
        const StorePath & drvPathResolved,
        const OutputName & outputName);
};

}

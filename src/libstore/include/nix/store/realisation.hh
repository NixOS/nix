#pragma once
///@file

#include <variant>

#include "nix/util/hash.hh"
#include "nix/store/path.hh"
#include "nix/store/derived-path.hh"
#include <nlohmann/json_fwd.hpp>
#include "nix/util/comparator.hh"
#include "nix/util/signature/signer.hh"

namespace nix {

class Store;
struct OutputsSpec;

/**
 * A general `Realisation` key.
 *
 * This is similar to a `DerivedPath::Opaque`, but the derivation is
 * identified by its "hash modulo" instead of by its store path.
 */
struct DrvOutput
{
    /**
     * The hash modulo of the derivation.
     *
     * Computed from the derivation itself for most types of
     * derivations, but computed from the (fixed) content address of the
     * output for fixed-output derivations.
     */
    Hash drvHash;

    /**
     * The name of the output.
     */
    OutputName outputName;

    std::string to_string() const;

    std::string strHash() const
    {
        return drvHash.to_string(HashFormat::Base16, true);
    }

    static DrvOutput parse(const std::string &);

    bool operator==(const DrvOutput &) const = default;
    auto operator<=>(const DrvOutput &) const = default;
};

struct UnkeyedRealisation
{
    StorePath outPath;

    StringSet signatures;

    /**
     * The realisations that are required for the current one to be valid.
     *
     * When importing this realisation, the store will first check that all its
     * dependencies exist, and map to the correct output path
     */
    std::map<DrvOutput, StorePath> dependentRealisations;

    std::string fingerprint(const DrvOutput & key) const;

    void sign(const DrvOutput & key, const Signer &);

    bool checkSignature(const DrvOutput & key, const PublicKeys & publicKeys, const std::string & sig) const;

    size_t checkSignatures(const DrvOutput & key, const PublicKeys & publicKeys) const;

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

    bool isCompatibleWith(const UnkeyedRealisation & other) const;

    static std::set<Realisation> closure(Store &, const std::set<Realisation> &);

    static void closure(Store &, const std::set<Realisation> &, std::set<Realisation> & res);

    bool operator==(const Realisation &) const = default;
    auto operator<=>(const Realisation &) const = default;
};

/**
 * Collection type for a single derivation's outputs' `Realisation`s.
 *
 * Since these are the outputs of a single derivation, we know the
 * output names are unique so we can use them as the map key.
 */
typedef std::map<OutputName, Realisation> SingleDrvOutputs;

/**
 * Collection type for multiple derivations' outputs' `Realisation`s.
 *
 * `DrvOutput` is used because in general the derivations are not all
 * the same, so we need to identify firstly which derivation, and
 * secondly which output of that derivation.
 */
typedef std::map<DrvOutput, Realisation> DrvOutputs;

struct OpaquePath
{
    StorePath path;

    const StorePath & getPath() const &
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
    const StorePath & path() const &;

    void closure(Store & store, Set & ret) const;
    static void closure(Store & store, const Set & startPaths, Set & ret);
    Set closure(Store & store) const;

    bool operator==(const RealisedPath &) const = default;
    auto operator<=>(const RealisedPath &) const = default;
};

class MissingRealisation : public Error
{
public:
    MissingRealisation(DrvOutput & outputId)
        : MissingRealisation(outputId.outputName, outputId.strHash())
    {
    }

    MissingRealisation(std::string_view drv, OutputName outputName)
        : Error(
              "cannot operate on output '%s' of the "
              "unbuilt derivation '%s'",
              outputName,
              drv)
    {
    }
};

} // namespace nix

JSON_IMPL(nix::DrvOutput)
JSON_IMPL(nix::UnkeyedRealisation)
JSON_IMPL(nix::Realisation)

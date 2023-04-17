#pragma once
///@file

#include <variant>

#include "hash.hh"
#include "path.hh"
#include <nlohmann/json_fwd.hpp>
#include "comparator.hh"
#include "crypto.hh"

namespace nix {

class Store;

/**
 * A general `Realisation` key.
 *
 * This is similar to a `DerivedPath::Opaque`, but the derivation is
 * identified by its "hash modulo" instead of by its store path.
 */
struct DrvOutput {
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
    std::string outputName;

    std::string to_string() const;

    std::string strHash() const
    { return drvHash.to_string(Base16, true); }

    static DrvOutput parse(const std::string &);

    GENERATE_CMP(DrvOutput, me->drvHash, me->outputName);
};

struct Realisation {
    DrvOutput id;
    StorePath outPath;

    StringSet signatures;

    /**
     * The realisations that are required for the current one to be valid.
     *
     * When importing this realisation, the store will first check that all its
     * dependencies exist, and map to the correct output path
     */
    std::map<DrvOutput, StorePath> dependentRealisations;

    nlohmann::json toJSON() const;
    static Realisation fromJSON(const nlohmann::json& json, const std::string& whence);

    std::string fingerprint() const;
    void sign(const SecretKey &);
    bool checkSignature(const PublicKeys & publicKeys, const std::string & sig) const;
    size_t checkSignatures(const PublicKeys & publicKeys) const;

    static std::set<Realisation> closure(Store &, const std::set<Realisation> &);
    static void closure(Store &, const std::set<Realisation> &, std::set<Realisation> & res);

    bool isCompatibleWith(const Realisation & other) const;

    StorePath getPath() const { return outPath; }

    GENERATE_CMP(Realisation, me->id, me->outPath);
};

/**
 * Collection type for a single derivation's outputs' `Realisation`s.
 *
 * Since these are the outputs of a single derivation, we know the
 * output names are unique so we can use them as the map key.
 */
typedef std::map<std::string, Realisation> SingleDrvOutputs;

/**
 * Collection type for multiple derivations' outputs' `Realisation`s.
 *
 * `DrvOutput` is used because in general the derivations are not all
 * the same, so we need to identify firstly which derivation, and
 * secondly which output of that derivation.
 */
typedef std::map<DrvOutput, Realisation> DrvOutputs;

struct OpaquePath {
    StorePath path;

    StorePath getPath() const { return path; }

    GENERATE_CMP(OpaquePath, me->path);
};


/**
 * A store path with all the history of how it went into the store
 */
struct RealisedPath {
    /*
     * A path is either the result of the realisation of a derivation or
     * an opaque blob that has been directly added to the store
     */
    using Raw = std::variant<Realisation, OpaquePath>;
    Raw raw;

    using Set = std::set<RealisedPath>;

    RealisedPath(StorePath path) : raw(OpaquePath{path}) {}
    RealisedPath(Realisation r) : raw(r) {}

    /**
     * Get the raw store path associated to this
     */
    StorePath path() const;

    void closure(Store& store, Set& ret) const;
    static void closure(Store& store, const Set& startPaths, Set& ret);
    Set closure(Store& store) const;

    GENERATE_CMP(RealisedPath, me->raw);
};

class MissingRealisation : public Error
{
public:
    MissingRealisation(DrvOutput & outputId)
        : Error( "cannot operate on an output of the "
                "unbuilt derivation '%s'",
                outputId.to_string())
    {}
};

}

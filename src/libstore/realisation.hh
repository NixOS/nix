#pragma once

#include <variant>

#include "hash.hh"
#include "path.hh"
#include <nlohmann/json_fwd.hpp>
#include "comparator.hh"

namespace nix {

struct DrvOutput {
    // The hash modulo of the derivation
    Hash drvHash;
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

    nlohmann::json toJSON() const;
    static Realisation fromJSON(const nlohmann::json& json, const std::string& whence);

    StorePath getPath() const { return outPath; }

    GENERATE_CMP(Realisation, me->id, me->outPath);
};

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

}

#pragma once

#include "path.hh"
#include <nlohmann/json_fwd.hpp>

namespace nix {

struct DrvOutput {
    // The hash modulo of the derivation
    Hash drvHash;
    std::string outputName;

    std::string to_string() const;

    std::string strHash() const
    { return drvHash.to_string(Base16, true); }

    static DrvOutput parse(const std::string &);

    bool operator<(const DrvOutput& other) const { return to_pair() < other.to_pair(); }
    bool operator==(const DrvOutput& other) const { return to_pair() == other.to_pair(); }

private:
    // Just to make comparison operators easier to write
    std::pair<Hash, std::string> to_pair() const
    { return std::make_pair(drvHash, outputName); }
};

struct Realisation {
    DrvOutput id;
    StorePath outPath;

    nlohmann::json toJSON() const;
    static Realisation fromJSON(const nlohmann::json& json, const std::string& whence);
};

typedef std::map<DrvOutput, Realisation> DrvOutputs;

}

#pragma once

#include "path.hh"
#include <nlohmann/json_fwd.hpp>

namespace nix {

struct DrvOutput {
    StorePath drvPath;
    std::string outputName;

    std::string to_string() const;

    static DrvOutput parse(const std::string &);

    bool operator<(const DrvOutput& other) const { return to_pair() < other.to_pair(); }
    bool operator==(const DrvOutput& other) const { return to_pair() == other.to_pair(); }

private:
    // Just to make comparison operators easier to write
    std::pair<StorePath, std::string> to_pair() const
    { return std::make_pair(drvPath, outputName); }
};

struct Realisation {
    DrvOutput id;
    StorePath outPath;

    nlohmann::json toJSON() const;
    static Realisation fromJSON(const nlohmann::json& json, const std::string& whence);
};

typedef std::map<DrvOutput, Realisation> DrvOutputs;

}

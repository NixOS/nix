#pragma once

#include "path.hh"

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

    std::string to_string() const;
    static Realisation parse(const std::string & s, const std::string & whence);
};

typedef std::map<DrvOutput, Realisation> DrvOutputs;

}

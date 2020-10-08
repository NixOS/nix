#pragma once

#include "path.hh"

namespace nix {

struct DrvOutputId {
    StorePath drvPath;
    std::string outputName;

    std::string to_string() const;

    static DrvOutputId parse(const std::string &);

    bool operator<(const DrvOutputId& other) const { return to_pair() < other.to_pair(); }
    bool operator==(const DrvOutputId& other) const { return to_pair() == other.to_pair(); }

private:
    // Just to make comparison operators easier to write
    std::pair<StorePath, std::string> to_pair() const
    { return std::make_pair(drvPath, outputName); }
};

typedef std::variant<StorePath, DrvOutputId> RawDrvInput;

struct DrvInput : RawDrvInput {
    using RawDrvInput::RawDrvInput;

    std::string to_string() const;
    static DrvInput parse(const std::string & strRep);
};

struct DrvOutputInfo {
    StorePath outPath;

    std::string to_string() const;
    static DrvOutputInfo parse(const std::string & s, const std::string & whence);
};

typedef std::map<DrvOutputId, DrvOutputInfo> DrvOutputs;

}

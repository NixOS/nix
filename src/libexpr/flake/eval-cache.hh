#pragma once

#include "sync.hh"
#include "flake.hh"

namespace nix { struct SQLite; struct SQLiteStmt; }

namespace nix::flake {

class EvalCache
{
    struct State;

    std::unique_ptr<Sync<State>> _state;

    EvalCache();

public:

    struct Derivation
    {
        Path drvPath;
        Path outPath;
        std::string outputName;
    };

    void addDerivation(
        const Fingerprint & fingerprint,
        const std::string & attrPath,
        const Derivation & drv);

    std::optional<Derivation> getDerivation(
        const Fingerprint & fingerprint,
        const std::string & attrPath);

    static EvalCache & singleton();
};

}

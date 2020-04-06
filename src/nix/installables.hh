#pragma once

#include "util.hh"
#include "path.hh"
#include "eval.hh"

#include <optional>

namespace nix {

struct Buildable
{
    std::optional<StorePath> drvPath;
    std::map<std::string, StorePath> outputs;
};

typedef std::vector<Buildable> Buildables;

struct Installable
{
    virtual ~Installable() { }

    virtual std::string what() = 0;

    virtual Buildables toBuildables()
    {
        throw Error("argument '%s' cannot be built", what());
    }

    Buildable toBuildable();

    virtual std::pair<Value *, Pos> toValue(EvalState & state)
    {
        throw Error("argument '%s' cannot be evaluated", what());
    }

    /* Return a value only if this installable is a store path or a
       symlink to it. */
    virtual std::optional<StorePath> getStorePath()
    {
        return {};
    }
};

}

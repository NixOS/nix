#pragma once
///@file

#include "nix/store/derivations.hh"

namespace nix {

// TODO: make pluggable.
void builtinFetchurl(
    const BasicDerivation & drv,
    const std::map<std::string, Path> & outputs,
    const std::string & netrcData,
    const std::string & caFileData);

using BuiltinBuilder =
    std::function<void(const BasicDerivation & drv, const std::map<std::string, Path> & outputs)>;

struct RegisterBuiltinBuilder
{
    typedef std::map<std::string, BuiltinBuilder> BuiltinBuilders;
    static BuiltinBuilders * builtinBuilders;

    RegisterBuiltinBuilder(const std::string & name, BuiltinBuilder && fun)
    {
        if (!builtinBuilders) builtinBuilders = new BuiltinBuilders;
        builtinBuilders->insert_or_assign(name, std::move(fun));
    }
};

}

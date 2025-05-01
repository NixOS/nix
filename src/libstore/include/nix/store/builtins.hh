#pragma once
///@file

#include "nix/store/derivations.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {

struct BuiltinBuilderContext
{
    BasicDerivation & drv;
    nlohmann::json * structuredAttrs;
    std::map<std::string, Path> outputs;
    std::string netrcData;
    std::string caFileData;
};

using BuiltinBuilder = std::function<void(const BuiltinBuilderContext &)>;

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

#pragma once
///@file

#include "eval.hh"

namespace nix {

struct AbstractNixRepl
{
    ref<EvalState> state;
    Bindings * autoArgs;

    AbstractNixRepl(ref<EvalState> state)
        : state(state)
    { }

    virtual ~AbstractNixRepl()
    { }

    typedef std::vector<std::pair<Value*,std::string>> AnnotatedValues;

    static std::unique_ptr<AbstractNixRepl> create(
        const SearchPath & searchPath, nix::ref<Store> store, ref<EvalState> state,
        std::function<AnnotatedValues()> getValues);

    static ReplExitStatus runSimple(
        ref<EvalState> evalState,
        const ValMap & extraEnv);

    virtual void initEnv() = 0;

    virtual ReplExitStatus mainLoop() = 0;
};

}

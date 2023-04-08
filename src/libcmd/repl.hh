#pragma once
///@file

#include "eval.hh"

#if HAVE_BOEHMGC
#define GC_INCLUDE_NEW
#include <gc/gc_cpp.h>
#endif

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
        const Strings & searchPath, nix::ref<Store> store, ref<EvalState> state,
        std::function<AnnotatedValues()> getValues);

    static void runSimple(
        ref<EvalState> evalState,
        const ValMap & extraEnv);

    virtual void initEnv() = 0;

    virtual void mainLoop() = 0;
};

}

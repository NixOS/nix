#pragma once
///@file

#include "nix/expr/eval.hh"

namespace nix {

struct AbstractNixRepl
{
    ref<EvalState> state;
    Bindings * autoArgs;

    AbstractNixRepl(ref<EvalState> state)
        : state(state)
    {
    }

    virtual ~AbstractNixRepl() {}

    typedef std::vector<std::pair<Value *, std::string>> AnnotatedValues;

    using RunNix = void(Path program, const Strings & args, const std::optional<std::string> & input);

    /**
     * @param runNix Function to run the nix CLI to support various
     * `:<something>` commands. Optional; if not provided,
     * everything else will still work fine, but those commands won't.
     */
    static std::unique_ptr<AbstractNixRepl> create(
        const LookupPath & lookupPath,
        nix::ref<Store> store,
        ref<EvalState> state,
        std::function<AnnotatedValues()> getValues,
        RunNix * runNix = nullptr);

    static ReplExitStatus runSimple(ref<EvalState> evalState, const ValMap & extraEnv);

    virtual void initEnv() = 0;

    virtual ReplExitStatus mainLoop() = 0;
};

} // namespace nix

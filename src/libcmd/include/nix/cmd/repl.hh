#pragma once
///@file

#include "nix/expr/eval.hh"
#include "nix/util/os-string.hh"

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

    /**
     * Run a nix executable
     *
     * @todo this is a layer violation
     *
     * @param programName Name of the command, e.g. `nix` or `nix-env`.
     * @param args arguments to the command.
     */
    using RunNix = void(const std::string & programName, OsStrings args, const std::optional<std::string> & input);

    /**
     * @param runNix Function to run the nix CLI to support various
     * `:<something>` commands. Optional; if not provided,
     * everything else will still work fine, but those commands won't.
     */
    static std::unique_ptr<AbstractNixRepl> create(
        const LookupPath & lookupPath,
        ref<EvalState> state,
        fun<AnnotatedValues()> getValues,
        RunNix * runNix = nullptr);

    static ReplExitStatus runSimple(ref<EvalState> evalState, const ValMap & extraEnv);

    virtual void initEnv() = 0;

    virtual ReplExitStatus mainLoop() = 0;
};

} // namespace nix

#include "primops.hh"
#include "eval-inline.hh"

namespace nix {

static void prim_unsafeDiscardStringContext(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    string s = state.coerceToString(pos, *args[0], context);
    mkString(v, s, PathSet());
}

static RegisterPrimOp r1("__unsafeDiscardStringContext", 1, prim_unsafeDiscardStringContext);


static void prim_hasContext(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    state.forceString(*args[0], context, pos);
    mkBool(v, !context.empty());
}

static RegisterPrimOp r2("__hasContext", 1, prim_hasContext);


/* Sometimes we want to pass a derivation path (i.e. pkg.drvPath) to a
   builder without causing the derivation to be built (for instance,
   in the derivation that builds NARs in nix-push, when doing
   source-only deployment).  This primop marks the string context so
   that builtins.derivation adds the path to drv.inputSrcs rather than
   drv.inputDrvs. */
static void prim_unsafeDiscardOutputDependency(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    string s = state.coerceToString(pos, *args[0], context);

    PathSet context2;
    for (auto & p : context)
        context2.insert(p.at(0) == '=' ? string(p, 1) : p);

    mkString(v, s, context2);
}

static RegisterPrimOp r3("__unsafeDiscardOutputDependency", 1, prim_unsafeDiscardOutputDependency);


/* Extract the context of a string as a structured Nix value.

   The context is represented as an attribute set whose keys are the
   paths in the context set and whose values are attribute sets with
   the following keys:
     path: True if the relevant path is in the context as a plain store
           path (i.e. the kind of context you get when interpolating
           a Nix path (e.g. ./.) into a string). False if missing.
     allOutputs: True if the relevant path is a derivation and it is
                  in the context as a drv file with all of its outputs
                  (i.e. the kind of context you get when referencing
                  .drvPath of some derivation). False if missing.
     outputs: If a non-empty list, the relevant path is a derivation
              and the provided outputs are referenced in the context
              (i.e. the kind of context you get when referencing
              .outPath of some derivation). Empty list if missing.
   Note that for a given path any combination of the above attributes
   may be present, but at least one must be set to something other
   than the default.
*/
static void prim_getContext(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    struct ContextInfo {
        bool path = false;
        bool allOutputs = false;
        Strings outputs;
    };
    PathSet context;
    state.forceString(*args[0], context, pos);
    auto contextInfos = std::map<Path, ContextInfo>();
    for (const auto & p : context) {
        Path drv;
        string output;
        const Path * path = &p;
        if (p.at(0) == '=') {
            drv = string(p, 1);
            path = &drv;
        } else if (p.at(0) == '!') {
            std::pair<string, string> ctx = decodeContext(p);
            drv = ctx.first;
            output = ctx.second;
            path = &drv;
        }
        auto isPath = drv.empty();
        auto isAllOutputs = (!drv.empty()) && output.empty();

        auto iter = contextInfos.find(*path);
        if (iter == contextInfos.end()) {
            contextInfos.emplace(*path, ContextInfo{isPath, isAllOutputs, output.empty() ? Strings{} : Strings{std::move(output)}});
        } else {
            if (isPath)
                iter->second.path = true;
            else if (isAllOutputs)
                iter->second.allOutputs = true;
            else
                iter->second.outputs.emplace_back(std::move(output));
        }
    }

    state.mkAttrs(v, contextInfos.size());

    auto sPath = state.symbols.create("path");
    auto sAllOutputs = state.symbols.create("allOutputs");
    for (const auto & info : contextInfos) {
        auto & infoVal = *state.allocAttr(v, state.symbols.create(info.first));
        state.mkAttrs(infoVal, 3);
        if (info.second.path)
            mkBool(*state.allocAttr(infoVal, sPath), true);
        if (info.second.allOutputs)
            mkBool(*state.allocAttr(infoVal, sAllOutputs), true);
        if (!info.second.outputs.empty()) {
            auto & outputsVal = *state.allocAttr(infoVal, state.sOutputs);
            state.mkList(outputsVal, info.second.outputs.size());
            size_t i = 0;
            for (const auto & output : info.second.outputs) {
                mkString(*(outputsVal.listElems()[i++] = state.allocValue()), output);
            }
        }
        infoVal.attrs->sort();
    }
    v.attrs->sort();
}

static RegisterPrimOp r4("__getContext", 1, prim_getContext);

}

#include "primops.hh"
#include "eval-inline.hh"
#include "derivations.hh"
#include "store-api.hh"

namespace nix {

static void prim_unsafeDiscardStringContext(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    auto s = state.coerceToString(pos, *args[0], context);
    v.mkString(*s);
}

static RegisterPrimOp primop_unsafeDiscardStringContext("__unsafeDiscardStringContext", 1, prim_unsafeDiscardStringContext);


static void prim_hasContext(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    state.forceString(*args[0], context, pos);
    v.mkBool(!context.empty());
}

static RegisterPrimOp primop_hasContext("__hasContext", 1, prim_hasContext);


/* Sometimes we want to pass a derivation path (i.e. pkg.drvPath) to a
   builder without causing the derivation to be built (for instance,
   in the derivation that builds NARs in nix-push, when doing
   source-only deployment).  This primop marks the string context so
   that builtins.derivation adds the path to drv.inputSrcs rather than
   drv.inputDrvs. */
static void prim_unsafeDiscardOutputDependency(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    auto s = state.coerceToString(pos, *args[0], context);

    PathSet context2;
    for (auto & p : context)
        context2.insert(p.at(0) == '=' ? std::string(p, 1) : p);

    v.mkString(*s, context2);
}

static RegisterPrimOp primop_unsafeDiscardOutputDependency("__unsafeDiscardOutputDependency", 1, prim_unsafeDiscardOutputDependency);


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
   may be present.
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
        std::string output;
        const Path * path = &p;
        if (p.at(0) == '=') {
            drv = std::string(p, 1);
            path = &drv;
        } else if (p.at(0) == '!') {
            NixStringContextElem ctx = decodeContext(*state.store, p);
            drv = state.store->printStorePath(ctx.first);
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

    auto attrs = state.buildBindings(contextInfos.size());

    auto sPath = state.symbols.create("path");
    auto sAllOutputs = state.symbols.create("allOutputs");
    for (const auto & info : contextInfos) {
        auto infoAttrs = state.buildBindings(3);
        if (info.second.path)
            infoAttrs.alloc(sPath).mkBool(true);
        if (info.second.allOutputs)
            infoAttrs.alloc(sAllOutputs).mkBool(true);
        if (!info.second.outputs.empty()) {
            auto & outputsVal = infoAttrs.alloc(state.sOutputs);
            state.mkList(outputsVal, info.second.outputs.size());
            for (const auto & [i, output] : enumerate(info.second.outputs))
                (outputsVal.listElems()[i] = state.allocValue())->mkString(output);
        }
        attrs.alloc(info.first).mkAttrs(infoAttrs);
    }

    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_getContext("__getContext", 1, prim_getContext);


/* Append the given context to a given string.

   See the commentary above unsafeGetContext for details of the
   context representation.
*/
static void prim_appendContext(EvalState & state, const Pos & pos, Value * * args, Value & v)
{
    PathSet context;
    auto orig = state.forceString(*args[0], context, pos);

    state.forceAttrs(*args[1], pos);

    auto sPath = state.symbols.create("path");
    auto sAllOutputs = state.symbols.create("allOutputs");
    for (auto & i : *args[1]->attrs) {
        if (!state.store->isStorePath(i.name))
            throw EvalError({
                .msg = hintfmt("Context key '%s' is not a store path", i.name),
                .errPos = *i.pos
            });
        if (!settings.readOnlyMode)
            state.store->ensurePath(state.store->parseStorePath(i.name));
        state.forceAttrs(*i.value, *i.pos);
        auto iter = i.value->attrs->find(sPath);
        if (iter != i.value->attrs->end()) {
            if (state.forceBool(*iter->value, *iter->pos))
                context.insert(i.name);
        }

        iter = i.value->attrs->find(sAllOutputs);
        if (iter != i.value->attrs->end()) {
            if (state.forceBool(*iter->value, *iter->pos)) {
                if (!isDerivation(i.name)) {
                    throw EvalError({
                        .msg = hintfmt("Tried to add all-outputs context of %s, which is not a derivation, to a string", i.name),
                        .errPos = *i.pos
                    });
                }
                context.insert("=" + std::string(i.name));
            }
        }

        iter = i.value->attrs->find(state.sOutputs);
        if (iter != i.value->attrs->end()) {
            state.forceList(*iter->value, *iter->pos);
            if (iter->value->listSize() && !isDerivation(i.name)) {
                throw EvalError({
                    .msg = hintfmt("Tried to add derivation output context of %s, which is not a derivation, to a string", i.name),
                    .errPos = *i.pos
                });
            }
            for (auto elem : iter->value->listItems()) {
                auto name = state.forceStringNoCtx(*elem, *iter->pos);
                context.insert(concatStrings("!", name, "!", i.name));
            }
        }
    }

    v.mkString(orig, context);
}

static RegisterPrimOp primop_appendContext("__appendContext", 2, prim_appendContext);

}

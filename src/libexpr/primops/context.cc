#include "primops.hh"
#include "eval-inline.hh"
#include "derivations.hh"
#include "store-api.hh"

namespace nix {

static void prim_unsafeDiscardStringContext(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    PathSet context;
    auto s = state.coerceToString(pos, *args[0], context, "while evaluating the argument passed to builtins.unsafeDiscardStringContext");
    v.mkString(*s);
}

static RegisterPrimOp primop_unsafeDiscardStringContext("__unsafeDiscardStringContext", 1, prim_unsafeDiscardStringContext);


static void prim_hasContext(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    PathSet context;
    state.forceString(*args[0], context, pos, "while evaluating the argument passed to builtins.hasContext");
    v.mkBool(!context.empty());
}

static RegisterPrimOp primop_hasContext("__hasContext", 1, prim_hasContext);


/* Sometimes we want to pass a derivation path (i.e. pkg.drvPath) to a
   builder without causing the derivation to be built (for instance,
   in the derivation that builds NARs in nix-push, when doing
   source-only deployment).  This primop marks the string context so
   that builtins.derivation adds the path to drv.inputSrcs rather than
   drv.inputDrvs. */
static void prim_unsafeDiscardOutputDependency(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    PathSet context;
    auto s = state.coerceToString(pos, *args[0], context, "while evaluating the argument passed to builtins.unsafeDiscardOutputDependency");

    PathSet context2;
    for (auto && p : context) {
        auto c = NixStringContextElem::parse(*state.store, p);
        if (auto * ptr = std::get_if<NixStringContextElem::DrvDeep>(&c)) {
            context2.emplace(state.store->printStorePath(ptr->drvPath));
        } else {
            /* Can reuse original item */
            context2.emplace(std::move(p));
        }
    }

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
static void prim_getContext(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    struct ContextInfo {
        bool path = false;
        bool allOutputs = false;
        Strings outputs;
    };
    PathSet context;
    state.forceString(*args[0], context, pos, "while evaluating the argument passed to builtins.getContext");
    auto contextInfos = std::map<StorePath, ContextInfo>();
    for (const auto & p : context) {
        Path drv;
        std::string output;
        NixStringContextElem ctx = NixStringContextElem::parse(*state.store, p);
        std::visit(overloaded {
            [&](NixStringContextElem::DrvDeep & d) {
                contextInfos[d.drvPath].allOutputs = true;
            },
            [&](NixStringContextElem::Built & b) {
                contextInfos[b.drvPath].outputs.emplace_back(std::move(output));
            },
            [&](NixStringContextElem::Opaque & o) {
                contextInfos[o.path].path = true;
            },
        }, ctx.raw());
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
        attrs.alloc(state.store->printStorePath(info.first)).mkAttrs(infoAttrs);
    }

    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_getContext("__getContext", 1, prim_getContext);


/* Append the given context to a given string.

   See the commentary above unsafeGetContext for details of the
   context representation.
*/
static void prim_appendContext(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    PathSet context;
    auto orig = state.forceString(*args[0], context, noPos, "while evaluating the first argument passed to builtins.appendContext");

    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.appendContext");

    auto sPath = state.symbols.create("path");
    auto sAllOutputs = state.symbols.create("allOutputs");
    for (auto & i : *args[1]->attrs) {
        const auto & name = state.symbols[i.name];
        if (!state.store->isStorePath(name))
            throw EvalError({
                .msg = hintfmt("context key '%s' is not a store path", name),
                .errPos = state.positions[i.pos]
            });
        if (!settings.readOnlyMode)
            state.store->ensurePath(state.store->parseStorePath(name));
        state.forceAttrs(*i.value, i.pos, "while evaluating the value of a string context");
        auto iter = i.value->attrs->find(sPath);
        if (iter != i.value->attrs->end()) {
            if (state.forceBool(*iter->value, iter->pos, "while evaluating the `path` attribute of a string context"))
                context.emplace(name);
        }

        iter = i.value->attrs->find(sAllOutputs);
        if (iter != i.value->attrs->end()) {
            if (state.forceBool(*iter->value, iter->pos, "while evaluating the `allOutputs` attribute of a string context")) {
                if (!isDerivation(name)) {
                    throw EvalError({
                        .msg = hintfmt("tried to add all-outputs context of %s, which is not a derivation, to a string", name),
                        .errPos = state.positions[i.pos]
                    });
                }
                context.insert(concatStrings("=", name));
            }
        }

        iter = i.value->attrs->find(state.sOutputs);
        if (iter != i.value->attrs->end()) {
            state.forceList(*iter->value, iter->pos, "while evaluating the `outputs` attribute of a string context");
            if (iter->value->listSize() && !isDerivation(name)) {
                throw EvalError({
                    .msg = hintfmt("tried to add derivation output context of %s, which is not a derivation, to a string", name),
                    .errPos = state.positions[i.pos]
                });
            }
            for (auto elem : iter->value->listItems()) {
                auto outputName = state.forceStringNoCtx(*elem, iter->pos, "while evaluating an output name within a string context");
                context.insert(concatStrings("!", outputName, "!", name));
            }
        }
    }

    v.mkString(orig, context);
}

static RegisterPrimOp primop_appendContext("__appendContext", 2, prim_appendContext);

}

#include "primops.hh"
#include "eval-inline.hh"
#include "derivations.hh"
#include "store-api.hh"

namespace nix {

static void prim_unsafeDiscardStringContext(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(pos, *args[0], context, "while evaluating the argument passed to builtins.unsafeDiscardStringContext");
    v.mkString(*s);
}

static RegisterPrimOp primop_unsafeDiscardStringContext({
    .name = "__unsafeDiscardStringContext",
    .arity = 1,
    .fun = prim_unsafeDiscardStringContext
});


static void prim_hasContext(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    state.forceString(*args[0], context, pos, "while evaluating the argument passed to builtins.hasContext");
    v.mkBool(!context.empty());
}

static RegisterPrimOp primop_hasContext({
    .name = "__hasContext",
    .args = {"s"},
    .doc = R"(
      Return `true` if string *s* has a non-empty context.
      The context can be obtained with
      [`getContext`](#builtins-getContext).

      > **Example**
      >
      > Many operations require a string context to be empty because they are intended only to work with "regular" strings, and also to help users avoid unintentionally loosing track of string context elements.
      > `builtins.hasContext` can help create better domain-specific errors in those case.
      >
      > ```nix
      > name: meta:
      >
      > if builtins.hasContext name
      > then throw "package name cannot contain string context"
      > else { ${name} = meta; }
      > ```
    )",
    .fun = prim_hasContext
});


static void prim_unsafeDiscardOutputDependency(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(pos, *args[0], context, "while evaluating the argument passed to builtins.unsafeDiscardOutputDependency");

    NixStringContext context2;
    for (auto && c : context) {
        if (auto * ptr = std::get_if<NixStringContextElem::DrvDeep>(&c.raw)) {
            context2.emplace(NixStringContextElem::Opaque {
                .path = ptr->drvPath
            });
        } else {
            /* Can reuse original item */
            context2.emplace(std::move(c).raw);
        }
    }

    v.mkString(*s, context2);
}

static RegisterPrimOp primop_unsafeDiscardOutputDependency({
    .name = "__unsafeDiscardOutputDependency",
    .args = {"s"},
    .doc = R"(
      Create a copy of the given string where every "derivation deep" string context element is turned into a constant string context element.

      This is the opposite of [`builtins.addDrvOutputDependencies`](#builtins-addDrvOutputDependencies).

      This is unsafe because it allows us to "forget" store objects we would have otherwise refered to with the string context,
      whereas Nix normally tracks all dependencies consistently.
      Safe operations "grow" but never "shrink" string contexts.
      [`builtins.addDrvOutputDependencies`] in contrast is safe because "derivation deep" string context element always refers to the underlying derivation (among many more things).
      Replacing a constant string context element with a "derivation deep" element is a safe operation that just enlargens the string context without forgetting anything.

      [`builtins.addDrvOutputDependencies`]: #builtins-addDrvOutputDependencies
    )",
    .fun = prim_unsafeDiscardOutputDependency
});


static void prim_addDrvOutputDependencies(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(pos, *args[0], context, "while evaluating the argument passed to builtins.addDrvOutputDependencies");

	auto contextSize = context.size();
    if (contextSize != 1) {
        state.error<EvalError>(
            "context of string '%s' must have exactly one element, but has %d",
            *s,
            contextSize
        ).atPos(pos).debugThrow();
    }
    NixStringContext context2 {
        (NixStringContextElem { std::visit(overloaded {
            [&](const NixStringContextElem::Opaque & c) -> NixStringContextElem::DrvDeep {
                if (!c.path.isDerivation()) {
                    state.error<EvalError>(
                        "path '%s' is not a derivation",
                        state.store->printStorePath(c.path)
                    ).atPos(pos).debugThrow();
                }
                return NixStringContextElem::DrvDeep {
                    .drvPath = c.path,
                };
            },
            [&](const NixStringContextElem::Built & c) -> NixStringContextElem::DrvDeep {
                state.error<EvalError>(
                    "`addDrvOutputDependencies` can only act on derivations, not on a derivation output such as '%1%'",
                    c.output
                ).atPos(pos).debugThrow();
            },
            [&](const NixStringContextElem::DrvDeep & c) -> NixStringContextElem::DrvDeep {
                /* Reuse original item because we want this to be idempotent. */
                return std::move(c);
            },
        }, context.begin()->raw) }),
    };

    v.mkString(*s, context2);
}

static RegisterPrimOp primop_addDrvOutputDependencies({
    .name = "__addDrvOutputDependencies",
    .args = {"s"},
    .doc = R"(
      Create a copy of the given string where a single constant string context element is turned into a "derivation deep" string context element.

      The store path that is the constant string context element should point to a valid derivation, and end in `.drv`.

      The original string context element must not be empty or have multiple elements, and it must not have any other type of element other than a constant or derivation deep element.
      The latter is supported so this function is idempotent.

      This is the opposite of [`builtins.unsafeDiscardOutputDependency`](#builtins-unsafeDiscardOutputDependency).
    )",
    .fun = prim_addDrvOutputDependencies
});


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
    NixStringContext context;
    state.forceString(*args[0], context, pos, "while evaluating the argument passed to builtins.getContext");
    auto contextInfos = std::map<StorePath, ContextInfo>();
    for (auto && i : context) {
        std::visit(overloaded {
            [&](NixStringContextElem::DrvDeep && d) {
                contextInfos[std::move(d.drvPath)].allOutputs = true;
            },
            [&](NixStringContextElem::Built && b) {
                // FIXME should eventually show string context as is, no
                // resolving here.
                auto drvPath = resolveDerivedPath(*state.store, *b.drvPath);
                contextInfos[std::move(drvPath)].outputs.emplace_back(std::move(b.output));
            },
            [&](NixStringContextElem::Opaque && o) {
                contextInfos[std::move(o.path)].path = true;
            },
        }, ((NixStringContextElem &&) i).raw);
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
            auto list = state.buildList(info.second.outputs.size());
            for (const auto & [i, output] : enumerate(info.second.outputs))
                (list[i] = state.allocValue())->mkString(output);
            infoAttrs.alloc(state.sOutputs).mkList(list);
        }
        attrs.alloc(state.store->printStorePath(info.first)).mkAttrs(infoAttrs);
    }

    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_getContext({
    .name = "__getContext",
    .args = {"s"},
    .doc = R"(
      Return the string context of *s*.

      The string context tracks references to derivations within a string.
      It is represented as an attribute set of [store derivation](@docroot@/glossary.md#gloss-store-derivation) paths mapping to output names.

      Using [string interpolation](@docroot@/language/string-interpolation.md) on a derivation will add that derivation to the string context.
      For example,

      ```nix
      builtins.getContext "${derivation { name = "a"; builder = "b"; system = "c"; }}"
      ```

      evaluates to

      ```
      { "/nix/store/arhvjaf6zmlyn8vh8fgn55rpwnxq0n7l-a.drv" = { outputs = [ "out" ]; }; }
      ```
    )",
    .fun = prim_getContext
});


/* Append the given context to a given string.

   See the commentary above getContext for details of the
   context representation.
*/
static void prim_appendContext(EvalState & state, const PosIdx pos, Value * * args, Value & v)
{
    NixStringContext context;
    auto orig = state.forceString(*args[0], context, noPos, "while evaluating the first argument passed to builtins.appendContext");

    state.forceAttrs(*args[1], pos, "while evaluating the second argument passed to builtins.appendContext");

    auto sPath = state.symbols.create("path");
    auto sAllOutputs = state.symbols.create("allOutputs");
    for (auto & i : *args[1]->attrs) {
        const auto & name = state.symbols[i.name];
        if (!state.store->isStorePath(name))
            state.error<EvalError>(
                "context key '%s' is not a store path",
                name
            ).atPos(i.pos).debugThrow();
        auto namePath = state.store->parseStorePath(name);
        if (!settings.readOnlyMode)
            state.store->ensurePath(namePath);
        state.forceAttrs(*i.value, i.pos, "while evaluating the value of a string context");
        auto iter = i.value->attrs->find(sPath);
        if (iter != i.value->attrs->end()) {
            if (state.forceBool(*iter->value, iter->pos, "while evaluating the `path` attribute of a string context"))
                context.emplace(NixStringContextElem::Opaque {
                    .path = namePath,
                });
        }

        iter = i.value->attrs->find(sAllOutputs);
        if (iter != i.value->attrs->end()) {
            if (state.forceBool(*iter->value, iter->pos, "while evaluating the `allOutputs` attribute of a string context")) {
                if (!isDerivation(name)) {
                    state.error<EvalError>(
                        "tried to add all-outputs context of %s, which is not a derivation, to a string",
                        name
                    ).atPos(i.pos).debugThrow();
                }
                context.emplace(NixStringContextElem::DrvDeep {
                    .drvPath = namePath,
                });
            }
        }

        iter = i.value->attrs->find(state.sOutputs);
        if (iter != i.value->attrs->end()) {
            state.forceList(*iter->value, iter->pos, "while evaluating the `outputs` attribute of a string context");
            if (iter->value->listSize() && !isDerivation(name)) {
                state.error<EvalError>(
                    "tried to add derivation output context of %s, which is not a derivation, to a string",
                    name
                ).atPos(i.pos).debugThrow();
            }
            for (auto elem : iter->value->listItems()) {
                auto outputName = state.forceStringNoCtx(*elem, iter->pos, "while evaluating an output name within a string context");
                context.emplace(NixStringContextElem::Built {
                    .drvPath = makeConstantStorePathRef(namePath),
                    .output = std::string { outputName },
                });
            }
        }
    }

    v.mkString(orig, context);
}

static RegisterPrimOp primop_appendContext({
    .name = "__appendContext",
    .arity = 2,
    .fun = prim_appendContext
});

}

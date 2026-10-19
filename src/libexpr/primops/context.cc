#include "nix/expr/primops.hh"
#include "nix/expr/eval-inline.hh"
#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"
#include "nix/store/build.hh"
#include "nix/store/globals.hh"

namespace nix {

static void prim_unsafeDiscardStringContext(EvalState & state, CallSite callSite, Value * const * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(
        noPos, *args[0], context, "while evaluating the argument passed to builtins.unsafeDiscardStringContext");
    v.mkString(*s, state.mem);
}

static RegisterPrimOp primop_unsafeDiscardStringContext({
    .name = "__unsafeDiscardStringContext",
    .args = {"s"},
    .doc = R"(
        Discard the [string context](@docroot@/language/string-context.md) from a value that can be coerced to a string.
    )",
    .impl = prim_unsafeDiscardStringContext,
});

static void prim_hasContext(EvalState & state, CallSite callSite, Value * const * args, Value & v)
{
    NixStringContext context;
    state.forceString(*args[0], context, noPos, "while evaluating the argument passed to builtins.hasContext");
    v.mkBool(!context.empty());
}

static RegisterPrimOp primop_hasContext(
    {.name = "__hasContext",
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
     .impl = prim_hasContext});

static void prim_unsafeDiscardOutputDependency(EvalState & state, CallSite callSite, Value * const * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(
        noPos, *args[0], context, "while evaluating the argument passed to builtins.unsafeDiscardOutputDependency");

    NixStringContext context2;
    for (auto && c : context) {
        if (auto * ptr = std::get_if<NixStringContextElem::DrvDeep>(&c.raw)) {
            context2.emplace(NixStringContextElem::Opaque{.path = ptr->drvPath});
        } else {
            /* Can reuse original item */
            context2.emplace(std::move(c).raw);
        }
    }

    v.mkString(*s, context2, state.mem);
}

static RegisterPrimOp primop_unsafeDiscardOutputDependency(
    {.name = "__unsafeDiscardOutputDependency",
     .args = {"s"},
     .doc = R"(
      Create a copy of the given string where every
      [derivation deep](@docroot@/language/string-context.md#string-context-element-derivation-deep)
      string context element is turned into a
      [constant](@docroot@/language/string-context.md#string-context-constant)
      string context element.

      This is the opposite of [`builtins.addDrvOutputDependencies`](#builtins-addDrvOutputDependencies).

      This is unsafe because it allows us to "forget" store objects we would have otherwise referred to with the string context,
      whereas Nix normally tracks all dependencies consistently.
      Safe operations "grow" but never "shrink" string contexts.
      [`builtins.addDrvOutputDependencies`] in contrast is safe because "derivation deep" string context element always refers to the underlying derivation (among many more things).
      Replacing a constant string context element with a "derivation deep" element is a safe operation that just enlargens the string context without forgetting anything.

      [`builtins.addDrvOutputDependencies`]: #builtins-addDrvOutputDependencies
    )",
     .impl = prim_unsafeDiscardOutputDependency});

static void prim_addDrvOutputDependencies(EvalState & state, CallSite callSite, Value * const * args, Value & v)
{
    NixStringContext context;
    auto s = state.coerceToString(
        noPos, *args[0], context, "while evaluating the argument passed to builtins.addDrvOutputDependencies");

    auto contextSize = context.size();
    if (contextSize != 1) {
        state.error<EvalError>("context of string '%s' must have exactly one element, but has %d", *s, contextSize)
            .atPos(noPos)
            .debugThrow();
    }
    NixStringContext context2{
        (NixStringContextElem{std::visit(
            overloaded{
                [&](const NixStringContextElem::Opaque & c) -> NixStringContextElem::DrvDeep {
                    if (!c.path.isDerivation()) {
                        state.error<EvalError>("path '%s' is not a derivation", state.store->printStorePath(c.path))
                            .atPos(noPos)
                            .debugThrow();
                    }
                    return NixStringContextElem::DrvDeep{
                        .drvPath = c.path,
                    };
                },
                [&](const NixStringContextElem::Built & c) -> NixStringContextElem::DrvDeep {
                    state
                        .error<EvalError>(
                            "`addDrvOutputDependencies` can only act on derivations, not on a derivation output such as '%1%'",
                            c.output)
                        .atPos(noPos)
                        .debugThrow();
                },
                [&](const NixStringContextElem::DrvDeep & c) -> NixStringContextElem::DrvDeep {
                    /* Reuse original item because we want this to be idempotent. */
                    /* FIXME: Suspicious move out of const. This is actually a copy, so the comment
                     above does not make much sense. */
                    return std::move(c);
                },
            },
            context.begin()->raw)}),
    };

    v.mkString(*s, context2, state.mem);
}

static RegisterPrimOp primop_addDrvOutputDependencies(
    {.name = "__addDrvOutputDependencies",
     .args = {"s"},
     .doc = R"(
      Create a copy of the given string where a single
      [constant](@docroot@/language/string-context.md#string-context-constant)
      string context element is turned into a
      [derivation deep](@docroot@/language/string-context.md#string-context-element-derivation-deep)
      string context element.

      The store path that is the constant string context element should point to a valid derivation, and end in `.drv`.

      The original string context element must not be empty or have multiple elements, and it must not have any other type of element other than a constant or derivation deep element.
      The latter is supported so this function is idempotent.

      This is the opposite of [`builtins.unsafeDiscardOutputDependency`](#builtins-unsafeDiscardOutputDependency).
    )",
     .impl = prim_addDrvOutputDependencies});

static void prim_getContext(EvalState & state, CallSite callSite, Value * const * args, Value & v)
{
    struct ContextInfo
    {
        bool path = false;
        bool allOutputs = false;
        /* Each referenced output as its `outputChain()`. */
        std::vector<std::vector<OutputName>> outputs;
    };

    NixStringContext context;
    state.forceString(*args[0], context, noPos, "while evaluating the argument passed to builtins.getContext");
    auto contextInfos = std::map<StorePath, ContextInfo>();
    for (auto && i : context) {
        std::visit(
            overloaded{
                [&](NixStringContextElem::DrvDeep && d) { contextInfos[std::move(d.drvPath)].allOutputs = true; },
                [&](NixStringContextElem::Built && b) {
                    contextInfos[b.getBaseStorePath()].outputs.push_back(b.outputChain());
                },
                [&](NixStringContextElem::Opaque && o) { contextInfos[std::move(o.path)].path = true; },
            },
            ((NixStringContextElem &&) i).raw);
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
            for (const auto & [i, output] : enumerate(info.second.outputs)) {
                auto & elem = *(list[i] = state.allocValue());
                if (output.size() == 1)
                    elem.mkString(output[0], state.mem);
                else {
                    auto chain = state.buildList(output.size());
                    for (const auto & [j, name] : enumerate(output))
                        (chain[j] = state.allocValue())->mkString(name, state.mem);
                    elem.mkList(chain);
                }
            }
            infoAttrs.alloc(state.s.outputs).mkList(list);
        }
        attrs.alloc(state.store->printStorePath(info.first)).mkAttrs(infoAttrs);
    }

    v.mkAttrs(attrs);
}

static RegisterPrimOp primop_getContext(
    {.name = "__getContext",
     .args = {"s"},
     .doc = R"(
      Return the [string context](@docroot@/language/string-context.md) of *s*, in its [Nix language representation](@docroot@/language/string-context.md#nix-language-representation).

      Using [string interpolation](@docroot@/language/string-interpolation.md) on a derivation adds that derivation to the string context.
      For example,

      ```nix
      builtins.getContext "${derivation { name = "a"; builder = "b"; system = "c"; }}"
      ```

      evaluates to

      ```
      { "/nix/store/arhvjaf6zmlyn8vh8fgn55rpwnxq0n7l-a.drv" = { outputs = [ "out" ]; }; }
      ```
    )",
     .impl = prim_getContext});

/* Parse the element at `index` of the `outputs` list of the derivation at `drvPath`. */
static NixStringContextElem::Built
parseOutputContextElem(EvalState & state, const StorePath & drvPath, size_t index, Value & output, PosIdx pos)
{
    state.forceValue(output, pos);
    Value * const outputPtr = &output;
    std::span<Value * const> outputValues(&outputPtr, 1);
    std::optional<ListView> chain;
    if (output.type() == nList)
        outputValues = chain.emplace(output.listView()).span();
    else if (output.type() != nString)
        state
            .error<TypeError>(
                "expected a string or a list of strings but found %s in the outputs of string context '%s'",
                showType(output),
                state.store->printStorePath(drvPath))
            .atPos(pos)
            .debugThrow();
    if (outputValues.empty())
        state
            .error<EvalError>(
                "expected a non-empty list of outputs, but the output at index %d is %s, suggesting a reference to the derivation itself, instead of an actual output. To represent a reference to a store derivation, use %s instead of %s.",
                index,
                "[ ]",
                "path = true",
                "outputs")
            .atPos(pos)
            .debugThrow();
    std::vector<std::string_view> outputNames;
    for (auto outputValue : outputValues)
        outputNames.push_back(
            state.forceStringNoCtx(*outputValue, pos, "while evaluating an output name within a string context"));

    ref<const SingleDerivedPath> outputDrvPath = makeConstantStorePathRef(drvPath);
    for (auto outputName : std::span(outputNames).first(outputNames.size() - 1))
        outputDrvPath = make_ref<const SingleDerivedPath>(SingleDerivedPath::Built{
            .drvPath = outputDrvPath,
            .output = std::string{outputName},
        });
    if (outputNames.size() > 1)
        try {
            experimentalFeatureSettings.require(
                Xp::DynamicDerivations, "nested output selection is a dynamic derivations feature");
        } catch (Error & e) {
            e.addTrace(
                state.positions[pos],
                "while evaluating the outputs of string context '%s'",
                state.store->printStorePath(drvPath));
            throw;
        }
    return {
        .drvPath = outputDrvPath,
        .output = std::string{outputNames.back()},
    };
}

static void prim_appendContext(EvalState & state, CallSite callSite, Value * const * args, Value & v)
{
    NixStringContext context;
    auto orig = state.forceString(
        *args[0], context, noPos, "while evaluating the first argument passed to builtins.appendContext");

    state.forceAttrs(*args[1], noPos, "while evaluating the second argument passed to builtins.appendContext");

    auto sPath = state.symbols.create("path");
    auto sAllOutputs = state.symbols.create("allOutputs");
    for (auto & i : *args[1]->attrs()) {
        const auto & name = state.symbols[i.name];
        if (!state.store->isStorePath(name))
            state.error<EvalError>("context key '%s' is not a store path", name).atPos(i.pos).debugThrow();
        auto namePath = state.store->parseStorePath(name);
        if (!settings.readOnlyMode)
            state.store->getBuilder()->ensurePath(namePath);
        state.forceAttrs(*i.value, i.pos, "while evaluating the value of a string context");

        if (auto attr = i.value->attrs()->get(sPath)) {
            if (state.forceBool(*attr->value, attr->pos, "while evaluating the `path` attribute of a string context"))
                context.emplace(
                    NixStringContextElem::Opaque{
                        .path = namePath,
                    });
        }

        if (auto attr = i.value->attrs()->get(sAllOutputs)) {
            if (state.forceBool(
                    *attr->value, attr->pos, "while evaluating the `allOutputs` attribute of a string context")) {
                if (!isDerivation(name)) {
                    state
                        .error<EvalError>(
                            "tried to add all-outputs context of %s, which is not a derivation, to a string", name)
                        .atPos(i.pos)
                        .debugThrow();
                }
                context.emplace(
                    NixStringContextElem::DrvDeep{
                        .drvPath = namePath,
                    });
            }
        }

        if (auto attr = i.value->attrs()->get(state.s.outputs)) {
            state.forceList(*attr->value, attr->pos, "while evaluating the `outputs` attribute of a string context");
            if (attr->value->listSize() && !isDerivation(name)) {
                state
                    .error<EvalError>(
                        "tried to add derivation output context of %s, which is not a derivation, to a string", name)
                    .atPos(i.pos)
                    .debugThrow();
            }
            for (auto [index, elem] : enumerate(attr->value->listView()))
                context.emplace(parseOutputContextElem(state, namePath, index, *elem, attr->pos));
        }
    }

    v.mkString(orig, context, state.mem);
}

static RegisterPrimOp primop_appendContext(
    {.name = "__appendContext",
     .args = {"s", "context"},
     .doc = R"(
      Return *s* with the [string context](@docroot@/language/string-context.md) elements described by *context* added to its own.

      *context* must be a string context in its [Nix language representation](@docroot@/language/string-context.md#nix-language-representation), as [`builtins.getContext`](#builtins-getContext) returns.
      A list of one output name is the same as that name.
      A nested output requires the [`dynamic-derivations`](@docroot@/development/experimental-features.md#xp-feature-dynamic-derivations) experimental feature.

      Unless Nix runs in read-only mode, the store paths must exist in the store or be substitutable.
    )",
     .impl = prim_appendContext});

} // namespace nix

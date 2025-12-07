# Evaluation

Evaluation is the process of turning a Nix expression into a [Nix value](types.md).

This happens by a number of rules, such as:
- Constructing values from literals. 
  For example the number literal `1` is turned into the number value `1`.
- Applying operators
  For example the addition operator `+` is applied to two number values to produce a new number value.
- Applying built-in functions
  For example the expression `builtins.isInt 1` is evaluated to `true`.
- Applying user-defined functions
  For example the expression `(x: x + 1) 10` can[*](#laziness) be thought of rewriting `x` in the function body to the argument, `10 + 1`, which is then evaluated to `11`.

These rules are applied as needed, driven by the specific use of the expression. For example, this can occur in the Nix command line interface or interactively with the [repl (read-eval-print loop)](@docroot@/command-ref/new-cli/nix3-repl.md), which is a useful tool when learning about evaluation.

# Details

## Values {#values}

Nix values can be thought of as a subset of Nix expressions.
For example, the expression `1 + 2` is not a value, because it can be reduced to `3`. The expression `3` is a value, because it cannot be reduced any further.

Evaluation normally happens by applying rules to the "head" of the expression, which is the outermost part of the expression. The head of an expression like `[ 1 2 ]` is the list literal (`[ a1 a2 ]`), for `1 + 2` it is the addition operator (`+`), and for `f 1` it is the function application "operator" (` `).

After applying all possible rules to the head until no rules can be applied, the expression is in "weak head normal form" (WHNF). This means that the outermost constructor of the expression is evaluated, but the inner values may or may not be. "Weak" only signifies that the expression may be a function. This is an historical or academic artifact, and Nix has no use for the non-weak "head normal form".

## Laziness and thunks {#laziness}

The Nix language implements _call by need_ (as opposed to _call by value_ or _call by reference_). <!-- No wikipedia link, which would be a huge distraction. --> Call by need is commonly known as laziness in functional programming, as it is a specific implementation of the concept where evaluation is deferred until the result is required, aiming to only evaluate the parts of an expression that are needed to produce the final result.

Furthermore, the result of evaluation is preserved, in values, in `let` bindings, in function _parameters_, which behave a lot like `let` bindings, but with the notable exception of function _calls_. Results of function calls rely on being put into `let` bindings, etc to be reused. <!-- which would be prohibitively expensive and too strict, or we wouldn't have a cache key for the argument -->

When discussing the process of evaluation in lower level terms, we may define values not as a subset of expressions, but separately, where each "value" is either a data constructor, a function or a _thunk_. A thunk is a delayed computation, represented by an expression reference and a "closure" &ndash; the values for the lexical scope around the delayed expression.

As a user of the language, you generally don't have to think about thunks, as they are not part of the language semantics, but you may encounter them in the repl, in the [C API] or in discussions.

## Strictness

Instead of thinking about thunks, it is often more productive to think in terms of _strictness_.
This term is used in functional programming to refer to the opposite of laziness, i.e. not just for something like error propagation. It refers to the need to evaluate certain expressions before evaluation can produce any result.

Statements about strictness usually implicitly refer to weak head normal form.
For example, we can say that the following function is strict in its argument:

```nix
x: isAttrs x || isFunction x
```

The above function must be strict in its argument `x` because determining its type requires evaluating `x` to at least some degree.

The following function is not strict in its argument:

```nix
x: { isOk = isAttrs x || isFunction x; }
```

It is not strict, because it can return the attribute set before evaluating `x`.
The attribute value for `isOk` _is_ strict in `x`.

A function with a _set pattern_ is always strict in its argument, as a consequence of checking the argument's type and/or attribute names:

```nix
let f = { ... }: "ok";
in f (throw "kablam")
=> error: kablam
```

However, a set pattern does not add any strictness beyond WHNF of the attribute set argument.

```nix
let f = orig@{ x, ... }: "ok";
in f { x = throw "error"; y = throw "error"; }
=> "ok"
```

## Evaluation order

The order in which expressions are evaluated is generally unspecified, because it does not affect successful evaluation outcomes.
This allows more freedom for the evaluator to evolve and to evaluate efficiently.

Data dependencies naturally impose some ordering constraints: a value cannot be used before it is computed.
Beyond these constraints, the evaluator is free to choose any order.

The order in which side effects such as [`builtins.trace`](@docroot@/language/builtins.md#builtins-trace) output occurs is not defined, but may be expected to follow data dependencies. <!-- we may want to be more specific about this. -->

In a lazy language, evaluation order is often opposite to expectations from strict languages.
For example, in `let wrap = x: { wrapped = x; }; in wrap (1 + 2)`, the function body produces a result (`{ wrapped = ...; }`) *before* evaluating `x`.

## Infinite recursion and stack overflow

During evaluation, two types of errors can occur when expressions reference themselves or call functions too deeply:

### Infinite recursion

This error occurs when a value depends on itself through a cycle, making it impossible to compute.

```nix
let x = x; in x
=> error: infinite recursion encountered
```

Infinite recursion happens at the value level when evaluating an expression requires evaluating the same expression again.

Despite the name, infinite recursion is cheap to compute and does not involve a stack overflow.
The cycle is finite and fairly easy to detect.

### Stack overflow

This error occurs when the call depth exceeds the maximum allowed limit.

```nix
let f = x: f (x + 1);
in f 0
=> error: stack overflow; max-call-depth exceeded
```

Stack overflow happens when too many function calls are nested without returning.
The maximum call depth is controlled by the [`max-call-depth` setting](@docroot@/command-ref/conf-file.md#conf-max-call-depth).

[C API]: @docroot@/c-api.md

# We want the rules about memoization of let bindings to hold even when an
# exception occurs.
#
# This is slightly counterintuitive, because we might think that we'd have better
# luck next time if we didn't memoize the result of the let binding.
# However this contradicts the design goal of having a declarative language and
# therefore idempotent evaluation.
#
# So instead of maintaining doubt about this situation, we choose to lean into
# the language semantics and exploit the idempotency. This improves the
# performance asymptotically and significantly in scenarios where we may
# evaluate more than once. These scenarios include:
#
#  - `tryEval` has been used, such as in these test cases, or
#  - traversal of multiple attributes, such as in `nix-build` with `recurseForDerivations`,
#  - applications that handle missing information by throwing an exception to be handled by a convergent main loop,
#  - re-parsing a file that used to have a syntax error in nix repl, without using `:r`.
#
# The latter is particularly useful for manual testing with real-world expressions:
#
#   1. Introduce a syntax error in a file that's imported somewhere deep/interesting
#   2. `nix repl`
#   3. Assign the expression to a variable, e.g. `x = haskellPackages.foo`
#   4. Evaluate partially and observe the syntax error by typing `x`
#   5. Fix the syntax error
#   6. Type `x` again and observe that the expression is now evaluated correctly,
#      and check that the result is the same as before the syntax error was introduced.

app_and_let_are_memoized() {
    expr='
      let
        f = { msg }:
            let expensive = builtins.trace "the ${"expensive"} computation" true;
            in builtins.seq expensive (throw (msg + exclaim));
        mem = f { msg = "nope"; };
        exclaim = "!";
      in builtins.seq (builtins.tryEval mem) mem
    '

    # The expensive computation is only performed once, despite evaluating the
    # `mem` thunk twice.
    n="$(expectStderr 1 nix-instantiate --eval -E "$expr" | spy | grep 'trace: the expensive computation' | wc -l)"
    [[ "$n" == "1" ]]

    # The result is as expected, confirming that the saved thunk is correct.
    expectStderr 1 nix-instantiate --eval -E "$expr" | spy | grep 'error: nope!'
}
app_and_let_are_memoized

app_and_let_are_memoized_eta_expanded_subexpr_call() {
    expr='
      let
        f = { msg }:
            let expensive = builtins.trace "the ${"expensive"} computation" true;
                second = a: builtins.trace "the second expensive computation" a;
            in builtins.seq expensive (second throw (msg + exclaim));
        mem = f { msg = "nope"; };
        exclaim = "!";
      in builtins.seq (builtins.tryEval mem) mem
    '

    n="$(expectStderr 1 nix-instantiate --eval -E "$expr" | grep 'trace: the second expensive computation' | wc -l)"
    [[ "$n" == "1" ]]
}
app_and_let_are_memoized_eta_expanded_subexpr_call

app_and_let_are_memoized_subexpr_call() {
    expr='
      let
        f = { msg }:
            let expensive = builtins.trace "the ${"expensive"} computation" true;
            in builtins.seq expensive (builtins.trace "the second expensive computation" throw (msg + exclaim));
        mem = f { msg = "nope"; };
        exclaim = "!";
      in builtins.seq (builtins.tryEval mem) mem
    '

    n="$(expectStderr 1 nix-instantiate --eval -E "$expr" | grep 'trace: the second expensive computation' | wc -l)"
    [[ "$n" == "1" ]]
}
app_and_let_are_memoized_subexpr_call

app_and_let_are_memoized_let_partial_subexpr_call() {
    expr='
      let
        f = { msg }:
            let expensive = builtins.trace "the ${"expensive"} computation" true;
                second = builtins.trace "the second expensive computation";
            in builtins.seq expensive (second throw (msg + exclaim));
        mem = f { msg = "nope"; };
        exclaim = "!";
      in builtins.seq (builtins.tryEval mem) mem
    '

    n="$(expectStderr 1 nix-instantiate --eval -E "$expr" | grep 'trace: the second expensive computation' | wc -l)"
    [[ "$n" == "1" ]]
}
app_and_let_are_memoized_let_partial_subexpr_call

app_and_let_are_memoized_functor() {
    expr='
      let
        trivialFunctor = f: { __functor = _: f; };
        f = { msg }:
            let expensive = builtins.trace "the ${"expensive"} computation" true;
            in builtins.seq expensive (throw (msg + exclaim));
        mem = trivialFunctor f { msg = "nope"; };
        exclaim = "!";
      in builtins.seq (builtins.tryEval mem) mem
    '

    # The expensive computation is only performed once, despite evaluating the
    # `mem` thunk twice.
    n="$(expectStderr 1 nix-instantiate --eval -E "$expr" | spy | grep 'trace: the expensive computation' | wc -l)"
    [[ "$n" == "1" ]]

    # The result is as expected, confirming that the saved thunk is correct.
    expectStderr 1 nix-instantiate --eval -E "$expr" | spy | grep 'error: nope!'
}
app_and_let_are_memoized_functor

app_and_let_are_memoized_primop_app() {
    expr='
      let
        trivialFunctor = f: { __functor = _: f; };
        f = builtins.seq (builtins.trace "the ${"expensive"} computation" true);
        exclaim = "!";
      in builtins.seq (builtins.tryEval (f (throw "app_and_let_are_memoized_primop_app"))) f "result ${"is ok"}"
    '

    # The expensive computation is only performed once, despite evaluating the
    # `mem` thunk twice.
    n="$(nix-instantiate --eval -E "$expr" 2>&1 | spy | grep 'trace: the expensive computation' | wc -l)"
    [[ "$n" == "1" ]]

    # The result is as expected, confirming that the saved thunk is correct.
    nix-instantiate --eval -E "$expr" 2>&1 | spy | grep '"result is ok"'
}
app_and_let_are_memoized_primop_app

app_and_let_are_memoized_primop_app_complete() {
    expr='
      let
        f = builtins.seq (builtins.trace "the ${"expensive"} computation" true) (throw "not ok");
      in
        builtins.seq (builtins.tryEval f)
        builtins.seq (builtins.tryEval f)
        "result ${"is ok"}"
    '

    # The expensive computation is only performed once, despite evaluating the
    # `mem` thunk twice.
    n="$(nix-instantiate --eval -E "$expr" 2>&1 | spy | grep 'trace: the expensive computation' | wc -l)"
    [[ "$n" == "1" ]]

    # The result is as expected, confirming that the saved thunk is correct.
    nix-instantiate --eval -E "$expr" 2>&1 | spy | grep '"result is ok"'
}
app_and_let_are_memoized_primop_app_complete

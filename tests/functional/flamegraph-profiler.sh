#!/usr/bin/env bash

source common.sh

set +x

expect_trace() {
    expr="$1"
    expect="$2"
    actual=$(
        nix-instantiate \
            --eval-profiler flamegraph \
            --eval-profiler-frequency 0 \
            --eval-profile-file /dev/stdout \
            --expr "$expr" |
            grep "«string»" || true
    )

    echo -n "Tracing expression '$expr'"
    msg=$(
        diff -swB \
            <(echo "$expect") \
            <(echo "$actual")
    ) && result=0 || result=$?
    if [ "$result" -eq 0 ]; then
        echo " ok."
    else
        echo " failed. difference:"
        echo "$msg"
        return "$result"
    fi
}

# lambda
expect_trace 'let f = arg: arg; in f 1' "
«string»:1:22:f 1
"

# unnamed lambda
expect_trace '(arg: arg) 1' "
«string»:1:1 1
"

# primop
expect_trace 'builtins.head [0 1]' "
«string»:1:1:primop head 1
"

# primop application
expect_trace 'let a = builtins.all (let f = x: x; in f); in a [1]' "
«string»:1:9:primop all 1
«string»:1:47:primop all 1
«string»:1:47:primop all;«string»:1:31:f 1
"

# functor
expect_trace '{__functor = x: arg: arg;} 1' "
«string»:1:1:functor 1
«string»:1:1:functor;«string»:1:2 1
"

# failure inside a tryEval
expect_trace 'builtins.tryEval (throw "example")' "
«string»:1:1:primop tryEval 1
«string»:1:1:primop tryEval;«string»:1:19:primop throw 1
"

# Missing argument to a formal function
expect_trace 'let f = ({ x }: x); in f { }' "
«string»:1:24:f 1
"

# Too many arguments to a formal function
expect_trace 'let f = ({ x }: x); in f { x = "x"; y = "y"; }' "
«string»:1:24:f 1
"

# Not enough arguments to a lambda
expect_trace 'let f = (x: y: x + y); in f 1' "
«string»:1:27:f 1
"

# Too many arguments to a lambda
expect_trace 'let f2 = (x: x); in f2 1 2' "
«string»:1:21:f2 1
"

# Not a function
expect_trace '1 2' "
«string»:1:1 1
"

# Derivation
expect_trace 'builtins.derivationStrict { name = "somepackage"; }' "
«string»:1:1:primop derivationStrict:somepackage 1
"

# Derivation without name attr
expect_trace 'builtins.derivationStrict { }' "
«string»:1:1:primop derivationStrict 1
"

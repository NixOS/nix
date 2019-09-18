source common.sh

set +x

expect_trace() {
    expr="$1"
    expect="$2"
    actual=$(
        nix-instantiate \
            --trace-function-calls \
            --expr "$expr" 2>&1 \
            | grep "function-trace" \
            | sed -e 's/ [0-9]*$//'
    );

    echo -n "Tracing expression '$expr'"
    set +e
    msg=$(diff -swB \
               <(echo "$expect") \
               <(echo "$actual")
    );
    result=$?
    set -e
    if [ $result -eq 0 ]; then
        echo " ok."
    else
        echo " failed. difference:"
        echo "$msg"
        return $result
    fi
}

# failure inside a tryEval
expect_trace 'builtins.tryEval (throw "example")' "
function-trace entered undefined position at
function-trace exited undefined position at
function-trace entered (string):1:1 at
function-trace entered (string):1:19 at
function-trace exited (string):1:19 at
function-trace exited (string):1:1 at
"

# Missing argument to a formal function
expect_trace '({ x }: x) { }' "
function-trace entered undefined position at
function-trace exited undefined position at
function-trace entered (string):1:1 at
function-trace exited (string):1:1 at
"

# Too many arguments to a formal function
expect_trace '({ x }: x) { x = "x"; y = "y"; }' "
function-trace entered undefined position at
function-trace exited undefined position at
function-trace entered (string):1:1 at
function-trace exited (string):1:1 at
"

# Not enough arguments to a lambda
expect_trace '(x: y: x + y) 1' "
function-trace entered undefined position at
function-trace exited undefined position at
function-trace entered (string):1:1 at
function-trace exited (string):1:1 at
"

# Too many arguments to a lambda
expect_trace '(x: x) 1 2' "
function-trace entered undefined position at
function-trace exited undefined position at
function-trace entered (string):1:1 at
function-trace exited (string):1:1 at
function-trace entered (string):1:1 at
function-trace exited (string):1:1 at
"

# Not a function
expect_trace '1 2' "
function-trace entered undefined position at
function-trace exited undefined position at
function-trace entered (string):1:1 at
function-trace exited (string):1:1 at
"

set -e

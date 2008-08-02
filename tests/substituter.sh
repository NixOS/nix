#! /bin/sh -e
echo substituter args: $* >&2

if test $1 = "--query"; then
    while read cmd; do
        echo FOO $cmd >&2
        if test "$cmd" = "have"; then
            read path
            if grep -q "$path" $TEST_ROOT/sub-paths; then
                echo 1
            else
                echo 0
            fi
        elif test "$cmd" = "info"; then
            read path
            echo 1
            echo "" # deriver
            echo 0 # nr of refs
        else
            echo "bad command $cmd"
            exit 1
        fi
    done
elif test $1 = "--substitute"; then
    mkdir $2
    echo "Hallo Wereld" > $2/hello
else
    echo "unknown substituter operation"
    exit 1
fi

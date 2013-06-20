#! /bin/sh -e
echo
echo substituter2 args: $* >&2

if test $1 = "--query"; then
    while read cmd args; do
        if test "$cmd" = have; then
            for path in $args; do
                if grep -q "$path" $TEST_ROOT/sub-paths; then
                    echo $path
                fi
            done
            echo
        elif test "$cmd" = info; then
            for path in $args; do
                echo $path
                echo "" # deriver
                echo 0 # nr of refs
                echo 0 # download size
                echo 0 # nar size
            done
            echo
        else
            echo "bad command $cmd"
            exit 1
        fi
    done
elif test $1 = "--substitute"; then
    exit 1
else
    echo "unknown substituter operation"
    exit 1
fi

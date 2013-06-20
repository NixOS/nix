#! /bin/sh -e
echo
echo substituter args: $* >&2

if test $1 = "--query"; then
    while read cmd args; do
        echo "CMD = $cmd, ARGS = $args" >&2
        if test "$cmd" = "have"; then
            for path in $args; do 
                read path
                if grep -q "$path" $TEST_ROOT/sub-paths; then
                    echo $path
                fi
            done
            echo
        elif test "$cmd" = "info"; then
            for path in $args; do
                echo $path
                echo "" # deriver
                echo 0 # nr of refs
                echo $((1 * 1024 * 1024)) # download size
                echo $((2 * 1024 * 1024)) # nar size
            done
            echo
        else
            echo "bad command $cmd"
            exit 1
        fi
    done
elif test $1 = "--substitute"; then
    mkdir $2
    echo "Hallo Wereld" > $2/hello
    echo # no expected hash
else
    echo "unknown substituter operation"
    exit 1
fi

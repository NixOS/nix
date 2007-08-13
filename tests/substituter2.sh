#! /bin/sh -e
echo substituter2 args: $* >&2

if test $1 = "--query-paths"; then
    cat $TEST_ROOT/sub-paths
elif test $1 = "--query-info"; then
    shift
    for i in in $@; do
        echo $i
        echo "" # deriver
        echo 0 # nr of refs
    done
elif test $1 = "--substitute"; then
    exit 1
else
    echo "unknown substituter operation"
    exit 1
fi

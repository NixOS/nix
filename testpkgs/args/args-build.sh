#! /bin/sh

IFS=

echo "printing list of args"

for i in $@; do
    echo "arg: $i"
done

touch $out
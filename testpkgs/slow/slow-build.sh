#! /bin/sh

echo "builder started..."

mkdir $out

for i in $(seq 1 30); do
    echo $i
    sleep 1
done

echo "done" > $out/bla

echo "builder finished"

#! /bin/sh

echo "builder started..."

for i in $(seq 1 30); do
    echo $i
    sleep 1
done

mkdir $out

echo "done" > $out/bla

echo "builder finished"

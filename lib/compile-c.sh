. $stdenv/setup
mkdir $out
gcc -Wall -c $main -o $out/$(basename $main).o

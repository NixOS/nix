. $stdenv/setup

objs=
for i in "$objects"; do
    obj=$i/*.o
    objs="$objs $obj"
done

mkdir $out
gcc -o $out/program $objs

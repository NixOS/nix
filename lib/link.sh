. $stdenv/setup

objs=
for i in "$objects"; do
    obj=$(echo $i/*.o)
    objs="$objs $obj"
done

echo "linking object files into \`$programName'..."

mkdir $out
gcc -o $out/$programName $objs

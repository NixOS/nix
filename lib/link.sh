. $stdenv/setup

objs=
for i in $objects; do
    obj=$(echo $i/*.o)
    objs="$objs $obj"
done

libs=
for i in $libraries; do
    lib=$(echo $i/*.a)
    name=$(echo $(basename $lib) | sed -e 's/^lib//' -e 's/.a$//')
    libs="$libs -L$(dirname $lib) -l$name" 
done

echo "linking object files into \`$programName'..."

mkdir $out
gcc -o $out/$programName $objs $libs

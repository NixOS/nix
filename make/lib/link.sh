. $stdenv/setup

shopt -s nullglob

objs=
for i in $objects; do
    obj=$(echo $i/*.o)
    objs="$objs $obj"
done

libs=
for i in $libraries; do
    lib=$(echo $i/*.a; echo $i/*.so)
    name=$(echo $(basename $lib) | sed -e 's/^lib//' -e 's/.a$//' -e 's/.so$//')
    libs="$libs -L$(dirname $lib) -l$name" 
done

echo "linking object files into \`$programName'..."

mkdir $out
gcc -o $out/$programName $objs $libs

. $stdenv/setup

objs=
for i in $objects; do
    obj=$(echo $i/*.o)
    objs="$objs $obj"
done

echo "archiving object files into library \`$libraryName'..."

outPath=$out/lib${libraryName}.a

mkdir $out
ar crs $outPath $objs
ranlib $outPath

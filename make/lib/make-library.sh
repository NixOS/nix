. $stdenv/setup

objs=
for i in $objects; do
    obj=$(echo $i/*.o)
    objs="$objs $obj"
done

echo "archiving object files into library \`$libraryName'..."

ensureDir $out

if test -z "$sharedLib"; then

    outPath=$out/lib${libraryName}.a

    ar crs $outPath $objs
    ranlib $outPath

else

    outPath=$out/lib${libraryName}.so

    gcc -shared -o $outPath $objs

fi    

    

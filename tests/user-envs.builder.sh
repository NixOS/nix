mkdir $out
mkdir $out/bin
echo "#! $shell" > $out/bin/$progName
echo "echo $name" >> $out/bin/$progName
chmod +x $out/bin/$progName


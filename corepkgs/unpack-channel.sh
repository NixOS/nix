mkdir $out
mkdir $out/tmp
cd $out/tmp

inputs=($inputs)
for ((n = 0; n < ${#inputs[*]}; n += 2)); do
    channelName=${inputs[n]}
    channelTarball=${inputs[n+1]}
    
    echo "unpacking channel $channelName"
    
    $bzip2 -d < $channelTarball | $tar xf -

    if test -e */channel-name; then
        channelName="$(cat */channel-name)"
    fi

    nr=1
    attrName=$(echo $channelName | $tr -- '- ' '__')
    dirName=$attrName
    while test -e ../$dirName; do
        nr=$((nr+1))
        dirName=$attrName-$nr
    done

    mv * ../$dirName # !!! hacky
done

cd ..
rmdir tmp

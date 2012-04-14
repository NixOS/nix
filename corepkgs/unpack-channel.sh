mkdir $out
cd $out
$bzip2 -d < $src | $tar xf -
mv * $out/$channelName

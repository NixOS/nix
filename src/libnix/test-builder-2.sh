#! /bin/sh

echo "builder 2"

/bin/mkdir $out || exit 1
cd $out || exit 1
echo "Hallo Wereld" > bla
echo $builder >> bla
echo $out >> bla

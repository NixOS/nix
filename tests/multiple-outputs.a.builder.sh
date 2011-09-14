mkdir $first
mkdir $second
test -z $all

echo "second" > $first/file
echo "first" > $second/file

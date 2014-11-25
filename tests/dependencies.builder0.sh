[ "${input1: -2}" = /. ]
[ "${input2: -2}" = /. ]

mkdir $out
echo $(cat $input1/foo)$(cat $input2/bar) > $out/foobar

ln -s $input2 $out/input-2

# Self-reference.
ln -s $out $out/self

# Executable.
echo program > $out/program
chmod +x $out/program

echo FOO

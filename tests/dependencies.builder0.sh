[ "${input1: -2}" = /. ]
[ "${input2: -2}" = /. ]

mkdir $out
echo $(cat $input1/foo)$(cat $input2/bar) > $out/foobar


if [[ "$(uname)" =~ ^MINGW|^MSYS ]]; then
    nix ln $input2 $out/input-2

    # Self-reference.
    nix ln $out $out/self
else
    ln -s $input2 $out/input-2

    # Self-reference.
    ln -s $out $out/self
fi

echo "$PATH" > $out/path

# Executable.
echo program > $out/program

if [[ "$(uname)" =~ ^MINGW|^MSYS ]]; then
    echo -n ""  # chmod does not work
else
    chmod +x $out/program
fi

echo FOO

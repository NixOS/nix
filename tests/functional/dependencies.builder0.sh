# shellcheck shell=bash
# shellcheck disable=SC2154
[ "${input1: -2}" = /. ]
# shellcheck disable=SC2154
[ "${input2: -2}" = /. ]

# shellcheck disable=SC2154
mkdir "$out"
echo "$(cat "$input1"/foo)$(cat "$input2"/bar)" > "$out"/foobar

ln -s "$input2" "$out"/reference-to-input-2

# Self-reference.
ln -s "$out" "$out"/self

# Executable.
echo program > "$out"/program
chmod +x "$out"/program

echo '1 + 2' > "$out"/foo.nix

echo FOO

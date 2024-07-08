source common.sh

[[ $(echo yay | nix eval --raw --impure --expr 'builtins.readFile "/dev/stdin"') = yay ]]

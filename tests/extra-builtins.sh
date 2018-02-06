source common.sh

set -o pipefail

e=$(nix eval --option extra-builtins-file extra-builtins/extra-builtins.nix '(builtins.extraBuiltins.true)')

[ "$e"x = "true"x ];

e=$(nix eval --option restrict-eval true --option allow-unsafe-native-code-during-evaluation true --option extra-builtins-file extra-builtins/extra-builtins.nix '(builtins.extraBuiltins.true)')

[ "$e"x = "true"x ];

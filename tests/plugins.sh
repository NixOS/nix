source common.sh

set -o pipefail

res=$(nix eval --expr builtins.anotherNull --option setting-set true --option plugin-files $PWD/plugins/libplugintest*)

[ "$res"x = "nullx" ]

# test a custom command
res=$(echo -e ':greet Hi\n:mySecondGreet Hello' | nix repl --option plugin-files $PWD/plugins/libplugintest* \
      | grep -Ev '(^$)|(^Welcome to Nix.*$)')

[ "$res"x = $'Hi greet!\nHello mySecondGreet!x' ]

# test help
res=$(echo ':?' | nix repl --option plugin-files $PWD/plugins/libplugintest* \
      | grep 'aaaa')
echo "$res" >&2

[ "$res"x = '  :greet ph     aaaaax' ]
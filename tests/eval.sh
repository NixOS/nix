source common.sh

clearStore

nix-instantiate --eval -E 'assert 1 + 2 == 3; true'
[[ $(nix-instantiate -A int --eval "./eval.nix") == 123 ]]
[[ $(nix-instantiate -A str --eval "./eval.nix") == '"foo"' ]]
[[ $(nix-instantiate -A attr --eval "./eval.nix") == '{ foo = "bar"; }' ]]
[[ $(nix-instantiate -A attr --eval --json "./eval.nix") == '{"foo":"bar"}' ]]
[[ $(nix-instantiate -A int --eval - < "./eval.nix") == 123 ]]

nix eval '(assert 1 + 2 == 3; true)'
[[ $(nix eval int -f "./eval.nix") == 123 ]]
[[ $(nix eval str -f "./eval.nix") == '"foo"' ]]
[[ $(nix eval str --raw -f "./eval.nix") == 'foo' ]]
[[ $(nix eval attr -f "./eval.nix") == '{ foo = "bar"; }' ]]
[[ $(nix eval attr --json -f "./eval.nix") == '{"foo":"bar"}' ]]
[[ $(nix eval int -f - < "./eval.nix") == 123 ]]

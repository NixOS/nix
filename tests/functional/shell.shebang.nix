#! @ENV_PROG@ nix-shell
#! nix-shell -I nixpkgs=shell.nix --no-substitute
#! nix-shell    --argstr s1 'foo "bar" \baz'"'"'qux'  --argstr s2 "foo 'bar' \"\baz" --argstr s3 \foo\ bar\'baz --argstr s4 ''  
#! nix-shell shell.shebang.nix --command true
{ s1, s2, s3, s4 }:
assert s1 == ''foo "bar" \baz'qux'';
assert s2 == "foo 'bar' \"baz";
assert s3 == "foo bar'baz";
assert s4 == "";
(import <nixpkgs> {}).runCommand "nix-shell" {} ""

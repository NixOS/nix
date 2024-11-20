#!/usr/bin/env bash

source ./common.sh

TODO_NixOS

requireGit

flake1Dir=$TEST_ROOT/flake1
nonFlakeDir=$TEST_ROOT/nonFlake

createGitRepo "$flake1Dir" ""
createSimpleGitFlake "$flake1Dir"
nix registry add --registry "$registry" flake1 "git+file://$flake1Dir"

mkdir -p "$nonFlakeDir"

cat > "$nonFlakeDir/shebang.sh" <<EOF
#! $(type -P env) nix
#! nix --offline shell
#! nix flake1#fooScript
#! nix --no-write-lock-file --command bash
set -ex
foo
echo "\$@"
EOF
chmod +x "$nonFlakeDir/shebang.sh"

#git -C "$nonFlakeDir" add shebang.sh

# this also tests a fairly trivial double backtick quoted string, ``--command``
cat > $nonFlakeDir/shebang-comments.sh <<EOF
#! $(type -P env) nix
# some comments
# some comments
# some comments
#! nix --offline shell
#! nix flake1#fooScript
#! nix --no-write-lock-file ``--command`` bash
foo
EOF
chmod +x $nonFlakeDir/shebang-comments.sh

cat > $nonFlakeDir/shebang-different-comments.sh <<EOF
#! $(type -P env) nix
# some comments
// some comments
/* some comments
* some comments
\ some comments
% some comments
@ some comments
-- some comments
(* some comments
#! nix --offline shell
#! nix flake1#fooScript
#! nix --no-write-lock-file --command cat
foo
EOF
chmod +x $nonFlakeDir/shebang-different-comments.sh

cat > $nonFlakeDir/shebang-reject.sh <<EOF
#! $(type -P env) nix
# some comments
# some comments
# some comments
#! nix --offline shell *
#! nix flake1#fooScript
#! nix --no-write-lock-file --command bash
foo
EOF
chmod +x $nonFlakeDir/shebang-reject.sh

cat > $nonFlakeDir/shebang-inline-expr.sh <<EOF
#! $(type -P env) nix
EOF
cat >> $nonFlakeDir/shebang-inline-expr.sh <<"EOF"
#! nix --offline shell
#! nix --impure --expr ``
#! nix let flake = (builtins.getFlake (toString ../flake1)).packages;
#! nix     fooScript = flake.${builtins.currentSystem}.fooScript;
#! nix     /* just a comment !@#$%^&*()__+ # */
#! nix  in fooScript
#! nix ``
#! nix --no-write-lock-file --command bash
set -ex
foo
echo "$@"
EOF
chmod +x $nonFlakeDir/shebang-inline-expr.sh

cat > $nonFlakeDir/fooScript.nix <<"EOF"
let flake = (builtins.getFlake (toString ../flake1)).packages;
    fooScript = flake.${builtins.currentSystem}.fooScript;
 in fooScript
EOF

cat > $nonFlakeDir/shebang-file.sh <<EOF
#! $(type -P env) nix
EOF
cat >> $nonFlakeDir/shebang-file.sh <<"EOF"
#! nix --offline shell
#! nix --impure --file ./fooScript.nix
#! nix --no-write-lock-file --command bash
set -ex
foo
echo "$@"
EOF
chmod +x $nonFlakeDir/shebang-file.sh

[[ $($nonFlakeDir/shebang.sh) = "foo" ]]
[[ $($nonFlakeDir/shebang.sh "bar") = "foo"$'\n'"bar" ]]
[[ $($nonFlakeDir/shebang-comments.sh ) = "foo" ]]
[[ "$($nonFlakeDir/shebang-different-comments.sh)" = "$(cat $nonFlakeDir/shebang-different-comments.sh)" ]]
[[ $($nonFlakeDir/shebang-inline-expr.sh baz) = "foo"$'\n'"baz" ]]
[[ $($nonFlakeDir/shebang-file.sh baz) = "foo"$'\n'"baz" ]]
expect 1 $nonFlakeDir/shebang-reject.sh 2>&1 | grepQuiet -F 'error: unsupported unquoted character in nix shebang: *. Use double backticks to escape?'

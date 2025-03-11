#!/usr/bin/env bash

source ./common.sh

requireGit

rootFlake="$TEST_ROOT/flake1"
subflake0="$rootFlake/sub0"
subflake1="$rootFlake/sub1"
subflake2="$rootFlake/sub2"

rm -rf "$rootFlake"
mkdir -p "$rootFlake" "$subflake0" "$subflake1" "$subflake2"

cat > "$rootFlake/flake.nix" <<EOF
{
  inputs.sub0.url = ./sub0;
  outputs = { self, sub0 }: {
    x = 2;
    y = self.x * sub0.x;
  };
}
EOF

cat > "$subflake0/flake.nix" <<EOF
{
  outputs = { self }: {
    x = 7;
  };
}
EOF

[[ $(nix eval "$rootFlake#x") = 2 ]]
[[ $(nix eval "$rootFlake#y") = 14 ]]

cat > "$subflake1/flake.nix" <<EOF
{
  inputs.root.url = "../";
  outputs = { self, root }: {
    x = 3;
    y = self.x * root.x;
  };
}
EOF

[[ $(nix eval "$rootFlake?dir=sub1#y") = 6 ]]

initGitRepo "$rootFlake"
git -C "$rootFlake" add flake.nix sub0/flake.nix sub1/flake.nix

[[ $(nix eval "$subflake1#y") = 6 ]]

cat > "$subflake2/flake.nix" <<EOF
{
  inputs.root.url = ./..;
  inputs.sub1.url = "../sub1";
  outputs = { self, root, sub1 }: {
    x = 5;
    y = self.x * sub1.x;
  };
}
EOF

git -C "$rootFlake" add flake.nix sub2/flake.nix

[[ $(nix eval "$subflake2#y") = 15 ]]

# Make sure that this still works after commiting the lock file.
git -C "$rootFlake" add sub2/flake.lock
[[ $(nix eval "$subflake2#y") = 15 ]]

# Make sure there are no content locks for relative path flakes.
(! grep "$TEST_ROOT" "$subflake2/flake.lock")
if ! isTestOnNixOS; then
    (! grep "$NIX_STORE_DIR" "$subflake2/flake.lock")
fi
(! grep narHash "$subflake2/flake.lock")

# Test `nix flake archive` with relative path flakes.
git -C "$rootFlake" add flake.lock
git -C "$rootFlake" commit -a -m Foo

json=$(nix flake archive --json "$rootFlake" --to "$TEST_ROOT/store2")
[[ $(echo "$json" | jq .inputs.sub0.inputs) = {} ]]
[[ -n $(echo "$json" | jq .path) ]]

nix flake prefetch --out-link "$TEST_ROOT/result" "$rootFlake"
outPath=$(readlink "$TEST_ROOT/result")

[ -e "$TEST_ROOT/store2/nix/store/$(basename "$outPath")" ]

# Test circular relative path flakes. FIXME: doesn't work at the moment.
if false; then

cat > "$rootFlake/flake.nix" <<EOF
{
  inputs.sub1.url = "./sub1";
  inputs.sub2.url = "./sub1";
  outputs = { self, sub1, sub2 }: {
    x = 2;
    y = self.x * sub1.x * sub2.x;
    z = sub1.y * sub2.y;
  };
}
EOF

[[ $(nix eval "$rootFlake#x") = 30 ]]
[[ $(nix eval "$rootFlake#z") = 90 ]]

fi

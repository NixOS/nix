#!/usr/bin/env bash

source common.sh

requireGit
[[ $(type -p ssh-keygen) ]] || skipTest "ssh-keygen not installed" # require ssh-keygen

enableFeatures "verified-fetches"

clearStoreIfPossible

repo="$TEST_ROOT/git"

# generate signing keys
keysDir=$TEST_ROOT/.ssh
mkdir -p "$keysDir"
ssh-keygen -f "$keysDir/testkey1" -t ed25519 -P "" -C "test key 1"
key1File="$keysDir/testkey1.pub"
publicKey1=$(awk '{print $2}' "$key1File")
ssh-keygen -f "$keysDir/testkey2" -t rsa -P "" -C "test key 2"
key2File="$keysDir/testkey2.pub"
publicKey2=$(awk '{print $2}' "$key2File")

git init "$repo"
git -C "$repo" config user.email "foobar@example.com"
git -C "$repo" config user.name "Foobar"
git -C "$repo" config gpg.format ssh

echo 'hello' > "$repo"/text
git -C "$repo" add text
git -C "$repo" -c "user.signingkey=$key1File" commit -S -m 'initial commit'

out=$(nix eval --impure --raw --expr "builtins.fetchGit { url = \"file://$repo\"; keytype = \"ssh-rsa\"; publicKey = \"$publicKey2\"; }" 2>&1) || status=$?
[[ $status == 1 ]]
[[ $out == *'No principal matched.'* ]]
[[ $(nix eval --impure --raw --expr "builtins.readFile (builtins.fetchGit { url = \"file://$repo\"; publicKey = \"$publicKey1\"; } + \"/text\")") = 'hello' ]]

echo 'hello world' > "$repo"/text

# Verification on a dirty repo should fail.
out=$(nix eval --impure --raw --expr "builtins.fetchGit { url = \"file://$repo\"; keytype = \"ssh-rsa\"; publicKey = \"$publicKey2\"; }" 2>&1) || status=$?
[[ $status == 1 ]]
[[ $out =~ 'dirty' ]]

git -C "$repo" add text
git -C "$repo" -c "user.signingkey=$key2File" commit -S -m 'second commit'

[[ $(nix eval --impure --raw --expr "builtins.readFile (builtins.fetchGit { url = \"file://$repo\"; publicKeys = [{key = \"$publicKey1\";} {type = \"ssh-rsa\"; key = \"$publicKey2\";}]; } + \"/text\")") = 'hello world' ]]

# Flake input test
flakeDir="$TEST_ROOT/flake"
mkdir -p "$flakeDir"
cat > "$flakeDir/flake.nix" <<EOF
{
  inputs.test = {
    type = "git";
    url = "file://$repo";
    flake = false;
    publicKeys = [
      { type = "ssh-rsa"; key = "$publicKey2"; }
    ];
  };

  outputs = { test, ... }: { test = test.outPath; };
}
EOF
nix build --out-link "$flakeDir/result" "$flakeDir#test"
[[ $(cat "$flakeDir/result/text") = 'hello world' ]]

cat > "$flakeDir/flake.nix" <<EOF
{
  inputs.test = {
    type = "git";
    url = "file://$repo";
    flake = false;
    publicKey= "$publicKey1";
  };

  outputs = { test, ... }: { test = test.outPath; };
}
EOF
out=$(nix build "$flakeDir#test" 2>&1) || status=$?

[[ $status == 1 ]]
[[ $out == *'No principal matched.'* ]]

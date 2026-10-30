#!/usr/bin/env bash

source common.sh

# Regression test for #11503.
mkdir -p "$TEST_ROOT/directory"
cat > "$TEST_ROOT/directory/default.nix" <<EOF
  let
    root = ./.;
    filter = path: type:
      let
        rootStr = builtins.toString ./.;
      in
        if builtins.substring 0 (builtins.stringLength rootStr) (builtins.toString path) == rootStr then true
        else builtins.throw "root path\n\${rootStr}\nnot prefix of path\n\${builtins.toString path}";
  in
    builtins.filterSource filter root
EOF

result="$(nix-store --add-fixed --recursive sha256 "$TEST_ROOT/directory")"
nix-instantiate --eval "$result"
nix-instantiate --eval "$result" --store "$TEST_ROOT/2nd-store"
nix-store --add-fixed --recursive sha256 "$TEST_ROOT/directory" --store "$TEST_ROOT/2nd-store"
nix-instantiate --eval "$result" --store "$TEST_ROOT/2nd-store"

# Misc tests.
echo example > "$TEST_ROOT"/example.txt
mkdir -p "$TEST_ROOT/x"

export NIX_STORE_DIR=/nix2/store

CORRECT_PATH=$(cd "$TEST_ROOT" && nix-store --store ./x --add example.txt)

[[ $CORRECT_PATH =~ ^/nix2/store/.*-example.txt$ ]]

PATH1=$(cd "$TEST_ROOT" && nix path-info --store ./x "$CORRECT_PATH")
[ "$CORRECT_PATH" == "$PATH1" ]

PATH2=$(nix path-info --store "$TEST_ROOT/x" "$CORRECT_PATH")
[ "$CORRECT_PATH" == "$PATH2" ]

PATH3=$(nix path-info --store "local?root=$TEST_ROOT/x" "$CORRECT_PATH")
[ "$CORRECT_PATH" == "$PATH3" ]

# Test chroot store path with + symbols in it to exercise pct-encoding issues.
cp -r "$TEST_ROOT/x" "$TEST_ROOT/x+chroot"

PATH4=$(nix path-info --store "local://$TEST_ROOT/x+chroot" "$CORRECT_PATH")
[ "$CORRECT_PATH" == "$PATH4" ]

PATH5=$(nix path-info --store "$TEST_ROOT/x+chroot" "$CORRECT_PATH")
[ "$CORRECT_PATH" == "$PATH5" ]

# Params are pct-encoded.
PATH6=$(nix path-info --store "local?root=$TEST_ROOT/x%2Bchroot" "$CORRECT_PATH")
[ "$CORRECT_PATH" == "$PATH6" ]

PATH7=$(nix path-info --store "local://$TEST_ROOT/x%2Bchroot" "$CORRECT_PATH")
[ "$CORRECT_PATH" == "$PATH7" ]
# Path gets decoded.
[[ ! -d "$TEST_ROOT/x%2Bchroot" ]]

# Regression test: `nix-store --register-validity` against a chroot store must
# canonicalise the real (chroot-prefixed) path, not the logical store path
# (which does not exist on the host filesystem).
regPath=$NIX_STORE_DIR/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-reg
touch "$TEST_ROOT/x/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-reg"
[[ ! -e "$regPath" ]]
(echo "$regPath" && echo && echo 0) | nix-store --store "local?root=$TEST_ROOT/x" --register-validity
nix-store --store "$TEST_ROOT/x" --check-validity "$regPath"

# Ensure store info trusted works with local store
nix --store "$TEST_ROOT/x" store info --json | jq -e '.trusted'

# Test building in a chroot store.
if canUseSandbox; then

    flakeDir=$TEST_ROOT/flake
    mkdir -p "$flakeDir"
    nixBin=$(dirname "$(type -p nix)")

    cat > "$flakeDir"/flake.nix <<EOF
{
  outputs = inputs: rec {
    packages.$system.default = import ./simple.nix;
    packages.$system.readOnlyInput =
      let config = import ./config.nix;
      in config.mkDerivation {
        name = "read-only-input";
        builder = ./read-only-input.builder.sh;
        input = builtins.path { path = ./input; name = "input"; };
        inputFile = ./input/file;
        PATH = config.path;
      };
    packages.$system.readOnlyRecursiveInput =
      let config = import ./config.nix;
      in config.mkDerivation {
        name = "read-only-recursive-input";
        builder = ./read-only-recursive-input.builder.sh;
        requiredSystemFeatures = [ "recursive-nix" ];
        nix = "$nixBin/nix";
      };
  };
}
EOF

    cp simple.nix shell.nix simple.builder.sh "${config_nix}" "$flakeDir/"

    # Regression test for #16645: the builder and store owner have the same UID.
    mkdir "$flakeDir/input"
    echo contents > "$flakeDir/input/file"
    cat > "$flakeDir/read-only-input.builder.sh" <<'EOF'
#!/usr/bin/env bash

set -euo pipefail

for path in "$input" "$input/file" "$inputFile"; do
    if chmod 0700 "$path"; then
        echo "input store path is writable: $path" >&2
        exit 1
    fi
done

for path in "$input/file" "$inputFile"; do
    if echo corrupted > "$path"; then
        echo "input store file is writable: $path" >&2
        exit 1
    fi
    [[ $(cat "$path") = contents ]]
done

if touch "$input/new"; then
    echo "input store directory is writable" >&2
    exit 1
fi

mkdir "$out"
EOF
    chmod +x "$flakeDir/read-only-input.builder.sh"

    cat > "$flakeDir/read-only-recursive-input.builder.sh" <<'EOF'
#!/usr/bin/env bash

set -euo pipefail

echo contents > dynamic-file
mkdir dynamic-dir
echo contents > dynamic-dir/file
ln -s /proc dynamic-link
input=$("$nix" --extra-experimental-features nix-command store add-path dynamic-link)
[[ -L "$input" ]]
[[ $(readlink "$input") = /proc ]]

for path in dynamic-file dynamic-dir; do
    input=$("$nix" --extra-experimental-features nix-command store add-path "$path")
    if chmod 0700 "$input"; then
        echo "dynamically added store path is writable: $input" >&2
        exit 1
    fi
done

mkdir "$out"
EOF

    TODO_NixOS
    requiresUnprivilegedUserNamespaces

    outPath=$(nix build --print-out-paths --no-link --sandbox-paths '/nix? /bin? /lib? /lib64? /usr?' --store "$TEST_ROOT/x" path:"$flakeDir")

    [[ $outPath =~ ^/nix2/store/.*-simple$ ]]

    base=$(basename "$outPath")
    [[ $(cat "$TEST_ROOT"/x/nix/store/"$base"/hello) = 'Hello World!' ]]

    nix build --no-link --sandbox-paths '/nix? /bin? /lib? /lib64? /usr?' --store "$TEST_ROOT/x" path:"$flakeDir"#readOnlyInput

    nix build -L --no-link --extra-experimental-features recursive-nix \
        --sandbox-paths '/nix? /bin? /lib? /lib64? /usr?' --extra-sandbox-paths "$(dirname "$nixBin")" \
        --store "$TEST_ROOT/x" path:"$flakeDir"#readOnlyRecursiveInput

    # Preserve nosuid/nodev when making inputs read-only in a user namespace.
    unshare --user --map-root-user --mount bash -e -u -o pipefail -s -- "$TEST_ROOT/x" "$flakeDir" "$(dirname "$nixBin")" <<'EOF'
mount --bind "$1" "$1"
mount -o remount,bind,nosuid,nodev "$1"
nix build --no-link --rebuild --option require-drop-supplementary-groups false \
    --extra-experimental-features recursive-nix \
    --sandbox-paths "/nix? /bin? /lib? /lib64? /usr?" \
    --extra-sandbox-paths "$3" \
    --store "$1" path:"$2"#readOnlyInput path:"$2"#readOnlyRecursiveInput
EOF
fi

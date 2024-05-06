source common.sh

flake1Dir=$TEST_ROOT/flake1

mkdir -p $flake1Dir

cat > $flake1Dir/flake.nix <<EOF
{
  outputs = { self }: {
    everything = builtins.path { path = ./.; name = "source"; };
    foo = ./foo;
    trace = builtins.trace "path \${toString ./foo}" 123;
    throw = builtins.throw "path \${toString ./foo}" 123;
    abort = builtins.abort "path \${toString ./foo}" 123;

    drv1 = with import ./config.nix; mkDerivation {
      name = "bla";
      buildCommand = ''
        mkdir \$out
        ln -s \${self.foo} \$out/foo
      '';
    };

    drv2 = with import ./config.nix; mkDerivation {
      name = "bla";
      buildCommand = ''
        mkdir \$out
        ln -s \${toString self.foo} \$out/foo
      '';
    };
  };
}
EOF

cp ../config.nix $flake1Dir/
echo foo > $flake1Dir/foo

# Add an uncopyable file to test laziness.
mkfifo $flake1Dir/fifo

expectStderr 1 nix build --json --out-link $TEST_ROOT/result $flake1Dir#everything | grep 'has an unsupported type'

nix build --json --out-link $TEST_ROOT/result $flake1Dir#foo
[[ $(cat $TEST_ROOT/result) = foo ]]
# FIXME: check that the name of `result` is `foo`, not `source`.

# Check that traces/errors refer to the pretty-printed source path, not a virtual path.
nix eval $flake1Dir#trace 2>&1 | grep "trace: path $flake1Dir/foo"
expectStderr 1 nix eval $flake1Dir#throw 2>&1 | grep "error: path $flake1Dir/foo"
expectStderr 1 nix eval $flake1Dir#abort 2>&1 | grep "error:.*path $flake1Dir/foo"

nix build --out-link $TEST_ROOT/result $flake1Dir#drv1
[[ $(cat $TEST_ROOT/result/foo) = foo ]]
[[ $(realpath $TEST_ROOT/result/foo) =~ $NIX_STORE_DIR/.*-foo$ ]]

# Check for warnings about passing `toString ./path` to a derivation.
nix build --out-link $TEST_ROOT/result $flake1Dir#drv2 2>&1 | grep "warning: derivation.*has an attribute that refers to source tree"
[[ $(readlink $TEST_ROOT/result/foo) =~ $NIX_STORE_DIR/lazylazy.*-source/foo$ ]]

# If the source tree can be hashed, the virtual path will be rewritten
# to the path that would exist if the source tree were copied to the
# Nix store.
rm $flake1Dir/fifo
nix build --out-link $TEST_ROOT/result $flake1Dir#drv2

# But we don't *actually* copy it.
(! realpath $TEST_ROOT/result/foo)

# Force the path to exist.
path=$(nix eval --raw $flake1Dir#everything)
realpath $TEST_ROOT/result/foo

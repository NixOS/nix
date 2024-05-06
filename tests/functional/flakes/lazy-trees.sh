source common.sh

flake1Dir=$TEST_ROOT/flake1

mkdir -p $flake1Dir

cat > $flake1Dir/flake.nix <<EOF
{
  outputs = { self }: {
    everything = ./.;
    foo = ./foo;
    trace = builtins.trace "path \${toString ./foo}" 123;
    throw = builtins.throw "path \${toString ./foo}" 123;
    abort = builtins.abort "path \${toString ./foo}" 123;
  };
}
EOF

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

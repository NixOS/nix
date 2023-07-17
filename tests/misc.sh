source common.sh

# Tests miscellaneous commands.

# Do all commands have help?
#nix-env --help | grepQuiet install
#nix-store --help | grepQuiet realise
#nix-instantiate --help | grepQuiet eval
#nix-hash --help | grepQuiet base32

# Can we ask for the version number?
nix-env --version | grep "$version"

# Usage errors.
expect 1 nix-env --foo 2>&1 | grep "no operation"
expect 1 nix-env -q --foo 2>&1 | grep "unknown flag"

# Eval Errors.
eval_arg_res=$(nix-instantiate --eval -E 'let a = {} // a; in a.foo' 2>&1 || true)
echo $eval_arg_res | grep "at «string»:1:15:"
echo $eval_arg_res | grep "infinite recursion encountered"

eval_stdin_res=$(echo 'let a = {} // a; in a.foo' | nix-instantiate --eval -E - 2>&1 || true)
echo $eval_stdin_res | grep "at «stdin»:1:15:"
echo $eval_stdin_res | grep "infinite recursion encountered"

# Attribute path errors
expectStderr 1 nix-instantiate --eval -E '{}' -A '"x' | grepQuiet "missing closing quote in selection path"
expectStderr 1 nix-instantiate --eval -E '[]' -A 'x' | grepQuiet "should be a set"
expectStderr 1 nix-instantiate --eval -E '{}' -A '1' | grepQuiet "should be a list"
expectStderr 1 nix-instantiate --eval -E '{}' -A '.' | grepQuiet "empty attribute name"
expectStderr 1 nix-instantiate --eval -E '[]' -A '1' | grepQuiet "out of range"

# Validate deterministic attrset comparison:
NIX_VALIDATE_EVAL_NONDETERMINISM=1 expectStderr 1 nix-instantiate --eval -E '{ a = 1; b = assert false; 1; } == { a = 2; b = 1; }' | grepQuiet "assertion 'false' failed"
NIX_VALIDATE_EVAL_NONDETERMINISM=1 expectStderr 1 nix-instantiate --eval -E '{ b = 1; a = assert false; 1; } == { b = 2; a = 1; }' | grepQuiet "assertion 'false' failed"

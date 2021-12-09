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

